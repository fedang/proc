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
};

struct decl {
    struct source *src;
    enum decl_tag tag;
    struct attr *attr;
};

struct decl_proc {
    struct decl decl;
    struct symbol *sym;
    struct type *out;
    struct proc_arg *args;
    struct stmt *body;
};

struct decl *decl_make_proc(struct symbol *sym, struct type *out,
                            struct proc_arg *args, struct stmt *body);

#endif
