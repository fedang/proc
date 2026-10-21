#ifndef _SPAN_H
#define _SPAN_H

#include <assert.h>

struct span {
    unsigned src_id;
    unsigned start;
    unsigned end;
    unsigned line;
};

static inline struct span
span_merge(struct span start, struct span end)
{
    struct span merge;

    assert(start.src_id == end.src_id && "unmergeable");
    assert(start.start <= end.end && "wrong ordering");
    assert(start.line <= end.line && "wrong lines");

    merge.src_id = start.src_id;
    merge.start = start.start;
    merge.end = end.end;
    merge.line = start.line;
    return merge;
}

#endif
