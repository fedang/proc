#include <stdlib.h>
#include <stdio.h>

#include "checker.h"
#include "report.h"

#define TYCHK_FIRST_PASS    0
#define TYCHK_SECOND_PASS   1
#define TYCHK_THIRD_PASS    2

#define TYCHK_ERROR(state, span, ...) \
    report_error((state)->src, &(span), __VA_ARGS__)

static void
checker_type_push(struct checker *state, struct symbol *sym,
                    struct type *type)
{
    assert(state->types_count < 256 && "too many types!");

    state->types[state->types_count].sym = sym;
    state->types[state->types_count].type = type;
    state->types_count++;
}

void
checker_init(struct checker *state, struct source *src)
{
    state->src = src;
    state->pass = TYCHK_FIRST_PASS;
    state->ret_type = NULL;
    state->loop_depth = 0;
    state->locals_count = 0;
    state->globals_count = 0;
    state->types_count = 0;

    checker_type_push(state, symbol_make("i8"), type_get_int(NULL, 8));
    checker_type_push(state, symbol_make("i16"), type_get_int(NULL, 16));
    checker_type_push(state, symbol_make("i32"), type_get_int(NULL, 32));
    checker_type_push(state, symbol_make("i64"), type_get_int(NULL, 64));
    checker_type_push(state, symbol_make("int"), type_get_int(NULL, 0));
    checker_type_push(state, symbol_make("u8"), type_get_uint(NULL, 8));
    checker_type_push(state, symbol_make("u16"), type_get_uint(NULL, 16));
    checker_type_push(state, symbol_make("u32"), type_get_uint(NULL, 32));
    checker_type_push(state, symbol_make("u64"), type_get_uint(NULL, 64));
    checker_type_push(state, symbol_make("uint"), type_get_uint(NULL, 0));
    checker_type_push(state, symbol_make("bool"), type_get_bool(NULL));
    checker_type_push(state, symbol_make("void"), type_get_void(NULL));
}

static void
checker_local_push(struct checker *state, struct symbol *sym,
                     struct type *type)
{
    assert(state->locals_count < 256 && "too many locals!");

    state->locals[state->locals_count].sym = sym;
    state->locals[state->locals_count].type = type;
    state->locals_count++;
}

static void
checker_local_pop(struct checker *state, unsigned n)
{
    assert(n <= state->locals_count && "locals overflow");

    // TODO: Cleanup memory?
    state->locals_count = n;
}

static void
checker_global_push(struct checker *state, struct symbol *sym,
                      struct type *type)
{
    assert(state->globals_count < 256 && "too many globals!");

    state->globals[state->globals_count].sym = sym;
    state->globals[state->globals_count].type = type;
    state->globals_count++;
}

static struct type *
checker_lookup(struct checker *state, struct symbol *sym)
{
    size_t i;

    /*
     * First search the locals, starting from the latest,
     * then continue with the globals (in any order)
     */
    for (i = state->locals_count; i > 0; i--) {
        if (state->locals[i - 1].sym == sym)
            return state->locals[i - 1].type;
    }

    for (i = 0; i < state->globals_count; i++) {
        if (state->globals[i].sym == sym)
            return state->globals[i].type;
    }

    return NULL;
}

static bool checker_resolve(struct checker *state, struct type **type);

static struct type *checker_eval_raw(struct checker *state, struct expr *expr);

