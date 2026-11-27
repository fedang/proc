#ifndef _IRGEN_H
#define _IRGEN_H

#include <stdio.h>
#include <stdbool.h>

#include "modcache.h"

enum irval_tag {
    IRVAL_INVALID,
    IRVAL_ZERO,
    IRVAL_UNDEF,
    IRVAL_POISON,
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

#define IRVAL_ZERO()    ((struct irval) { IRVAL_ZERO, {} })
#define IRVAL_UNDEF()   ((struct irval) { IRVAL_UNDEF, {} })
#define IRVAL_POISON()  ((struct irval) { IRVAL_POISON, {} })
#define IRVAL_INT(v)    ((struct irval) { IRVAL_INT, { .ival = (v) } })
#define IRVAL_REG(v)    ((struct irval) { IRVAL_REG, { .reg = (v) } })
#define IRVAL_GLOBAL(v) ((struct irval) { IRVAL_GLOBAL, { .global = (v) } })
#define IRVAL_STRING(v) ((struct irval) { IRVAL_STRING, { .reg = (v) } })

struct irgen {
    FILE *out;
    unsigned regs;
    unsigned labels;
    bool terminated;
    unsigned in_block;
    unsigned loop_cond;
    unsigned loop_exit;
    struct irval *slots;
    size_t slots_size;
    struct symbol **externs;
    size_t externs_count;
    size_t externs_size;
    struct value_string *strings;
    size_t strings_count;
    size_t strings_size;
};

void irgen_init(struct irgen *state, FILE *out);

bool irgen_modcache(struct irgen *state, struct modcache *cache);

#endif
