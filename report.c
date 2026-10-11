#include <stdio.h>
#include <stdarg.h>

#include "report.h"

#define REPORT_ERROR    0
#define REPORT_WARNING  1
#define REPORT_INFO     2

#define TERM_NORM   "\033[0m"
#define TERM_BOLD   "\033[1m"
#define TERM_RED    "\033[1;31m"
#define TERM_MAGENTA "\033[1;35m"
#define TERM_CYAN "\033[1;32m"

static void
report_source_span(const struct source *src, const struct span *span,
                   const char *color)
{
    const char *line_ptr, *line_end, *tmp;
    unsigned line;
    bool in_color, in_span;

    line_ptr = span->start;
    line = span->start_line;

    while (line_ptr > src->str && *(line_ptr - 1) != '\n') {
        line_ptr--;
    }

    while (line_ptr <= src->str + src->len && line <= span->end_line) {
        line_end = line_ptr;

        while (*line_end != '\0' && *line_end != '\n') {
            line_end++;
        }

        /*
         * Ellipsise spans longer than 4 lines
         */
        if (span->end_line - span->start_line >= 4) {
            if (line > span->start_line + 1 && line < span->end_line - 1) {
                if (line == span->start_line + 2) {
                    fprintf(stderr, "      | ...\n");
                }
                goto done;
            }
        }

        fprintf(stderr, " %4u | ", line);

        in_color = false;
        for (tmp = line_ptr; tmp < line_end; tmp++) {
            in_span = tmp >= span->start && tmp < span->end;

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

        if (line == span->start_line) {
            fprintf(stderr, "      | %s", color);

            for (tmp = line_ptr; tmp <= line_end && tmp < span->end; tmp++) {
                if (tmp == span->start) {
                    fputc('^', stderr);
                } else if (tmp > span->start) {
                    fputc('~', stderr);
                } else if (tmp < line_end) {
                    fputc(*tmp == '\t' ? '\t' : ' ', stderr);
                }
            }
            fprintf(stderr, TERM_NORM "\n");
        }

done:
        if (*line_end == '\0')
            break;

        line_ptr = line_end + 1;
        line++;
    }

    /*
     * If we have a multi-line span, add a final line for spacing
     */
    if (span->end_line > span->start_line) {
        fprintf(stderr, "      |\n");
    }
}

static void
report_message(const struct source *src, const struct span *span, int level,
               const char *fmt, va_list args)
{
    const char *color, *label;

    fprintf(stderr, TERM_BOLD "%s:", src->path);

    if (span)
        fprintf(stderr, "%u:", span->start_line);

    if (level == REPORT_ERROR) {
        label = "error";
        color = TERM_RED;
    } else if (level == REPORT_WARNING) {
        label = "warning";
        color = TERM_MAGENTA;
    } else if (level == REPORT_INFO) {
        label = "info";
        color = TERM_CYAN;
    }

    fprintf(stderr, TERM_NORM " %s%s: " TERM_NORM, color, label);
    vfprintf(stderr, fmt, args);

    if (span)
        report_source_span(src, span, color);
}

void
report_error(const struct source *src, const struct span *span,
             const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_message(src, span, REPORT_ERROR, fmt, args);
    va_end(args);
}

void
report_warning(const struct source *src, const struct span *span,
               const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_message(src, span, REPORT_WARNING, fmt, args);
    va_end(args);
}

void
report_info(const struct source *src, const struct span *span,
            const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_message(src, span, REPORT_INFO, fmt, args);
    va_end(args);
}