static bool
checker_eval_const(struct checker *state, struct expr *expr, int64_t *out)
{
    struct expr_literal *lit;
    struct expr_unop *unop;
    struct expr_binop *binop;
    struct expr_macro *macro;
    struct type *type;
    int64_t lhs, rhs;

    switch (expr->tag) {
        case EXPR_LITERAL: {
            lit = (struct expr_literal *)expr;
            if (lit->literal != LIT_INTEGER) {
                TYCHK_ERROR(state, expr->span,
                        "Expected integer literal in constant expression\n");
                return false;
            }

            *out = lit->integer;
            return true;
        }

        case EXPR_UNOP: {
            unop = (struct expr_unop *)expr;
            if (!checker_eval_const(state, unop->op, out))
                return false;

            if (unop->unop == UNOP_NEG) {
                *out *= -1;
                return true;
            }

            TYCHK_ERROR(state, expr->span,
                        "Invalid unary operator in constant expression\n");
            return false;
        }

        case EXPR_BINOP: {
            binop = (struct expr_binop *)expr;
            if (!checker_eval_const(state, binop->op_lhs, &lhs) ||
                !checker_eval_const(state, binop->op_rhs, &rhs))
                return false;

            switch (binop->binop) {
                case BINOP_ADD:
                    *out = lhs + rhs;
                    return true;

                case BINOP_SUB:
                    *out = lhs - rhs;
                    return true;

                case BINOP_MUL:
                    *out = lhs * rhs;
                    return true;

                case BINOP_DIV:
                    if (rhs == 0) {
                        TYCHK_ERROR(state, expr->span,
                                    "Division by zero in constant expression\n");
                        return false;
                    }
                    *out = lhs / rhs;
                    return true;

                case BINOP_MOD:
                    *out = lhs % rhs;
                    return true;

                case BINOP_SHL:
                    *out = lhs << rhs;
                    return true;

                case BINOP_SHR:
                    *out = lhs >> rhs;
                    return true;

                case BINOP_OR:
                    *out = lhs | rhs;
                    return true;

                case BINOP_AND:
                    *out = lhs & rhs;
                    return true;

                case BINOP_XOR:
                    *out = lhs ^ rhs;
                    return true;

                default:
                    TYCHK_ERROR(state, expr->span,
                                "Operator not allowed in constant expression\n");
                    return false;
            }
        }

        case EXPR_MACRO: {
            macro = (struct expr_macro *)expr;

            if (macro->name == symbol_size_of || macro->name == symbol_align_of) {
                if (macro->body || macro->args_count != 1) {
                    TYCHK_ERROR(state, expr->span, "Expected 1 type argument and no body\n");
                    return false;
                }

                type = checker_eval_raw(state, macro->args[0]);
                if (!type)
                    return false;

                *out = macro->name == symbol_size_of
                    ? type_sizeof(type)
                    : type_alignof(type);
                return true;
            }

            TYCHK_ERROR(state, expr->span,
                        "Macro '@%s' cannot be evaluated at compile time\n", macro->name->str);
            return false;
        }

        default:
            TYCHK_ERROR(state, expr->span,
                        "Expression is not a compile-time constant\n");
            return false;
    }
}

static struct type *
checker_eval_raw(struct checker *state, struct expr *expr)
{
    struct type *type;
    struct expr_ident *ident;
    struct expr_unop *unop;
    struct expr_index *index;
    int64_t count;
    size_t i;

    switch (expr->tag) {
        case EXPR_IDENT: {
            ident = (struct expr_ident *)expr;

            for (i = 0; i < state->types_count; i++) {
                if (state->types[i].sym == ident->sym) {
                    type = state->types[i].type;

                    if (!checker_resolve(state, &type))
                        return NULL;

                    return type;
                }
            }

            TYCHK_ERROR(state, expr->span,
                        "Unknown type '%s'\n", ident->sym->str);
            return NULL;
        }

        case EXPR_UNOP: {
            unop = (struct expr_unop *)expr;
            if (unop->unop != UNOP_DEREF) {
                TYCHK_ERROR(state, expr->span,
                            "Invalid unary operator in type\n");
                return NULL;
            }

            type = checker_eval_raw(state, unop->op);
            if (!type)
                return NULL;

            return type_get_ptr(NULL, type);
        }

        case EXPR_INDEX: {
            index = (struct expr_index *)expr;

            type = checker_eval_raw(state, index->op);
            if (!type)
                return NULL;

            if (!checker_eval_const(state, index->index, &count))
                return NULL;

            if (count <= 0) {
                TYCHK_ERROR(state, index->index->span,
                            "Array size must be greater than 0\n");
                return NULL;
            }

            return type_get_array(NULL, type, count);
        }

        default:
            TYCHK_ERROR(state, expr->span,
                        "Invalid type expression\n");
            return NULL;
    }
}

static bool
checker_resolve(struct checker *state, struct type **type)
{
    size_t i;
    struct type *eval, *t;
    bool ok = true;

    if (!type || !*type)
        return true;

    t = *type;

    if (t->tag == TYPE_RAW) {
        eval = checker_eval_raw(state, t->raw);
        if (!eval)
            return false;

        *type = eval;
        return true;
    }

    if (t->tag == TYPE_PTR) {
        return checker_resolve(state, &t->pointer);
    }

    if (t->tag == TYPE_ARRAY) {
        return checker_resolve(state, &t->array.item);
    }

    if (t->tag == TYPE_PROC) {
        if (!checker_resolve(state, &t->proc.out)) {
            ok = false;
        }

        for (i = 0; i < t->proc.args_count; i++) {
            if (!checker_resolve(state, &t->proc.args[i])) {
                ok = false;
            }
        }
    }

    return ok;
}

