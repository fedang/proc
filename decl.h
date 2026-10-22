#ifndef _DECL_H
#define _DECL_H

#include <stddef.h>
#include <stdint.h>

#include "expr.h"

struct scope;

struct binding;

struct proc_arg {
    struct attr *attr;
    struct type *type;
    struct symbol *sym;
    struct proc_arg *next;
};

enum decl_tag {
    DECL_INVALID,
    DECL_PROC,
    DECL_STRUCT,
    DECL_TYPE,
    DECL_MODULE,
    DECL_SIG,
    DECL_IMPORT,
    DECL_VAR,
    DECL_EXPR,
};

struct decl {
    struct span span;
    enum decl_tag tag;
    struct attr *attr;
    struct binding *bind;
};

struct decl_proc {
    struct decl decl;
    struct symbol *sym;
    struct type *out;
    struct proc_arg *args;
    size_t args_count;
    struct expr *body;
};

struct decl_struct {
    struct decl decl;
    struct symbol *sym;
    struct struct_field *fields;
    size_t fields_count;
    struct type *type;
};

struct decl_type {
    struct decl decl;
    struct symbol *sym;
    struct type *type;
};

struct decl_module {
    struct decl decl;
    struct symbol *sym;
    struct symbol *prefix;
    struct type *sig;
    struct expr *block;
    struct type *type;
};

struct decl_sig {
    struct decl decl;
    struct symbol *sym;
    struct expr *block;
};

struct decl_import {
    struct decl decl;
    struct expr *expr;
    struct scope *target_scope;
    bool resolved;
};

struct decl_var {
    struct decl decl;
    struct symbol *sym;
    struct type *type;
    struct expr *value;
    unsigned slot;
};

struct decl_expr {
    struct decl decl;
    struct expr *expr;
};

static inline void
decl_set_span(struct decl *decl, struct span span)
{
    decl->span = span;
}

static inline void
decl_set_attr(struct decl *decl, struct attr *attr)
{
    decl->attr = attr;
}

struct decl *decl_make_proc(struct symbol *sym, struct type *out,
                            struct proc_arg *args, size_t args_count,
                            struct expr *body);

struct decl *decl_make_struct(struct symbol *sym, struct struct_field *fields,
                              size_t fields_count);

struct decl *decl_make_type(struct symbol *sym, struct type *type);

struct decl *decl_make_module(struct symbol *sym, struct type *sig,
                              struct expr *block);

struct decl *decl_make_sig(struct symbol *sym, struct expr *block);

struct decl *decl_make_import(struct expr *expr);


struct decl *decl_make_var(struct symbol *sym, struct type *type,
                           struct expr *value);

struct decl *decl_make_expr(struct expr *expr);

#endif
