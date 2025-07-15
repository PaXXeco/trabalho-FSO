#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <pthread.h>

// Garante que as estruturas sejam armazenados sem padding
#pragma pack(1)

//=============================== Estrutura do BMP ===============================
typedef struct {
    unsigned short type;
    unsigned int sizeFile;
    unsigned short reserved1;
    unsigned short reserved2;
    unsigned int offset;
} FileHeader;

typedef struct {
    unsigned int sizeImageHeader;
    int width;
    int height;
    unsigned short planes;
    unsigned short bitsPerPixel;
    unsigned int compression;
    unsigned int imageSize;
    int xPixelsPerMeter;
    int yPixelsPerMeter;
    unsigned int numberColors;
    unsigned int importantColors;
} ImageHeader;

typedef struct {
    unsigned char blue;
    unsigned char green;
    unsigned char red;
} RgbPixel;
#pragma pack()

//=============================== Aplicação da mediana ===============================

// Compara uma sequência de pixels para obter a mediana
int comparaPixels(const void *a, const void *b) {
    return (*(unsigned char *)a - *(unsigned char *)b);
}

// Função que aplica o filtro de mediana
void mediana(unsigned char *in, unsigned char *out, int width, int height, int start_row, int end_row, int tam_masc){
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
            qsort(janela, tamanho_vetor, sizeof(unsigned char), comparaPixels);
            out[y * width + x] = janela[tamanho_vetor / 2];
        }
    }
    free(janela);
}


//=============================== Aplicação da Laplace ===============================

//Função que gera as mascaras de laplace
int *geraMascLaplace(int size) {
    if (size % 2 == 0 || size < 3 || size > 7) {
        printf("Mascara invalida: use 3, 5 ou 7\n");
        return NULL;
    }
    int mid = size / 2;
    int *nucleo = malloc(size * size * sizeof(int));
    
    for (int i = 0; i < size * size; i++) {
        nucleo[i] = 0;  
    } 

    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            int dist = abs(i - mid) + abs(j - mid);
            if (dist >= 1 && dist <= mid) {
                nucleo[i * size + j] = -(mid - dist + 1);
            }
        }
    }

    int somaAbs = 0;
    for (int k = 0; k < size * size; k++) {
        if (nucleo[k] < 0) somaAbs += -nucleo[k];
    }
    nucleo[mid * size + mid] = somaAbs;
    
    // for(int k=0; k < size; k++) {
    //     for(int l=0; l < size; l++){
    //         printf("%d ", nucleo[k * size + l]);
    //     }
    //     printf("\n");
    // }
    
    return nucleo;
}

// Função que aplica o filtro laplaciano
void laplaciano(unsigned char *input, unsigned char *output, int width, int height, int startRow, int endRow, int maskSize, int *nucleo) {
    int offset = maskSize / 2;
    
    for (int y = startRow; y < endRow; y++) {
        for (int x = 0; x < width; x++) {
            int acc = 0;
            for (int i = -offset; i <= offset; i++) {
                for (int j = -offset; j <= offset; j++) {
                    int xi = x + j;
                    int yi = y + i;
                    if (xi >= 0 && xi < width && yi >= 0 && yi < height) {
                        int k = (i + offset) * maskSize + (j + offset);
                        acc += input[yi * width + xi] * nucleo[k];
                    }
                }
            }
            acc = abs(acc);
            if (acc > 255) acc = 255;
            output[y * width + x] = acc;
        }
    }
}

void salvaImagem(const char *path, FileHeader fh, ImageHeader ih, unsigned char *data) {
    FILE *f = fopen(path, "wb");
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);

    int padding = (4 - (ih.width * 3) % 4) % 4;

    for (int i = 0; i < ih.height; i++) {
        for (int j = 0; j < ih.width; j++) {
            unsigned char g = data[i * ih.width + j];
            RgbPixel px = {g, g, g};
            fwrite(&px, sizeof(RgbPixel), 1, f);
        }
        for (int p = 0; p < padding; p++) fputc(0x00, f);
    }
    fclose(f);
}