static bool
checker_cmp(struct checker *state, struct type *t1, struct type *t2)
{
    return t1 == t2;
}

static bool checker_expr(struct checker *state, struct expr *expr);

static bool
checker_expr_literal(struct checker *state, struct expr_literal *expr)
{
    switch (expr->literal) {
        case LIT_INTEGER:
            expr->expr.type = type_get_int(NULL, 0);
            break;

        case LIT_STRING:
            expr->expr.type = type_get_ptr(NULL, type_get_uint(NULL, 8));
            break;

        default:
            unreachable();
    }

    return true;
}

static bool
checker_expr_ident(struct checker *state, struct expr_ident *expr)
{
    struct type *type;

    type = checker_lookup(state, expr->sym);
    if (!type) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Undefined variable '%s'\n", expr->sym->str);
        return false;
    }

    expr->expr.type = type;
    return true;
}

static bool
checker_expr_binop(struct checker *state, struct expr_binop *expr)
{
    if (!checker_expr(state, expr->op_lhs) || !checker_expr(state, expr->op_rhs))
        return false;

    if (!checker_cmp(state, expr->op_lhs->type, expr->op_rhs->type)) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Binop operands have different types\n");
        return false;
    }

    if (expr->binop >= BINOP_SET && expr->binop <= BINOP_MOD_SET) {
        if (!expr_is_lvalue(expr->op_lhs)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Binop left operand is not an lvalue\n");
            return false;
        }
    }

    switch (expr->binop) {
        case BINOP_BOOL_OR:
        case BINOP_BOOL_AND:
            if (expr->op_lhs->type->tag != TYPE_BOOL) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Expected bool operands for and/or\n");
                return false;
            }
            expr->expr.type = type_get_bool(NULL);
            break;

        case BINOP_EQ:
        case BINOP_NOTEQ:
        case BINOP_GT:
        case BINOP_GTEQ:
        case BINOP_LT:
        case BINOP_LTEQ:
            if (!type_is_scalar(expr->op_lhs->type)) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Invalid operand types for comparison\n");
                return false;
            }
            expr->expr.type = type_get_bool(NULL);
            break;

        case BINOP_SET:
            break;

        default:
            if (!type_is_integer(expr->op_lhs->type)) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Expected integer operands for arithmetic/bitwise operator\n");
                return false;
            }
            expr->expr.type = expr->op_lhs->type;
            break;
    }

    if (expr->binop >= BINOP_SET && expr->binop <= BINOP_MOD_SET) {
            expr->expr.type = type_get_void(NULL);
    }

    return true;
}

static bool
checker_expr_unop(struct checker *state, struct expr_unop *expr)
{
    if (expr->unop == UNOP_ADDROF) {
        if (!expr_is_lvalue(expr->op)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Unop operand is not an lvalue\n");
            return false;
        }
    }

    if (!checker_expr(state, expr->op))
        return false;

    switch (expr->unop) {
        case UNOP_DEREF:
            if (expr->op->type->tag != TYPE_PTR) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Only pointer types can be dereferenced\n");
                return false;
            }

            expr->expr.type = expr->op->type->pointer;
            break;

        case UNOP_ADDROF:
            expr->expr.type = type_get_ptr(NULL, expr->op->type);
            break;

        case UNOP_NOT:
            if (expr->op->type->tag != TYPE_BOOL) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Only bool values can be negated\n");
                return false;
            }

            expr->expr.type = type_get_bool(NULL);
            break;

        case UNOP_NEG:
            if (expr->op->type->tag != TYPE_INT && expr->op->type->tag != TYPE_UINT) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Only integers can be negated\n");
                return false;
            }

            expr->expr.type = expr->op->type;
            break;

        default:
            unreachable();
    }

    return true;
}

static bool
checker_expr_call(struct checker *state, struct expr_call *expr)
{
    struct type *proc;
    size_t i;

    if (!checker_expr(state, expr->op))
        return false;

    proc = expr->op->type;
    if (proc->tag != TYPE_PROC || proc->proc.args_count != expr->args_count) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Call target is not a proc\n");
        return false;
    }

    for (i = 0; i < expr->args_count; i++) {
        if (!checker_expr(state, expr->args[i]))
            return false;

        if (!checker_cmp(state, proc->proc.args[i], expr->args[i]->type)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Call argument does not match proc\n");
            return false;
        }
    }

    expr->expr.type = proc->proc.out;
    return true;
}

