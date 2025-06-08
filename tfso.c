#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>

#pragma pack(1)

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

int* generateLaplacianMask(int size) {
    if (size % 2 == 0 || size < 3 || size > 99) {
        printf("Tamanho de máscara inválido. Use um ímpar >=3\n");
        return NULL;
    }

    int *kernel = malloc(size * size * sizeof(int));
    int center = size / 2;
    int sum = 0;

    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            int di = abs(i - center);
            int dj = abs(j - center);
            int weight = (di + dj == 0) ? 0 : -(di + dj);
            kernel[i * size + j] = weight;
            sum += weight;
        }
    }

    kernel[center * size + center] = -sum;

    return kernel;
}

int comparePixels(const void *a, const void *b) {
    return (*(unsigned char *)a - *(unsigned char *)b);
}

void applyMedianFilter(unsigned char *input, unsigned char *output, int width, int height, int startRow, int endRow, int maskSize) {
    int offset = maskSize / 2;
    int windowSize = maskSize * maskSize;
    unsigned char *window = malloc(windowSize);

    for (int y = startRow; y < endRow; y++) {
        for (int x = 0; x < width; x++) {
            int index = 0;
            for (int dy = -offset; dy <= offset; dy++) {
                for (int dx = -offset; dx <= offset; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                        window[index++] = 0;
                    } else {
                        window[index++] = input[ny * width + nx];
                    }
                }
            }
            qsort(window, windowSize, sizeof(unsigned char), comparePixels);
            output[y * width + x] = window[windowSize / 2];
        }
    }

    free(window);
}

void applyLaplacianFilter(unsigned char *input, unsigned char *output, int width, int height, int startRow, int endRow, int maskSize, int *kernel) {
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
                        acc += input[yi * width + xi] * kernel[k];
                    }
                }
            }
            acc = abs(acc);
            if (acc > 255) acc = 255;
            output[y * width + x] = acc;
        }
    }
}

void saveImage(const char *path, FileHeader fileHeader, ImageHeader imageHeader, unsigned char *data) {
    FILE *f = fopen(path, "wb");
    fwrite(&fileHeader, sizeof(fileHeader), 1, f);
    fwrite(&imageHeader, sizeof(imageHeader), 1, f);

    int padding = (4 - (imageHeader.width * 3) % 4) % 4;

    for (int i = 0; i < imageHeader.height; i++) {
        for (int j = 0; j < imageHeader.width; j++) {
            unsigned char gray = data[i * imageHeader.width + j];
            RgbPixel px = {gray, gray, gray};
            fwrite(&px, sizeof(RgbPixel), 1, f);
        }
        for (int p = 0; p < padding; p++) fputc(0x00, f);
    }

    fclose(f);
}

int main(int argc, char *argv[]) {
    if (argc != 8) {
        printf("Uso: %s <entrada.bmp> <saidaCinza.bmp> <saidaMediana.bmp> <saidaLaplaciano.bmp> <tamMascMediana> <tamMascLaplaciano> <numProcessos>\n", argv[0]);
        return 1;
    }

    const char *inputPath = argv[1];
    const char *grayOutputPath = argv[2];
    const char *medianOutputPath = argv[3];
    const char *laplacianOutputPath = argv[4];
    int medianMaskSize = atoi(argv[5]);
    int laplacianMaskSize = atoi(argv[6]);
    int numProcesses = atoi(argv[7]);

    int *laplacianKernel = generateLaplacianMask(laplacianMaskSize);
    if (laplacianKernel == NULL) return 1;

    FILE *f = fopen(inputPath, "rb");
    if (!f) {
        perror("Erro ao abrir imagem de entrada");
        return 1;
    }

    FileHeader fileHeader;
    ImageHeader imageHeader;
    fread(&fileHeader, sizeof(fileHeader), 1, f);
    fread(&imageHeader, sizeof(imageHeader), 1, f);

    if (fileHeader.type != 0x4D42 || imageHeader.bitsPerPixel != 24) {
        printf("Formato inválido. Esperado BMP 24 bits\n");
        fclose(f);
        return 1;
    }

    int width = imageHeader.width;
    int height = imageHeader.height;
    int padding = (4 - (width * 3) % 4) % 4;

    unsigned char *grayImage = malloc(width * height);
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

    saveImage(grayOutputPath, fileHeader, imageHeader, grayImage);

    int shmIdIn = shmget(IPC_PRIVATE, width * height, IPC_CREAT | 0666);
    int shmIdMedian = shmget(IPC_PRIVATE, width * height, IPC_CREAT | 0666);
    int shmIdLaplacian = shmget(IPC_PRIVATE, width * height, IPC_CREAT | 0666);

    unsigned char *shmIn = (unsigned char *)shmat(shmIdIn, NULL, 0);
    unsigned char *shmMedian = (unsigned char *)shmat(shmIdMedian, NULL, 0);
    unsigned char *shmLaplacian = (unsigned char *)shmat(shmIdLaplacian, NULL, 0);

    memcpy(shmIn, grayImage, width * height);

    int slice = height / numProcesses;
    for (int i = 0; i < numProcesses; i++) {
        if (fork() == 0) {
            int start = i * slice;
            int end = (i == numProcesses - 1) ? height : (i + 1) * slice;
            applyMedianFilter(shmIn, shmMedian, width, height, start, end, medianMaskSize);
            exit(0);
        }
    }
    for (int i = 0; i < numProcesses; i++) wait(NULL);

    for (int i = 0; i < numProcesses; i++) {
        if (fork() == 0) {
            int start = i * slice;
            int end = (i == numProcesses - 1) ? height : (i + 1) * slice;
            applyLaplacianFilter(shmMedian, shmLaplacian, width, height, start, end, laplacianMaskSize, laplacianKernel);
            exit(0);
        }
    }
    for (int i = 0; i < numProcesses; i++) wait(NULL);

    saveImage(medianOutputPath, fileHeader, imageHeader, shmMedian);
    saveImage(laplacianOutputPath, fileHeader, imageHeader, shmLaplacian);

    shmdt(shmIn); shmdt(shmMedian); shmdt(shmLaplacian);
    shmctl(shmIdIn, IPC_RMID, NULL);
    shmctl(shmIdMedian, IPC_RMID, NULL);
    shmctl(shmIdLaplacian, IPC_RMID, NULL);
    free(grayImage);
    free(laplacianKernel);

    printf("Filtro de mediana e Laplaciano aplicados com sucesso.\n");
    return 0;
}
