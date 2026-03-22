#include <stdarg.h>

#include "irgen.h"

//#define unreachable() (__builtin_unreachable())

#include <assert.h>
#define unreachable() assert(!"unreachable")

static void
irval_sprintf(struct irval val, char buf[static 32])
{
    switch (val.tag) {
        case IRVAL_INT:
            snprintf(buf, 32, "%ld", val.ival);
            break;

        case IRVAL_REG:
            snprintf(buf, 32, "%%%u", val.reg);
            break;

        case IRVAL_LABEL:
            snprintf(buf, 32, "%%b%u", val.label);
            break;

        case IRVAL_GLOBAL:
            snprintf(buf, 32, "@%s", val.global);
            break;

        default:
            unreachable();
    }
}

void
irgen_init(struct irgen *state, FILE *out)
{
    state->out = out;
    state->regs = 0;
    state->labels = 0;
    state->in_block = 0;
    state->locals_count = 0;
    state->globals_count = 0;
}

static inline unsigned
irgen_fresh_reg(struct irgen *state)
{
    return ++state->regs;
}

static inline unsigned
irgen_fresh_label(struct irgen *state)
{
    return ++state->labels;
}

static struct irval
irgen_emit_simple0(struct irgen *state, const char *instr)
{
    struct irval out;

    out = IRVAL_REG(irgen_fresh_reg(state));
    fprintf(state->out, "\t%%%u = %s i32\n", out.reg, instr);
    return out;
}

static struct irval
irgen_emit_simple2(struct irgen *state, const char *instr,
                   struct irval op1, struct irval op2)
{
    char buf1[32], buf2[32];
    struct irval out;

    irval_sprintf(op1, buf1);
    irval_sprintf(op2, buf2);

    out = IRVAL_REG(irgen_fresh_reg(state));
    fprintf(state->out, "\t%%%u = %s i32 %s, %s\n", out.reg, instr, buf1, buf2);
    return out;
}

static struct irval
irgen_emit_phi(struct irgen *state, unsigned n, ...)
{
    char buf[32];
    struct irval val, out;
    unsigned i, label;
    va_list args;

    va_start(args, n);
    out = IRVAL_REG(irgen_fresh_reg(state));
    fprintf(state->out, "\t%%%u = phi i32", out.reg);

    for (i = 0; i < n; i++) {
        val = va_arg(args, struct irval);
        label = va_arg(args, unsigned);

        if (i != 0)
            fputc(',', state->out);

        irval_sprintf(val, buf);
        fprintf(state->out, " [ %s, %%b%u ]", buf, label);
    }

    fputc('\n', state->out);
    va_end(args);
    return out;
}

static void
irgen_emit_br(struct irgen *state, unsigned label)
{
    fprintf(state->out, "\tbr label %%b%u\n", label);
}

static void
irgen_emit_condbr(struct irgen *state, struct irval cond,
                  unsigned t_label, unsigned f_label)
{
    char buf[32];

    irval_sprintf(cond, buf);
    fprintf(state->out, "\tbr i1 %s, label %%b%u, label %%b%u\n",
            buf, t_label, f_label);
}

static unsigned
irgen_emit_block(struct irgen *state, unsigned label)
{
    unsigned prev;

    prev = state->in_block;
    state->in_block = label;

    fprintf(state->out, "b%u:\n", label);
    return prev;
}

/*
 * Expressions
 */

struct irval irgen_expr(struct irgen *state, struct expr *expr);

static struct irval
irgen_expr_const(struct irgen *state, struct expr_const *expr)
{
    return IRVAL_INT(expr->value);
}

static struct irval
irgen_expr_ident(struct irgen *state, struct expr_ident *expr)
{
    struct irval slot, out;
    size_t i;

    for (i = state->locals_count; i > 0; i--) {
        if (state->locals[i - 1].sym == expr->sym) {
            slot = state->locals[i - 1].val;
            goto found;
        }
    }

    for (i = 0; i < state->globals_count; i++) {
        if (state->globals[i].sym == expr->sym) {
            return state->globals[i].val;
        }
    }

    printf("Undefined variables '%s'\n", expr->sym->str);
    assert(0);

found:
    out = IRVAL_REG(irgen_fresh_reg(state));
    fprintf(state->out, "\t%%%u = load i32, ptr %%%u\n", out.reg, slot.reg);
    return out;
}