static bool
checker_expr_index(struct checker *state, struct expr_index *expr)
{
    if (!checker_expr(state, expr->op) || !checker_expr(state, expr->index))
        return false;

    if (expr->index->type->tag != TYPE_INT && expr->index->type->tag != TYPE_UINT) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Invalid index type\n");
        return false;
    }

    if (expr->op->type->tag == TYPE_ARRAY) {
        expr->expr.type = expr->op->type->array.item;
        return true;
    } else if (expr->op->type->tag == TYPE_PTR) {
        expr->expr.type = expr->op->type->pointer;
        return true;
    }

    TYCHK_ERROR(state, expr->expr.span,
                "Invalid type used for indexing\n");
    return false;
}

static bool
checker_expr_access(struct checker *state, struct expr_access *expr)
{
    struct type *type;
    size_t i;

    if (!checker_expr(state, expr->op))
        return false;

    type = expr->op->type;

    /*
     * Automatically dereference struct pointers
     */
    while (type->tag == TYPE_PTR) {
        type = type->pointer;
    }

    if (type->tag != TYPE_STRUCT) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Tried to access a field from a non-struct type\n");
        return false;
    }

    for (i = 0; i < type->strukt.fields_count; i++) {
        if (type->strukt.fields[i].name == expr->field) {
            expr->expr.type = type->strukt.fields[i].type;
            expr->offset = i;
            return true;
        }
    }

    TYCHK_ERROR(state, expr->expr.span,
                "Found no field named '%s'\n", expr->field->str);
    return false;
}

static bool
checker_expr_macro(struct checker *state, struct expr_macro *expr)
{
    struct type *type;

    if (expr->name == symbol_size_of || expr->name == symbol_align_of) {
        if (expr->body || expr->args_count != 1) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Expected 1 argument and no block\n");
            return false;
        }

        type = checker_eval_raw(state, expr->args[0]);
        if (!type)
            return false;

        if (expr->name == symbol_size_of) {
            expr->macro.tag = MACRO_SIZEOF;
            expr->macro.value = type_sizeof(type);
        } else {
            expr->macro.tag = MACRO_ALIGNOF;
            expr->macro.value = type_alignof(type);
        }

        expr->expr.type = type_get_uint(NULL, 64);
        return true;
    }

    if (expr->name == symbol_cast) {
        if (expr->body || expr->args_count != 2) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Expected 2 arguments: @cast(type, expr)\n");
            return false;
        }

        if (!checker_expr(state, expr->args[1]))
            return false;

        type = checker_eval_raw(state, expr->args[0]);
        if (!type)
            return false;

        if (!type_is_scalar(expr->args[1]->type) || !type_is_scalar(type)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Cast can only be used with scalar types\n");
            return false;
        }

        expr->macro.tag = MACRO_CAST;
        expr->expr.type = type;
        return true;
    }

    TYCHK_ERROR(state, expr->expr.span,
                "Unknown builtin macro '@%s'\n", expr->name->str);
    return false;
}

static bool
checker_expr(struct checker *state, struct expr *expr)
{
    switch (expr->tag) {
        case EXPR_LITERAL:
            return checker_expr_literal(state, (struct expr_literal *)expr);

        case EXPR_IDENT:
            return checker_expr_ident(state, (struct expr_ident *)expr);

        case EXPR_BINOP:
            return checker_expr_binop(state, (struct expr_binop *)expr);

        case EXPR_UNOP:
            return checker_expr_unop(state, (struct expr_unop *)expr);

        case EXPR_CALL:
            return checker_expr_call(state, (struct expr_call *)expr);

        case EXPR_INDEX:
            return checker_expr_index(state, (struct expr_index *)expr);

        case EXPR_ACCESS:
            return checker_expr_access(state, (struct expr_access *)expr);

        case EXPR_MACRO:
            return checker_expr_macro(state, (struct expr_macro *)expr);

        default:
            unreachable();
    }
}

static bool checker_stmt(struct checker *state, struct stmt *stmt);

