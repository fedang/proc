#ifndef _CHECKER_H
#define _CHECKER_H

#include <stddef.h>

#include "type.h"
#include "decl.h"
#include "modcache.h"
#include "scope.h"
#include "eval.h"

struct local_var {
    struct symbol *sym;
    struct type *type;
};

struct checker {
    struct modcache *cache;
    struct eval eval;
    unsigned loop_depth;
    struct type *ret_type;
    struct scope *root_scope;
    struct scope *curr_scope;
    struct local_var *locals;
    size_t locals_count;
    size_t locals_size;
};

void checker_init(struct checker *state, struct modcache *cache);

bool checker_modcache(struct checker *state);

#endif
