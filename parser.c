#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "parser.h"

static struct symbol *sym_let;
static struct symbol *sym_var;
static struct symbol *sym_if;
static struct symbol *sym_else;
static struct symbol *sym_return;
static struct symbol *sym_proc;

static struct symbol *sym_i8;
static struct symbol *sym_i16;
static struct symbol *sym_i32;
static struct symbol *sym_i64;
static struct symbol *sym_int;
static struct symbol *sym_u8;
static struct symbol *sym_u16;
static struct symbol *sym_u32;
static struct symbol *sym_u64;
static struct symbol *sym_uint;
static struct symbol *sym_bool;
static struct symbol *sym_void;

void
parser_init(struct parser *state, struct token *tokens)
{
    state->tokens = tokens;
    state->curr = 0;
    state->error = false;
    state->perfect = true;

    /*
     * Intern keyword symbols
     */
    sym_let = symbol_make("let");
    sym_var = symbol_make("var");
    sym_if = symbol_make("if");
    sym_else = symbol_make("else");
    sym_return = symbol_make("return");
    sym_proc = symbol_make("proc");

    /*
     * Intern type symbols
     */
    sym_i8 = symbol_make("i8");
    sym_i16 = symbol_make("i16");
    sym_i32 = symbol_make("i32");
    sym_i64 = symbol_make("i64");
    sym_int = symbol_make("int");
    sym_u8 = symbol_make("u8");
    sym_u16 = symbol_make("u16");
    sym_u32 = symbol_make("u32");
    sym_u64 = symbol_make("u64");
    sym_uint = symbol_make("uint");
    sym_bool = symbol_make("bool");
    sym_void = symbol_make("void");
}

static inline struct token *
parser_curr(struct parser *state)
{
    return &state->tokens[state->curr];
}

static inline struct token *
parser_prev(struct parser *state)
{
    return &state->tokens[state->curr - 1];
}

static inline bool
parser_eof(struct parser *state)
{
    return parser_curr(state)->tag == TOKEN_EOF;
}

static inline void
parser_advance(struct parser *state)
{
    if (!parser_eof(state))
        state->curr++;
}

static inline bool
parser_match(struct parser *state, enum token_tag tag)
{
    if (parser_curr(state)->tag != tag)
        return false;

    parser_advance(state);
    return true;
}

static void
parser_error(struct parser *state, const char *msg)
{
    printf("Error at line %u: %s\n",
           parser_curr(state)->source.start_line, msg);

    state->error = true;
    state->perfect = false;
}

static inline bool
parser_check(struct parser *state, enum token_tag tag, const char *msg)
{
    if (parser_match(state, tag))
        return true;

    parser_error(state, msg);
    return false;
}

static bool
parser_peek_sym(struct parser *state, struct symbol **sym)
{
    struct token *token;

    if (parser_curr(state)->tag != TOKEN_SYMBOL)
        return false;

    token = parser_curr(state);
    *sym = symbol_intern(token->source.start,
                         token->source.end - token->source.start);
    return true;
}

static bool
parser_match_sym(struct parser *state, struct symbol **sym)
{
    if (!parser_peek_sym(state, sym))
        return false;

    parser_advance(state);
    return true;
}

static bool
parser_check_sym(struct parser *state, struct symbol **sym, const char *msg)
{
    if (parser_match_sym(state, sym))
        return true;

    parser_error(state, msg);
    return false;
}

static void
parser_sync(struct parser *state)
{
    struct symbol *sym;

    state->error = false;

    while (!parser_eof(state)) {
        if (parser_peek_sym(state, &sym)) {
            if (sym == sym_proc)
                return;
        }

        parser_advance(state);
    }
}

/*
 * Types
 */

#define MATCH_TYPE(x, ...) \
    do { \
        if (sym == (x)) { \
            *type = __VA_ARGS__; \
            return true; \
        } \
    } while (false)

static bool
parser_type(struct parser *state, struct type **type)
{
    struct symbol *sym;
    struct type *pointer;

    if (parser_match(state, TOKEN_STAR)) {
        if (!parser_type(state, &pointer))
            return false;

        *type = type_get_ptr(NULL, pointer);
        return true;
    }

    if (!parser_check_sym(state, &sym, "Expected primitive type"))
        return false;

    MATCH_TYPE(sym_i8, type_get_int(NULL, 8));
    MATCH_TYPE(sym_i16, type_get_int(NULL, 16));
    MATCH_TYPE(sym_i32, type_get_int(NULL, 32));
    MATCH_TYPE(sym_i64, type_get_int(NULL, 64));
    MATCH_TYPE(sym_int, type_get_int(NULL, 0));

    MATCH_TYPE(sym_u8, type_get_uint(NULL, 8));
    MATCH_TYPE(sym_u16, type_get_uint(NULL, 16));
    MATCH_TYPE(sym_u32, type_get_uint(NULL, 32));
    MATCH_TYPE(sym_u64, type_get_uint(NULL, 64));
    MATCH_TYPE(sym_uint, type_get_uint(NULL, 0));

    MATCH_TYPE(sym_bool, type_get_bool(NULL));
    MATCH_TYPE(sym_void, type_get_void(NULL));

    parser_error(state, "Unknown type name");
    return false;
}

