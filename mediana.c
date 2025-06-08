#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <string.h>
#pragma pack(1)

//=================== Estruturas do BMP ===============================
typedef struct {
    unsigned short type;
    unsigned int size_file;
    unsigned short reserved1;
    unsigned short reserved2;
    unsigned int offset;
} FILEHEADER;

typedef struct {
    unsigned int size_image_header;
    int width;
    int height;
    unsigned short planes;
    unsigned short bits_per_pixel;
    unsigned int compression;
    unsigned int image_size;
    int x_pixels_per_meter;
    int y_pixels_per_meter;
    unsigned int number_colors;
    unsigned int important_colors;
} IMAGEHEADER;

typedef struct {
    unsigned char blue;
    unsigned char green;
    unsigned char red;
} RGB;

//================= Funções de Processamento de Imagem ===============================

// Ordenar uma sequência de pixels e obter a mediana
int cmpfunc(const void *a, const void *b) {
    return (*(unsigned char *)a - *(unsigned char *)b);
}

void mediana(unsigned char *in, unsigned char *out, int width, int height, int start_row, int end_row, int tam_masc) {
    int offset = tam_masc / 2;
    int tamanho_vetor = tam_masc * tam_masc;
    unsigned char *janela = malloc(tamanho_vetor);

    for (int y = start_row; y < end_row; y++) {
        for (int x = 0; x < width; x++) {
            int idx = 0;
            for (int dy = -offset; dy <= offset; dy++) {
                for (int dx = -offset; dx <= offset; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                        janela[idx++] = 0;
                    } else {
                        janela[idx++] = in[ny * width + nx];
                    }
                }
            }
            qsort(janela, tamanho_vetor, sizeof(unsigned char), cmpfunc);
            out[y * width + x] = janela[tamanho_vetor / 2];
        }
    }

    free(janela);
}

void salva_imagem(const char *path, FILEHEADER fh, IMAGEHEADER ih, unsigned char *data) {
    FILE *f = fopen(path, "wb");
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);

    int padding = (4 - (ih.width * 3) % 4) % 4;

    for (int i = 0; i < ih.height; i++) {
        for (int j = 0; j < ih.width; j++) {
            unsigned char g = data[i * ih.width + j];
            RGB px = {g, g, g};  // Todos os canais iguais = imagem cinza
            fwrite(&px, sizeof(RGB), 1, f);
        }
        for (int p = 0; p < padding; p++) fputc(0x00, f);
    }

    fclose(f);
}

//========================== Função Principal ==========================
int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Uso: %s <entrada.bmp> <saida.bmp> <tam_masc:3|5|7> <n_processos>\n", argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];
    int tam_masc = atoi(argv[3]);
    int n_processos = atoi(argv[4]);

    if (tam_masc != 3 && tam_masc != 5 && tam_masc != 7) {
        printf("Máscara inválida. Use 3, 5 ou 7.\n");
        return 1;
    }

    FILE *f = fopen(input_path, "rb");
    if (!f) {
        perror("Imagem não encontrada");
        return 1;
    }

    FILEHEADER fh;
    IMAGEHEADER ih;

    fread(&fh, sizeof(fh), 1, f);
    fread(&ih, sizeof(ih), 1, f);

    if (ih.bits_per_pixel != 24) {
        printf("Formato incompatível: a imagem deve ser BMP de 24 bits em tons de cinza.\n");
        return 1;
    }

    int width = ih.width;
    int height = ih.height;
    int padding = (4 - (width * 3) % 4) % 4;

    unsigned char *cinza = malloc(width * height);

    // Lê a imagem assumindo que já está em tons de cinza (R = G = B)
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            RGB px;
            fread(&px, sizeof(RGB), 1, f);
            cinza[(i * width) + j] = px.red; // Basta ler um canal
        }
        fseek(f, padding, SEEK_CUR);
    }

    fclose(f);

    // Criação da memória compartilhada
    int shmid_in = shmget(IPC_PRIVATE, width * height, IPC_CREAT | 0666);
    int shmid_out = shmget(IPC_PRIVATE, width * height, IPC_CREAT | 0666);
    unsigned char *in_shared = (unsigned char *)shmat(shmid_in, NULL, 0);
    unsigned char *out_shared = (unsigned char *)shmat(shmid_out, NULL, 0);

    memcpy(in_shared, cinza, width * height);

    // Criação dos processos
    int slice = height / n_processos;
    for (int i = 0; i < n_processos; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            int start = i * slice;
            int end = (i == n_processos - 1) ? height : (i + 1) * slice;
            mediana(in_shared, out_shared, width, height, start, end, tam_masc);
            exit(0);
        }
    }

    for (int i = 0; i < n_processos; i++) wait(NULL);

    // Salva imagem final
    salva_imagem(output_path, fh, ih, out_shared);

    // Libera recursos
    shmdt(in_shared);
    shmdt(out_shared);
    shmctl(shmid_in, IPC_RMID, NULL);
    shmctl(shmid_out, IPC_RMID, NULL);
    free(cinza);

    printf("Filtro de mediana aplicado com sucesso na imagem já em tons de cinza.\n");
    return 0;
}