#ifndef _IRGEN_H
#define _IRGEN_H

#include <stdio.h>
#include <stdbool.h>

#include "decl.h"

enum irval_tag {
    IRVAL_INVALID,
    IRVAL_ZERO,
    IRVAL_UNDEF,
    IRVAL_INT,
    IRVAL_REG,
    IRVAL_GLOBAL,
    IRVAL_STRING,
};

struct irval {
    enum irval_tag tag;
    union {
        int64_t ival;
        unsigned reg;
        const char *global;
    };
};

#define IRVAL_ZERO() ((struct irval) { IRVAL_ZERO })
#define IRVAL_UNDEF() ((struct irval) { IRVAL_UNDEF })
#define IRVAL_INT(v) ((struct irval) { IRVAL_INT, { .ival = (v) } })
#define IRVAL_REG(v) ((struct irval) { IRVAL_REG, { .reg = (v) } })
#define IRVAL_GLOBAL(v) ((struct irval) { IRVAL_GLOBAL, { .global = (v) } })
#define IRVAL_STRING(v) ((struct irval) { IRVAL_STRING, { .reg = (v) } })

struct irvar {
    struct symbol *sym;
    struct irval val;
};

struct irgen {
    struct source *src;
    FILE *out;
    unsigned regs;
    unsigned labels;

    bool terminated;
    unsigned in_block;
    unsigned loop_cond;
    unsigned loop_exit;

    struct irvar locals[256];
    unsigned locals_count;
    struct irvar globals[256];
    unsigned globals_count;
    const char *strings[256];
    unsigned strings_count;
};

void irgen_init(struct irgen *state, struct source *src, FILE *out);

bool irgen_module(struct irgen *state, struct decl *decl);

#endif