/*
 * Expressions
 */
static bool parser_expr_prec(struct parser *state, struct expr **expr,
                             unsigned base_prec);

static bool
parser_expr_simple(struct parser *state, struct expr **expr)
{
    int64_t value;
    struct symbol *sym;
    struct expr *op;

    if (parser_match(state, TOKEN_INT)) {
        value = strtol(parser_prev(state)->source.start, NULL, 10);
        *expr = expr_make_const(value);
        return true;
    }

    if (parser_match(state, TOKEN_STRING)) {
        value = strtol(parser_prev(state)->source.start, NULL, 10);
        *expr = expr_make_const(value);
        return true;
    }

    if (parser_match_sym(state, &sym)) {
        // TODO: Check for disallowed keywords?
        *expr = expr_make_ident(sym);
        return true;
    }

    if (parser_match(state, TOKEN_LPAREN)) {
        if (!parser_expr_prec(state, expr, 0))
            return false;

        return parser_check(state, TOKEN_RPAREN, "Expected ')' after expr");
    }

    if (parser_match(state, TOKEN_MINUS)) {
        if (!parser_expr_prec(state, &op, 13))
            return false;

        *expr = expr_make_unop(op, UNOP_NEG);
        return true;
    }

    if (parser_match(state, TOKEN_NOT)) {
        if (!parser_expr_prec(state, &op, 13))
            return false;

        *expr = expr_make_unop(op, UNOP_NOT);
        return true;
    }

    if (parser_match(state, TOKEN_STAR)) {
        if (!parser_expr_prec(state, &op, 13))
            return false;

        *expr = expr_make_unop(op, UNOP_DEREF);
        return true;
    }

    if (parser_match(state, TOKEN_AND)) {
        if (!parser_expr_prec(state, &op, 13))
            return false;

        *expr = expr_make_unop(op, UNOP_ADDROF);
        return true;
    }

    parser_error(state, "Expected expression");
    return false;
}

static inline unsigned
parser_token_prec(enum token_tag tag)
{
    static const int prec_table[TOKEN_EOF] = {
        [TOKEN_EQ] = 1,
        [TOKEN_ANDEQ] = 1,
        [TOKEN_XOREQ] = 1,
        [TOKEN_SHLEQ] = 1,
        [TOKEN_SHREQ] = 1,
        [TOKEN_PLUSEQ] = 1,
        [TOKEN_MINUSEQ] = 1,
        [TOKEN_STAREQ] = 1,
        [TOKEN_SLASHEQ] = 1,
        [TOKEN_PERCEQ] = 1,
        [TOKEN_OROR] = 2,
        [TOKEN_ANDAND] = 3,
        [TOKEN_OR] = 4,
        [TOKEN_XOR] = 5,
        [TOKEN_AND] = 6,
        [TOKEN_EQEQ] = 7,
        [TOKEN_NOTEQ] = 7,
        [TOKEN_LT] = 8,
        [TOKEN_GT] = 8,
        [TOKEN_LTEQ] = 8,
        [TOKEN_GTEQ] = 8,
        [TOKEN_SHL] = 9,
        [TOKEN_SHR] = 9,
        [TOKEN_PLUS] = 10,
        [TOKEN_MINUS] = 10,
        [TOKEN_STAR] = 11,
        [TOKEN_SLASH] = 11,
        [TOKEN_PERC] = 11,
        [TOKEN_LPAREN] = 12,
        [TOKEN_LBRACK] = 12,
        [TOKEN_DOT] = 12,
    };
    return prec_table[tag];
}

