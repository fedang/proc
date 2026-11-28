#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "type.h"
#include "scope.h"
#include "report.h"
#include "options.h"
#include "arena.h"

#define TYPE_PRIM(sym, _tag, ...) \
    static struct type type_ ## sym = { .tag = _tag, __VA_ARGS__ };

TYPE_PRIM(invalid, TYPE_INVALID)
TYPE_PRIM(any_int, TYPE_ANY_INT)
TYPE_PRIM(void, TYPE_VOID)
TYPE_PRIM(bool, TYPE_BOOL)
TYPE_PRIM(int, TYPE_INT, .bit_width = 0)
TYPE_PRIM(i8, TYPE_INT, .bit_width = 8)
TYPE_PRIM(i16, TYPE_INT, .bit_width = 16)
TYPE_PRIM(i32, TYPE_INT, .bit_width = 32)
TYPE_PRIM(i64, TYPE_INT, .bit_width = 64)
TYPE_PRIM(uint, TYPE_UINT, .bit_width = 0)
TYPE_PRIM(u8, TYPE_UINT, .bit_width = 8)
TYPE_PRIM(u16, TYPE_UINT, .bit_width = 16)
TYPE_PRIM(u32, TYPE_UINT, .bit_width = 32)
TYPE_PRIM(u64, TYPE_UINT, .bit_width = 64)

static struct type **type_table;
static size_t type_count;
static size_t type_size;

/*
 * FNV-1A hash algorithm
 */
static inline uint64_t
hash_mix(uint64_t hash, uint64_t val)
{
    hash ^= val;
    hash *= 1099511628211ULL;
    return hash;
}

static uint32_t
type_hash(const struct type *type)
{
    uint64_t hash = 14695981039346656037ULL;
    size_t i;

    hash = hash_mix(hash, (uint64_t)type->tag);
    hash = hash_mix(hash, (uintptr_t)type->attr);

    switch (type->tag) {
        case TYPE_VOID:
        case TYPE_BOOL:
            break;

        case TYPE_INT:
        case TYPE_UINT:
            hash = hash_mix(hash, (uint64_t)type->bit_width);
            break;

        case TYPE_PTR:
            hash = hash_mix(hash, (uintptr_t)type->pointer);
            break;

        case TYPE_ARRAY:
            hash = hash_mix(hash, (uintptr_t)type->array.item);
            hash = hash_mix(hash, (uint64_t)type->array.count);
            break;

        case TYPE_SLICE:
            hash = hash_mix(hash, (uintptr_t)type->slice.item);
            break;

        case TYPE_STRUCT:
            hash = hash_mix(hash, (uintptr_t)type->strukt.decl);
            if (!type->strukt.decl) {
                hash = hash_mix(hash, (uint64_t)type->strukt.fields_count);
                for (i = 0; i < type->strukt.fields_count; i++) {
                    hash = hash_mix(hash, (uintptr_t)type->strukt.fields[i].name);
                    hash = hash_mix(hash, (uintptr_t)type->strukt.fields[i].type);
                }
            }
            break;

        case TYPE_PROC:
            hash = hash_mix(hash, (uintptr_t)type->proc.out);
            hash = hash_mix(hash, (uint64_t)type->proc.args_count);
            for (i = 0; i < type->proc.args_count; i++)
                hash = hash_mix(hash, (uintptr_t)type->proc.args[i]);
            break;

        case TYPE_OPAQUE:
            hash = hash_mix(hash, (uintptr_t)type->opaque.bind);
            break;

        case TYPE_MODULE:
            hash = hash_mix(hash, (uintptr_t)type->module.scope);
            break;

        case TYPE_SIG:
            hash = hash_mix(hash, (uintptr_t)type->sig.scope);
            break;

        default:
            break;
    }

    return (uint32_t)(hash ^ (hash >> 32));
}

