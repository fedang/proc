#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "lexer.h"
#include "parser.h"
#include "checker.h"
#include "irgen.h"
#include "report.h"

int
main(int argc, char **argv)
{
    struct source src;
    struct lexer lexer;
    struct parser parser;
    struct checker tychk;
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
        report_error(&src, NULL, "Failed to read input file %s\n", argv[1]);
        return 1;
    }

    symbol_init();

    lexer_init(&lexer, &src);
    lexer_tokenize(&lexer, &tokens);

    parser_init(&parser, &src, tokens);
    if (!parser_module(&parser, &decls, &decls_count)) {
        report_info(&src, NULL, "Failed to parse source file\n");
        return 1;
    }

    checker_init(&tychk, &src);
    if (!checker_module(&tychk, decls, decls_count)) {
        report_info(&src, NULL, "Failed semantic analysis\n");
        return 1;
    }

    report_info(&src, NULL, "Parsed %zu declarations\n", decls_count);

    out = fopen("test.ll", "wb");
    if (!out) {
        report_info(&src, NULL, "Failed to open output file\n");
        return 1;
    }

    irgen_init(&irgen, &src, out);
    if (!irgen_module(&irgen, decls, decls_count)) {
        report_info(&src, NULL, "Failed to generate IR\n");
        return 1;
    }

    fclose(out);

    if (system("clang test.ll test.c -O1 -o test -Wno-override-module")) {
        report_info(&src, NULL, "Failed to compile IR\n");
        return 1;
    }

    return 0;
}
