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

//#define IR_EMITF(state, ...) fprintf((state)->out, __VA_ARGS__)
//#define IR_EMITC(state, chr) fputc((chr), (state)->out)

/*
 * Types
 */
void
irgen_type(struct irgen *state, struct type *type)
{
    size_t i;

    if (!type) {
        printf("NULL TYPE\n");
        fprintf(state->out, "i32");
        return;
    }

    switch (type->tag) {
        case TYPE_INT:
        case TYPE_UINT:
            fprintf(state->out, "i%zu",
                    type->bit_size ? type->bit_size : sizeof(int) * 8);
            break;

        case TYPE_BOOL:
            fprintf(state->out, "i1");
            break;

        case TYPE_VOID:
            fprintf(state->out, "void");
            break;

        case TYPE_PTR:
        case TYPE_PROC:
            fprintf(state->out, "ptr");
            break;

        case TYPE_ARRAY:
            fprintf(state->out, "[%zu x ", type->array.count);
            irgen_type(state, type->array.item);
            fprintf(state->out, "]");
            break;

        case TYPE_STRUCT:
            if (type->strukt.name) {
                fprintf(state->out, "%%%s", type->strukt.name->str);
            } else {
                /*
                 * Anonymous structs
                 */
                fprintf(state->out, "{ ");
                for (i = 0; i < type->strukt.fields_count; i++) {
                    if (i != 0)
                        fprintf(state->out, ", ");

                    irgen_type(state, type->strukt.fields[i].type);
                }
                fprintf(state->out, " }");
            }
            break;

        default:
            unreachable();
    }
}

/*
 * IR instruction helpers
 */

static struct irval
irgen_emit_simple0(struct irgen *state, const char *instr, struct type *type)
{
    struct irval out;

    out = IRVAL_REG(irgen_fresh_reg(state));
    fprintf(state->out, "\t%%%u = %s ", out.reg, instr);
    irgen_type(state, type);
    fputc('\n', state->out);
    return out;
}

static struct irval
irgen_emit_simple2(struct irgen *state, const char *instr, struct type *type,
                   struct irval op1, struct irval op2)
{
    char buf1[32], buf2[32];
    struct irval out;

    out = IRVAL_REG(irgen_fresh_reg(state));
    fprintf(state->out, "\t%%%u = %s ", out.reg, instr);
    irgen_type(state, type);

    irval_sprintf(op1, buf1);
    irval_sprintf(op2, buf2);
    fprintf(state->out, " %s, %s\n", buf1, buf2);
    return out;
}

