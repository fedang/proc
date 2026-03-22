#include <stdio.h>

#include "expr.h"
#include "irgen.h"

int main()
{
    struct expr *expr =
        expr_make_binop(
            expr_make_const(64),
                expr_make_unop(
                    expr_make_const(12),
                    UNOP_NEG
            ),
            BINOP_AND
        );

    FILE *out = fopen("test.ll", "wb");

    struct irgen state;
    irgen_init(&state, out);

    irgen_expr2(&state, expr);
}
