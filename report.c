#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "report.h"
#include "type.h"

#define REPORT_ERROR    0
#define REPORT_WARNING  1
#define REPORT_INFO     2

#define TERM_NORM   "\033[0m"
#define TERM_BOLD   "\033[1m"
#define TERM_RED    "\033[1;31m"
#define TERM_GREEN  "\033[1;32m"
#define TERM_MAG    "\033[1;35m"
#define TERM_CYAN   "\033[1;36m"

static void
report_format(FILE *out, const struct source_map *map,
              const char *fmt, va_list args)
{
    const struct source *src;
    struct symbol *sym;
    struct span *span;
    int len;

    while (*fmt) {
        if (*fmt != '%') {
            fputc(*fmt++, out);
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            fputc('%', out);
            fmt++;
            continue;
        }

        if (fmt[0] == 'p') {
            if (fmt[1] == 'T') {
                fprintf(out, "%s", TERM_GREEN);
                type_format(out, va_arg(args, struct type *));
                fprintf(out, "%s", TERM_NORM);
                fmt += 2;
                continue;
            }

            if (fmt[1] == 'S') {
                sym = va_arg(args, struct symbol *);
                fprintf(out, "%s", TERM_BOLD);

                if (sym) {
                    fprintf(out, "%.*s", (int)sym->len, sym->str);
                } else {
                    fprintf(out, "<null>");
                }

                fprintf(out, "%s", TERM_NORM);
                fmt += 2;
                continue;
            }

            if (fmt[1] == 'L') {
                span = va_arg(args, struct span *);
                if (span) {
                    src = source_map_get(map, span->src_id);
                    assert(map && src && "expected valid src");

                    fprintf(out, "%.*s",
                            (int)(span->end - span->start),
                            &src->str[span->start]);
                } else {
                    fprintf(out, "<null>");
                }
                fmt += 2;
                continue;
            }

            // XXX: Maybe make this invalid?
            fprintf(out, "%p", va_arg(args, void *));
            fmt++;
            continue;
        }

        if (!strncmp(fmt, ".*s", 3)) {
            len = va_arg(args, int);
            fprintf(out, "%.*s", len, va_arg(args, const char *));
            fmt += 3;
            continue;
        }

#define FMT_SPEC(spec, type) \
        if (!strncmp(fmt, spec, sizeof(spec) - 1)) { \
            fprintf(out, "%" spec, va_arg(args, type)); \
            fmt += sizeof(spec) - 1; \
            continue; \
        }

        FMT_SPEC("zu", size_t)
        FMT_SPEC("ld", long)
        FMT_SPEC("lu", unsigned long)
        FMT_SPEC("d",  int)
        FMT_SPEC("i",  int)
        FMT_SPEC("u",  unsigned)
        FMT_SPEC("x",  unsigned)
        FMT_SPEC("s",  const char *)
        FMT_SPEC("c",  int)
        FMT_SPEC("p",  void *)
#undef FMT_SPEC

        fputc('%', out);
        fputc(fmt[0], out);
        fmt++;
    }
}

static void
report_source_span(const struct source *src, const struct span *span,
                   const char *color)
{
    const char *start, *end, *tmp, *line_head, *line_tail;
    bool in_color, in_span;
    unsigned line, start_line, end_line;

    assert(src->str && "invalid src");
    assert(span->start <= span->end && "invalid span");

    start = &src->str[span->start];
    end = &src->str[span->end];
    start_line = span->line;
    end_line = span->line;

    /*
     * Compute the last line in the span
     */
    for (tmp = start; tmp < end; tmp++) {
        if (*tmp == '\n')
            end_line++;
    }

    /*
     * Find the start of the the first line in the span
     */
    line_head = start;
    while (line_head > src->str && line_head[-1] != '\n') {
        line_head--;
    }

    line = start_line;
    while (line_head <= src->str + src->len && line <= end_line) {
        /*
         * Calculate the end of the current line
         */
        line_tail = line_head;
        while (*line_tail != '\0' && *line_tail != '\n') {
            line_tail++;
        }

        /*
         * Ellipsise spans longer than 4 lines
         */
        if (end_line - start_line >= 4) {
            if (line > start_line + 1 && line < end_line - 1) {
                if (line == start_line + 2) {
                    fprintf(stderr, "      | ...\n");
                }
                goto done;
            }
        }

        fprintf(stderr, " %4u | ", line);

        in_color = false;
        for (tmp = line_head; tmp < line_tail; tmp++) {
            in_span = tmp >= start && tmp < end;

            if (in_span && !in_color) {
                fprintf(stderr, "%s", color);
                in_color = true;
            } else if (!in_span && in_color) {
                fprintf(stderr, TERM_NORM);
                in_color = false;
            }
            fputc(*tmp, stderr);
        }

        if (in_color) {
            fprintf(stderr, TERM_NORM);
        }
        fprintf(stderr, "\n");

        if (line == start_line) {
            fprintf(stderr, "      | %s", color);

            for (tmp = line_head; tmp <= line_tail && tmp < end; tmp++) {
                if (tmp == start) {
                    fputc('^', stderr);
                } else if (tmp > start) {
                    fputc('~', stderr);
                } else if (tmp < line_tail) {
                    fputc(*tmp == '\t' ? '\t' : ' ', stderr);
                }
            }
            fprintf(stderr, TERM_NORM "\n");
        }

done:
        if (*line_tail == '\0')
            break;

        line_head = line_tail + 1;
        line++;
    }

    /*
     * If we have a multi-line span, add a final line for spacing
     */
    if (end_line > start_line) {
        fprintf(stderr, "      |\n");
    }
}

static void
report_message(const struct source_map *map, const struct span *span,
               int level, const char *fmt, va_list args)
{
    const char *color, *label;
    const struct source *src;

    assert(((span && map) || (!span && !map)) && "either both or neither");

    if (span) {
        src = source_map_get(map, span->src_id);

        fprintf(stderr, TERM_BOLD "%s:", src->path);

        if (span->line > 0)
            fprintf(stderr, "%u:", span->line);

        fprintf(stderr, " ");
    }

    if (level == REPORT_ERROR) {
        label = "error";
        color = TERM_RED;
    } else if (level == REPORT_WARNING) {
        label = "warning";
        color = TERM_MAG;
    } else if (level == REPORT_INFO) {
        label = "info";
        color = TERM_CYAN;
    } else {
        unreachable();
    }

    fprintf(stderr, TERM_NORM "%s%s: " TERM_NORM, color, label);
    report_format(stderr, map, fmt, args);

    if (span && span->line > 0)
        report_source_span(src, span, color);
}

void
report_error(const struct source_map *map, const struct span *span,
             const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_message(map, span, REPORT_ERROR, fmt, args);
    va_end(args);
}

void
report_warning(const struct source_map *map, const struct span *span,
               const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_message(map, span, REPORT_WARNING, fmt, args);
    va_end(args);
}

void
report_info(const struct source_map *map, const struct span *span,
            const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_message(map, span, REPORT_INFO, fmt, args);
    va_end(args);
}
