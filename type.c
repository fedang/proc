#include <assert.h>

#include "type.h"

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
