#ifndef _TYPECHECK_H
#define _TYPECHECK_H

#include <stddef.h>

#include "type.h"
#include "decl.h"

struct variable {
    struct symbol *sym;
    struct type *type;
};

struct typecheck {
    int pass;
    struct variable locals[256];
    unsigned locals_count;
    struct variable globals[256];
    unsigned globals_count;
    struct type *ret_type;
};

void typecheck_init(struct typecheck *tychk);

bool typecheck_module(struct typecheck *tychk, struct decl **decls, size_t decls_count);

#endif
