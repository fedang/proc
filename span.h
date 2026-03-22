#ifndef _SPAN_H
#define _SPAN_H

struct span {
    const char *file;
    const char *start;
    const char *end;
    unsigned start_line;
    unsigned end_line;
};

static inline struct span
span_merge(struct span start, struct span end)
{
    struct span merge;

    merge.start = start.start;
    merge.end = end.end;
    merge.start_line = start.start_line;
    merge.end_line = end.end_line;
}

#endif
