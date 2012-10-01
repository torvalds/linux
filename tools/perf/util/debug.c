/* For general debugging purposes */

#include "../perf.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "cache.h"
#include "color.h"
#include "event.h"
#include "debug.h"
#include "util.h"
#include "target.h"

int verbose;
bool dump_trace = false, quiet = false;

int eprintf(int level, const char *fmt, ...)
{
	va_list args;
	int ret = 0;

	if (verbose >= level) {
		va_start(args, fmt);
		if (use_browser == 1)
			ret = ui_helpline__show_help(fmt, args);
		else if (use_browser == 2)
			ret = perf_gtk__show_helpline(fmt, args);
		else
			ret = vfprintf(stderr, fmt, args);
		va_end(args);
	}

	return ret;
}

int dump_printf(const char *fmt, ...)
{
	va_list args;
	int ret = 0;

	if (dump_trace) {
		va_start(args, fmt);
		ret = vprintf(fmt, args);
		va_end(args);
	}

	return ret;
}

#if defined(NO_NEWT_SUPPORT) && defined(NO_GTK2_SUPPORT)
int ui__warning(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	return 0;
}
#endif

int ui__error_paranoid(void)
{
	return ui__error("Permission error - are you root?\n"
		    "Consider tweaking /proc/sys/kernel/perf_event_paranoid:\n"
		    " -1 - Not paranoid at all\n"
		    "  0 - Disallow raw tracepoint access for unpriv\n"
		    "  1 - Disallow cpu events for unpriv\n"
		    "  2 - Disallow kernel profiling for unpriv\n");
}

void trace_event(union perf_event *event)
{
	unsigned char *raw_event = (void *)event;
	const char *color = PERF_COLOR_BLUE;
	int i, j;

	if (!dump_trace)
		return;

	printf(".");
	color_fprintf(stdout, color, "\n. ... raw event: size %d bytes\n",
		      event->header.size);

	for (i = 0; i < event->header.size; i++) {
		if ((i & 15) == 0) {
			printf(".");
			color_fprintf(stdout, color, "  %04x: ", i);
		}

		color_fprintf(stdout, color, " %02x", raw_event[i]);

		if (((i & 15) == 15) || i == event->header.size-1) {
			color_fprintf(stdout, color, "  ");
			for (j = 0; j < 15-(i & 15); j++)
				color_fprintf(stdout, color, "   ");
			for (j = i & ~15; j <= i; j++) {
				color_fprintf(stdout, color, "%c",
					      isprint(raw_event[j]) ?
					      raw_event[j] : '.');
			}
			color_fprintf(stdout, color, "\n");
		}
	}
	printf(".\n");
}