static bool
checker_stmt_var(struct checker *state, struct stmt_var *stmt)
{
    /*
     * The user could have provided an explicit type
     */
    if (stmt->type) {
        if (!checker_resolve(state, &stmt->type))
            return false;
    }

    if (stmt->value) {
        if (!checker_expr(state, stmt->value))
            return false;

        if (!stmt->type) {
            stmt->type = stmt->value->type;
        } else if (!checker_cmp(state, stmt->type, stmt->value->type)) {
            TYCHK_ERROR(state, stmt->stmt.span,
                        "Type mismatch in variable assignment\n");
            return false;
        }
    }

    if (!stmt->type) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "Cannot infer type for variable '%s'\n", stmt->sym->str);
        return false;
    }

    checker_local_push(state, stmt->sym, stmt->type);
    return true;
}

static bool
checker_stmt_expr(struct checker *state, struct stmt_expr *stmt)
{
    return checker_expr(state, stmt->expr);
}

static bool
checker_stmt_block(struct checker *state, struct stmt_block *stmt)
{
    size_t i;
    unsigned locals;
    bool ok = true;

    locals = state->locals_count;

    for (i = 0; i < stmt->items_count; i++) {
        if (!checker_stmt(state, stmt->items[i])) {
            ok = false;
        }
    }

    checker_local_pop(state, locals);

    return ok;
}

static bool
checker_stmt_if(struct checker *state, struct stmt_if *stmt)
{
    if (!checker_expr(state, stmt->cond))
        return false;

    if (!checker_cmp(state, stmt->cond->type, type_get_bool(NULL))) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "If condition is not of type bool\n");
        return false;
    }

    if (!checker_stmt(state, stmt->b_true))
        return false;

    if (stmt->b_false && !checker_stmt(state, stmt->b_false))
        return false;

    return true;
}

static bool
checker_stmt_return(struct checker *state, struct stmt_return *stmt)
{
    if (!stmt->expr) {
        if (!checker_cmp(state, state->ret_type, type_get_void(NULL))) {
            TYCHK_ERROR(state, stmt->stmt.span,
                        "Expected return value\n");
            return false;
        }
    } else {
        if (!checker_expr(state, stmt->expr))
            return false;

        if (!checker_cmp(state, stmt->expr->type, state->ret_type)) {
            TYCHK_ERROR(state, stmt->stmt.span,
                        "Return value type does not match\n");
            return false;
        }
    }

    return true;
}

static bool
checker_stmt_while(struct checker *state, struct stmt_while *stmt)
{
    if (!checker_expr(state, stmt->cond))
        return false;

    if (!checker_cmp(state, stmt->cond->type, type_get_bool(NULL))) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "While condition is not of type bool\n");
        return false;
    }

    state->loop_depth++;
    if (!checker_stmt(state, stmt->body))
        return false;

    state->loop_depth--;
    return true;
}

static bool
checker_stmt(struct checker *state, struct stmt *stmt)
{
    switch (stmt->tag) {
        case STMT_VAR:
            return checker_stmt_var(state, (struct stmt_var *)stmt);

        case STMT_EXPR:
            return checker_stmt_expr(state, (struct stmt_expr *)stmt);

        case STMT_BLOCK:
            return checker_stmt_block(state, (struct stmt_block *)stmt);

        case STMT_IF:
            return checker_stmt_if(state, (struct stmt_if *)stmt);

        case STMT_RETURN:
            return checker_stmt_return(state, (struct stmt_return *)stmt);

        case STMT_WHILE:
            return checker_stmt_while(state, (struct stmt_while *)stmt);

        case STMT_BREAK:
        case STMT_CONTINUE:
            if (state->loop_depth == 0) {
                TYCHK_ERROR(state, stmt->span,
                            "Cannot break/continue outside of a loop\n");
                return false;
            }
            return true;

        default:
            unreachable();
    }
}

