#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "irgen.h"
#include "lexer.h"
#include "parser.h"

static bool
file_read(const char *path, char **src, size_t *src_len)
{
    FILE *file;
    size_t fsize;

    file = fopen(path, "rb");
    if (!file)
        return false;

    fseek(file, 0, SEEK_END);
    fsize = ftell(file);

    if (fsize < 0) {
        fclose(file);
        return false;
    }

    rewind(file);

    *src = malloc(fsize + 1);
    if (fread(*src, fsize, 1, file) != 1) {
        free(*src);
        fclose(file);
        return false;
    }

    fclose(file);

    (*src)[fsize] = 0;
    *src_len = fsize;
    return true;
}

int main(int argc, char **argv)
{
    struct lexer lexer;
    struct parser parser;
    struct irgen irgen;
    struct token *tokens;
    struct decl **decls;
    size_t src_len, decls_count;
    char *src;
    FILE *out;

    if (argc != 2) {
        printf("Usage: %s file\n", argv[0]);
        return 1;
    }

    if (!file_read(argv[1], &src, &src_len)) {
        printf("Failed to read input file %s\n", argv[1]);
        return 1;
    }

    lexer_init(&lexer, src, src_len);
    lexer_tokenize(&lexer, &tokens);

    parser_init(&parser, tokens);
    if (!parser_module(&parser, &decls, &decls_count)) {
        printf("Failed to parse file\n");
        return 1;
    }

    printf("Parsed %zu declarations\n", decls_count);

    out = fopen("test.ll", "wb");
    if (!out) {
        printf("Failed to open output file\n");
        return 1;
    }

    irgen_init(&irgen, out);
    irgen_module(&irgen, decls, decls_count);

    fclose(out);
    system("clang test.ll test.c -o test");

    return 0;
}
