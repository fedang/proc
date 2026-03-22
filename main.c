#include <stdio.h>

#include "irgen.h"

int main()
{
    struct expr *call = expr_make_call(expr_make_ident(symbol_make("print32")), 1);
    ((struct expr_call *)call)->args[0] = expr_make_const(59);

    struct expr *expr =
        expr_make_binop(
            expr_make_binop(
                expr_make_const(64),
                    expr_make_unop(
                        expr_make_const(12),
                        UNOP_NEG
                ),
                BINOP_ADD
            ),
            call,
            BINOP_AND
        );

    struct stmt *stmt = stmt_make_block(3);
    ((struct stmt_block *)stmt)->items[0] =
        stmt_make_var(
                symbol_make("xxx"),
                NULL,
                expr_make_const(59)
        );

    ((struct stmt_block *)stmt)->items[1] =
        stmt_make_expr(expr);

    ((struct stmt_block *)stmt)->items[2] =
        stmt_make_return(expr_make_ident(symbol_make("xxx")));

    struct proc_arg darg = { 0 };
    darg.sym = symbol_make("x");

    struct decl *forward =
        decl_make_proc(
                symbol_make("print32"),
                NULL,
                &darg,
                NULL
        );

    struct decl *decl =
        decl_make_proc(
                symbol_make("main"),
                NULL,
                NULL,
                stmt
        );

    struct decl *decls[] = {
        forward,
        decl,
    };

    FILE *out = fopen("test.ll", "wb");

    struct irgen state;
    irgen_init(&state, out);
    irgen_module(&state, decls, 2);
}
