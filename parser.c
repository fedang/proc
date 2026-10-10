#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "parser.h"
#include "report.h"

static struct symbol *sym_let;
static struct symbol *sym_var;
static struct symbol *sym_if;
static struct symbol *sym_else;
static struct symbol *sym_return;
static struct symbol *sym_while;
static struct symbol *sym_break;
static struct symbol *sym_continue;
static struct symbol *sym_proc;
static struct symbol *sym_struct;
static struct symbol *sym_type;

void
parser_init(struct parser *state, struct source *src, struct token *tokens)
{
    state->src = src;
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
    sym_while = symbol_make("while");
    sym_break = symbol_make("break");
    sym_continue = symbol_make("continue");
    sym_proc = symbol_make("proc");
    sym_struct = symbol_make("struct");
    sym_type = symbol_make("type");
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
    report_error(state->src, parser_curr(state)->span, "%s", msg);

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

static inline bool
parser_same_line(struct parser *state)
{
    return parser_curr(state)->span.start_line == parser_prev(state)->span.end_line;
}

static bool
parser_check_end(struct parser *state, const char *msg)
{
    if (parser_match(state, TOKEN_SEMI))
        return true;

    if (parser_eof(state))
        return true;

    if (parser_curr(state)->tag == TOKEN_RBRACE)
        return true;

    /*
     * Implicit newline termination
     */
    if (parser_curr(state)->span.start_line > parser_prev(state)->span.end_line)
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
    *sym = symbol_intern(token->span.start,
                         token->span.end - token->span.start);
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
    size_t count;

    if (parser_match(state, TOKEN_STAR)) {
        if (!parser_type(state, &pointer))
            return false;

        *type = type_get_ptr(NULL, pointer);
        return true;
    }

    if (parser_match(state, TOKEN_LPAREN)) {
        if (!parser_type(state, type))
            return false;

        if (!parser_check(state, TOKEN_RPAREN, "Expected ')' after type"))
            return false;
    } else {
        if (!parser_check_sym(state, &sym, "Expected type name"))
            return false;

        *type = type_get_named(NULL, sym);
    }

    while (parser_match(state, TOKEN_LBRACK)) {
        if (parser_curr(state)->tag != TOKEN_INT) {
            parser_error(state, "Expected integer for array size");
            return false;
        }

        count = strtoull(parser_curr(state)->span.start, NULL, 10);
        parser_advance(state);

        if (!parser_check(state, TOKEN_RBRACK, "Expected ']' after array size"))
            return false;

        *type = type_get_array(NULL, *type, count);
    }

    return true;
}

/*
 * Expressions
 */
static bool parser_expr_prec(struct parser *state, struct expr **expr,
                             unsigned base_prec);

static bool
parser_escape_string(struct parser *state, const char **string)
{
    struct span span;
    size_t i, j, len;
    char c, *buf;

    span = parser_prev(state)->span;
    len = (span.end - span.start) - 2;
    buf = malloc(len + 1);

    for (i = 0, j = 0; j < len; ++j) {
        c = span.start[j + 1];

        if (c == '\\' && j + 1 < len) {
            j++;
            c = span.start[j + 1];

            switch (c) {
                case 'n':
                    buf[i++] = '\n';
                    break;
                case 't':
                    buf[i++] = '\t';
                    break;
                case 'r':
                    buf[i++] = '\r';
                    break;
                case '0':
                    buf[i++] = '\0';
                    break;
                case '\\':
                    buf[i++] = '\\';
                    break;
                case '"':
                    buf[i++] = '"';
                    break;
                default:
                    buf[i++] = c;
                    break;
            }
        } else {
            buf[i++] = c;
        }
    }

    buf[i] = '\0';
    *string = buf;
    return true;
}

static bool
parser_expr_simple(struct parser *state, struct expr **expr)
{
    int64_t value;
    struct symbol *sym;
    struct expr *op;
    const char *string;
    struct span start;

    start = parser_curr(state)->span;

    if (parser_match(state, TOKEN_INT)) {
        value = strtol(parser_prev(state)->span.start, NULL, 10);
        *expr = expr_make_literal(LIT_INTEGER, value);
        goto done;
    }

    if (parser_match(state, TOKEN_STRING)) {
        if (!parser_escape_string(state, &string))
            return false;

        *expr = expr_make_literal(LIT_STRING, (intptr_t)string);
        goto done;
    }

    if (parser_match_sym(state, &sym)) {
        // TODO: Check for disallowed keywords?
        *expr = expr_make_ident(sym);
        goto done;
    }

    if (parser_match(state, TOKEN_LPAREN)) {
        if (!parser_expr_prec(state, expr, 0))
            return false;

        if (!parser_check(state, TOKEN_RPAREN, "Expected ')' after expr"))
            return false;
        goto done;
    }

    if (parser_match(state, TOKEN_MINUS)) {
        if (!parser_expr_prec(state, &op, 13))
            return false;

        *expr = expr_make_unop(op, UNOP_NEG);
        goto done;
    }

    if (parser_match(state, TOKEN_NOT)) {
        if (!parser_expr_prec(state, &op, 13))
            return false;

        *expr = expr_make_unop(op, UNOP_NOT);
        goto done;
    }

    if (parser_match(state, TOKEN_STAR)) {
        if (!parser_expr_prec(state, &op, 13))
            return false;

        *expr = expr_make_unop(op, UNOP_DEREF);
        goto done;
    }

    if (parser_match(state, TOKEN_AND)) {
        if (!parser_expr_prec(state, &op, 13))
            return false;

        *expr = expr_make_unop(op, UNOP_ADDROF);
        goto done;
    }

    parser_error(state, "Expected expression");
    return false;

done:
    expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
    return true;
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
    struct span start;

    start = parser_curr(state)->span;

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

            expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
            continue;
        }

        if (op_tag == TOKEN_LBRACK) {
            if (!parser_expr_prec(state, &rhs, 0))
                return false;

            if (!parser_check(state, TOKEN_RBRACK, "Expected ']' after index"))
                return false;

            *expr = expr_make_index(*expr, rhs);
            expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
            continue;
        }

        if (op_tag == TOKEN_DOT) {
            if (!parser_check_sym(state, &field, "Expected field name after '.'"))
                return false;

            *expr = expr_make_access(*expr, field);
            expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
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
        expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
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
    struct stmt *tmp[64];
    unsigned n;
    struct span start;

    start = parser_prev(state)->span;

    for (n = 0; !parser_eof(state); n++) {
        if (parser_match(state, TOKEN_RBRACE))
            break;

        assert(n < 64 && "too many stmts!");
        if (!parser_stmt(state, &tmp[n]))
            return false;
    }

    *stmt = stmt_make_block(n);
    memcpy(((struct stmt_block *)*stmt)->items, tmp, n * sizeof(struct stmt *));
    stmt_set_span(*stmt, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_stmt_var(struct parser *state, struct stmt **stmt)
{
    struct symbol *name;
    struct type *type;
    struct expr *value;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_check_sym(state, &name, "Expected var name"))
        return false;

    type = NULL;
    if (parser_match(state, TOKEN_COLON)) {
        if (!parser_type(state, &type))
            return false;
    }

    value = NULL;
    if (parser_match(state, TOKEN_EQ)) {
        if (!parser_expr(state, &value))
            return false;
    }

    if (!parser_check_end(state, "Expected newline or ';' after statement"))
        return false;

    *stmt = stmt_make_var(name, type, value);
    stmt_set_span(*stmt, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_stmt_return(struct parser *state, struct stmt **stmt)
{
    struct expr *value;
    struct span start;

    start = parser_prev(state)->span;

    value = NULL;
    if (parser_same_line(state) && parser_curr(state)->tag != TOKEN_SEMI && parser_curr(state)->tag != TOKEN_RBRACE) {
        if (!parser_expr(state, &value))
            return false;
    }

    if (!parser_check_end(state, "Expected newline or ';' after return"))
        return false;

    *stmt = stmt_make_return(value);
    stmt_set_span(*stmt, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_stmt_if(struct parser *state, struct stmt **stmt)
{
    struct symbol *sym;
    struct expr *cond;
    struct stmt *t_branch, *f_branch;
    struct span start;

    start = parser_prev(state)->span;

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
    stmt_set_span(*stmt, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_stmt_while(struct parser *state, struct stmt **stmt)
{
    struct expr *cond;
    struct stmt *body;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_expr(state, &cond))
        return false;

    if (!parser_check(state, TOKEN_LBRACE, "Expect '{' after while condition"))
        return false;

    if (!parser_stmt_block(state, &body))
        return false;

    *stmt = stmt_make_while(cond, body);
    stmt_set_span(*stmt, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_stmt(struct parser *state, struct stmt **stmt)
{
    struct symbol *sym;
    struct expr *expr;
    struct span start;

    start = parser_curr(state)->span;

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

        if (sym == sym_while) {
            parser_advance(state);
            return parser_stmt_while(state, stmt);
        }

        if (sym == sym_break) {
            parser_advance(state);
            if (!parser_check_end(state, "Expected newline or ';' after break"))
                return false;

            *stmt = stmt_make_break();
            goto done;
        }

        if (sym == sym_continue) {
            parser_advance(state);
            if (!parser_check_end(state, "Expected newline or ';' after continue"))
                return false;

            *stmt = stmt_make_continue();
            goto done;
        }
    }

    if (!parser_expr(state, &expr))
        return false;

    if (!parser_check_end(state, "Expected newline or ';' after statement"))
        return false;

    *stmt = stmt_make_expr(expr);

done:
    stmt_set_span(*stmt, span_merge(start, parser_prev(state)->span));
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
    size_t args_count;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_check_sym(state, &sym, "Expected procedure name"))
        return false;

    if (!parser_check(state, TOKEN_LPAREN, "Expected '(' after function name"))
        return false;

    args = NULL;
    args_count = 0;
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
            args_count++;
        } while (parser_match(state, TOKEN_COMMA));

        if (!parser_check(state, TOKEN_RPAREN, "Expected ')' after function args"))
            return false;
    }

    out = NULL;
    if (parser_match(state, TOKEN_COLON)) {
        if (!parser_type(state, &out))
            return false;
    }

    body = NULL;
    if (parser_match(state, TOKEN_LBRACE)) {
        if (!parser_stmt_block(state, &body))
            return false;
    } else {
        if (!parser_check_end(state, "Expected '{' or termination after proc signature"))
            return false;
    }

    *decl = decl_make_proc(sym, out, args, args_count, body);
    decl_set_span(*decl, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_decl_struct(struct parser *state, struct decl **decl)
{
    struct symbol *sym, *field_sym;
    struct type *field_type;
    struct struct_field *copy, fields[64];
    size_t fields_count;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_check_sym(state, &sym, "Expected structure name"))
        return false;

    if (!parser_check(state, TOKEN_LBRACE, "Expected '{' after structure"))
        return false;

    fields_count = 0;
    do {
        if (!parser_check_sym(state, &field_sym, "Expected field name"))
            return false;

        if (!parser_check(state, TOKEN_COLON, "Expected ':' after field name"))
            return false;

        field_type = NULL;
        if (!parser_type(state, &field_type))
            return false;

        assert(fields_count < 64 && "too many fields!");
        fields[fields_count].name = field_sym;
        fields[fields_count].type = field_type;
        fields[fields_count].offset = fields_count;
        fields_count++;

        if (!parser_check_end(state, "Expected newline or ';' after field"))
            return false;
    } while (!parser_match(state, TOKEN_RBRACE));

    copy = malloc(fields_count * sizeof(struct struct_field));
    memcpy(copy, fields, fields_count * sizeof(struct struct_field));

    *decl = decl_make_struct(sym, copy, fields_count);
    decl_set_span(*decl, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_decl_type(struct parser *state, struct decl **decl)
{
    struct symbol *sym;
    struct type *type;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_check_sym(state, &sym, "Expected type name"))
        return false;

    if (!parser_check(state, TOKEN_EQ, "Expected '=' after name"))
        return false;

    if (!parser_type(state, &type))
        return false;

    if (!parser_check_end(state, "Expected newline or ';' after declaration"))
        return false;

    *decl = decl_make_type(sym, type);
    decl_set_span(*decl, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_decl(struct parser *state, struct decl **decl)
{
    struct symbol *sym;

    if (parser_peek_sym(state, &sym)) {
        if (sym == sym_proc) {
            parser_advance(state);
            return parser_decl_proc(state, decl);
        }

        if (sym == sym_struct) {
            parser_advance(state);
            return parser_decl_struct(state, decl);
        }

        if (sym == sym_type) {
            parser_advance(state);
            return parser_decl_type(state, decl);
        }
    }

    parser_error(state, "Expected declaration");
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
