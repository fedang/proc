#ifndef _IRGEN_H
#define _IRGEN_H

#include <stdio.h>

#include "decl.h"

enum irval_tag {
    IRVAL_INVALID,
    IRVAL_INT,
    IRVAL_REG,
    IRVAL_LABEL,
    IRVAL_GLOBAL,
};

struct irval {
    enum irval_tag tag;
    union {
        int64_t ival;
        unsigned reg;
        unsigned label;
        const char *global;
    };
};

#define IRVAL_INT(v) ((struct irval) { IRVAL_INT, { .ival = (v) } })
#define IRVAL_REG(v) ((struct irval) { IRVAL_REG, { .reg = (v) } })
#define IRVAL_LABEL(v) ((struct irval) { IRVAL_LABEL, { .label = (v) } })
#define IRVAL_GLOBAL(v) ((struct irval) { IRVAL_GLOBAL, { .global = (v) } })

struct irvar {
    struct symbol *sym;
    struct irval val;
};

struct irgen {
    FILE *out;
    unsigned regs;
    unsigned labels;
    unsigned in_block;

    struct irvar locals[256];
    unsigned locals_count;
    struct irvar globals[256];
    unsigned globals_count;
};

void irgen_init(struct irgen *state, FILE *out);

void irgen_module(struct irgen *state, struct decl **decls, size_t decls_count);

#endif
