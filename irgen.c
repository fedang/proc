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

void
irgen_init(struct irgen *state, FILE *out)
{
    state->out = out;
    state->regs = 0;
    state->labels = 0;
}

static struct irval
irgen_fresh_reg(struct irgen *state)
{
    struct irval out;

    out.tag = IRVAL_REG;
    out.reg = ++state->regs;
    return out;
}

static struct irval
irgen_fresh_label(struct irgen *state)
{
    struct irval out;

    out.tag = IRVAL_LABEL;
    out.label = ++state->labels;
    return out;
}

static void
irgen_print_val(struct irgen *state, struct irval val, char *buf)
{
    switch (val.tag) {
        case IRVAL_INT:
            sprintf(buf, "%ld", val.ival);
            break;

        case IRVAL_REG:
            sprintf(buf, "%%%u", val.reg);
            break;

        case IRVAL_LABEL:
            sprintf(buf, "%%%u", val.label);
            break;

        default:
            unreachable();
    }
}

struct irval irgen_expr(struct irgen *state, struct expr *expr);

static struct irval
irgen_expr_const(struct irgen *state, struct expr_const *expr)
{
    struct irval out;

    out.tag = IRVAL_INT;
    out.ival = expr->value;
    return out;
}

static struct irval
irgen_expr_ident(struct irgen *state, struct expr_ident *expr)
{
    unreachable();
}

static struct irval
irgen_expr_binop(struct irgen *state, struct expr_binop *expr)
{
    struct irval lhs, rhs, out;
    char right_str[32];
    char left_str[32];

    if (expr->binop == BINOP_OR || expr->binop == BINOP_AND) {
        // TODO: Shortcircuit
        unreachable();
    }

    lhs = irgen_expr(state, expr->op_lhs);
    irgen_print_val(state, lhs, left_str);

    rhs = irgen_expr(state, expr->op_rhs);
    irgen_print_val(state, rhs, right_str);

    out = irgen_fresh_reg(state);

    switch (expr->binop) {
        case BINOP_OR:
			break;

        case BINOP_AND:
			break;

        case BINOP_BITOR:
            fprintf(state->out, "  %%%d = or i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_BITAND:
            fprintf(state->out, "  %%%d = and i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_BITXOR:
            fprintf(state->out, "  %%%d = xor i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_EQ:
            fprintf(state->out, "  %%%d = icmp eq i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_NEQ:
            fprintf(state->out, "  %%%d = icmp ne i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_GT:
            fprintf(state->out, "  %%%d = icmp sgt i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_GTEQ:
            fprintf(state->out, "  %%%d = icmp sge i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_LT:
            fprintf(state->out, "  %%%d = icmp slt i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_LTEQ:
            fprintf(state->out, "  %%%d = icmp sle i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_LSHIFT:
            fprintf(state->out, "  %%%d = shl i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_RSHIFT:
            fprintf(state->out, "  %%%d = ashr i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_ADD:
            fprintf(state->out, "  %%%d = add i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_SUB:
            fprintf(state->out, "  %%%d = sub i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_MUL:
            fprintf(state->out, "  %%%d = mul i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_DIV:
            fprintf(state->out, "  %%%d = sdiv i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        case BINOP_MOD:
            fprintf(state->out, "  %%%d = srem i32 %s, %s\n",
                    out.reg, left_str, right_str);
			break;

        default:
            unreachable();
    }

    return out;
}

static struct irval
irgen_expr_unop(struct irgen *state, struct expr_unop *expr)
{
    struct irval op, out;
    char op_str[32];

    op = irgen_expr(state, expr->op);
    irgen_print_val(state, op, op_str);

    out = irgen_fresh_reg(state);

    switch (expr->unop) {
        case UNOP_DEREF:
            fprintf(state->out, "  %%%d = load i32, ptr %s\n", out.reg, op_str);
            break;

        case UNOP_NOT:
            fprintf(state->out, "  %%%d = xor i32 %s, -1\n", out.reg, op_str);
            break;

        case UNOP_NEG:
            fprintf(state->out, "  %%%d = sub i32 0, %s\n", out.reg, op_str);
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
    fprintf(state->out, "entry:\n");

    struct irval val = irgen_expr(state, expr);

    char val_str[32];
    irgen_print_val(state, val, val_str);

    fprintf(state->out, "  call i32 (ptr, ...) @printf(ptr @.str.fmt, i32 %s)\n", val_str);
    fprintf(state->out, "  ret i32 0\n");
    fprintf(state->out, "}\n");
}
