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
    struct modcache cache;
    struct checker check;
    struct irgen irgen;
    const char *ir_file;
    FILE *ir_out;
    size_t i;

    if (!options_init(&opts, argc, argv))
        return 1;

    symbol_init();

    modcache_init(&cache, ".");

    /*
     * Load all the root modules asked by the user
     */
    for (i = 0; i < opts.inputs_count; i++) {
        if (!modcache_load(&cache, opts.inputs[i], NULL, NULL)) {
            report_info(NULL, NULL,
                        "Failed to load input file '%s'\n", opts.inputs[i]);
            return 1;
        }
    }

    /*
     * Verify and typecheck the modules
     */
    checker_init(&check, &cache);
    if (!checker_modcache(&check)) {
        report_info(NULL, NULL, "Failed semantic analysis\n");
        return 1;
    }

    ir_file = "test.ll"; // XXX
    ir_out = fopen(ir_file, "wb");
    if (!ir_out) {
        report_info(NULL, NULL, "Failed to open IR output file\n");
        return 1;
    }

    /*
     * Generate the LLVM IR
     */
    irgen_init(&irgen, ir_out);
    if (!irgen_modcache(&irgen, &cache)) {
        report_info(NULL, NULL, "Failed to generate IR\n");
        fclose(ir_out);
        return 1;
    }

    fclose(ir_out);
    if (run_clang(&opts, ir_file)) {
        report_info(NULL, NULL, "Failed to compile IR\n");
        return 1;
    }

    return 0;
}