static struct irval
irgen_emit_phi(struct irgen *state, struct type *type, unsigned n, ...)
{
    char buf[32];
    struct irval val, out;
    unsigned i, label;
    va_list args;

    va_start(args, n);
    out = IRVAL_REG(irgen_fresh_reg(state));
    fprintf(state->out, "\t%%%u = phi ", out.reg);
    irgen_type(state, type);

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

static struct irval
irgen_emit_gep(struct irgen *state, struct type *type, struct irval ptr,
               unsigned n, ...)
{
    char buf[32];
    struct irval val, out;
    unsigned i;
    va_list args;

    va_start(args, n);
    out = IRVAL_REG(irgen_fresh_reg(state));
    fprintf(state->out, "\t%%%u = getelementptr ", out.reg);
    irgen_type(state, type);

    irval_sprintf(ptr, buf);
    fprintf(state->out, ", ptr %s", buf);

    for (i = 0; i < n; i++) {
        val = va_arg(args, struct irval);

        irval_sprintf(val, buf);
        fprintf(state->out, ", i32 %s", buf);
    }

    fputc('\n', state->out);
    va_end(args);
    return out;
}

static struct irval
irgen_emit_load(struct irgen *state, struct type *type, struct irval slot)
{
    struct irval out;
    char buf[32];

    out = IRVAL_REG(irgen_fresh_reg(state));
    fprintf(state->out, "\t%%%u = load ", out.reg);
    irgen_type(state, type);

    irval_sprintf(slot, buf);
    fprintf(state->out, ", ptr %s\n", buf);
    return out;
}

static void
irgen_emit_store(struct irgen *state, struct type *type, struct irval slot,
                 struct irval val)
{
    char buf1[32], buf2[32];

    fprintf(state->out, "\tstore ");
    irgen_type(state, type);

    irval_sprintf(val, buf1);
    irval_sprintf(slot, buf2);
    fprintf(state->out, " %s, ptr %s\n", buf1, buf2);
}

static void
irgen_emit_ret(struct irgen *state, struct type *type, struct irval val)
{
    char buf[32];

    if (state->terminated)
        return;

    fprintf(state->out, "\tret ");
    irgen_type(state, type);

    if (!type || type->tag != TYPE_VOID) {
        irval_sprintf(val, buf);
        fprintf(state->out, " %s", buf);
    }

    fputc('\n', state->out);
    state->terminated = true;
}

static void
irgen_emit_br(struct irgen *state, unsigned label)
{
    if (state->terminated)
        return;

    fprintf(state->out, "\tbr label %%b%u\n", label);
    state->terminated = true;
}

static void
irgen_emit_condbr(struct irgen *state, struct irval cond,
                  unsigned t_label, unsigned f_label)
{
    char buf[32];

    if (state->terminated)
        return;

    irval_sprintf(cond, buf);
    fprintf(state->out, "\tbr i1 %s, label %%b%u, label %%b%u\n",
            buf, t_label, f_label);
    state->terminated = true;
}

static unsigned
irgen_emit_block(struct irgen *state, unsigned label)
{
    unsigned prev;

    prev = state->in_block;
    state->in_block = label;
    state->terminated = false;

    fprintf(state->out, "b%u:\n", label);
    return prev;
}

/*
 * Expressions
 */

struct irval irgen_expr(struct irgen *state, struct expr *expr);

static struct irval
irgen_expr_lvalue(struct irgen *state, struct expr *expr)
{
    struct expr_ident *ident;
    struct expr_unop *unop;
    struct expr_index *index;
    struct expr_access *access;
    struct irval ptr, idx;
    struct type *type;
    size_t i;

    switch (expr->tag) {
        case EXPR_IDENT:
            ident = (struct expr_ident *)expr;

            for (i = state->locals_count; i > 0; i--) {
                if (state->locals[i - 1].sym == ident->sym)
                    return state->locals[i - 1].val;
            }

            for (i = 0; i < state->globals_count; i++) {
                if (state->globals[i].sym == ident->sym)
                    return state->globals[i].val;
            }

            printf("Undefined variable '%s'\n", ident->sym->str);
            assert(0);

        case EXPR_UNOP:
            unop = (struct expr_unop *)expr;
            if (unop->unop == UNOP_DEREF) {
                return irgen_expr(state, unop->op);
            }
            break;

        case EXPR_INDEX:
            index = (struct expr_index *)expr;
            ptr = irgen_expr_lvalue(state, index->op);
            idx = irgen_expr(state, index->index);

            /*
             * Pointers must be loaded beforehand
             */
            if (index->op->type->tag == TYPE_PTR) {
                ptr = irgen_emit_load(state, index->op->type, ptr);
                return irgen_emit_gep(state, index->expr.type, ptr, 1, idx);
            }

            return irgen_emit_gep(state, index->op->type, ptr,
                                  2, IRVAL_INT(0), idx);

        case EXPR_ACCESS:
            access = (struct expr_access *)expr;
            ptr = irgen_expr_lvalue(state, access->op);

            /*
             * Auto-dereference struct pointers
             */
            type = access->op->type;
            while (type->tag == TYPE_PTR) {
                ptr = irgen_emit_load(state, type, ptr);
                type = type->pointer;
            }

            return irgen_emit_gep(state, type, ptr,
                                  2, IRVAL_INT(0), IRVAL_INT(access->offset));

        default:
            break;
    }

    assert(0 && "invalid lvalue");
}

static struct irval
irgen_expr_const(struct irgen *state, struct expr_const *expr)
{
    return IRVAL_INT(expr->value);
}

static struct irval
irgen_expr_binop(struct irgen *state, struct expr_binop *expr)
{
    struct irval lhs, rhs, tmp;
    unsigned block_lhs, block_rhs, rhs_label, merge_label;
    const char *instr;

    /*
     * Select the correct IR instruction
     */
    switch (expr->binop) {
        case BINOP_BOOL_OR:
        case BINOP_BOOL_AND:
        case BINOP_SET:
            instr = NULL;
            break;

        case BINOP_EQ:
            instr = "icmp eq";
            break;

        case BINOP_NOTEQ:
            instr = "icmp ne";
            break;

        case BINOP_GT:
            instr = "icmp sgt";
            break;

        case BINOP_GTEQ:
            instr = "icmp sge";
            break;

        case BINOP_LT:
            instr = "icmp slt";
            break;

        case BINOP_LTEQ:
            instr = "icmp sle";
            break;

        case BINOP_OR:
        case BINOP_OR_SET:
            instr = "or";
            break;

        case BINOP_AND:
        case BINOP_AND_SET:
            instr = "and";
            break;

        case BINOP_XOR:
        case BINOP_XOR_SET:
            instr = "xor";
            break;

        case BINOP_SHL:
        case BINOP_SHL_SET:
            instr = "shl";
            break;

        case BINOP_SHR:
        case BINOP_SHR_SET:
            instr = "ashr";
            break;

        case BINOP_ADD:
        case BINOP_ADD_SET:
            instr = "add";
            break;

        case BINOP_SUB:
        case BINOP_SUB_SET:
            instr = "sub";
            break;

        case BINOP_MUL:
        case BINOP_MUL_SET:
            instr = "mul";
            break;

        case BINOP_DIV:
        case BINOP_DIV_SET:
            instr = "sdiv";
            break;

        case BINOP_MOD:
        case BINOP_MOD_SET:
            instr = "srem";
            break;

        default:
            unreachable();
    }

    /*
     * Assignments require lvalue handling
     */
    if (expr->binop >= BINOP_SET && expr->binop <= BINOP_MOD_SET) {
        lhs = irgen_expr_lvalue(state, expr->op_lhs);
        rhs = irgen_expr(state, expr->op_rhs);

        if (expr->binop != BINOP_SET) {
            tmp = irgen_emit_load(state, expr->op_rhs->type, lhs);

            rhs = irgen_emit_simple2(state, instr, expr->op_rhs->type,
                                     tmp, rhs);
        }

        irgen_emit_store(state, expr->op_rhs->type, lhs, rhs);
        return IRVAL_INT(0);
    }

    lhs = irgen_expr(state, expr->op_lhs);

    /*
     * Logical && and || require shortcircuiting
     */
    if (expr->binop == BINOP_BOOL_OR || expr->binop == BINOP_BOOL_AND) {
        rhs_label = irgen_fresh_label(state);
        merge_label = irgen_fresh_label(state);

        if (expr->binop == BINOP_BOOL_AND) {
            irgen_emit_condbr(state, lhs, rhs_label, merge_label);
        } else {
            irgen_emit_condbr(state, lhs, merge_label, rhs_label);
        }

        block_lhs = irgen_emit_block(state, rhs_label);
        rhs = irgen_expr(state, expr->op_rhs);

        irgen_emit_br(state, merge_label);
        block_rhs = irgen_emit_block(state, merge_label);

        /*
         * Coming from block_lhs means we exited early,
         * for && this means false, while for || this means true
         */
        return irgen_emit_phi(state, expr->expr.type, 2,
                              IRVAL_INT(expr->binop == BINOP_BOOL_OR),
                              block_lhs, rhs, block_rhs);
    }

    rhs = irgen_expr(state, expr->op_rhs);
    return irgen_emit_simple2(state, instr, expr->op_lhs->type, lhs, rhs);
}

static struct irval
irgen_expr_unop(struct irgen *state, struct expr_unop *expr)
{
    struct irval op;

    if (expr->unop == UNOP_ADDROF) {
        return irgen_expr_lvalue(state, expr->op);
    }

    op = irgen_expr(state, expr->op);

    switch (expr->unop) {
        case UNOP_DEREF:
            return irgen_emit_load(state, expr->expr.type, op);

        case UNOP_NOT:
            return irgen_emit_simple2(state, "xor", expr->expr.type,
                                      op, IRVAL_INT(-1));

        case UNOP_NEG:
            return irgen_emit_simple2(state, "sub", expr->expr.type,
                                      IRVAL_INT(0), op);

        default:
            unreachable();
    }
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
    fprintf(state->out, "\t%%%u = call ", out.reg);
    irgen_type(state, expr->expr.type);
    fprintf(state->out, " %s(", buf);

    for (i = 0; i < expr->args_count; i++) {
        if (i != 0)
            fprintf(state->out, ", ");

        irval_sprintf(vals[i], buf);
        irgen_type(state, expr->args[i]->type);
        fprintf(state->out, " %s", buf);
    }

    fprintf(state->out, ")\n");
    return out;
}

struct irval
irgen_expr(struct irgen *state, struct expr *expr)
{
    struct irval ptr;

    if (expr_is_lvalue(expr)) {
        ptr = irgen_expr_lvalue(state, expr);
        return ptr.tag == IRVAL_GLOBAL
             ? ptr
             : irgen_emit_load(state, expr->type, ptr);
    }

    switch (expr->tag) {
        case EXPR_CONST:
            return irgen_expr_const(state, (struct expr_const *)expr);

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

    slot = irgen_emit_simple0(state, "alloca", stmt->type);

    assert(state->locals_count < 256 && "too many locals!");
    state->locals[state->locals_count].sym = stmt->sym;
    state->locals[state->locals_count].val = slot;
    state->locals_count++;

    if (stmt->value) {
        val = irgen_expr(state, stmt->value);
        irgen_emit_store(state, stmt->value->type, slot, val);
    }
}

static void
irgen_stmt_expr(struct irgen *state, struct stmt_expr *stmt)
{
    irgen_expr(state, stmt->expr);
}

static void
irgen_stmt_return(struct irgen *state, struct stmt_return *stmt)
{
    struct irval val;

    if (stmt->expr) {
        val = irgen_expr(state, stmt->expr);
        irgen_emit_ret(state, stmt->expr->type, val);
    } else {
        val = IRVAL_INT(0);
        irgen_emit_ret(state, type_get_void(NULL), val);
    }
}

static void
irgen_stmt_block(struct irgen *state, struct stmt_block *stmt)
{
    size_t i, locals;

    locals = state->locals_count;

    for (i = 0; i < stmt->items_count; i++) {
        /*
         * If the current block has been terminated emit a new one
         */
        if (state->terminated)
            irgen_emit_block(state, irgen_fresh_label(state));

        irgen_stmt(state, stmt->items[i]);
    }

    state->locals_count = locals;
}

static void
irgen_stmt_if(struct irgen *state, struct stmt_if *stmt)
{
    struct irval cond;
    unsigned t_label, f_label, merge_label;

    cond = irgen_expr(state, stmt->cond);

    t_label = irgen_fresh_label(state);

    if (stmt->b_false) {
        f_label = irgen_fresh_label(state);
        merge_label = irgen_fresh_label(state);
        irgen_emit_condbr(state, cond, t_label, f_label);
    } else {
        merge_label = irgen_fresh_label(state);
        irgen_emit_condbr(state, cond, t_label, merge_label);
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

        case STMT_RETURN:
            irgen_stmt_return(state, (struct stmt_return *)stmt);
            break;

        default:
            unreachable();
    }
}

/*
 * Declarations
 */

static void
irgen_decl_proc(struct irgen *state, struct decl_proc *decl)
{
    struct proc_arg *ptr;
    struct irval slot;
    unsigned reg;

    /*
     * External declaration
     */
    if (!decl->body) {
        fprintf(state->out, "\ndeclare ");
        irgen_type(state, decl->out);
        fprintf(state->out, " @%s(", decl->sym->str);

        for (ptr = decl->args; ptr; ptr = ptr->next) {
            irgen_type(state, ptr->type);
            if (ptr->next)
                fprintf(state->out, ", ");
        }

        fprintf(state->out, ")\n");
        return;
    }

    state->regs = 0;
    state->labels = 0;
    state->locals_count = 0;

    fprintf(state->out, "\ndefine ");
    irgen_type(state, decl->out);
    fprintf(state->out, " @%s(", decl->sym->str);

    for (ptr = decl->args; ptr; ptr = ptr->next) {
        irgen_type(state, ptr->type);
        fprintf(state->out, " %%%u", irgen_fresh_reg(state));
        if (ptr->next)
            fprintf(state->out, ", ");
    }

    fprintf(state->out, ") {\n");

    irgen_emit_block(state, 0);
    reg = 1;

    for (ptr = decl->args; ptr; ptr = ptr->next) {
        slot = irgen_emit_simple0(state, "alloca", ptr->type);
        irgen_emit_store(state, ptr->type, slot, IRVAL_REG(reg++));

        assert(state->locals_count < 256 && "too many locals!");
        state->locals[state->locals_count].sym = ptr->sym;
        state->locals[state->locals_count].val = slot;
        state->locals_count++;
    }

    irgen_stmt(state, decl->body);

    if (!state->terminated) {
        if (decl->out && decl->out->tag == TYPE_VOID) {
            fprintf(state->out, "\tret void\n");
        } else {
            fprintf(state->out, "\tunreachable\n");
        }
    }

    fprintf(state->out, "}\n");
}

void
irgen_decl(struct irgen *state, struct decl *decl)
{
    switch (decl->tag) {
        case DECL_PROC:
            irgen_decl_proc(state, (struct decl_proc *)decl);
            break;

        default:
            unreachable();
    }
}

void
irgen_module(struct irgen *state, struct decl **decls, size_t decls_count)
{
    struct symbol *sym;
    size_t i;

    for (i = 0; i < decls_count; i++) {
        if (decls[i]->tag != DECL_PROC)
            continue;

        sym = ((struct decl_proc *)decls[i])->sym;
        assert(state->globals_count < 256 && "too many globals!");
        state->globals[state->globals_count].sym = sym;
        state->globals[state->globals_count].val = IRVAL_GLOBAL(sym->str);
        state->globals_count++;

    }

    fprintf(state->out, "target triple = \"x86_64-pc-linux-gnu\"\n");
    for (i = 0; i < decls_count; i++) {
        irgen_decl(state, decls[i]);
    }
}