static inline enum binop_tag
parser_token_binop(enum token_tag tag)
{
    static const enum binop_tag binop_table[TOKEN_EOF] = {
        [TOKEN_OR] = BINOP_OR,
        [TOKEN_AND] = BINOP_AND,
        [TOKEN_OROR] = BINOP_BOOL_OR,
        [TOKEN_ANDAND] = BINOP_BOOL_AND,
        [TOKEN_XOR] = BINOP_XOR,
        [TOKEN_EQ] = BINOP_SET,
        [TOKEN_GT] = BINOP_GT,
        [TOKEN_LT] = BINOP_LT,
        [TOKEN_SHL] = BINOP_SHL,
        [TOKEN_SHR] = BINOP_SHR,
        [TOKEN_PLUS] = BINOP_ADD,
        [TOKEN_MINUS] = BINOP_SUB,
        [TOKEN_STAR] = BINOP_MUL,
        [TOKEN_SLASH] = BINOP_DIV,
        [TOKEN_PERC] = BINOP_MOD,
        [TOKEN_EQEQ] = BINOP_EQ,
        [TOKEN_NOTEQ] = BINOP_NOTEQ,
        [TOKEN_GTEQ] = BINOP_GTEQ,
        [TOKEN_LTEQ] = BINOP_LTEQ,
        [TOKEN_OREQ] = BINOP_OR_SET,
        [TOKEN_ANDEQ] = BINOP_AND_SET,
        [TOKEN_XOREQ] = BINOP_XOR_SET,
        [TOKEN_SHLEQ] = BINOP_SHL_SET,
        [TOKEN_SHREQ] = BINOP_SHR_SET,
        [TOKEN_PLUSEQ] = BINOP_ADD_SET,
        [TOKEN_MINUSEQ] = BINOP_SUB_SET,
        [TOKEN_STAREQ] = BINOP_MUL_SET,
        [TOKEN_SLASHEQ] = BINOP_DIV_SET,
        [TOKEN_PERCEQ] = BINOP_MOD_SET,
    };
    return binop_table[tag];
}

static bool
parser_expr_prec(struct parser *state, struct expr **expr, unsigned base_prec)
{
    struct expr *rhs, *args[32];
    enum token_tag op_tag;
    struct symbol *field;
    unsigned prec, count, i;

    if (!parser_expr_simple(state, expr))
        return false;

    while (true) {
        op_tag = parser_curr(state)->tag;
        prec = parser_token_prec(op_tag);

        if (prec < base_prec || prec == 0)
            break;

        parser_advance(state);

        /*
         * Special cases
         */
        if (op_tag == TOKEN_LPAREN) {
            count = 0;
            if (parser_curr(state)->tag != TOKEN_RPAREN) {
                do {
                    assert(count < 32 && "too many args");
                    if (!parser_expr_prec(state, &args[count++], 0))
                        return false;
                } while (parser_match(state, TOKEN_COMMA));
            }

            if (!parser_check(state, TOKEN_RPAREN, "Expected ')' after arguments"))
                return false;

            *expr = expr_make_call(*expr, count);
            for (i = 0; i < count; i++) {
                ((struct expr_call *)*expr)->args[i] = args[i];
            }
            continue;
        }

        if (op_tag == TOKEN_LBRACK) {
            if (!parser_expr_prec(state, &rhs, 0))
                return false;

            if (!parser_check(state, TOKEN_RBRACK, "Expected ']' after index"))
                return false;

            *expr = expr_make_index(*expr, rhs);
            continue;
        }

        if (op_tag == TOKEN_DOT) {
            if (!parser_check_sym(state, &field, "Expected field name after '.'"))
                return false;

            *expr = expr_make_access(*expr, field);
            continue;
        }

        /*
         * Assignment operators are right associative
         */
        if (prec != 1) {
            prec++;
        }

        if (!parser_expr_prec(state, &rhs, prec))
            return false;

        *expr = expr_make_binop(*expr, rhs, parser_token_binop(op_tag));
    }

    return true;
}

static bool
parser_expr(struct parser *state, struct expr **expr)
{
    return parser_expr_prec(state, expr, 0);
}

/*
 * Statements
 */
static bool parser_stmt(struct parser *state, struct stmt **stmt);

static bool
parser_stmt_block(struct parser *state, struct stmt **stmt)
{
    struct stmt *tmp[32];
    unsigned n;

    n = 0;

    while (!parser_eof(state)) {
        if (parser_match(state, TOKEN_RBRACE))
            break;

        assert(n++ <= 32);

        if (!parser_stmt(state, &tmp[n-1]))
            return false;
    }

    *stmt = stmt_make_block(n);
    memcpy(((struct stmt_block *)*stmt)->items, tmp, n * sizeof(struct stmt *));
    return true;
}

static bool
parser_stmt_var(struct parser *state, struct stmt **stmt)
{
    struct symbol *name;
    struct type *type;
    struct expr *value;

    if (!parser_check_sym(state, &name, "Expected var name"))
        return false;

    type = NULL;
    if (parser_match(state, TOKEN_COLON)) {
        if (!parser_type(state, &type))
            return false;
    }

    if (!parser_check(state, TOKEN_EQ, "Expected '=' after var"))
        return false;

    if (!parser_expr(state, &value))
        return false;

    if (!parser_check(state, TOKEN_SEMI, "Expected ';' after var"))
        return false;

    *stmt = stmt_make_var(name, type, value);
    return true;
}

