#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "lexer.h"
#include "parser.h"
#include "typecheck.h"
#include "irgen.h"

int main(int argc, char **argv)
{
    struct source src;
    struct lexer lexer;
    struct parser parser;
    struct typecheck tychk;
    struct irgen irgen;
    struct token *tokens;
    struct decl **decls;
    size_t decls_count;
    FILE *out;

    if (argc != 2) {
        printf("Usage: %s file\n", argv[0]);
        return 1;
    }

    if (!source_open(&src, argv[1])) {
        printf("Failed to read input file %s\n", argv[1]);
        return 1;
    }

    lexer_init(&lexer, &src);
    lexer_tokenize(&lexer, &tokens);

    parser_init(&parser, &src, tokens);
    if (!parser_module(&parser, &decls, &decls_count)) {
        printf("Failed to parse file\n");
        return 1;
    }

    typecheck_init(&tychk, &src);
    if (!typecheck_module(&tychk, decls, decls_count)) {
        printf("Failed to typecheck file\n");
        return 1;
    }

    printf("Parsed %zu declarations\n", decls_count);

    out = fopen("test.ll", "wb");
    if (!out) {
        printf("Failed to open output file\n");
        return 1;
    }

    irgen_init(&irgen, out);
    if (!irgen_module(&irgen, decls, decls_count)) {
        printf("Failed to generate IR\n");
        return 1;
    }

    fclose(out);
    system("clang test.ll test.c -o test");

    return 0;
}
