#ifndef _CHECKER_H
#define _CHECKER_H

#include <stddef.h>

#include "type.h"
#include "decl.h"
#include "source.h"
#include "scope.h"

struct local_var {
    struct symbol *sym;
    struct type *type;
};

struct checker {
    struct source *src;
    unsigned loop_depth;
    struct type *ret_type;
    struct scope *root_scope;
    struct scope *curr_scope;
    struct local_var locals[256];
    unsigned locals_count;
};

void checker_init(struct checker *state, struct source *src);

bool checker_module(struct checker *state, struct decl *decl);

#endif