static bool
parser_stmt_return(struct parser *state, struct stmt **stmt)
{
    struct expr *value;

    value = NULL;
    if (parser_curr(state)->tag != TOKEN_SEMI) {
        if (!parser_expr(state, &value))
            return false;

    }

    if (!parser_check(state, TOKEN_SEMI, "Expect ';' after return"))
        return false;

    *stmt = stmt_make_return(value);
    return true;
}

static bool
parser_stmt_if(struct parser *state, struct stmt **stmt)
{
    struct symbol *sym;
    struct expr *cond;
    struct stmt *t_branch, *f_branch;

    if (!parser_expr(state, &cond))
        return false;

    if (!parser_check(state, TOKEN_LBRACE, "Expect '{' after if condition"))
        return false;

    if (!parser_stmt_block(state, &t_branch))
        return false;

    f_branch = NULL;
    if (parser_peek_sym(state, &sym) && sym == sym_else) {
        parser_advance(state);

        if (!parser_check(state, TOKEN_LBRACE, "Expect '{' after else"))
            return false;

        if (!parser_stmt_block(state, &f_branch))
            return false;
    }

    *stmt = stmt_make_if(cond, t_branch, f_branch);
    return true;
}

static bool
parser_stmt(struct parser *state, struct stmt **stmt)
{
    struct symbol *sym;
    struct expr *expr;

    if (parser_match(state, TOKEN_LBRACE)) {
        return parser_stmt_block(state, stmt);
    }

    if (parser_peek_sym(state, &sym)) {
        if (sym == sym_return) {
            parser_advance(state);
            return parser_stmt_return(state, stmt);
        }

        if (sym == sym_if) {
            parser_advance(state);
            return parser_stmt_if(state, stmt);
        }

        if (sym == sym_var) {
            parser_advance(state);
            return parser_stmt_var(state, stmt);
        }
    }

    if (!parser_expr(state, &expr))
        return false;

    if (!parser_check(state, TOKEN_SEMI, "Expect ';' after stmt"))
        return false;

    *stmt = stmt_make_expr(expr);
    return true;
}

/*
 * Declarations
 */
static bool
parser_decl_proc(struct parser *state, struct decl **decl)
{
    struct symbol *sym, *arg_sym;
    struct type *out, *arg_type;
    struct stmt *body;
    struct proc_arg *args, **tail, *tmp;

    if (!parser_check_sym(state, &sym, "Expected procedure name"))
        return false;

    if (!parser_check(state, TOKEN_LPAREN, "Expected '(' after function name"))
        return false;

    args = NULL;
    tail = &args;

    if (!parser_match(state, TOKEN_RPAREN)) {
        do {
            if (!parser_check_sym(state, &arg_sym, "Expected arg name"))
                return false;

            if (!parser_check(state, TOKEN_COLON, "Expected ':' after arg name"))
                return false;

            arg_type = NULL;
            if (!parser_type(state, &arg_type))
                return false;

            tmp = calloc(1, sizeof(struct proc_arg));
            tmp->sym = arg_sym;
            tmp->type = arg_type;

            *tail = tmp;
            tail = &tmp->next;
        } while (parser_match(state, TOKEN_COMMA));

        if (!parser_check(state, TOKEN_RPAREN, "Expected ')' after function args"))
            return false;
    }

    out = NULL;
    if (parser_match(state, TOKEN_COLON)) {
        if (!parser_type(state, &out))
            return false;
    }

    if (parser_match(state, TOKEN_SEMI)) {
        body = NULL;
    } else {
        if (!parser_check(state, TOKEN_LBRACE, "Expected '{' or ';' after function signature."))
            return false;

        if (!parser_stmt_block(state, &body))
            return false;
    }

    *decl = decl_make_proc(sym, out, args, body);
    return true;
}

static bool
parser_decl(struct parser *state, struct decl **decl)
{
    struct symbol *sym;

    if (!parser_check_sym(state, &sym, "Expected proc decl"))
        return false;

    if (sym == sym_proc) {
        return parser_decl_proc(state, decl);
    }

    parser_error(state, "Unknown declaration type");
    return false;
}

bool
parser_module(struct parser *state, struct decl ***decls, size_t *decls_count)
{
    size_t max;

    max = 0;
    *decls = NULL;
    *decls_count = 0;

    while (!parser_eof(state)) {
        if (*decls_count >= max) {
            max = max < 32 ? 32 : max * 2;
            *decls = realloc(*decls, max * sizeof(struct decl *));
            assert(*decls && "out of mem");
        }

        if (parser_decl(state, &(*decls)[*decls_count])) {
            (*decls_count)++;
        } else {
            parser_sync(state);
        }
    }

    return state->perfect;
}
