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

static bool
check_extension(const char *str, const char *ext)
{
    size_t str_len, ext_len;

    str_len = strlen(str);
    ext_len = strlen(ext);

    if (str_len <= ext_len)
        return false;

    if (strcmp(str + (str_len - ext_len), ext))
        return false;

    return str[str_len - ext_len] == '.';
}

static int
run_clang(struct options *opts, const char *ir_file)
{
    char cmd[1024];
    size_t i, cmd_len;

    cmd_len = snprintf(cmd, sizeof(cmd),
                       "clang -O1 -Wno-override-module");

    if (opts->emit_asm) {
        cmd_len += snprintf(cmd + cmd_len, sizeof(cmd) - cmd_len,
                            " -S");
    } else {
        cmd_len += snprintf(cmd + cmd_len, sizeof(cmd) - cmd_len,
                            " -o %s", opts->output);
    }

    cmd_len += snprintf(cmd + cmd_len, sizeof(cmd) - cmd_len,
                        " %s", ir_file);

    for (i = 0; i < opts->inputs_count; i++) {
        if (!check_extension(opts->inputs[i], ".c"))
            continue;

        cmd_len += snprintf(cmd + cmd_len, sizeof(cmd) - cmd_len,
                            " %s", opts->inputs[i]);
    }

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
    bool ok = true;

    if (!options_init(&opts, argc, argv))
        return 1;

    symbol_init();

    modcache_init(&cache, ".");

    /*
     * Load all the root modules asked by the user
     */
    for (i = 0; i < opts.inputs_count; i++) {
        if (check_extension(opts.inputs[i], ".c")) {
            report_info(NULL, NULL,
                        "Input file '%s' will be passed directly to clang\n",
                        opts.inputs[i]);
            continue;
        }

        if (!modcache_load(&cache, opts.inputs[i], NULL, NULL)) {
            ok = false;
        }
    }

    if (!ok)
        return 1;

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
