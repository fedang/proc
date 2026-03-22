#ifndef _TOKEN_H
#define _TOKEN_H

#include "span.h"

enum token_tag {
    TOKEN_INVALID,
    TOKEN_EOF,

    /*
     * Keywords
     */
    TOKEN_PROC,
    TOKEN_LET,
    TOKEN_VAR,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_RETURN,

    /*
     * Literals
     */
    TOKEN_IDENT,
    TOKEN_INT,
    TOKEN_BOOL,
    TOKEN_STRING,

    /*
     * Separators
     */
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COLON,
    TOKEN_SEMI,
    TOKEN_COMMA,
    TOKEN_HASH,

    /*
     * Operators
     */
    TOKEN_OR,
    TOKEN_AND,
    TOKEN_BITOR,
    TOKEN_BITAND,
    TOKEN_BITXOR,
    TOKEN_EQ,
    TOKEN_NEQ,
    TOKEN_GT,
    TOKEN_GTEQ,
    TOKEN_LT,
    TOKEN_LTEQ,
    TOKEN_LSHIFT,
    TOKEN_RSHIFT,
    TOKEN_ADD,
    TOKEN_SUB,
    TOKEN_MUL,
    TOKEN_DIV,
    TOKEN_MOD,
};

struct token {
    struct span source;
    enum token_tag tag;
};

#endif