static bool
type_equal(const struct type *t1, const struct type *t2)
{
    size_t i;

    if (t1->tag != t2->tag || t1->attr != t2->attr)
        return false;

    switch (t1->tag) {
        case TYPE_VOID:
        case TYPE_BOOL:
            return true;

        case TYPE_INT:
        case TYPE_UINT:
            return t1->bit_width == t2->bit_width;

        case TYPE_PTR:
            return t1->pointer == t2->pointer;

        case TYPE_ARRAY:
            return t1->array.item == t2->array.item
                && t1->array.count == t2->array.count;

        case TYPE_SLICE:
            return t1->slice.item == t2->slice.item;

        case TYPE_STRUCT:
            /*
             * Nominal structs are identified by their declaration
             */
            if (t1->strukt.decl != t2->strukt.decl)
                return false;

            if (t1->strukt.decl)
                return true;

            /*
             * Do a purely structural comparison of the anonymous ones
             */
            if (t1->strukt.fields_count != t2->strukt.fields_count)
                return false;

            for (i = 0; i < t1->strukt.fields_count; i++) {
                if (t1->strukt.fields[i].name != t2->strukt.fields[i].name
                    || t1->strukt.fields[i].type != t2->strukt.fields[i].type) {
                    return false;
                }
            }
            return true;

        case TYPE_PROC:
            if (t1->proc.out != t2->proc.out
                || t1->proc.args_count != t2->proc.args_count) {
                return false;
            }
            for (i = 0; i < t1->proc.args_count; i++) {
                if (t1->proc.args[i] != t2->proc.args[i])
                    return false;
            }
            return true;

        case TYPE_OPAQUE:
            /*
             * Opaque types are identified by their binding
             */
            return t1->opaque.bind == t2->opaque.bind;

        case TYPE_MODULE:
            return t1->module.scope == t2->module.scope;

        case TYPE_SIG:
            return t1->sig.scope == t2->sig.scope;

        default:
            unreachable();
    }
}

static void
type_grow(size_t size)
{
    struct type **table;
    size_t i, idx;

    table = calloc(size, sizeof(struct type *));
    assert(table && "out of memory");

    for (i = 0; i < type_size; i++) {
        if (!type_table[i])
            continue;

        idx = type_table[i]->hash & (size - 1);
        while (table[idx])
            idx = (idx + 1) & (size - 1);
        table[idx] = type_table[i];
    }

    free(type_table);
    type_table = table;
    type_size = size;
}

static struct type *
type_intern(const struct type *key)
{
    struct type *type;
    uint32_t hash;
    size_t idx;

    if (type_count * 2 >= type_size)
        type_grow(type_size * 2);

    hash = type_hash(key);

    /*
     * Open-addressing hashtable loop
     */
    idx = hash & (type_size - 1);
    while (type_table[idx]) {
        type = type_table[idx];
        if (type->hash == hash && type_equal(type, key))
            return type;
        idx = (idx + 1) & (type_size - 1);
    }

    type = arena_alloc(&arena_type, sizeof(struct type));
    *type = *key;
    type->hash = hash;

    if (type->tag == TYPE_PROC) {
        if (type->proc.args_count > 0) {
            type->proc.args = arena_alloc(&arena_type,
                                          type->proc.args_count * sizeof(struct type *));
            memcpy(type->proc.args, key->proc.args,
                   type->proc.args_count * sizeof(struct type *));
        } else {
            type->proc.args = NULL;
        }
    } else if (type->tag == TYPE_STRUCT && !type->strukt.decl) {
        if (type->strukt.fields_count > 0) {
            type->strukt.fields = arena_alloc(&arena_type,
                                              type->strukt.fields_count * sizeof(struct struct_field));
            memcpy(type->strukt.fields, key->strukt.fields,
                   type->strukt.fields_count * sizeof(struct struct_field));
        } else {
            type->strukt.fields = NULL;
        }
    }

    type_table[idx] = type;
    type_count++;
    return type;
}

struct type *
type_make_raw(struct expr *raw)
{
    struct type *type;

    type = arena_alloc_zero(&arena_type, sizeof(struct type));
    type->tag = TYPE_RAW;
    type->raw = raw;
    return type;
}

struct type *
type_get_invalid(void)
{
    return &type_invalid;
}

struct type *
type_get_any_int(void)
{
    return &type_any_int;
}

struct type *
type_get_bool(struct attr *attr)
{
    struct type key = {
        .tag = TYPE_BOOL,
        .attr = attr
    };

    return attr
        ? type_intern(&key)
        : &type_bool;
}

struct type *
type_get_void(struct attr *attr)
{
    struct type key = {
        .tag = TYPE_VOID,
        .attr = attr
    };

    return attr
        ? type_intern(&key)
        : &type_void;
}

struct type *
type_get_int(struct attr *attr, size_t bit_width)
{
    struct type key = {
        .tag = TYPE_INT,
        .attr = attr,
        .bit_width = bit_width,
    };

    if (!attr) {
        switch (bit_width) {
            case 0:
                return &type_int;
            case 8:
                return &type_i8;
            case 16:
                return &type_i16;
            case 32:
                return &type_i32;
            case 64:
                return &type_i64;
            default:
                break;
        }
    }

    return type_intern(&key);
}

