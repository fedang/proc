#ifndef _EXPR_H
#define _EXPR_H

#include <stddef.h>
#include <stdint.h>

enum expr_tag {
    EXPR_INVALID,
    EXPR_CONST,
    EXPR_IDENT,
    EXPR_BINOP,
    EXPR_UNOP,
    EXPR_CAST,
    EXPR_CALL,
};

enum binop_tag {
    BINOP_INVALID,
    BINOP_OR,
    BINOP_AND,
    BINOP_BITOR,
    BINOP_BITAND,
    BINOP_BITXOR,
    BINOP_EQ,
    BINOP_NEQ,
    BINOP_GT,
    BINOP_GTEQ,
    BINOP_LT,
    BINOP_LTEQ,
    BINOP_LSHIFT,
    BINOP_RSHIFT,
    BINOP_ADD,
    BINOP_SUB,
    BINOP_MUL,
    BINOP_DIV,
    BINOP_MOD,
};

enum unop_tag {
    UNOP_INVALID,
    UNOP_DEREF,
    UNOP_NEG,
    UNOP_NOT,
};

struct expr {
    struct source *src;
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

struct expr *make_expr_const(int64_t value);

struct expr *make_expr_ident(struct symbol *sym);

struct expr *make_expr_binop(struct expr *op_lhs, struct expr *op_rhs,
                             enum binop_tag binop);

struct expr *make_expr_unop(struct expr *op, enum unop_tag unop);

struct expr *make_expr_cast(struct expr *op, struct type *type);

struct expr *make_expr_call(struct expr *op, size_t args);

#endif
