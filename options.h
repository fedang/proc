#ifndef _OPTIONS_H
#define _OPTIONS_H

#include <stdbool.h>

struct options {
    const char **inputs;
    size_t inputs_count;
    size_t inputs_size;
    const char **clang_inputs;
    size_t clang_inputs_count;
    size_t clang_inputs_size;
    const char **clang_args;
    size_t clang_args_count;
    size_t clang_args_size;
    const char *output;
    const char *ir_file;
    bool emit_asm;
    bool emit_obj;
    bool emit_ir;
    bool color;
    bool help;
};

extern struct options opts;

bool options_init(int argc, const char **argv);

void options_help(const char *prog);

bool options_run_clang(void);

#endif
