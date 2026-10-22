#include <stdarg.h>
#include <string.h>

#include "irgen.h"
#include "report.h"

#define IR_EMITF(state, ...) \
    fprintf((state)->out, __VA_ARGS__)

#define IR_EMITC(state, chr) \
    fputc((chr), (state)->out)

void
irgen_init(struct irgen *state, FILE *out)
{
    memset(state, 0, sizeof(struct irgen));
    state->out = out;
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

static void
irgen_val(struct irgen *state, struct irval val)
{
    switch (val.tag) {
        case IRVAL_ZERO:
            IR_EMITF(state, "zeroinitializer");
            break;

        case IRVAL_UNDEF:
            IR_EMITF(state, "undef");
            break;

        case IRVAL_POISON:
            IR_EMITF(state, "poison");
            break;

        case IRVAL_INT:
            IR_EMITF(state, "%ld", val.ival);
            break;

        case IRVAL_REG:
            IR_EMITF(state, "%%%u", val.reg);
            break;

        case IRVAL_GLOBAL:
            IR_EMITF(state, "@\"%s\"", val.global);
            break;

        case IRVAL_STRING:
            IR_EMITF(state, "@.str.%u", val.reg);
            break;

        default:
            unreachable();
    }
}

static struct symbol *
irgen_link_sym(struct irgen *state, struct decl *decl)
{
    assert(decl->bind && "unresolved decl?");
    assert(BIND_IS_LINKED(decl->bind) && "unlinked struct?");
    return decl->bind->link_sym;
}

/*
 * Types
 */
static void
irgen_type(struct irgen *state, struct type *type)
{
    struct symbol *link_sym;
    size_t i;

    assert(type && "expected valid type");
    switch (type->tag) {
        case TYPE_BOOL:
        case TYPE_INT:
        case TYPE_UINT:
            IR_EMITF(state, "i%zu", type_bit_width(type));
            break;

        case TYPE_VOID:
            IR_EMITF(state, "{}");
            break;

        case TYPE_PTR:
        case TYPE_PROC:
            IR_EMITF(state, "ptr");
            break;

        case TYPE_ARRAY:
            IR_EMITF(state, "[%zu x ", type->array.count);
            irgen_type(state, type->array.item);
            IR_EMITF(state, "]");
            break;

        case TYPE_SLICE:
            IR_EMITF(state, "{ ptr, i64 }");
            break;

        case TYPE_STRUCT:
            if (type->strukt.decl) {
                link_sym = irgen_link_sym(state, &type->strukt.decl->decl);
                IR_EMITF(state, "%%\"%s\"", link_sym->str);
            } else {
                /*
                 * Anonymous structs
                 */
                IR_EMITF(state, "{ ");
                for (i = 0; i < type->strukt.fields_count; i++) {
                    if (i != 0)
                        IR_EMITF(state, ", ");

                    irgen_type(state, type->strukt.fields[i].type);
                }
                IR_EMITF(state, " }");
            }
            break;

        default:
            unreachable();
    }
}

static void
irgen_type_ret(struct irgen *state, struct type *type)
{
    assert(type && "expected valid type");

    if (type_is_zst(type)) {
        IR_EMITF(state, "void");
    } else {
        irgen_type(state, type);
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
    IR_EMITF(state, "\t%%%u = %s ", out.reg, instr);
    irgen_type(state, type);
    fputc('\n', state->out);
    return out;
}

static struct irval
irgen_emit_simple2(struct irgen *state, const char *instr, struct type *type,
                   struct irval op1, struct irval op2)
{
    struct irval out;

    out = IRVAL_REG(irgen_fresh_reg(state));
    IR_EMITF(state, "\t%%%u = %s ", out.reg, instr);
    irgen_type(state, type);

    IR_EMITC(state, ' ');
    irgen_val(state, op1);

    IR_EMITF(state, ", ");
    irgen_val(state, op2);

    IR_EMITC(state, '\n');
    return out;
}

static struct irval
irgen_emit_phi(struct irgen *state, struct type *type, unsigned n, ...)
{
    struct irval val, out;
    unsigned i, label;
    va_list args;

    va_start(args, n);
    out = IRVAL_REG(irgen_fresh_reg(state));
    IR_EMITF(state, "\t%%%u = phi ", out.reg);
    irgen_type(state, type);

    for (i = 0; i < n; i++) {
        val = va_arg(args, struct irval);
        label = va_arg(args, unsigned);

        if (i != 0)
            fputc(',', state->out);

        IR_EMITF(state, " [ ");
        irgen_val(state, val);
        IR_EMITF(state, ", %%b%u ]", label);
    }

    fputc('\n', state->out);
    va_end(args);
    return out;
}

static struct irval
irgen_emit_gep(struct irgen *state, struct type *type, struct irval ptr,
               unsigned n, ...)
{
    struct irval idx, out;
    struct type *idx_type;
    unsigned i;
    va_list args;

    va_start(args, n);
    out = IRVAL_REG(irgen_fresh_reg(state));
    IR_EMITF(state, "\t%%%u = getelementptr ", out.reg);
    irgen_type(state, type);

    IR_EMITF(state, ", ptr ");
    irgen_val(state, ptr);

    for (i = 0; i < n; i++) {
        idx = va_arg(args, struct irval);
        idx_type = va_arg(args, struct type *);

        IR_EMITF(state, ", ");
        if (idx_type)
            irgen_type(state, idx_type);
        else
            IR_EMITF(state, "i32");

        IR_EMITC(state, ' ');

        irgen_val(state, idx);
    }

    fputc('\n', state->out);
    va_end(args);
    return out;
}

static struct irval
irgen_emit_extractvalue(struct irgen *state, struct irval arr,
                        struct type *arr_type, struct type *type, size_t idx)
{
    struct irval out;

    if (type_is_zst(arr_type) || type_is_zst(type))
        return IRVAL_POISON();

    out = IRVAL_REG(irgen_fresh_reg(state));
    IR_EMITF(state, "\t%%%u = extractvalue ", out.reg);
    irgen_type(state, arr_type);
    IR_EMITC(state, ' ');
    irgen_val(state, arr);
    IR_EMITF(state, ", %zu\n", idx);
    return out;
}

static struct irval
irgen_emit_insertvalue(struct irgen *state, struct irval arr, struct type *arr_type,
                       struct irval val, struct type *type, size_t idx)
{
    struct irval out;

    if (type_is_zst(arr_type) || type_is_zst(type))
        return IRVAL_POISON();

    out = IRVAL_REG(irgen_fresh_reg(state));
    IR_EMITF(state, "\t%%%u = insertvalue ", out.reg);
    irgen_type(state, arr_type);
    IR_EMITC(state, ' ');
    irgen_val(state, arr);
    IR_EMITF(state, ", ");
    irgen_type(state, type);
    IR_EMITC(state, ' ');
    irgen_val(state, val);
    IR_EMITF(state, ", %zu\n", idx);
    return out;
}

static struct irval
irgen_emit_conv(struct irgen *state, const char *instr, struct irval val,
                struct type *src, struct type *dst)
{
    struct irval out;

    out = IRVAL_REG(irgen_fresh_reg(state));

    IR_EMITF(state, "\t%%%u = %s ", out.reg, instr);
    irgen_type(state, src);

    IR_EMITC(state, ' ');
    irgen_val(state, val);
    IR_EMITF(state, " to ");
    irgen_type(state, dst);

    fputc('\n', state->out);
    return out;
}

static struct irval
irgen_emit_load(struct irgen *state, struct type *type, struct irval slot)
{
    struct irval out;

    if (type_is_zst(type))
        return IRVAL_POISON();

    out = IRVAL_REG(irgen_fresh_reg(state));
    IR_EMITF(state, "\t%%%u = load ", out.reg);
    irgen_type(state, type);

    IR_EMITF(state, ", ptr ");
    irgen_val(state, slot);
    IR_EMITC(state, '\n');
    return out;
}

static void
irgen_emit_store(struct irgen *state, struct type *type, struct irval slot,
                 struct irval val)
{
    if (type_is_zst(type))
        return;

    IR_EMITF(state, "\tstore ");
    irgen_type(state, type);
    IR_EMITC(state, ' ');
    irgen_val(state, val);
    IR_EMITF(state, ", ptr ");
    irgen_val(state, slot);
    IR_EMITC(state, '\n');
}

static void
irgen_emit_ret(struct irgen *state, struct type *type, struct irval val)
{
    if (state->terminated)
        return;

    IR_EMITF(state, "\tret ");
    irgen_type_ret(state, type);

    if (type && !type_is_zst(type)) {
        IR_EMITC(state, ' ');
        irgen_val(state, val);
    }

    fputc('\n', state->out);
    state->terminated = true;
}

static void
irgen_emit_br(struct irgen *state, unsigned label)
{
    if (state->terminated)
        return;

    IR_EMITF(state, "\tbr label %%b%u\n", label);
    state->terminated = true;
}

static void
irgen_emit_condbr(struct irgen *state, struct irval cond,
                  unsigned t_label, unsigned f_label)
{
    if (state->terminated)
        return;

    IR_EMITF(state, "\tbr i1 ");
    irgen_val(state, cond);
    IR_EMITF(state, ", label %%b%u, label %%b%u\n", t_label, f_label);
    state->terminated = true;
}

static unsigned
irgen_emit_block(struct irgen *state, unsigned label)
{
    unsigned prev;

    prev = state->in_block;
    state->in_block = label;
    state->terminated = false;

    IR_EMITF(state, "b%u:\n", label);
    return prev;
}

/*
 * Expressions
 */
struct irval irgen_expr(struct irgen *state, struct expr *expr);

static struct irval
irgen_expr_addr(struct irgen *state, struct expr *expr)
{
    struct expr_ident *ident;
    struct expr_unop *unop;
    struct expr_index *index;
    struct expr_access *access;
    struct expr_array *array;
    struct expr_struct *strukt;
    struct expr_macro *macro;
    struct irval val, ptr, idx;
    struct type *type;
    unsigned field_index;
    size_t i, j;

    switch (expr->tag) {
        case EXPR_IDENT:
            ident = (struct expr_ident *)expr;
            if (ident->bind) {
                assert(BIND_IS_LINKED(ident->bind) && ident->bind->link_sym);
                return IRVAL_GLOBAL(ident->bind->link_sym->str);
            }

            for (i = state->locals_count; i > 0; i--) {
                if (state->locals[i - 1].sym == ident->sym)
                    return state->locals[i - 1].val;
            }

            assert(false && "undefined vars slipped through!");

        case EXPR_UNOP:
            unop = (struct expr_unop *)expr;
            if (unop->unop == UNOP_DEREF) {
                return irgen_expr(state, unop->op);
            }
            break;

        case EXPR_INDEX:
            index = (struct expr_index *)expr;

            /*
             * Array operands must lvalues, while pointers can by anything
             */
            if (index->op->type->tag == TYPE_ARRAY) {
                ptr = irgen_expr_addr(state, index->op);
                idx = irgen_expr(state, index->index);
                return irgen_emit_gep(state, index->op->type, ptr, 2,
                                      IRVAL_INT(0), NULL,
                                      idx, index->index->type);
            }

            if (index->op->type->tag == TYPE_SLICE) {
                val = irgen_expr(state, index->op);

                idx = irgen_expr(state, index->index);

                ptr = irgen_emit_extractvalue(state, val, index->op->type,
                                              type_get_ptr(NULL, index->expr.type), 0);
            } else {
                ptr = irgen_expr(state, index->op);
                idx = irgen_expr(state, index->index);
            }

            return irgen_emit_gep(state, index->expr.type, ptr, 1,
                                  idx, index->index->type);

        case EXPR_ACCESS:
            access = (struct expr_access *)expr;
            if (access->bind) {
                assert(BIND_IS_LINKED(access->bind) && access->bind->link_sym);
                return IRVAL_GLOBAL(access->bind->link_sym->str);
            }

            type = access->op->type;
            if (type->tag == TYPE_PTR) {
                ptr = irgen_expr(state, access->op);
                type = type->pointer;

                /*
                 * Auto-dereference struct pointers
                 */
                while (type->tag == TYPE_PTR) {
                    ptr = irgen_emit_load(state, type, ptr);
                    type = type->pointer;
                }
            } else {
                ptr = irgen_expr_addr(state, access->op);
            }

            return irgen_emit_gep(state, type, ptr, 2,
                                  IRVAL_INT(0), NULL,
                                  IRVAL_INT(access->index), NULL);

        case EXPR_ARRAY:
            array = (struct expr_array *)expr;
            ptr = IRVAL_REG(array->slot);

            for (i = 0; i < array->args_count; i++) {
                val = irgen_expr(state, array->args[i]);
                if (type_is_zst(array->expr.type))
                    continue;

                idx = irgen_emit_gep(state, array->expr.type, ptr, 2,
                                     IRVAL_INT(0), NULL,
                                     IRVAL_INT(i), NULL);
                irgen_emit_store(state, array->args[i]->type, idx, val);
            }
            return ptr;

        case EXPR_STRUCT:
            strukt = (struct expr_struct *)expr;
            ptr = IRVAL_REG(strukt->slot);

            /*
             * Zero initialize the struct, then populate the fields
             */
            irgen_emit_store(state, strukt->expr.type, ptr, IRVAL_ZERO());

            for (i = 0; i < strukt->fields_count; i++) {
                for (j = 0; j < strukt->expr.type->strukt.fields_count; j++) {
                    if (strukt->expr.type->strukt.fields[j].name
                            == strukt->fields[i].field->sym) {
                        field_index = strukt->expr.type->strukt.fields[j].index;
                        break;
                    }
                }

                assert(j != strukt->expr.type->strukt.fields_count && "not found!");

                val = irgen_expr(state, strukt->fields[i].value);
                if (type_is_zst(strukt->fields[i].value->type))
                    continue;

                idx = irgen_emit_gep(state, strukt->expr.type, ptr, 2,
                                     IRVAL_INT(0), NULL,
                                     IRVAL_INT(field_index), NULL);
                irgen_emit_store(state, strukt->fields[i].value->type, idx, val);
            }
            return ptr;

        case EXPR_MACRO:
            macro = (struct expr_macro *)expr;
            if (macro->macro.tag == MACRO_EXPR)
                return irgen_expr_addr(state, macro->macro.expr);
            break;

        default:
            break;
    }

    unreachable();
}

static struct irval
irgen_expr_literal(struct irgen *state, struct expr_literal *expr)
{
    switch (expr->literal) {
        case LIT_INTEGER:
        case LIT_BOOL:
            return IRVAL_INT(expr->integer);

        case LIT_STRING:
            state->strings[state->strings_count] = expr->string;
            assert(state->strings_count < 256 && "too many strings!");
            return IRVAL_STRING(state->strings_count++);

        default:
            unreachable();
    }
}

static struct irval
irgen_expr_binop(struct irgen *state, struct expr_binop *expr)
{
    struct irval lhs, rhs, tmp;
    unsigned block_lhs, block_rhs, rhs_label, merge_label;
    const char *instr;
    bool unsigned_op;

    unsigned_op = expr->op_lhs->type->tag == TYPE_UINT
               || expr->op_lhs->type->tag == TYPE_PTR;

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
            instr = unsigned_op ? "icmp ugt": "icmp sgt";
            break;

        case BINOP_GTEQ:
            instr = unsigned_op ? "icmp uge": "icmp sge";
            break;

        case BINOP_LT:
            instr = unsigned_op ? "icmp ult": "icmp slt";
            break;

        case BINOP_LTEQ:
            instr = unsigned_op ? "icmp ule": "icmp sle";
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
            instr = unsigned_op ? "lshr": "ashr";
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
            instr = unsigned_op ? "udiv": "sdiv";
            break;

        case BINOP_MOD:
        case BINOP_MOD_SET:
            instr = unsigned_op ? "urem": "srem";
            break;

        default:
            unreachable();
    }

    /*
     * Assignments require lvalue handling
     */
    if (expr->binop >= BINOP_SET && expr->binop <= BINOP_MOD_SET) {
        lhs = irgen_expr_addr(state, expr->op_lhs);
        rhs = irgen_expr(state, expr->op_rhs);

        if (expr->binop != BINOP_SET) {
            tmp = irgen_emit_load(state, expr->op_rhs->type, lhs);

            rhs = irgen_emit_simple2(state, instr, expr->op_rhs->type,
                                     tmp, rhs);
        }

        irgen_emit_store(state, expr->op_rhs->type, lhs, rhs);
        return IRVAL_POISON();
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
        return irgen_expr_addr(state, expr->op);
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
irgen_expr_call(struct irgen *state, struct expr_call *expr)
{
    struct irval func, out, vals[expr->args_count];
    unsigned i, k;

    func = irgen_expr(state, expr->op);

    for (i = 0; i < expr->args_count; i++) {
        vals[i] = irgen_expr(state, expr->args[i]);
    }

    if (type_is_zst(expr->expr.type)) {
        out = IRVAL_POISON();
        IR_EMITF(state, "\tcall ");
    } else {
        out = IRVAL_REG(irgen_fresh_reg(state));
        IR_EMITF(state, "\t%%%u = call ", out.reg);
    }

    irgen_type_ret(state, expr->expr.type);
    IR_EMITC(state, ' ');
    irgen_val(state, func);
    IR_EMITC(state, '(');

    for (i = 0, k = 0; i < expr->args_count; i++) {
        if (type_is_zst(expr->args[i]->type))
            continue;

        if (k != 0)
            IR_EMITF(state, ", ");

        irgen_type(state, expr->args[i]->type);
        IR_EMITC(state, ' ');
        irgen_val(state, vals[i]);
        k++;
    }

    IR_EMITF(state, ")\n");
    return out;
}

static struct irval
irgen_expr_access(struct irgen *state, struct expr_access *expr)
{
    struct irval val;

    val = irgen_expr(state, expr->op);
    return irgen_emit_extractvalue(state, val, expr->op->type,
                                   expr->expr.type, expr->index);
}

static const char *
irgen_cast_instr(const struct type *src, const struct type *dst)
{
    size_t src_bits, dst_bits;
    bool src_ptr, dst_ptr;

    src_ptr = src->tag == TYPE_PTR;
    dst_ptr = dst->tag == TYPE_PTR;
    if (src_ptr && dst_ptr)
        return NULL;

    if (src_ptr)
        return "ptrtoint";

    if (dst_ptr)
        return "inttoptr";

    src_bits = type_bit_width(src);
    dst_bits = type_bit_width(dst);

    if (src_bits == dst_bits)
        return NULL;

    if (dst_bits < src_bits)
        return "trunc";

    return src->tag == TYPE_INT ? "sext" : "zext";
}

static struct irval
irgen_expr_macro(struct irgen *state, struct expr_macro *expr)
{
    struct type *type;
    struct expr *arg;
    struct irval val, ptr;
    const char *instr;
    size_t count;

    switch (expr->macro.tag) {
        case MACRO_EXPR:
            return irgen_expr(state, expr->macro.expr);

        case MACRO_SIZEOF:
        case MACRO_ALIGNOF:
            return IRVAL_INT(expr->macro.value);

        case MACRO_CAST:
            arg = expr->args[1];
            val = irgen_expr(state, arg);
            instr = irgen_cast_instr(arg->type, expr->expr.type);
            return instr
                 ? irgen_emit_conv(state, instr, val, arg->type, expr->expr.type)
                 : val;

        case MACRO_SLICE:
            arg = expr->args[0];

            if (arg->type->tag == TYPE_PTR) {
                type = arg->type->pointer->array.item;
                count = arg->type->pointer->array.count;
                ptr = irgen_expr(state, arg);
            } else {
                type = arg->type->array.item;
                count = arg->type->array.count;
                ptr = irgen_expr_addr(state, arg);
            }

            val = IRVAL_UNDEF();
            val = irgen_emit_insertvalue(state, val, expr->expr.type,
                                         ptr, type_get_ptr(NULL, type), 0);
            val = irgen_emit_insertvalue(state, val, expr->expr.type,
                                         IRVAL_INT(count), type_get_int(NULL, 64), 1);
            return val;

        default:
            unreachable();
    }
}

static void
irgen_block_item(struct irgen *state, struct decl *decl)
{
    struct decl_var *var;
    struct irval slot, val;

    switch (decl->tag) {
        case DECL_VAR:
            var = (struct decl_var *)decl;
            slot = IRVAL_REG(var->slot);

            assert(state->locals_count < 256 && "too many locals!");
            state->locals[state->locals_count].sym = var->sym;
            state->locals[state->locals_count].val = slot;
            state->locals_count++;

            if (var->value) {
                val = irgen_expr(state, var->value);
                irgen_emit_store(state, var->value->type, slot, val);
            }
            break;

        case DECL_EXPR:
            irgen_expr(state, ((struct decl_expr *)decl)->expr);
            break;

        default:
            unreachable();
    }
}

static struct irval
irgen_expr_block(struct irgen *state, struct expr_block *expr)
{
    size_t i, locals;

    locals = state->locals_count;

    for (i = 0; i < expr->items_count; i++) {
        /*
         * If the current block has been terminated emit a new one
         */
        if (state->terminated)
            irgen_emit_block(state, irgen_fresh_label(state));

        irgen_block_item(state, expr->items[i]);
    }

    state->locals_count = locals;
    return IRVAL_POISON();
}

static struct irval
irgen_expr_return(struct irgen *state, struct expr_return *expr)
{
    struct irval val;

    if (expr->value) {
        val = irgen_expr(state, expr->value);
        irgen_emit_ret(state, expr->value->type, val);
    } else {
        val = IRVAL_POISON();
        irgen_emit_ret(state, type_get_void(NULL), val);
    }

    return IRVAL_POISON();
}

/*
 * IR structure
 *
 *      ...
 *      br i1 %cond, label %t_label, label %f_label
 * t_label:
 *      ...
 *      br label %merge_label
 * f_label:
 *      ...
 *      br label %merge_label
 *  merge_label:
 *      ...
 */
static struct irval
irgen_expr_if(struct irgen *state, struct expr_if *expr)
{
    struct irval cond;
    unsigned t_label, f_label, merge_label;

    cond = irgen_expr(state, expr->cond);

    t_label = irgen_fresh_label(state);

    if (expr->b_false) {
        f_label = irgen_fresh_label(state);
        merge_label = irgen_fresh_label(state);
        irgen_emit_condbr(state, cond, t_label, f_label);
    } else {
        merge_label = irgen_fresh_label(state);
        irgen_emit_condbr(state, cond, t_label, merge_label);
    }

    irgen_emit_block(state, t_label);
    irgen_expr(state, expr->b_true);
    irgen_emit_br(state, merge_label);

    if (expr->b_false) {
        irgen_emit_block(state, f_label);
        irgen_expr(state, expr->b_false);
        irgen_emit_br(state, merge_label);
    }

    irgen_emit_block(state, merge_label);
    return IRVAL_POISON();
}

/*
 * IR structure
 *
 *      ...
 *      br label %cond_label
 * cond_label:
 *      ...
 *      br i1 %cond, label %body_label, label %exit_label
 * body_label:
 *      ...
 *      br label %cond_label
 * exit_label:
 *      ...
 */
static struct irval
irgen_expr_while(struct irgen *state, struct expr_while *expr)
{
    struct irval cond;
    unsigned cond_label, body_label, exit_label;
    unsigned saved_cond, saved_exit;

    cond_label = irgen_fresh_label(state);
    body_label = irgen_fresh_label(state);
    exit_label = irgen_fresh_label(state);

    irgen_emit_br(state, cond_label);
    irgen_emit_block(state, cond_label);
    cond = irgen_expr(state, expr->cond);
    irgen_emit_condbr(state, cond, body_label, exit_label);

    saved_cond = state->loop_cond;
    saved_exit = state->loop_exit;
    state->loop_cond = cond_label;
    state->loop_exit = exit_label;

    irgen_emit_block(state, body_label);
    irgen_expr(state, expr->body);
    irgen_emit_br(state, cond_label);

    state->loop_cond = saved_cond;
    state->loop_exit = saved_exit;

    irgen_emit_block(state, exit_label);
    return IRVAL_POISON();
}

static struct irval
irgen_expr_continue(struct irgen *state, struct expr *expr)
{
    irgen_emit_br(state, state->loop_cond);
    return IRVAL_POISON();
}

static struct irval
irgen_expr_break(struct irgen *state, struct expr *expr)
{
    irgen_emit_br(state, state->loop_exit);
    return IRVAL_POISON();
}

struct irval
irgen_expr(struct irgen *state, struct expr *expr)
{
    struct irval ptr;

    if (expr_is_mem(expr)) {
        ptr = irgen_expr_addr(state, expr);
        return !type_is_sized(expr->type)
             ? ptr
             : irgen_emit_load(state, expr->type, ptr);
    }

    switch (expr->tag) {
        case EXPR_LITERAL:
            return irgen_expr_literal(state, (struct expr_literal *)expr);

        case EXPR_BINOP:
            return irgen_expr_binop(state, (struct expr_binop *)expr);

        case EXPR_UNOP:
            return irgen_expr_unop(state, (struct expr_unop *)expr);

        case EXPR_CALL:
            return irgen_expr_call(state, (struct expr_call *)expr);

        case EXPR_ACCESS:
            return irgen_expr_access(state, (struct expr_access *)expr);

        case EXPR_MACRO:
            return irgen_expr_macro(state, (struct expr_macro *)expr);

        case EXPR_BLOCK:
            return irgen_expr_block(state, (struct expr_block *)expr);

        case EXPR_IF:
            return irgen_expr_if(state, (struct expr_if *)expr);

        case EXPR_RETURN:
            return irgen_expr_return(state, (struct expr_return *)expr);

        case EXPR_WHILE:
            return irgen_expr_while(state, (struct expr_while *)expr);

        case EXPR_CONTINUE:
            return irgen_expr_continue(state, (struct expr *)expr);

        case EXPR_BREAK:
            return irgen_expr_break(state, (struct expr *)expr);

        default:
            unreachable();
    }
}

/*
 * Hoist local vars to the first IR block for mem2reg
 */
static void irgen_hoist_expr(struct irgen *state, struct expr *expr);

static void
irgen_hoist_decl(struct irgen *state, struct decl *decl)
{
    struct decl_var *var;

    switch (decl->tag) {
        case DECL_VAR:
            var = (struct decl_var *)decl;
            var->slot = irgen_emit_simple0(state, "alloca", var->type).reg;
            irgen_hoist_expr(state, var->value);
            break;

        case DECL_EXPR:
            irgen_hoist_expr(state, ((struct decl_expr *)decl)->expr);
            break;

        default:
            unreachable();
    }
}

static void
irgen_hoist_expr(struct irgen *state, struct expr *expr)
{
    struct expr_call *call;
    struct expr_array *array;
    struct expr_struct *strukt;
    struct expr_macro *macro;
    struct expr_block *block;
    size_t i;

    if (!expr)
        return;

    switch (expr->tag) {
        case EXPR_LITERAL:
        case EXPR_IDENT:
            break;

        case EXPR_BINOP:
            irgen_hoist_expr(state, ((struct expr_binop *)expr)->op_lhs);
            irgen_hoist_expr(state, ((struct expr_binop *)expr)->op_rhs);
            break;

        case EXPR_UNOP:
            irgen_hoist_expr(state, ((struct expr_unop *)expr)->op);
            break;

        case EXPR_CALL:
            call = (struct expr_call *)expr;
            irgen_hoist_expr(state, call->op);
            for (i = 0; i < call->args_count; i++) {
                irgen_hoist_expr(state, call->args[i]);
            }
            break;

        case EXPR_INDEX:
            irgen_hoist_expr(state, ((struct expr_index *)expr)->op);
            irgen_hoist_expr(state, ((struct expr_index *)expr)->index);
            break;

        case EXPR_ACCESS:
            irgen_hoist_expr(state, ((struct expr_access *)expr)->op);
            break;

        case EXPR_ARRAY:
            array = (struct expr_array *)expr;
            array->slot = irgen_emit_simple0(state, "alloca", expr->type).reg;
            for (i = 0; i < array->args_count; i++) {
                irgen_hoist_expr(state, array->args[i]);
            }
            break;

        case EXPR_STRUCT:
            strukt = (struct expr_struct *)expr;
            strukt->slot = irgen_emit_simple0(state, "alloca", expr->type).reg;
            for (i = 0; i < strukt->fields_count; i++) {
                irgen_hoist_expr(state, strukt->fields[i].value);
            }
            break;

        case EXPR_MACRO:
            macro = (struct expr_macro *)expr;

            if (macro->macro.tag == MACRO_EXPR) {
                irgen_hoist_expr(state, macro->macro.expr);
            } else if (macro->macro.tag == MACRO_CAST) {
                irgen_hoist_expr(state, macro->args[1]);
            } else if (macro->macro.tag == MACRO_SLICE) {
                irgen_hoist_expr(state, macro->args[0]);
            }
            break;

        case EXPR_BLOCK:
            block = (struct expr_block *)expr;
            for (i = 0; i < block->items_count; i++)
                irgen_hoist_decl(state, block->items[i]);
            break;

        case EXPR_IF:
            irgen_hoist_expr(state, ((struct expr_if *)expr)->cond);
            irgen_hoist_expr(state, ((struct expr_if *)expr)->b_true);
            irgen_hoist_expr(state, ((struct expr_if *)expr)->b_false);
            break;

        case EXPR_WHILE:
            irgen_hoist_expr(state, ((struct expr_while *)expr)->cond);
            irgen_hoist_expr(state, ((struct expr_while *)expr)->body);
            break;

        case EXPR_RETURN:
            irgen_hoist_expr(state, ((struct expr_return *)expr)->value);
            break;

        case EXPR_CONTINUE:
        case EXPR_BREAK:
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
    struct symbol *link_sym;
    struct irval slot;
    unsigned reg, i, k;

    link_sym = irgen_link_sym(state, &decl->decl);

    /*
     * External declaration
     */
    if (!decl->body) {
        /*
         * TODO: Move this into a helper for when we have globals
         * Also add a dedicated check to see if all bindings have the same type
         */
        for (i = 0; i < state->externs_count; i++) {
            if (state->externs[i] == link_sym)
                return;
        }

        assert(state->externs_count < 256 && "too many externs!");
        state->externs[state->externs_count] = link_sym;
        state->externs_count++;

        IR_EMITF(state, "\ndeclare ");
        irgen_type_ret(state, decl->out);
        IR_EMITF(state, " @\"%s\"(", link_sym->str);

        for (ptr = decl->args, k = 0; ptr; ptr = ptr->next) {
            if (type_is_zst(ptr->type))
                continue;

            if (k != 0)
                IR_EMITF(state, ", ");

            irgen_type(state, ptr->type);
            k++;
        }

        IR_EMITF(state, ")\n");
        return;
    }

    state->regs = 0;
    state->labels = 0;
    state->locals_count = 0;

    IR_EMITF(state, "\ndefine ");
    irgen_type_ret(state, decl->out);
    IR_EMITF(state, " @\"%s\"(", link_sym->str);

    for (ptr = decl->args, k = 0; ptr; ptr = ptr->next) {
        if (type_is_zst(ptr->type))
            continue;

        if (k != 0)
            IR_EMITF(state, ", ");

        irgen_type(state, ptr->type);
        IR_EMITF(state, " %%%u", irgen_fresh_reg(state));
        k++;
    }

    IR_EMITF(state, ") {\n");

    irgen_emit_block(state, 0);
    reg = 1;

    for (ptr = decl->args; ptr; ptr = ptr->next) {
        slot = irgen_emit_simple0(state, "alloca", ptr->type);
        if (!type_is_zst(ptr->type))
            irgen_emit_store(state, ptr->type, slot, IRVAL_REG(reg++));

        assert(state->locals_count < 256 && "too many locals!");
        state->locals[state->locals_count].sym = ptr->sym;
        state->locals[state->locals_count].val = slot;
        state->locals_count++;
    }

    irgen_hoist_expr(state, decl->body);
    irgen_expr(state, decl->body);

    if (!state->terminated) {
        if (decl->out && type_is_zst(decl->out)) {
            IR_EMITF(state, "\tret void\n");
        } else {
            IR_EMITF(state, "\tunreachable\n");
        }
    }

    IR_EMITF(state, "}\n");
}

static void
irgen_decl_struct(struct irgen *state, struct decl_struct *decl)
{
    struct symbol *link_sym;
    size_t i;

    link_sym = irgen_link_sym(state, &decl->decl);
    IR_EMITF(state, "\n%%\"%s\" = type { ", link_sym->str);

    for (i = 0; i < decl->fields_count; i++) {
        if (i != 0)
            IR_EMITF(state, ", ");

        irgen_type(state, decl->fields[i].type);
    }

    IR_EMITF(state, " }\n");
}

static void
irgen_decl_var(struct irgen *state, struct decl_var *decl)
{
    struct symbol *link_sym;
    struct expr_literal *lit;

    link_sym = irgen_link_sym(state, &decl->decl);
    IR_EMITF(state, "\n@\"%s\" = global ", link_sym->str);
    irgen_type(state, decl->type);

    if (decl->value && decl->value->tag == EXPR_LITERAL) {
        lit = (struct expr_literal *)decl->value;

        switch (lit->literal) {
            case LIT_INTEGER:
            case LIT_BOOL:
                IR_EMITF(state, " %ld\n", (long)lit->integer);
                return;

            case LIT_STRING:
                // TODO
                break;

            default:
                break;
        }
    }

    IR_EMITF(state, " zeroinitializer\n");
}

static void
irgen_decl_types(struct irgen *state, struct decl *decl)
{
    struct decl_module *mod;
    struct expr_block *block;
    size_t i;

    switch (decl->tag) {
        case DECL_STRUCT:
            irgen_decl_struct(state, (struct decl_struct *)decl);
            break;

        case DECL_MODULE:
            mod = (struct decl_module *)decl;
            if (mod->block) {
                assert(mod->block->tag == EXPR_BLOCK);

                block = (struct expr_block *)mod->block;
                for (i = 0; i < block->items_count; i++) {
                    irgen_decl_types(state, block->items[i]);
                }
            }
            break;

        default:
            /* No IR to generate */
            break;
    }
}

static void
irgen_decl_bodies(struct irgen *state, struct decl *decl)
{
    struct decl_module *mod;
    struct expr_block *block;
    size_t i;

    switch (decl->tag) {
        case DECL_PROC:
            irgen_decl_proc(state, (struct decl_proc *)decl);
            break;

        case DECL_MODULE:
            mod = (struct decl_module *)decl;
            if (mod->block) {
                assert(mod->block->tag == EXPR_BLOCK);

                block = (struct expr_block *)mod->block;
                for (i = 0; i < block->items_count; i++) {
                    irgen_decl_bodies(state, block->items[i]);
                }
            }
            break;

        case DECL_VAR:
            irgen_decl_var(state, (struct decl_var *)decl);
            break;

        default:
            /* No IR to generate */
            break;
    }
}

bool
irgen_modcache(struct irgen *state, struct modcache *cache)
{
    size_t i, j, len;
    uint8_t c;

    IR_EMITF(state, "target triple = \"x86_64-pc-linux-gnu\"\n");

    /*
     * Emit all types to avoid declaration ordering issues
     */
    for (i = 0; i < cache->mods_count; i++) {
        irgen_decl_types(state, cache->mods[i].decl);
    }

    /*
     * Emit all procedure bodies
     */
    for (i = 0; i < cache->mods_count; i++) {
        irgen_decl_bodies(state, cache->mods[i].decl);
    }

    /*
     * Emit collected string literals
     */
    for (i = 0; i < state->strings_count; i++) {
        len = strlen(state->strings[i]);
        IR_EMITF(state, "\n@.str.%zu ="
                 " private unnamed_addr constant [%zu x i8] c\"",
                 i, len + 1);

        for (j = 0; j < len; j++) {
            c = state->strings[i][j];

            // XXX
            if (c >= 32 && c <= 126 && c != '"' && c != '\\') {
                IR_EMITC(state, c);
            } else {
                IR_EMITF(state, "\\%02X", c);
            }
        }
        IR_EMITF(state, "\\00\", align 1\n");
    }

    return true;
}
