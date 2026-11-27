#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>

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
        assert(*strs && "out of mem");
    }
    (*strs)[(*count)++] = value;
}

bool
options_init(int argc, const char **argv)
{
    int i;

    opts.color = isatty(STDERR_FILENO) && !getenv("NO_COLOR");

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
            opts.help = true;
            continue;
        }

        if (!strcmp(argv[i], "-c")) {
            opts.emit_obj = true;
            continue;
        }

        if (!strcmp(argv[i], "--emit-ir")) {
            opts.emit_ir = true;
            continue;
        }

        if (!strcmp(argv[i], "-o")) {
            if (++i >= argc) {
                report_error(NULL, NULL, "Missing argument for -o\n");
                return false;
            }
            opts.output = argv[i];
            continue;
        }

        if (!strcmp(argv[i], "--target")) {
            if (++i >= argc) {
                report_error(NULL, NULL, "Missing argument for --target\n");
                return false;
            }

            opts.target = target_find(argv[i]);
            if (!opts.target) {
                report_error(NULL, NULL, "Unknown target '%s'\n", argv[i]);
                printf("Available targets:\n");
                target_list();
                return false;
            }
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
            opts.color = true;
            continue;
        }

        if (!strcmp(argv[i], "--no-color")) {
            opts.color = false;
            continue;
        }

        report_error(NULL, NULL, "Unknown option '%s'\n", argv[i]);
        return false;
    }

    if (opts.help) {
        options_help(argv[0]);
        return false;
    }

    if (!opts.target)
        opts.target = target_default();

    if (opts.inputs_count == 0 && opts.clang_inputs_count == 0) {
        report_error(NULL, NULL, "No input files specified\n");
        return false;
    }

    return true;
}

void
options_set_output(const char *name)
{
    const char *ext;
    char *out;
    size_t len;

    assert((!name || name[0]) && "impossible");

    if (opts.emit_ir)
        ext = ".ll";
    else if (opts.emit_asm)
        ext = ".S";
    else if (opts.emit_obj)
        ext = ".o";
    else if (!name)
        ext = ".elf";
    else
        ext = "";

    if (!name)
        name = "out";

    len = strlen(name) + strlen(ext) + 1;
    out = malloc(len);
    assert(out && "out of mem");
    snprintf(out, len, "%s%s", name, ext);
    opts.output = out;
}

void
options_help(const char *prog)
{
    printf("Usage: %s [options] file...\n\n", prog);
    printf("Options:\n");
    printf("  -o <file>          Select output file\n");
    printf("  -C <clang_arg>     Pass option directly to the clang backend\n");
    printf("  -c                 Compile to object file without linking\n");
    printf("  -S                 Emit assembly instead of linking\n");
    printf("  --emit-ir          Emit LLVM IR only without invoking clang\n");
    printf("  --color            Force colored diagnostics\n");
    printf("  --no-color         Disable colored diagnostics\n");
    printf("  -h, --help         Display this help message\n");
    printf("  --target <name>    Set target name or triple (default: %s)\n",
           target_default()->name);
    printf("Available targets:\n");
    target_list();
}

bool
options_run_clang(const char *ir_file)
{
    const char **argv;
    size_t i, len, argc;
    pid_t pid;
    int status;

    len = 16 + opts.clang_inputs_count + opts.clang_args_count;
    argv = malloc(len * sizeof(*argv));
    assert(argv && "out of mem");

    argc = 0;
    argv[argc++] = "clang";
    argv[argc++] = "-O1";
    argv[argc++] = "-Wno-override-module";

    if (opts.emit_asm) {
        argv[argc++] = "-S";
    } else if (opts.emit_obj) {
        argv[argc++] = "-c";
    }

    argv[argc++] = "-o";
    argv[argc++] = opts.output;
    argv[argc++] = ir_file;

    for (i = 0; i < opts.clang_inputs_count; i++) {
        argv[argc++] = opts.clang_inputs[i];
    }

    for (i = 0; i < opts.clang_args_count; i++) {
        argv[argc++] = opts.clang_args[i];
    }

    argv[argc++] = "-target";
    argv[argc++] = opts.target->triple;

    if (opts.target->freestanding) {
        argv[argc++] = "-ffreestanding";
        if (!opts.emit_asm && !opts.emit_obj)
            argv[argc++] = "-nostdlib";
    }

    argv[argc] = NULL;
    assert(argc < len && "out of bounds");

    pid = fork();
    if (pid < 0) {
        free(argv);
        return false;
     }

    if (pid == 0) {
        execvp("clang", (char **)argv);
        perror("execvp(clang)");
        _exit(127);
    }

    free(argv);
    if (waitpid(pid, &status, 0) < 0)
        return false;

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
