#ifndef _IRGEN_H
#define _IRGEN_H

#include <stdio.h>

#include "expr.h"

struct irgen {
    FILE *out;
    unsigned regs;
    unsigned labels;
};

void irgen_init(struct irgen *state, FILE *out);

void irgen_expr2(struct irgen *state, struct expr *expr);

#endif
