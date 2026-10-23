#ifndef _OPTIONS_H
#define _OPTIONS_H

#include <stdbool.h>

struct options {
    const char **inputs;
    size_t inputs_count;
    size_t inputs_size;
    const char *output;
    const char *ir_file;
    const char **clang_args;
    size_t clang_args_count;
    size_t clang_args_size;
    bool emit_asm;
};

bool options_init(struct options *opts, int argc, const char **argv);

void options_help(const char *prog);

#endif