struct type *
type_get_uint(struct attr *attr, size_t bit_width)
{
    struct type key = {
        .tag = TYPE_UINT,
        .attr = attr,
        .bit_width = bit_width,
    };

    if (!attr) {
        switch (bit_width) {
            case 0:
                return &type_uint;
            case 8:
                return &type_u8;
            case 16:
                return &type_u16;
            case 32:
                return &type_u32;
            case 64:
                return &type_u64;
            default:
                break;
        }
    }

    return type_intern(&key);
}

struct type *
type_get_ptr(struct attr *attr, struct type *pointer)
{
    struct type key = {
        .tag = TYPE_PTR,
        .attr = attr,
        .pointer = pointer,
    };

    return type_intern(&key);
}

struct type *
type_get_array(struct attr *attr, struct type *item, size_t count)
{
    struct type key = {
        .tag = TYPE_ARRAY,
        .attr = attr,
        .array = {
            .item = item,
            .count = count
        },
    };

    return type_intern(&key);
}

struct type *
type_get_slice(struct attr *attr, struct type *item)
{
    struct type key = {
        .tag = TYPE_SLICE,
        .attr = attr,
        .slice = {
            .item = item
        },
    };

    return type_intern(&key);
}

struct type *
type_get_struct(struct attr *attr, struct decl *decl, struct symbol *name,
                struct struct_field *fields, size_t fields_count)
{
    struct type key = {
        .tag = TYPE_STRUCT,
        .attr = attr,
        .strukt = {
            .decl = (struct decl_struct *)decl,
            .name = name,
            .fields = fields,
            .fields_count = fields_count,
        },
    };

    assert(!decl || decl->tag == DECL_STRUCT);
    return type_intern(&key);
}

struct type *
type_get_proc(struct attr *attr, struct type *out,
              struct type **args, size_t args_count)
{
    struct type key = {
        .tag = TYPE_PROC,
        .attr = attr,
        .proc = {
            .out = out,
            .args = args,
            .args_count = args_count,
        },
    };

    return type_intern(&key);
}

struct type *
type_get_opaque(struct attr *attr, struct binding *bind)
{
    struct type key = {
        .tag = TYPE_OPAQUE,
        .attr = attr,
        .opaque = {
            .bind = bind,
        },
    };

    assert(bind && "opaque type requires a binding");
    return type_intern(&key);
}

struct type *
type_get_module(struct attr *attr, struct scope *scope)
{
    struct type key = {
        .tag = TYPE_MODULE,
        .attr = attr,
        .module = {
            .scope = scope
        },
    };

    assert(scope && (!scope->decl || scope->decl->tag == DECL_MODULE));
    return type_intern(&key);
}

struct type *
type_get_sig(struct attr *attr, struct scope *scope)
{
    struct type key = {
        .tag = TYPE_SIG,
        .attr = attr,
        .sig = {
            .scope = scope
        },
    };

    assert(scope && (!scope->decl || scope->decl->tag == DECL_SIG));
    return type_intern(&key);
}

size_t
type_ptr_size(void)
{
    assert(opts.target && "no target!");
    return opts.target->word_size;
}

size_t
type_int_size(void)
{
    assert(opts.target && "no target!");
    return opts.target->word_size;
}

size_t
type_sizeof(const struct type *type)
{
    size_t i, size, align;

    switch (type->tag) {
        case TYPE_INVALID:
            /* fall through */

        case TYPE_VOID:
            return 0;

        case TYPE_BOOL:
            return 1;

        case TYPE_INT:
        case TYPE_UINT:
            return type_bit_width(type) / 8;

        case TYPE_PTR:
            return type_ptr_size();

        case TYPE_ARRAY:
            size = type_sizeof(type->array.item);
            assert(type->array.count == 0 || size <= (size_t)INT64_MAX / type->array.count);
            return type->array.count * size;

        case TYPE_SLICE:
            align = type_int_size();
            return ROUND_UP(type_ptr_size(), align) + type_int_size();

        case TYPE_STRUCT:
            for (i = 0, size = 0; i < type->strukt.fields_count; i++) {
                align = type_alignof(type->strukt.fields[i].type);
                size = ROUND_UP(size, align);
                size += type_sizeof(type->strukt.fields[i].type);
            }

            align = type_alignof(type);
            return ROUND_UP(size, align);

        default:
            unreachable();
    }
}

