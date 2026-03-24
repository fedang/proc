#ifndef _EXPR_H
#define _EXPR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "symbol.h"
#include "type.h"
#include "span.h"

enum expr_tag {
    EXPR_INVALID,
    EXPR_CONST,
    EXPR_IDENT,
    EXPR_BINOP,
    EXPR_UNOP,
    EXPR_CAST,
    EXPR_CALL,
    EXPR_INDEX,
    EXPR_ACCESS,
};

enum binop_tag {
    BINOP_INVALID,
    BINOP_EQ,
    BINOP_NOTEQ,
    BINOP_GT,
    BINOP_GTEQ,
    BINOP_LT,
    BINOP_LTEQ,
    BINOP_BOOL_OR,
    BINOP_BOOL_AND,
    BINOP_OR,
    BINOP_AND,
    BINOP_XOR,
    BINOP_SHL,
    BINOP_SHR,
    BINOP_ADD,
    BINOP_SUB,
    BINOP_MUL,
    BINOP_DIV,
    BINOP_MOD,
    BINOP_SET,
    BINOP_OR_SET,
    BINOP_AND_SET,
    BINOP_XOR_SET,
    BINOP_SHL_SET,
    BINOP_SHR_SET,
    BINOP_ADD_SET,
    BINOP_SUB_SET,
    BINOP_MUL_SET,
    BINOP_DIV_SET,
    BINOP_MOD_SET,
};

enum unop_tag {
    UNOP_INVALID,
    UNOP_ADDROF,
    UNOP_DEREF,
    UNOP_NEG,
    UNOP_NOT,
};

struct expr {
    struct span source;
    struct type *type;
    enum expr_tag tag;
};

struct expr_const {
    struct expr expr;
    // assume int for now
    int64_t value;
};

struct expr_ident {
    struct expr expr;
    struct symbol *sym;
};

struct expr_binop {
    struct expr expr;
    struct expr *op_lhs;
    struct expr *op_rhs;
    enum binop_tag binop;
};

struct expr_unop {
    struct expr expr;
    struct expr *op;
    enum unop_tag unop;
};

struct expr_cast {
    struct expr expr;
    struct expr *op;
    struct type *cast;
};

struct expr_call {
    struct expr expr;
    struct expr *op;
    size_t args_count;
    struct expr *args[];
};

struct expr_index {
    struct expr expr;
    struct expr *op;
    struct expr *index;
};

struct expr_access {
    struct expr expr;
    struct expr *op;
    struct symbol *field;
    unsigned offset;
};

static inline bool
expr_is_lvalue(struct expr *expr)
{
    if (expr->tag == EXPR_UNOP)
        return ((struct expr_unop *)expr)->unop == UNOP_DEREF;

    return expr->tag == EXPR_IDENT
        || expr->tag == EXPR_INDEX
        || expr->tag == EXPR_ACCESS;
}

struct expr *expr_make_const(int64_t value);

struct expr *expr_make_ident(struct symbol *sym);

struct expr *expr_make_binop(struct expr *op_lhs, struct expr *op_rhs,
                             enum binop_tag binop);

struct expr *expr_make_unop(struct expr *op, enum unop_tag unop);

struct expr *expr_make_cast(struct expr *op, struct type *type);

struct expr *expr_make_call(struct expr *op, size_t args);

struct expr *expr_make_index(struct expr *op, struct expr *index);

struct expr *expr_make_access(struct expr *op, struct symbol *field);

#endif
