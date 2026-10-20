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
        case LIT_BOOL:
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

struct expr *
expr_make_array(size_t args_count)
{
    struct expr_array *expr;
    size_t size;

    size = sizeof(struct expr_array) + args_count * sizeof(struct expr *);
    expr = expr_make(size, EXPR_ARRAY);
    expr->args_count = args_count;
    return &expr->expr;
}

struct expr *
expr_make_struct(struct expr *op, size_t fields_count)
{
    struct expr_struct *expr;
    size_t size;

    size = sizeof(struct expr_struct) + fields_count * sizeof(struct struct_init);
    expr = expr_make(size, EXPR_STRUCT);
    expr->op = op;
    expr->fields_count = fields_count;
    return &expr->expr;
}

struct expr *
expr_make_macro(struct symbol *name, struct expr *body, size_t args_count)
{
    struct expr_macro *expr;
    size_t size;

    size = sizeof(struct expr_macro) + args_count * sizeof(struct expr *);
    expr = expr_make(size, EXPR_MACRO);
    expr->name = name;
    expr->body = body;
    expr->args_count = args_count;
    return &expr->expr;
}

struct expr *
expr_make_block(size_t items_count)
{
    struct expr_block *expr;
    size_t size;

    size = sizeof(struct expr_block) + items_count * sizeof(struct decl *);
    expr = expr_make(size, EXPR_BLOCK);
    expr->items_count = items_count;
    return &expr->expr;
}

struct expr *
expr_make_if(struct expr *cond, struct expr *b_true, struct expr *b_false)
{
    struct expr_if *expr;

    expr = expr_make(sizeof(struct expr_if), EXPR_IF);
    expr->cond = cond;
    expr->b_true = b_true;
    expr->b_false = b_false;
    return &expr->expr;
}

struct expr *
expr_make_return(struct expr *value)
{
    struct expr_return *expr;

    expr = expr_make(sizeof(struct expr_return), EXPR_RETURN);
    expr->value = value;
    return &expr->expr;
}

struct expr *
expr_make_while(struct expr *cond, struct expr *body)
{
    struct expr_while *expr;

    expr = expr_make(sizeof(struct expr_while), EXPR_WHILE);
    expr->cond = cond;
    expr->body = body;
    return &expr->expr;
}

struct expr *
expr_make_continue(void)
{
    return expr_make(sizeof(struct expr), EXPR_CONTINUE);
}

struct expr *
expr_make_break(void)
{
    return expr_make(sizeof(struct expr), EXPR_BREAK);
}

bool
expr_is_lvalue(const struct expr *expr)
{
    const struct expr_index *index;
    const struct expr_access *access;
    const struct expr_macro *macro;

    switch (expr->tag) {
        case EXPR_IDENT:
            return true;

        case EXPR_INDEX:
            index = (const struct expr_index *)expr;
            if (index->op->type && index->op->type->tag == TYPE_PTR)
                return true;
            return expr_is_lvalue(index->op);

        case EXPR_ACCESS:
            access = (const struct expr_access *)expr;
            if (access->op->type && access->op->type->tag == TYPE_PTR)
                return true;
            return expr_is_lvalue(access->op);

        case EXPR_UNOP:
            return ((const struct expr_unop *)expr)->unop == UNOP_DEREF;

        case EXPR_MACRO:
            macro = (const struct expr_macro *)expr;
            if (macro->macro.tag == MACRO_EXPR)
                return expr_is_lvalue(macro->macro.expr);
            /* fall through */

        default:
            return false;
    }
}

bool
expr_is_mem(const struct expr *expr)
{
    const struct expr_index *index;
    const struct expr_access *access;
    const struct expr_macro *macro;

    switch (expr->tag) {
        case EXPR_IDENT:
        case EXPR_ARRAY:
        case EXPR_STRUCT:
            return true;

        case EXPR_UNOP:
            return ((const struct expr_unop *)expr)->unop == UNOP_DEREF;

        case EXPR_INDEX:
            index = (const struct expr_index *)expr;
            if (index->op->type && index->op->type->tag == TYPE_PTR)
                return true;
            return expr_is_mem(index->op);

        case EXPR_ACCESS:
            access = (const struct expr_access *)expr;
            if (access->op->type->tag == TYPE_PTR)
                return true;
            return expr_is_mem(access->op);

        case EXPR_MACRO:
            macro = (const struct expr_macro *)expr;
            if (macro->macro.tag == MACRO_EXPR)
                return expr_is_mem(macro->macro.expr);
            /* fall through */

        default:
            return false;
    }
}
