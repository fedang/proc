#ifndef _DECL_H
#define _DECL_H

#include <stddef.h>
#include <stdint.h>

#include "stmt.h"

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
};

struct decl {
    struct span source;
    enum decl_tag tag;
    struct attr *attr;
};

struct decl_proc {
    struct decl decl;
    struct symbol *sym;
    struct type *out;
    struct proc_arg *args;
    size_t args_count;
    struct stmt *body;
};

struct decl_struct {
    struct decl decl;
    struct symbol *sym;
    struct struct_field *fields;
    size_t fields_count;
};

struct decl_type {
    struct decl decl;
    struct symbol *sym;
    struct type *type;
};

static inline void
decl_set_source(struct decl *decl, struct span source)
{
    decl->source = source;
}

static inline void
decl_set_attr(struct decl *decl, struct attr *attr)
{
    decl->attr = attr;
}

struct decl *decl_make_proc(struct symbol *sym, struct type *out,
                            struct proc_arg *args, size_t args_count,
                            struct stmt *body);

struct decl *decl_make_struct(struct symbol *sym, struct struct_field *fields,
                              size_t fields_count);

struct decl *decl_make_type(struct symbol *sym, struct type *type);

#endif
