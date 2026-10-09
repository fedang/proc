#ifndef _STMT_H
#define _STMT_H

#include <stddef.h>
#include <stdint.h>

#include "expr.h"

enum stmt_tag {
    STMT_INVALID,
    STMT_VAR,
    STMT_EXPR,
    STMT_BLOCK,
    STMT_IF,
    STMT_RETURN,
    STMT_WHILE,
    STMT_CONTINUE,
    STMT_BREAK,
};

struct stmt {
    struct span source;
    enum stmt_tag tag;
};

struct stmt_var {
    struct stmt stmt;
    struct symbol *sym;
    struct type *type;
    struct expr *value;
};

struct stmt_expr {
    struct stmt stmt;
    struct expr *expr;
};

struct stmt_block {
    struct stmt stmt;
    size_t items_count;
    struct stmt *items[];
};

struct stmt_if {
    struct stmt stmt;
    struct expr *cond;
    struct stmt *b_true;
    struct stmt *b_false;
};

struct stmt_return {
    struct stmt stmt;
    struct expr *expr;
};

struct stmt_while {
    struct stmt stmt;
    struct expr *cond;
    struct stmt *body;
};

struct stmt *stmt_make_var(struct symbol *sym, struct type *type,
                           struct expr *value);

struct stmt *stmt_make_expr(struct expr *expr);

struct stmt *stmt_make_block(size_t items_count);

struct stmt *stmt_make_if(struct expr *cond, struct stmt *b_true,
                          struct stmt *b_false);

struct stmt *stmt_make_return(struct expr *expr);

struct stmt *stmt_make_while(struct expr *cond, struct stmt *body);

struct stmt *stmt_make_continue(void);

struct stmt *stmt_make_break(void);

#endif
