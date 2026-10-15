#include <stdlib.h>
#include <stdio.h>

#include "scope.h"

struct symbol *
scope_mangle(struct symbol *name, struct symbol *prefix)
{
    char buf[1024];
    size_t len;

    if (!prefix)
        return name;

    len = snprintf(buf, sizeof(buf), "%s.%s", prefix->str, name->str);
    assert(len < sizeof(buf) && "name too long");

    return symbol_intern(buf, len);
}

struct scope *
scope_make(struct symbol *name, struct symbol *prefix, struct scope *parent)
{
    struct scope *scope = calloc(1, sizeof(struct scope));
    scope->name = name;
    scope->parent = parent;
    scope->prefix = prefix;
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
    bind->link_sym = scope_mangle(sym, scope->prefix);
    bind->status = decl ? RESOLVE_NONE : RESOLVE_DONE;
    bind->scope = scope;
    bind->is_type = is_type;
    bind->is_public = true;
    bind->decl = decl;
    bind->type = type;
    return bind;
}

struct binding *
scope_lookup_local(struct scope *scope, struct symbol *sym, bool is_type)
{
    size_t i;

    for (i = 0; i < scope->bindings_count; i++) {
        if (scope->bindings[i].is_type == is_type &&
            scope->bindings[i].sym == sym)
            return &scope->bindings[i];
    }

    return NULL;
}

struct binding *
scope_lookup(struct scope *scope, struct symbol *sym, bool is_type)
{
    struct binding *bind;

    do {
        bind = scope_lookup_local(scope, sym, is_type);
        if (bind)
            return bind;
    } while ((scope = scope->parent) != NULL);

    return NULL;
}
