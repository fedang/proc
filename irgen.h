#ifndef _IRGEN_H
#define _IRGEN_H

#include <stdio.h>

#include "stmt.h"

struct irgen {
    FILE *out;
    unsigned regs;
    unsigned labels;
    unsigned in_block;
};

void irgen_init(struct irgen *state, FILE *out);

void irgen_module(struct irgen *state, struct stmt *stmt);

#endif
