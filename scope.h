#ifndef _SCOPE_H
#define _SCOPE_H

#include <stddef.h>
#include <stdbool.h>

#include "type.h"
#include "decl.h"

#define BIND_TYPE   (1 << 0)
#define BIND_MACRO  (1 << 1)
#define BIND_LINKED (1 << 2)
#define BIND_SEALED (1 << 3)

#define BIND_CHECK_FLAG(b, f)   (((b)->flags & (f)) == (f))
#define BIND_CLEAR_FLAG(b, f)   ((b)->flags &= ~(f))
#define BIND_SET_FLAG(b, f)     ((b)->flags |= (f))

#define BIND_IS_TYPE(b)     BIND_CHECK_FLAG(b, BIND_TYPE)
#define BIND_IS_MACRO(b)    BIND_CHECK_FLAG(b, BIND_MACRO)
#define BIND_IS_LINKED(b)   BIND_CHECK_FLAG(b, BIND_LINKED)
#define BIND_IS_SEALED(b)   BIND_CHECK_FLAG(b, BIND_SEALED)


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
    struct decl *decl;
    struct decl *orig_decl;
    struct type *type;
    struct type *seal_type;
    int flags;
};

struct scope {
    struct symbol *name;
    struct symbol *prefix;
    struct scope *parent;
    struct modfile *file;
    struct decl *decl;
    struct binding bindings[256];
    size_t bindings_count;
};

struct scope *scope_make(struct symbol *name, struct symbol *prefix,
                         struct scope *parent, struct modfile *file);

struct binding *scope_push(struct scope *scope, struct symbol *sym,
                           struct type *type, struct decl *decl, int flags);

struct binding *scope_lookup_local(struct scope *scope, struct symbol *sym);

struct binding *scope_lookup(struct scope *scope, struct symbol *sym);

bool scope_contained(const struct scope *curr, const struct scope *target);

#endif