static struct irval
irgen_expr_binop(struct irgen *state, struct expr_binop *expr)
{
    struct irval lhs, rhs, lhs_cond, rhs_cond, rhs_zext;
    unsigned block_lhs, block_rhs, rhs_label, merge_label;

    lhs = irgen_expr(state, expr->op_lhs);

    /*
     * Logical && and || require shortcircuiting
     */
    if (expr->binop == BINOP_OR || expr->binop == BINOP_AND) {
        rhs_label = irgen_fresh_label(state);
        merge_label = irgen_fresh_label(state);

        lhs_cond = irgen_emit_simple2(state, "icmp ne", lhs, IRVAL_INT(0));

        if (expr->binop == BINOP_AND) {
            irgen_emit_condbr(state, lhs_cond, rhs_label, merge_label);
        } else {
            irgen_emit_condbr(state, lhs_cond, merge_label, rhs_label);
        }

        block_lhs = irgen_emit_block(state, rhs_label);
        rhs = irgen_expr(state, expr->op_rhs);

        rhs_cond = irgen_emit_simple2(state, "icmp ne", rhs, IRVAL_INT(0));

        rhs_zext = IRVAL_REG(irgen_fresh_reg(state));
        fprintf(state->out, "\t%%%u = zext i1 %%%u to i32\n",
                rhs_zext.reg, rhs_cond.reg);

        irgen_emit_br(state, merge_label);
        block_rhs = irgen_emit_block(state, merge_label);

        /*
         * Coming from block_lhs means we exited early,
         * for && this means false, while for || this means true
         */
        return irgen_emit_phi(state, 2,
                              IRVAL_INT(expr->binop == BINOP_OR), block_lhs,
                              rhs_zext, block_rhs);
    }

    rhs = irgen_expr(state, expr->op_rhs);

    switch (expr->binop) {
        case BINOP_BITOR:
            return irgen_emit_simple2(state, "or", lhs, rhs);

        case BINOP_BITAND:
            return irgen_emit_simple2(state, "and", lhs, rhs);

        case BINOP_BITXOR:
            return irgen_emit_simple2(state, "xor", lhs, rhs);

        case BINOP_EQ:
            return irgen_emit_simple2(state, "icmp eq", lhs, rhs);

        case BINOP_NEQ:
            return irgen_emit_simple2(state, "icmp ne", lhs, rhs);

        case BINOP_GT:
            return irgen_emit_simple2(state, "icmp sgt", lhs, rhs);

        case BINOP_GTEQ:
            return irgen_emit_simple2(state, "icmp sge", lhs, rhs);

        case BINOP_LT:
            return irgen_emit_simple2(state, "icmp slt", lhs, rhs);

        case BINOP_LTEQ:
            return irgen_emit_simple2(state, "icmp sle", lhs, rhs);

        case BINOP_LSHIFT:
            return irgen_emit_simple2(state, "shl", lhs, rhs);

        case BINOP_RSHIFT:
            return irgen_emit_simple2(state, "ashr", lhs, rhs);

        case BINOP_ADD:
            return irgen_emit_simple2(state, "add", lhs, rhs);

        case BINOP_SUB:
            return irgen_emit_simple2(state, "sub", lhs, rhs);

        case BINOP_MUL:
            return irgen_emit_simple2(state, "mul", lhs, rhs);

        case BINOP_DIV:
            return irgen_emit_simple2(state, "sdiv", lhs, rhs);

        case BINOP_MOD:
            return irgen_emit_simple2(state, "srem", lhs, rhs);

        default:
            unreachable();
    }
}

static struct irval
irgen_expr_unop(struct irgen *state, struct expr_unop *expr)
{
    struct irval op, out;
    char op_str[32];

    op = irgen_expr(state, expr->op);

    switch (expr->unop) {
        case UNOP_DEREF:
            out = IRVAL_REG(irgen_fresh_reg(state));
            irval_sprintf(op, op_str);
            fprintf(state->out, "\t%%%u = load i32, ptr %s\n", out.reg, op_str);
            break;

        case UNOP_NOT:
            out = irgen_emit_simple2(state, "xor", op, IRVAL_INT(-1));
            break;

        case UNOP_NEG:
            out = irgen_emit_simple2(state, "sub", IRVAL_INT(0), op);
            break;

        default:
            unreachable();
    }

    return out;
}

static struct irval
irgen_expr_cast(struct irgen *state, struct expr_cast *expr)
{
    unreachable();
}

static struct irval
irgen_expr_call(struct irgen *state, struct expr_call *expr)
{
    struct irval func, out, vals[expr->args_count];
    char buf[32];
    unsigned i;

    func = irgen_expr(state, expr->op);
    irval_sprintf(func, buf);

    for (i = 0; i < expr->args_count; i++) {
        vals[i] = irgen_expr(state, expr->args[i]);
    }

    out = IRVAL_REG(irgen_fresh_reg(state));
    fprintf(state->out, "\t%%%u = call i32 %s(", out.reg, buf);

    for (i = 0; i < expr->args_count; i++) {
        if (i != 0)
            fprintf(state->out, ", ");

        irval_sprintf(vals[i], buf);
        fprintf(state->out, "i32 %s", buf);
    }

    fprintf(state->out, ")\n");
    return out;
}

