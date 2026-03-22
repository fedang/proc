#include <stdarg.h>

#include "irgen.h"

//#define unreachable() (__builtin_unreachable())

#include <assert.h>
#define unreachable() assert(!"unreachable")

enum irval_tag {
    IRVAL_INVALID,
    IRVAL_INT,
    IRVAL_REG,
    IRVAL_LABEL,
};

struct irval {
    enum irval_tag tag;
    union {
        int64_t ival;
        unsigned reg;
        unsigned label;
    };
};

#define IRVAL_INT(v) ((struct irval) { IRVAL_INT, { (v) } })
#define IRVAL_REG(v) ((struct irval) { IRVAL_REG, { (v) } })
#define IRVAL_LABEL(v) ((struct irval) { IRVAL_LABEL, { (v) } })

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
}

static struct irval
irgen_fresh_reg(struct irgen *state)
{
    return IRVAL_REG(++state->regs);
}

static struct irval
irgen_fresh_label(struct irgen *state)
{
    return IRVAL_LABEL(++state->labels);
}

static struct irval
irgen_emit_simple2(struct irgen *state, const char *instr,
                   struct irval op1, struct irval op2)
{
    char buf1[32], buf2[32];
    struct irval out;

    irval_sprintf(op1, buf1);
    irval_sprintf(op2, buf2);

    out = irgen_fresh_reg(state);
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

    out = irgen_fresh_reg(state);
    fprintf(state->out, "\t%%%u = phi i32 ", out.reg);

    va_start(args, n);

    for (i = 0; i < n; i++) {
        val = va_arg(args, struct irval);
        label = va_arg(args, unsigned);

        if (i != 0)
            fputc(',', state->out);

        irval_sprintf(val, buf);
        fprintf(state->out, "[ %s, %%b%u ]", buf, label);
    }

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
 * Expression
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
    unreachable();
}

static struct irval
irgen_expr_binop(struct irgen *state, struct expr_binop *expr)
{
    struct irval lhs, rhs, lhs_cond, rhs_cond, rhs_zext, rhs_label, merge_label;
    unsigned block_lhs, block_rhs;

    lhs = irgen_expr(state, expr->op_lhs);

    /*
     * Logical && and || require shortcircuiting
     */
    if (expr->binop == BINOP_OR || expr->binop == BINOP_AND) {
        rhs_label = irgen_fresh_label(state);
        merge_label = irgen_fresh_label(state);

        lhs_cond = irgen_emit_simple2(state, "icmp ne", lhs, IRVAL_INT(0));

        if (expr->binop == BINOP_AND) {
            irgen_emit_condbr(state, lhs_cond, rhs_label.label, merge_label.label);
        } else {
            irgen_emit_condbr(state, lhs_cond, merge_label.label, rhs_label.label);
        }

        block_lhs = irgen_emit_block(state, rhs_label.label);
        rhs = irgen_expr(state, expr->op_rhs);

        rhs_cond = irgen_emit_simple2(state, "icmp ne", rhs, IRVAL_INT(0));

        rhs_zext = irgen_fresh_reg(state);
        fprintf(state->out, "\t%%%u = zext i1 %%%u to i32\n",
                rhs_zext.reg, rhs_cond.reg);

        irgen_emit_br(state, merge_label.label);
        block_rhs = irgen_emit_block(state, merge_label.label);

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
            out = irgen_fresh_reg(state);
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
    unreachable();
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

void
irgen_expr2(struct irgen *state, struct expr *expr)
{
    fprintf(state->out, "target triple = \"x86_64-pc-linux-gnu\"\n\n");
    fprintf(state->out, "@.str.fmt = private unnamed_addr constant [4 x i8] c\"%%d\\0A\\00\"\n\n");
    fprintf(state->out, "declare i32 @printf(ptr, ...)\n\n");

    fprintf(state->out, "define i32 @main() {\n");

    irgen_emit_block(state, 0);
    struct irval val = irgen_expr(state, expr);

    char val_str[32];
    irval_sprintf(val, val_str);

    fprintf(state->out, "\tcall i32 (ptr, ...) @printf(ptr @.str.fmt, i32 %s)\n", val_str);
    fprintf(state->out, "\tret i32 0\n");
    fprintf(state->out, "}\n");
}