static bool
checker_decl_proc(struct checker *state, struct decl_proc *decl)
{
    struct type *proc;
    struct type **args;
    struct proc_arg *ptr;
    size_t i;
    bool ok = true;

    if (state->pass == TYCHK_FIRST_PASS) {
        args = malloc(decl->args_count * sizeof(struct type *));

        for (i = 0, ptr = decl->args; ptr != NULL; ptr = ptr->next) {
            args[i++] = ptr->type;
        }

        if (checker_lookup(state, decl->sym) != NULL) {
            TYCHK_ERROR(state, decl->decl.span,
                        "Invalid redefinition of symbol '%s'\n", decl->sym->str);
            return false;
        }

        // TODO: Maybe do this in the parser directly?
        if (!decl->out) {
            decl->out = type_get_void(decl->decl.attr);
        }

        proc = type_get_proc(decl->decl.attr, decl->out, args, decl->args_count);
        checker_global_push(state, decl->sym, proc);

        return true;
    }

    if (state->pass == TYCHK_SECOND_PASS) {
        if (!checker_resolve(state, &decl->out)) {
            ok = false;
        }

        for (ptr = decl->args; ptr != NULL; ptr = ptr->next) {
            if (!checker_resolve(state, &ptr->type)) {
                ok = false;
            }
        }

        proc = checker_lookup(state, decl->sym);
        if (proc && proc->tag == TYPE_PROC) {
            if (!checker_resolve(state, &proc->proc.out)) {
                ok = false;
            }

            for (i = 0; i < proc->proc.args_count; i++) {
                if (!checker_resolve(state, &proc->proc.args[i])) {
                    ok = false;
                }
            }
        }
        return ok;
    }

    /*
     * Proc without a body should not be checked...
     */
    if (state->pass == TYCHK_THIRD_PASS && decl->body) {
        assert(state->locals_count == 0 && "leftover locals!");

        for (ptr = decl->args; ptr != NULL; ptr = ptr->next) {
            checker_local_push(state, ptr->sym, ptr->type);
        }

        state->loop_depth = 0;
        state->ret_type = decl->out;
        ok = checker_stmt(state, decl->body);
        checker_local_pop(state, 0);
    }

    return ok;
}

static bool
checker_decl_struct(struct checker *state, struct decl_struct *decl)
{
    struct type *type;
    size_t i;
    bool ok = true;

    if (state->pass == TYCHK_FIRST_PASS) {
        type = type_get_struct(decl->decl.attr, decl->sym,
                               decl->fields, decl->fields_count);

        checker_type_push(state, decl->sym, type);
    } else if (state->pass == TYCHK_SECOND_PASS) {
        for (i = 0; i < decl->fields_count; i++) {
            if (!checker_resolve(state, &decl->fields[i].type)) {
                ok = false;
            }
        }

        for (i = 0; i < state->types_count; i++) {
            if (state->types[i].sym == decl->sym) {
                type = state->types[i].type;
                break;
            }
        }

        if (type && type->tag == TYPE_STRUCT) {
            for (size_t i = 0; i < type->strukt.fields_count; i++) {
                if (!checker_resolve(state, &type->strukt.fields[i].type)) {
                    ok = false;
                }
            }
        }
    }

    return ok;
}

static bool
checker_decl_type(struct checker *state, struct decl_type *decl)
{
    size_t i;

    if (state->pass == TYCHK_FIRST_PASS) {
        checker_type_push(state, decl->sym, decl->type);
    } else if (state->pass == TYCHK_SECOND_PASS) {
        if (!checker_resolve(state, &decl->type))
            return false;

        for (i = 0; i < state->types_count; i++) {
            if (state->types[i].sym == decl->sym) {
                state->types[i].type = decl->type;
                break;
            }
        }
    }

    return true;
}

static bool
checker_decl(struct checker *state, struct decl *decl)
{
    switch (decl->tag) {
        case DECL_PROC:
            return checker_decl_proc(state, (struct decl_proc *)decl);

        case DECL_STRUCT:
            return checker_decl_struct(state, (struct decl_struct *)decl);

        case DECL_TYPE:
            return checker_decl_type(state, (struct decl_type *)decl);

        default:
            unreachable();
    }
}

bool
checker_module(struct checker *state, struct decl **decls, size_t decls_count)
{
    size_t i;
    bool ok = true;

    state->pass = TYCHK_FIRST_PASS;

    for (i = 0; i < decls_count; i++) {
        if (!checker_decl(state, decls[i])) {
            ok = false;
        }
    }

    if (!ok)
        goto done;

    state->pass = TYCHK_SECOND_PASS;

    for (i = 0; i < decls_count; i++) {
        if (!checker_decl(state, decls[i])) {
            ok = false;
        }
    }

    if (!ok)
        goto done;

    state->pass = TYCHK_THIRD_PASS;

    for (i = 0; i < decls_count; i++) {
        if (!checker_decl(state, decls[i])) {
            ok = false;
        }
    }

done:
    return ok;
}
