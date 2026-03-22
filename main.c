#include <stdio.h>
#include <string.h>

#include "irgen.h"
#include "lexer.h"

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


    struct lexer lex;
    struct token *toks;

    const char *src = "proc func(id:i32): bool {\n if (id == 0) { return false; }\n return true;\n}\n";

    lexer_init(&lex, src, strlen(src));
    lexer_tokenize(&lex, &toks);

    static const char *tags[] = {
        "TOKEN_INVALID",
        "TOKEN_EOF",
        "TOKEN_SYMBOL",
        "TOKEN_STRING",
        "TOKEN_INT",
        "TOKEN_LPAREN",
        "TOKEN_RPAREN",
        "TOKEN_LBRACK",
        "TOKEN_RBRACK",
        "TOKEN_LBRACE",
        "TOKEN_RBRACE",
        "TOKEN_COLON",
        "TOKEN_SEMI",
        "TOKEN_COMMA",
        "TOKEN_DOT",
        "TOKEN_HASH",
        "TOKEN_OR",
        "TOKEN_AND",
        "TOKEN_OROR",
        "TOKEN_ANDAND",
        "TOKEN_XOR",
        "TOKEN_NOT",
        "TOKEN_EQ",
        "TOKEN_GT",
        "TOKEN_LT",
        "TOKEN_SHL",
        "TOKEN_SHR",
        "TOKEN_PLUS",
        "TOKEN_MINUS",
        "TOKEN_STAR",
        "TOKEN_SLASH",
        "TOKEN_PERC",
        "TOKEN_EQEQ",
        "TOKEN_NOTEQ",
        "TOKEN_GTEQ",
        "TOKEN_LTEQ",
        "TOKEN_OREQ",
        "TOKEN_ANDEQ",
        "TOKEN_XOREQ",
        "TOKEN_SHLEQ",
        "TOKEN_SHREQ",
        "TOKEN_PLUSEQ",
        "TOKEN_MINUSEQ",
        "TOKEN_STAREQ",
        "TOKEN_SLASHEQ",
        "TOKEN_PERCEQ",
    };

    for (size_t i = 0; toks[i].tag != TOKEN_EOF; i++) {
        struct token *t = &toks[i];

        int len = (int)(t->source.end - t->source.start);

        printf("[%04zu] %-12s '%.*s' (Line %u)\n",
               i,
               tags[t->tag],
               len, t->source.start,
               t->source.start_line);
    }
}
