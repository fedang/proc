#include <stdlib.h>
#include <stdio.h>

#include "scope.h"

struct scope *
scope_make(struct symbol *name, struct symbol *prefix, struct scope *parent,
           struct modfile *file)
{
    struct scope *scope = calloc(1, sizeof(struct scope));
    scope->name = name;
    scope->prefix = prefix;
    scope->parent = parent;
    scope->file = file ? file : (parent ? parent->file : NULL);
    return scope;
}

struct binding *
scope_push(struct scope *scope, struct symbol *sym,
           struct type *type, struct decl *decl, bool is_type)
{
    struct binding *bind;

    assert(scope->bindings_count < 256 && "too many bindings!");
    bind = &scope->bindings[scope->bindings_count++];

    bind->sym = sym;
    bind->status = decl ? RESOLVE_NONE : RESOLVE_DONE;
    bind->scope = scope;
    bind->is_type = is_type;
    bind->is_public = true;
    bind->decl = decl;
    bind->type = type;

    /*
     * Avoid computing link symbol for declarations that don't need it
     */
    if (decl && (decl->tag == DECL_PROC
        || decl->tag == DECL_STRUCT || decl->tag == DECL_VAR))
        bind->link_sym = symbol_mangle(scope->prefix, sym);

    return bind;
}

struct binding *
scope_lookup_local(struct scope *scope, struct symbol *sym)
{
    size_t i;

    for (i = 0; i < scope->bindings_count; i++) {
        if (scope->bindings[i].sym == sym)
            return &scope->bindings[i];
    }

    return NULL;
}

struct binding *
scope_lookup(struct scope *scope, struct symbol *sym)
{
    struct binding *bind;

    do {
        bind = scope_lookup_local(scope, sym);
        if (bind)
            return bind;
    } while ((scope = scope->parent) != NULL);

    return NULL;
}
