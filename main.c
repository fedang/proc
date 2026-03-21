#include <stdio.h>

#include "expr.h"
#include "irgen.h"

int main()
{
    struct expr *expr =
        make_expr_binop(
            make_expr_const(64),
                make_expr_unop(
                    make_expr_const(12),
                    UNOP_NEG
            ),
            BINOP_ADD
        );

    FILE *out = fopen("test.ll", "wb");

    struct irgen state;
    irgen_init(&state, out);

    irgen_expr2(&state, expr);
}