struct irval
irgen_expr(struct irgen *state, struct expr *expr)
{
    switch (expr->tag) {
        case EXPR_CONST:
            return irgen_expr_const(state, (struct expr_const *)expr);

        case EXPR_IDENT:
            return irgen_expr_ident(state, (struct expr_ident *)expr);

        case EXPR_BINOP:
            return irgen_expr_binop(state, (struct expr_binop *)expr);

        case EXPR_UNOP:
            return irgen_expr_unop(state, (struct expr_unop *)expr);

        case EXPR_CAST:
            return irgen_expr_cast(state, (struct expr_cast *)expr);

        case EXPR_CALL:
            return irgen_expr_call(state, (struct expr_call *)expr);

        default:
            unreachable();
    }
}

/*
 * Statements
 */

void irgen_stmt(struct irgen *state, struct stmt *stmt);

static void
irgen_stmt_var(struct irgen *state, struct stmt_var *stmt)
{
    struct irval slot, val;
    char buf[32];

    slot = irgen_emit_simple0(state, "alloca");

    assert(state->locals_count < 256 && "too many locals!");
    state->locals[state->locals_count].sym = stmt->sym;
    state->locals[state->locals_count].val = slot;
    state->locals_count++;

    if (stmt->value) {
        val = irgen_expr(state, stmt->value);
        irval_sprintf(val, buf);
        fprintf(state->out, "\tstore i32 %s, ptr %%%u\n", buf, slot.reg);
    }
}

static void
irgen_stmt_expr(struct irgen *state, struct stmt_expr *stmt)
{
    irgen_expr(state, stmt->expr);
}

static void
irgen_stmt_block(struct irgen *state, struct stmt_block *stmt)
{
    size_t i, locals;

    locals = state->locals_count;

    for (i = 0; i < stmt->items_count; i++) {
        irgen_stmt(state, stmt->items[i]);
    }

    state->locals_count = locals;
}

static void
irgen_stmt_if(struct irgen *state, struct stmt_if *stmt)
{
    struct irval cond, cond_cmp;
    unsigned t_label, f_label, merge_label;

    cond = irgen_expr(state, stmt->cond);
    cond_cmp = irgen_emit_simple2(state, "icmp ne", cond, IRVAL_INT(0));

    t_label = irgen_fresh_label(state);

    if (stmt->b_false) {
        f_label = irgen_fresh_label(state);
        merge_label = irgen_fresh_label(state);
        irgen_emit_condbr(state, cond_cmp, t_label, f_label);
    } else {
        merge_label = irgen_fresh_label(state);
        irgen_emit_condbr(state, cond_cmp, t_label, merge_label);
    }

    irgen_emit_block(state, t_label);
    irgen_stmt(state, stmt->b_true);
    irgen_emit_br(state, merge_label);

    if (stmt->b_false) {
        irgen_emit_block(state, f_label);
        irgen_stmt(state, stmt->b_false);
        irgen_emit_br(state, merge_label);
    }

    irgen_emit_block(state, merge_label);
}

void
irgen_stmt(struct irgen *state, struct stmt *stmt)
{
    switch (stmt->tag) {
        case STMT_VAR:
            irgen_stmt_var(state, (struct stmt_var *)stmt);
            break;

        case STMT_EXPR:
            irgen_stmt_expr(state, (struct stmt_expr *)stmt);
            break;

        case STMT_BLOCK:
            irgen_stmt_block(state, (struct stmt_block *)stmt);
            break;

        case STMT_IF:
            irgen_stmt_if(state, (struct stmt_if *)stmt);
            break;

        default:
            unreachable();
    }
}

void
irgen_module(struct irgen *state, struct stmt *stmt)
{
    fprintf(state->out, "target triple = \"x86_64-pc-linux-gnu\"\n\n");
    fprintf(state->out, "@.str.fmt = private unnamed_addr constant [4 x i8] c\"%%d\\0A\\00\"\n\n");
    fprintf(state->out, "declare i32 @printf(ptr, ...)\n\n");

    fprintf(state->out, "define i32 @main() {\n");

    irgen_emit_block(state, 0);
    irgen_stmt(state, stmt);

    fprintf(state->out, "\tret i32 0\n");
    fprintf(state->out, "}\n");
}
