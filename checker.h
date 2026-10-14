#ifndef _CHECKER_H
#define _CHECKER_H

#include <stddef.h>

#include "type.h"
#include "decl.h"
#include "source.h"

enum resolve_status {
    RESOLVE_NONE,
    RESOLVE_PENDING,
    RESOLVE_DONE,
};

struct binding {
    struct symbol *sym;
    struct scope *scope;
    enum resolve_status status;
    bool is_type;
    bool is_public;
    struct decl *decl;
    struct type *type;
};

struct scope {
    struct scope *parent;
    struct symbol *name;
    struct binding bindings[256];
    size_t bindings_count;
};

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

bool checker_module(struct checker *state, struct decl **decls,
                    size_t decls_count);

#endif
