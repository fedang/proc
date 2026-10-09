#include <stdlib.h>

#include "expr.h"

static void *
expr_make(size_t size, enum expr_tag tag)
{
    struct expr *expr;

    expr = calloc(1, size);
    expr->tag = tag;
    return expr;
}

struct expr *
expr_make_literal(enum literal_tag literal, intptr_t value)
{
    struct expr_literal *expr;

    expr = expr_make(sizeof(struct expr_literal), EXPR_LITERAL);
    expr->literal = literal;

    switch (literal) {
        case LIT_INTEGER:
            expr->integer = value;
            break;

        case LIT_STRING:
            expr->string = (void *)value;
            break;

        default:
            break;
    }
    return &expr->expr;
}

struct expr *
expr_make_ident(struct symbol *sym)
{
    struct expr_ident *expr;

    expr = expr_make(sizeof(struct expr_ident), EXPR_IDENT);
    expr->sym = sym;
    return &expr->expr;
}

struct expr *
expr_make_binop(struct expr *op_lhs, struct expr *op_rhs, enum binop_tag binop)
{
    struct expr_binop *expr;

    expr = expr_make(sizeof(struct expr_binop), EXPR_BINOP);
    expr->binop = binop;
    expr->op_lhs = op_lhs;
    expr->op_rhs = op_rhs;
    return &expr->expr;
}

struct expr *
expr_make_unop(struct expr *op, enum unop_tag unop)
{
    struct expr_unop *expr;

    expr = expr_make(sizeof(struct expr_unop), EXPR_UNOP);
    expr->unop = unop;
    expr->op = op;
    return &expr->expr;
}

struct expr *
expr_make_call(struct expr *op, size_t args_count)
{
    struct expr_call *expr;
    size_t size;

    size = sizeof(struct expr_call) + args_count * sizeof(struct expr *);
    expr = expr_make(size, EXPR_CALL);
    expr->op = op;
    expr->args_count = args_count;
    return &expr->expr;
}

struct expr *
expr_make_builtin(struct symbol *name, size_t args_count)
{
    struct expr_builtin *expr;
    size_t size;

    size = sizeof(struct expr_builtin) + args_count * sizeof(struct expr *);
    expr = expr_make(size, EXPR_BUILTIN);
    expr->name = name;
    expr->args_count = args_count;
    return &expr->expr;
}

struct expr *
expr_make_index(struct expr *op, struct expr *index)
{
    struct expr_index *expr;

    expr = expr_make(sizeof(struct expr_index), EXPR_INDEX);
    expr->op = op;
    expr->index = index;
    return &expr->expr;
}

struct expr *
expr_make_access(struct expr *op, struct symbol *field)
{
    struct expr_access *expr;

    expr = expr_make(sizeof(struct expr_access), EXPR_ACCESS);
    expr->op = op;
    expr->field = field;
    return &expr->expr;
}