size_t
type_alignof(const struct type *type)
{
    size_t i, align, max;

    switch (type->tag) {
        case TYPE_INVALID:
            /* fall through */

        case TYPE_VOID:
        case TYPE_BOOL:
            return 1;

        case TYPE_INT:
        case TYPE_UINT:
            return type_bit_width(type) / 8;

        case TYPE_PTR:
            return type_ptr_size();

        case TYPE_SLICE:
            return type_ptr_size() > type_int_size()
                ? type_ptr_size()
                : type_int_size();

        case TYPE_ARRAY:
            return type_alignof(type->array.item);

        case TYPE_STRUCT:
            for (i = 0, max = 1; i < type->strukt.fields_count; i++) {
                align = type_alignof(type->strukt.fields[i].type);
                if (align > max)
                    max = align;
            }
            return max;

        default:
            unreachable();
    }
}

static void
scope_format_path(FILE *out, const struct scope *scope)
{
    if (!scope || !scope->parent || !scope->name)
        return;

    if (scope->parent->parent) {
        scope_format_path(out, scope->parent);
        fprintf(out, "%.*s.", (int)scope->name->len, scope->name->str);
    }
}

static void
type_format_named(FILE *out, struct decl *decl, struct symbol *name)
{
    if (decl && decl->bind && decl->bind->scope) {
        scope_format_path(out, decl->bind->scope);
    }
    fprintf(out, "%.*s", (int)name->len, name->str);
}

void
type_format(FILE *out, const struct type *type)
{
    size_t i;

    assert(type);
    switch (type->tag) {
        case TYPE_INVALID:
            fprintf(out, "<invalid>");
            break;

        case TYPE_RAW:
            fprintf(out, "<unresolved>");
            break;

        case TYPE_BOOL:
            fprintf(out, "bool");
            break;

        case TYPE_VOID:
            fprintf(out, "void");
            break;

        case TYPE_INT:
            if (type->bit_width)
                fprintf(out, "i%zu", type->bit_width);
            else
                fprintf(out, "int");
            break;

        case TYPE_UINT:
            if (type->bit_width)
                fprintf(out, "u%zu", type->bit_width);
            else
                fprintf(out, "uint");
            break;

        case TYPE_ANY_INT:
            fprintf(out, "<integer>");
            break;

        case TYPE_PTR:
            fprintf(out, "*");
            type_format(out, type->pointer);
            break;

        case TYPE_ARRAY:
            fprintf(out, "[");
            type_format(out, type->array.item);
            fprintf(out, ", %zu]", type->array.count);
            break;

        case TYPE_SLICE:
            fprintf(out, "[");
            type_format(out, type->slice.item);
            fprintf(out, "]");
            break;

        case TYPE_OPAQUE:
            if (type->opaque.bind->scope) {
                scope_format_path(out, type->opaque.bind->scope);
            }
            fprintf(out, "%.*s", (int)type->opaque.bind->sym->len,
                    type->opaque.bind->sym->str);
            break;

        case TYPE_STRUCT:
            if (type->strukt.name) {
                type_format_named(out, &type->strukt.decl->decl, type->strukt.name);
            } else {
                fprintf(out, "struct { ");
                for (i = 0; i < type->strukt.fields_count; i++) {
                    if (i > 0)
                        fprintf(out, "; ");

                    fprintf(out, "%.*s: ", (int)type->strukt.fields[i].name->len,
                                           type->strukt.fields[i].name->str);
                    type_format(out, type->strukt.fields[i].type);
                }
                fprintf(out, " }");
            }
            break;

        case TYPE_PROC:
            fprintf(out, "proc(");
            for (i = 0; i < type->proc.args_count; i++) {
                if (i > 0)
                    fprintf(out, ", ");
                type_format(out, type->proc.args[i]);
            }
            fprintf(out, ") ");

            if (type->proc.out->tag != TYPE_VOID) {
                fprintf(out, "-> ");
                type_format(out, type->proc.out);
            }
            break;

        case TYPE_MODULE:
            fprintf(out, "<module>");
            break;

        case TYPE_SIG:
            fprintf(out, "<signature>");
            break;

        default:
            unreachable();
    }
}

void
type_init(void)
{
    /*
     * Allocate a sensible size for the type table
     */
    type_grow(8192);
}
