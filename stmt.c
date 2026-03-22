#include <stdlib.h>

#include "stmt.h"

static void *
stmt_make(size_t size, enum stmt_tag tag)
{
    struct stmt *stmt;

    stmt = calloc(1, size);
    stmt->tag = tag;
    return stmt;
}

struct stmt *
stmt_make_var(struct symbol *sym, struct type *type, struct expr *value)
{
    struct stmt_var *stmt;

    stmt = stmt_make(sizeof(struct stmt_var), STMT_VAR);
    stmt->sym = sym;
    stmt->type = type;
    stmt->value = value;
    return &stmt->stmt;
}

struct stmt *
stmt_make_expr(struct expr *expr)
{
    struct stmt_expr *stmt;

    stmt = stmt_make(sizeof(struct stmt_expr), STMT_EXPR);
    stmt->expr = expr;
    return &stmt->stmt;
}

struct stmt *
stmt_make_block(size_t items_count)
{
    struct stmt_block *stmt;
    size_t size;

    size = sizeof(struct stmt_block) + items_count * sizeof(struct stmt *);
    stmt = stmt_make(size, STMT_BLOCK);
    stmt->items_count = items_count;
    return &stmt->stmt;
}

struct stmt *
stmt_make_if(struct expr *cond, struct stmt *b_true, struct stmt *b_false)
{
    struct stmt_if *stmt;

    stmt = stmt_make(sizeof(struct stmt_if), STMT_IF);
    stmt->cond = cond;
    stmt->b_true = b_true;
    stmt->b_false = b_false;
    return &stmt->stmt;
}
