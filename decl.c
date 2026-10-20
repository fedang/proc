#include <stdlib.h>

#include "decl.h"

static void *
decl_make(size_t size, enum decl_tag tag)
{
    struct decl *decl;

    decl = calloc(1, size);
    decl->tag = tag;
    return decl;
}

struct decl *
decl_make_proc(struct symbol *sym, struct type *out,
               struct proc_arg *args, size_t args_count, struct expr *body)
{
    struct decl_proc *decl;

    decl = decl_make(sizeof(struct decl_proc), DECL_PROC);
    decl->sym = sym;
    decl->out = out;
    decl->args = args;
    decl->args_count = args_count;
    decl->body = body;
    return &decl->decl;
}

struct decl *
decl_make_struct(struct symbol *sym, struct struct_field *fields,
                 size_t fields_count)
{
    struct decl_struct *decl;

    decl = decl_make(sizeof(struct decl_struct), DECL_STRUCT);
    decl->sym = sym;
    decl->fields = fields;
    decl->fields_count = fields_count;
    return &decl->decl;
}

struct decl *
decl_make_type(struct symbol *sym, struct type *type)
{
    struct decl_type *decl;

    decl = decl_make(sizeof(struct decl_type), DECL_TYPE);
    decl->sym = sym;
    decl->type = type;
    return &decl->decl;
}

struct decl *
decl_make_module(struct symbol *sym, struct type *sig, struct decl **items,
                 size_t items_count)
{
    struct decl_module *decl;

    decl = decl_make(sizeof(struct decl_module), DECL_MODULE);
    decl->sym = sym;
    decl->sig = sig;
    decl->items = items;
    decl->items_count = items_count;
    return &decl->decl;
}

struct decl *
decl_make_sig(struct symbol *sym, struct decl **items, size_t items_count)
{
    struct decl_sig *decl;

    decl = decl_make(sizeof(struct decl_sig), DECL_SIG);
    decl->sym = sym;
    decl->items = items;
    decl->items_count = items_count;
    return &decl->decl;
}

struct decl *
decl_make_import(struct expr *expr)
{
    struct decl_import *decl;

    decl = decl_make(sizeof(struct decl_import), DECL_IMPORT);
    decl->expr = expr;
    return &decl->decl;
}

struct decl *
decl_make_var(struct symbol *sym, struct type *type, struct expr *value)
{
    struct decl_var *decl;

    decl = decl_make(sizeof(struct decl_var), DECL_VAR);
    decl->sym = sym;
    decl->type = type;
    decl->value = value;
    return &decl->decl;
}

struct decl *
decl_make_expr(struct expr *expr)
{
    struct decl_expr *decl;

    decl = decl_make(sizeof(struct decl_expr), DECL_EXPR);
    decl->expr = expr;
    return &decl->decl;
}
