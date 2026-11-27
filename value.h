#ifndef _VALUE_H
#define _VALUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

enum value_tag {
    VALUE_INVALID,
    VALUE_PENDING,
    VALUE_INTEGER,
    VALUE_BOOL,
    VALUE_STRING,
    VALUE_AGGREGATE,
};

struct value_string {
    const char *data;
    size_t len;
};

struct value {
    enum value_tag tag;
    union {
        int64_t integer;
        struct value_string string;
        struct value_aggregate *agg;
    };
};

struct value_aggregate {
    size_t count;
    struct value *items;
};

static inline struct value
value_make_integer(int64_t val)
{
    return (struct value) {
        .tag = VALUE_INTEGER,
        .integer = val
    };
}

static inline struct value
value_make_bool(bool val)
{
    return (struct value) {
        .tag = VALUE_BOOL,
        .integer = val
    };
}

static inline struct value
value_make_string(const char *data, size_t len)
{
    return (struct value) {
        .tag = VALUE_STRING,
        .string = { .data = data, .len = len }
    };
}

#endif
