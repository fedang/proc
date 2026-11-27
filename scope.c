#include <stdlib.h>
#include <stdio.h>

#include "arena.h"
#include "scope.h"

struct scope *
scope_make(struct symbol *name, struct symbol *prefix, struct scope *parent,
           struct modfile *file)
{
    struct scope *scope;

    scope = arena_alloc_zero(&arena_scope, sizeof(struct scope));
    scope->name = name;
    scope->prefix = prefix;
    scope->parent = parent;
    scope->file = file ? file : (parent ? parent->file : NULL);
    return scope;
}

struct binding *
scope_push(struct scope *scope, struct symbol *sym,
           struct type *type, struct decl *decl, int flags)
{
    struct binding *bind;

    bind = arena_alloc_zero(&arena_scope, sizeof(struct binding));
    if (scope->tail) {
        scope->tail->next = bind;
    } else {
        scope->head = bind;
    }
    scope->tail = bind;

    bind->sym = sym;
    bind->status = decl ? RESOLVE_NONE : RESOLVE_DONE;
    bind->scope = scope;
    bind->decl = decl;
    bind->type = type;
    bind->flags = flags;

    if (BIND_IS_LINKED(bind))
        bind->link_sym = symbol_mangle(scope->prefix, sym);

    return bind;
}

struct binding *
scope_lookup_local(struct scope *scope, struct symbol *sym)
{
    struct binding *bind;

    for (bind = scope->head; bind != NULL; bind = bind->next) {
        if (bind->sym == sym)
            return bind;
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

bool
scope_contained(const struct scope *curr, const struct scope *target)
{
    while (curr != NULL) {
        if (curr == target)
            return true;
        curr = curr->parent;
    }

    return false;
}
