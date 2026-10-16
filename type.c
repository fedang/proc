#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "type.h"
#include "scope.h"
#include "report.h"

#define MAX_TYPES 4096

struct type type_pool[MAX_TYPES];

size_t type_count;

static struct type *
type_get_fresh(struct attr *attr, enum type_tag tag)
{
    struct type *type;

    assert(type_count < MAX_TYPES && "type pool full");
    type = &type_pool[type_count++];
    type->attr = attr;
    type->tag = tag;
    return type;
}

static struct type *
type_get_xint(struct attr *attr, size_t bit_size, enum type_tag tag)
{
    struct type *type;
    size_t i;

    if (bit_size == 0)
        bit_size = INT_SIZE * 8;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag == tag && type->attr == attr
                && type->bit_size == bit_size) {
            return type;
        }
    }

    type = type_get_fresh(attr, tag);
    type->bit_size = bit_size;
    return type;
}

static struct type *
type_get_simple(struct attr *attr, enum type_tag tag)
{
    struct type *type;
    size_t i;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag == tag && type->attr == attr) {
            return type;
        }
    }

    type = type_get_fresh(attr, tag);
    return type;
}

struct type *
type_make_raw(struct expr *raw)
{
    struct type *type;

    type = malloc(sizeof(struct type));
    type->attr = NULL;
    type->tag = TYPE_RAW;
    type->raw = raw;
    return type;
}

struct type *
type_get_invalid(void)
{
    static struct type invalid = { TYPE_INVALID };
    return &invalid;
}

struct type *
type_get_bool(struct attr *attr)
{
    return type_get_simple(attr, TYPE_BOOL);
}

struct type *
type_get_void(struct attr *attr)
{
    return type_get_simple(attr, TYPE_VOID);
}

struct type *
type_get_int(struct attr *attr, size_t bit_size)
{
    return type_get_xint(attr, bit_size, TYPE_INT);
}

struct type *
type_get_uint(struct attr *attr, size_t bit_size)
{
    return type_get_xint(attr, bit_size, TYPE_UINT);
}

struct type *
type_get_ptr(struct attr *attr, struct type *pointer)
{
    struct type *type;
    size_t i;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag == TYPE_PTR && type->attr == attr
                && type->pointer == pointer) {
            return type;
        }
    }

    type = type_get_fresh(attr, TYPE_PTR);
    type->pointer = pointer;
    return type;
}

struct type *
type_get_array(struct attr *attr, struct type *item, size_t count)
{
    struct type *type;
    size_t i;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag == TYPE_ARRAY && type->attr == attr
                && type->array.item == item && type->array.count == count) {
            return type;
        }
    }

    type = type_get_fresh(attr, TYPE_ARRAY);
    type->array.item = item;
    type->array.count = count;
    return type;
}

struct type *
type_get_slice(struct attr *attr, struct type *item)
{
    struct type *type;
    size_t i;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];
        if (type->tag == TYPE_SLICE && type->attr == attr
                && type->slice.item == item) {
            return type;
        }
    }

    type = type_get_fresh(attr, TYPE_SLICE);
    type->slice.item = item;
    return type;
}

struct type *
type_get_struct(struct attr *attr, struct symbol *name,
                struct struct_field *fields, size_t fields_count)
{
    struct type *type;
    size_t i, j;
    bool found;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag != TYPE_STRUCT || type->attr != attr)
            continue;

        if (type->strukt.name == name
                && type->strukt.fields_count == fields_count) {
            found = true;

            for (j = 0; j < fields_count; j++) {
                if (fields[j].name != type->strukt.fields[j].name
                        || fields[j].type != type->strukt.fields[j].type) {
                    found = false;
                    break;
                }
            }

            if (found)
                return type;
        }
    }

    type = type_get_fresh(attr, TYPE_STRUCT);
    type->strukt.name = name;
    type->strukt.fields = fields;
    type->strukt.fields_count = fields_count;
    return type;
}

struct type *
type_get_proc(struct attr *attr, struct type *out,
              struct type **args, size_t args_count)
{
    struct type *type;
    size_t i, j;
    bool found;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag != TYPE_PROC || type->attr != attr)
            continue;

        if (type->proc.out == out && type->proc.args_count == args_count) {
            found = true;

            for (j = 0; j < args_count; j++) {
                if (args[j] != type->proc.args[j]) {
                    found = false;
                    break;
                }
            }

            if (found)
                return type;
        }
    }

    type = type_get_fresh(attr, TYPE_PROC);
    type->proc.out = out;
    type->proc.args_count = args_count;

    if (args_count > 0) {
        type->proc.args = malloc(args_count * sizeof(struct type *));
        memcpy(type->proc.args, args, args_count * sizeof(struct type *));
    } else {
        type->proc.args = NULL;
    }
    return type;
}

struct type *
type_get_opaque(struct attr *attr, struct symbol *name)
{
    struct type *type;
    size_t i;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];
        if (type->tag == TYPE_OPAQUE && type->attr == attr
                && type->opaque.name == name) {
            return type;
        }
    }

    type = type_get_fresh(attr, TYPE_OPAQUE);
    type->opaque.name = name;
    return type;
}

struct type *
type_get_module(struct attr *attr, struct scope *scope)
{
    struct type *type;
    size_t i;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];
        if (type->tag == TYPE_MODULE && type->attr == attr
                && type->module.scope == scope) {
            return type;
        }
    }

    type = type_get_fresh(attr, TYPE_MODULE);
    type->module.scope = scope;
    return type;
}

#define ROUND_UP(x, y) \
    ((x) + (y) - 1) & ~((y) - 1)

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
            return type_integer_bits(type) / 8;

        case TYPE_PTR:
            return PTR_SIZE;

        case TYPE_ARRAY:
            return type->array.count * type_sizeof(type->array.item);

        case TYPE_SLICE:
            return PTR_SIZE + 8;

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
            return type_integer_bits(type) / 8;

        case TYPE_PTR:
        case TYPE_SLICE:
            return PTR_SIZE;

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
            if (type->bit_size)
                fprintf(out, "i%zu", type->bit_size);
            else
                fprintf(out, "int");
            break;

        case TYPE_UINT:
            if (type->bit_size)
                fprintf(out, "u%zu", type->bit_size);
            else
                fprintf(out, "uint");
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
            fprintf(out, "%.*s", (int)type->opaque.name->len,
                    type->opaque.name->str);
            break;

        case TYPE_STRUCT:
            if (type->strukt.name) {
                fprintf(out, "%.*s", (int)type->strukt.name->len,
                        type->strukt.name->str);
            } else {
                fprintf(out, "struct { ");
                for (i = 0; i < type->strukt.fields_count; i++) {
                    if (i > 0) fprintf(out, ", ");
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

        default:
            unreachable();
    }
}
