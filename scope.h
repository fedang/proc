#ifndef _SCOPE_H
#define _SCOPE_H

#include <stddef.h>
#include <stdbool.h>

#include "type.h"
#include "decl.h"

enum resolve_status {
    RESOLVE_NONE,
    RESOLVE_PENDING,
    RESOLVE_DONE,
};

struct binding {
    struct symbol *sym;
    struct symbol *link_sym;
    struct symbol *import_sym;
    struct scope *scope;
    enum resolve_status status;
    bool is_type;
    bool is_public;
    struct decl *decl;
    struct type *type;
};

struct scope {
    struct symbol *name;
    struct symbol *prefix;
    struct scope *parent;
    struct modfile *file;
    struct binding bindings[256];
    size_t bindings_count;
};

struct symbol *scope_mangle(struct symbol *name, struct symbol *prefix);

struct scope *scope_make(struct symbol *name, struct symbol *prefix,
                         struct scope *parent, struct modfile *file);

struct binding *scope_push(struct scope *scope, struct symbol *sym,
                           struct type *type, struct decl *decl, bool is_type);

struct binding *scope_lookup_local(struct scope *scope, struct symbol *sym);

struct binding *scope_lookup(struct scope *scope, struct symbol *sym);

#endif
