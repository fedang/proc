#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "options.h"
#include "report.h"

static void
options_push(const char ***strs, size_t *count, size_t *size, const char *value)
{
    if (*count >= *size) {
        *size = *size == 0 ? 8 : *size * 2;
        *strs = realloc(*strs, *size * sizeof(*strs));
        assert(strs && "out of mem");
    }
    (*strs)[(*count)++] = value;
}

bool
options_init(struct options *opts, int argc, const char **argv)
{
    int i;

    memset(opts, 0, sizeof(struct options));
    opts->output = "a.out";
    opts->ir_file = "test.ll";

    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            options_push(&opts->inputs, &opts->inputs_count,
                         &opts->inputs_size, argv[i]);
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
            opts->output = argv[i];
            continue;
        }

        if (!strcmp(argv[i], "-i")) {
            if (++i >= argc) {
                report_error(NULL, NULL, "Missing argument for -i\n");
                return false;
            }
            opts->ir_file = argv[i];
            continue;
        }

        if (!strcmp(argv[i], "-S")) {
            opts->emit_asm = true;
            continue;
        }

        if (!strcmp(argv[i], "-C")) {
            if (++i >= argc) {
                report_error(NULL, NULL, "Missing argument for -C\n");
                return false;
            }
            options_push(&opts->clang_args, &opts->clang_args_count,
                         &opts->clang_args_size, argv[i]);
            continue;
        }

        report_error(NULL, NULL, "Unknown option '%s'\n", argv[i]);
        return false;
    }

    if (opts->inputs_count == 0) {
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
    printf("  -h, --help        Display this help message\n");
}
