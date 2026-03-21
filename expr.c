#include <stdlib.h>

#include "expr.h"

static void *
make_expr(size_t size, enum expr_tag tag)
{
    struct expr *expr;

    expr = calloc(1, size);
    expr->tag = tag;
    return expr;
}

struct expr *
make_expr_const(int64_t value)
{
    struct expr_const *expr;

    expr = make_expr(sizeof(struct expr_const), EXPR_CONST);
    expr->value = value;
    return (struct expr *)expr;
}

struct expr *
make_expr_ident(struct symbol *sym)
{
    struct expr_ident *expr;

    expr = make_expr(sizeof(struct expr_ident), EXPR_IDENT);
    expr->sym = sym;
    return (struct expr *)expr;
}

struct expr *
make_expr_binop(struct expr *op_lhs, struct expr *op_rhs, enum binop_tag binop)
{
    struct expr_binop *expr;

    expr = make_expr(sizeof(struct expr_binop), EXPR_BINOP);
    expr->binop = binop;
    expr->op_lhs = op_lhs;
    expr->op_rhs = op_rhs;
    return (struct expr *)expr;
}

struct expr *
make_expr_unop(struct expr *op, enum unop_tag unop)
{
    struct expr_unop *expr;

    expr = make_expr(sizeof(struct expr_unop), EXPR_UNOP);
    expr->unop = unop;
    expr->op = op;
    return (struct expr *)expr;
}

struct expr *
make_expr_cast(struct expr *op, struct type *type)
{
    struct expr_cast *expr;

    expr = make_expr(sizeof(struct expr_cast), EXPR_CAST);
    expr->op = op;
    expr->cast = type;
    return (struct expr *)expr;
}

struct expr *
make_expr_call(struct expr *op, size_t args)
{
    struct expr_call *expr;

    expr = make_expr(sizeof(struct expr_call) + args * sizeof(struct expr *),
                     EXPR_CALL);
    expr->op = op;
    expr->args_count = args;
    return (struct expr *)expr;
}
