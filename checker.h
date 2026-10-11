#ifndef _CHECKER_H
#define _CHECKER_H

#include <stddef.h>

#include "type.h"
#include "decl.h"
#include "source.h"

struct named {
    struct symbol *sym;
    struct type *type;
};

struct checker {
    struct source *src;
    int pass;
    unsigned loop_depth;
    struct type *ret_type;
    struct named locals[256];
    unsigned locals_count;
    struct named globals[256];
    unsigned globals_count;
    struct named types[256];
    unsigned types_count;
};

void checker_init(struct checker *state, struct source *src);

bool checker_module(struct checker *state, struct decl **decls,
                      size_t decls_count);

#endif
