#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "lexer.h"
#include "parser.h"
#include "checker.h"
#include "irgen.h"
#include "report.h"
#include "options.h"

static int
run_clang(struct options *opts, const char *ir_file)
{
    char cmd[1024];
    int i, cmd_len;

    cmd_len = snprintf(cmd, sizeof(cmd),
                       "clang %s test.c -O1 -o %s -Wno-override-module",
                        ir_file, opts->output);

    for (i = 0; i < opts->clang_args_count; i++) {
        cmd_len += snprintf(cmd + cmd_len, sizeof(cmd) - cmd_len,
                            " %s", opts->clang_args[i]);
    }

    return system(cmd);
}

int
main(int argc, const char **argv)
{
    struct options opts;
    struct source src;
    struct lexer lexer;
    struct parser parser;
    struct checker tychk;
    struct irgen irgen;
    struct token *tokens;
    struct decl *mod;
    const char *ir_file;
    FILE *ir_out;

    if (!options_init(&opts, argc, argv))
        return 1;

    source_init(&src, opts.inputs[0], NULL);
    if (!source_load(&src)) {
        report_error(&src, NULL, "Failed to read input file %s\n",
                     opts.inputs[0]);
        return 1;
    }

    symbol_init();

    lexer_init(&lexer, &src);
    lexer_tokenize(&lexer, &tokens);

    parser_init(&parser, &src, tokens);
    if (!parser_module(&parser, &mod)) {
        report_info(&src, NULL, "Failed to parse source file\n");
        return 1;
    }

    checker_init(&tychk, &src);
    if (!checker_module(&tychk, mod)) {
        report_info(&src, NULL, "Failed semantic analysis\n");
        return 1;
    }

    ir_file = "test.ll"; // XXX
    ir_out = fopen(ir_file, "wb");
    if (!ir_out) {
        report_info(&src, NULL, "Failed to open IR output file\n");
        return 1;
    }

    irgen_init(&irgen, &src, ir_out);
    if (!irgen_module(&irgen, mod)) {
        report_info(&src, NULL, "Failed to generate IR\n");
        return 1;
    }

    fclose(ir_out);

    if (run_clang(&opts, ir_file)) {
        report_info(&src, NULL, "Failed to compile IR\n");
        return 1;
    }

    return 0;
}
