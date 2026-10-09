#ifndef _TYPECHECK_H
#define _TYPECHECK_H

#include <stddef.h>

#include "type.h"
#include "decl.h"

struct named {
    struct symbol *sym;
    struct type *type;
};

struct typecheck {
    int pass;
    struct type *ret_type;
    struct named locals[256];
    unsigned locals_count;
    struct named globals[256];
    unsigned globals_count;
    struct named types[256];
    unsigned types_count;
};

void typecheck_init(struct typecheck *tychk);

bool typecheck_module(struct typecheck *tychk, struct decl **decls, size_t decls_count);

#endif
