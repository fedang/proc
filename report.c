#include <stdio.h>
#include <stdarg.h>

#include "report.h"

#define REPORT_ERROR    0
#define REPORT_WARNING  1
#define REPORT_INFO     2

static void
report_message(struct source *src, struct span *span, int level,
               const char *fmt, va_list args)
{
    if (src)
        fprintf(stderr, "%s:", src->path);

    if (span)
        fprintf(stderr, "%u:", span->start_line);

    if (level == REPORT_ERROR) {
        fprintf(stderr, " error: ");
    } else if (level == REPORT_WARNING) {
        fprintf(stderr, " warning: ");
    } else if (level == REPORT_INFO) {
        fprintf(stderr, " info: ");
    }

    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

void
report_error(struct source *src, struct span *span, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_message(src, span, REPORT_ERROR, fmt, args);
    va_end(args);
}

void
report_warning(struct source *src, struct span *span, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_message(src, span, REPORT_WARNING, fmt, args);
    va_end(args);
}

void
report_info(struct source *src, struct span *span, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_message(src, span, REPORT_INFO, fmt, args);
    va_end(args);
}
