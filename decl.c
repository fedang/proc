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
               struct proc_arg *args, struct stmt *body)
{
    struct decl_proc *decl;

    decl = decl_make(sizeof(struct decl_proc), DECL_PROC);
    decl->sym = sym;
    decl->out = out;
    decl->args = args;
    decl->body = body;
    return &decl->decl;
}
