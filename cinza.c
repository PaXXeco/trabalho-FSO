#include <stdio.h>
#include <stdlib.h>

// Garante que as estruturas sejam armazenados sem padding
#pragma pack(1)

//=============================== Estrutura do BMP ===============================
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

//=============================== Converte para escala cinza ===============================
void cinza(const char* input, const char* output) {
    FILE *in = fopen(input, "rb");
    FILE *out = fopen(output, "wb");

    if (!in || !out) {
        printf("Erro ao abrir arquivo.\n");
        exit(1);
    }

    FILEHEADER file_header;
    IMAGEHEADER image_header;

    fread(&file_header, sizeof(FILEHEADER), 1, in);
    fread(&image_header, sizeof(IMAGEHEADER), 1, in);

    if (file_header.type != 0x4D42 || image_header.bits_per_pixel != 24) {
        printf("Erro Formato inválido\n");
        fclose(in);
        fclose(out);
        exit(1);
    }

    fwrite(&file_header, sizeof(FILEHEADER), 1, out);
    fwrite(&image_header, sizeof(IMAGEHEADER), 1, out);

    int width = image_header.width;
    int height = image_header.height;
    int padding = (4 - (width * 3) % 4) % 4;

    RGB pixel;

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            fread(&pixel, sizeof(RGB), 1, in);
            unsigned char gray = (unsigned char)((0.299 * pixel.red) + (0.587 * pixel.green) + (0.114 * pixel.blue));
            pixel.red = pixel.green = pixel.blue = gray;
            fwrite(&pixel, sizeof(RGB), 1, out);
        }
       
        fseek(in, padding, SEEK_CUR);
        for (int p = 0; p < padding; p++){
            fputc(0x00, out);
        }
    }

    fclose(in);
    fclose(out);
}

//=============================== Main recebe arquivo ===============================
int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Favor inserir: %s <entrada.bmp> <saida.bmp>\n", argv[0]);
        return 1;
    }
    cinza(argv[1], argv[2]);
    return 0;
}