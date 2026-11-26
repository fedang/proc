#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "options.h"
#include "report.h"

struct options opts;

static bool
options_check_ext(const char *str, const char *ext)
{
    size_t str_len, ext_len;

    str_len = strlen(str);
    ext_len = strlen(ext);

    if (str_len <= ext_len)
        return false;

    return strcmp(str + (str_len - ext_len), ext) == 0;
}

static bool
options_is_clang_input(const char *path)
{
    return options_check_ext(path, ".c")
        || options_check_ext(path, ".ll")
        || options_check_ext(path, ".s")
        || options_check_ext(path, ".S");
}

static void
options_push(const char ***strs, size_t *count, size_t *size, const char *value)
{
    if (*count >= *size) {
        *size = *size == 0 ? 8 : *size * 2;
        *strs = realloc(*strs, *size * sizeof(**strs));
        assert(strs && "out of mem");
    }
    (*strs)[(*count)++] = value;
}

bool
options_init(int argc, const char **argv)
{
    int i;

    opts.output = "a.out";
    opts.ir_file = "test.ll";
    opts.term_color = isatty(STDERR_FILENO) && !getenv("NO_COLOR");

    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            if (options_is_clang_input(argv[i])) {
                report_info(NULL, NULL,
                            "Input file '%s' will be passed directly to clang\n",
                            argv[i]);
                options_push(&opts.clang_inputs, &opts.clang_inputs_count,
                             &opts.clang_inputs_size, argv[i]);
            } else {
                options_push(&opts.inputs, &opts.inputs_count,
                             &opts.inputs_size, argv[i]);
            }
            continue;
        }

        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            options_help(argv[0]);
            return false;
        }

        if (!strcmp(argv[i], "-o")) {
            if (++i >= argc) {
                report_error(NULL, NULL, "Missing argument for -o\n");
                return false;
            }
            opts.output = argv[i];
            continue;
        }

        if (!strcmp(argv[i], "-i")) {
            if (++i >= argc) {
                report_error(NULL, NULL, "Missing argument for -i\n");
                return false;
            }
            opts.ir_file = argv[i];
            continue;
        }

        if (!strcmp(argv[i], "-S")) {
            opts.emit_asm = true;
            continue;
        }

        if (!strcmp(argv[i], "-C")) {
            if (++i >= argc) {
                report_error(NULL, NULL, "Missing argument for -C\n");
                return false;
            }
            options_push(&opts.clang_args, &opts.clang_args_count,
                         &opts.clang_args_size, argv[i]);
            continue;
        }

        if (!strcmp(argv[i], "--color")) {
            opts.term_color = true;
            continue;
        }

        if (!strcmp(argv[i], "--no-color")) {
            opts.term_color = false;
             continue;
         }

        report_error(NULL, NULL, "Unknown option '%s'\n", argv[i]);
        return false;
    }

    if (opts.inputs_count == 0 && opts.clang_inputs_count == 0) {
        report_error(NULL, NULL, "No input files specified\n");
        return false;
    }

    return true;
}

void
options_help(const char *prog)
{
    printf("Usage: %s [options] file...\n\n", prog);
    printf("Options:\n");
    printf("  -o <file>         Select output file\n");
    printf("  -i <file>         Select LLVM IR file\n");
    printf("  -C <clang_arg>    Pass option directly to the clang backend\n");
    printf("  -S                Emit assembly instead of linking\n");
    printf("  --color           Force colored diagnostics\n");
    printf("  --no-color        Disable colored diagnostics\n");
    printf("  -h, --help        Display this help message\n");
}

int
options_run_clang(void)
{
    char cmd[2048];
    size_t i, cmd_len;

    cmd_len = snprintf(cmd, sizeof(cmd),
                       "clang -O1 -Wno-override-module");

    if (opts.emit_asm) {
        cmd_len += snprintf(cmd + cmd_len, sizeof(cmd) - cmd_len,
                            " -S");
    } else {
        cmd_len += snprintf(cmd + cmd_len, sizeof(cmd) - cmd_len,
                            " -o %s", opts.output);
    }

    cmd_len += snprintf(cmd + cmd_len, sizeof(cmd) - cmd_len,
                        " %s", opts.ir_file);

    for (i = 0; i < opts.clang_inputs_count; i++) {
        cmd_len += snprintf(cmd + cmd_len, sizeof(cmd) - cmd_len,
                            " %s", opts.clang_inputs[i]);
    }

    for (i = 0; i < opts.clang_args_count; i++) {
        cmd_len += snprintf(cmd + cmd_len, sizeof(cmd) - cmd_len,
                            " %s", opts.clang_args[i]);
    }

    return system(cmd);
}