//=============================== Pthreads ===============================

typedef struct {
    unsigned char *in;
    unsigned char *out;
    int width;
    int height;
    int start;
    int end;
    int maskSize;
    int *mask; 
} threadData;

void *threadMediana(void *arg) {
    threadData *t = arg;
    mediana(t->in, t->out, t->width, t->height, t->start, t->end, t->maskSize);
    return NULL;
}

void *threadLaplaciano(void *arg) {
    threadData *t = arg;
    laplaciano(t->in, t->out, t->width, t->height, t->start, t->end, t->maskSize, t->mask);
    return NULL;
}

/*============================== Main ==============================*/
int main(int argc, char *argv[]) {
    if (argc != 8) {
        printf("Uso: %s <entrada.bmp> <saidaCinza.bmp> <saidaMediana.bmp> <saidaLaplaciano.bmp> <tamMascMediana> <tamMascLaplaciana> <numThreads>\n", argv[0]);
        return 1;
    }

    const char *imgEntrada = argv[1];
    const char *saidaCinza = argv[2];
    const char *saidaMediana = argv[3];
    const char *saidaLaplaciano = argv[4];
    int tamMascMed = atoi(argv[5]);
    int tamMascLap = atoi(argv[6]);
    int numThreads = atoi(argv[7]);

    int *laplaceNucleo = geraMascLaplace(tamMascLap);
    if (laplaceNucleo == NULL) return 1;

    FILE *f = fopen(imgEntrada, "rb");
    if (!f) {
        perror("Erro ao abrir imagem");
        return 1;
    }

    FileHeader fh;
    ImageHeader ih;
    fread(&fh, sizeof(fh), 1, f);
    fread(&ih, sizeof(ih), 1, f);

    if (fh.type != 0x4D42 || ih.bitsPerPixel != 24) {
        printf("Formato invalido\n");
        fclose(f);
        return 1;
    }

    int width = ih.width;
    int height = ih.height;
    int padding = (4 - (width * 3) % 4) % 4;

    unsigned char *grayImage = malloc(width * height);
    unsigned char *medianaImg = malloc(width * height);
    unsigned char *laplacianoImg = malloc(width * height);

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            RgbPixel px;
            fread(&px, sizeof(RgbPixel), 1, f);
            unsigned char gray = (unsigned char)(0.299 * px.red + 0.587 * px.green + 0.114 * px.blue);
            grayImage[i * width + j] = gray;
        }
        fseek(f, padding, SEEK_CUR);
    }
    fclose(f);

    salvaImagem(saidaCinza, fh, ih, grayImage);

    int slice = height / numThreads;
    pthread_t threads[ numThreads ];
    threadData tdata[ numThreads ];

    for (int i = 0; i < numThreads; i++) {
        int start = i * slice;
        int end = (i == numThreads - 1) ? height : (i + 1) * slice;
        tdata[i] = (threadData){grayImage, medianaImg, width, height,
                               start, end, tamMascMed, NULL};
        pthread_create(&threads[i], NULL, threadMediana, &tdata[i]);
    }

    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }

    for (int i = 0; i < numThreads; i++) {
        int start = i * slice;
        int end = (i == numThreads - 1) ? height : (i + 1) * slice;
        tdata[i] = (threadData){medianaImg, laplacianoImg, width, height,
                               start, end, tamMascLap, laplaceNucleo};
        pthread_create(&threads[i], NULL, threadLaplaciano, &tdata[i]);
    }

    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }

    salvaImagem(saidaMediana, fh, ih, medianaImg);
    salvaImagem(saidaLaplaciano, fh, ih, laplacianoImg);

    free(grayImage);
    free(medianaImg);
    free(laplacianoImg);
    free(laplaceNucleo);

    printf("Filtros aplicados!\n");
    return 0;
}