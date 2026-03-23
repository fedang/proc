#ifndef _TOKEN_H
#define _TOKEN_H

#include "span.h"

enum token_tag {
    TOKEN_INVALID,

    /*
     * Literals
     */
    TOKEN_SYMBOL,
    TOKEN_STRING,
    TOKEN_INT,

    /*
     * Separators
     */
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACK,
    TOKEN_RBRACK,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COLON,
    TOKEN_SEMI,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_HASH,

    /*
     * Operators
     */
    TOKEN_OR,
    TOKEN_AND,
    TOKEN_OROR,
    TOKEN_ANDAND,
    TOKEN_XOR,
    TOKEN_NOT,
    TOKEN_EQ,
    TOKEN_GT,
    TOKEN_LT,
    TOKEN_SHL,
    TOKEN_SHR,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERC,
    TOKEN_EQEQ,
    TOKEN_NOTEQ,
    TOKEN_GTEQ,
    TOKEN_LTEQ,
    TOKEN_OREQ,
    TOKEN_ANDEQ,
    TOKEN_XOREQ,
    TOKEN_SHLEQ,
    TOKEN_SHREQ,
    TOKEN_PLUSEQ,
    TOKEN_MINUSEQ,
    TOKEN_STAREQ,
    TOKEN_SLASHEQ,
    TOKEN_PERCEQ,

    /*
     * Terminator sentinel
     */
    TOKEN_EOF,
};

struct token {
    struct span source;
    enum token_tag tag;
};

#endif
