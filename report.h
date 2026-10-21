#ifndef _REPORT_H
#define _REPORT_H

#include <assert.h>

#include "source.h"
#include "span.h"

#define unreachable() \
    assert(!"unreachable")

#if defined(__GNUC__) || defined(__clang__)
#define CHECK_FMT(...) \
    __attribute__((format(printf, __VA_ARGS__)))
#else
#define CHECK_FMT(...)
#endif

CHECK_FMT(3, 4)
void report_error(const struct source_map *map, const struct span *span,
                  const char *fmt, ...);

CHECK_FMT(3, 4)
void report_warning(const struct source_map *map, const struct span *span,
                    const char *fmt, ...);

CHECK_FMT(3, 4)
void report_info(const struct source_map *map, const struct span *span,
                 const char *fmt, ...);

// TODO: report_blunder for ICEs

#endif
