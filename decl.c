#include <stdlib.h>
#include <string.h>

#include "decl.h"
#include "arena.h"

static void *
decl_make(size_t size, enum decl_tag tag)
{
    struct decl *decl;

    decl = arena_alloc_zero(&arena_ast, size);
    decl->tag = tag;
    return decl;
}

struct decl *
decl_make_proc(struct symbol *sym, struct type *out,
               struct proc_arg *args, size_t args_count, struct expr *body)
{
    struct decl_proc *decl;
    size_t size;

    size = sizeof(struct decl_proc) + args_count * sizeof(struct proc_arg);
    decl = decl_make(size, DECL_PROC);
    decl->sym = sym;
    decl->out = out;
    decl->body = body;
    decl->args_count = args_count;
    if (args_count > 0)
        memcpy(decl->args, args, args_count * sizeof(struct proc_arg));
    return &decl->decl;
}

struct decl *
decl_make_struct(struct symbol *sym, struct struct_field *fields,
                 size_t fields_count)
{
    struct decl_struct *decl;
    size_t size;

    size = sizeof(struct decl_struct) + fields_count * sizeof(struct struct_field);
    decl = decl_make(size, DECL_STRUCT);
    decl->sym = sym;
    decl->fields_count = fields_count;
    if (fields_count > 0)
        memcpy(decl->fields, fields, fields_count * sizeof(struct struct_field));
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
decl_make_module(struct symbol *sym, struct type *sig, struct expr *block)
{
    struct decl_module *decl;

    decl = decl_make(sizeof(struct decl_module), DECL_MODULE);
    decl->sym = sym;
    decl->sig = sig;
    decl->block = block;
    return &decl->decl;
}

struct decl *
decl_make_sig(struct symbol *sym, struct expr *block)
{
    struct decl_sig *decl;

    decl = decl_make(sizeof(struct decl_sig), DECL_SIG);
    decl->sym = sym;
    decl->block = block;
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
