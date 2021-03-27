#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Syntax: bin2toinc FILE\n");
        exit(EXIT_FAILURE);
    }

    FILE *file = fopen(argv[1], "rb");
    if (file == 0) {
        fprintf(stderr, "Error opening %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    int c;
    int i = 0;
    while ((c = getc(file)) != EOF) {
        printf("0x%02x, ", c);
        ++i;
        if ((i % 16) == 0) {
            printf("\n");
        }
    }
    if (ferror(file)) {
        fprintf(stderr, "Error reading from input\n");
        exit(EXIT_FAILURE);
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "Error closing input\n");
        exit(EXIT_FAILURE);
    }
    if (i != 16 * 1024) {
        fprintf(stderr, "ROM image %s is not exactly 16K\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    return EXIT_SUCCESS;
}
