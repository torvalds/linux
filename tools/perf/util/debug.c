/* For general debugging purposes */

#include "../perf.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "color.h"
#include "event.h"
#include "debug.h"

int verbose = 0;
int dump_trace = 0;

int eprintf(const char *fmt, ...)
{
	va_list args;
	int ret = 0;

	if (verbose) {
		va_start(args, fmt);
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

static int dump_printf_color(const char *fmt, const char *color, ...)
{
	va_list args;
	int ret = 0;

	if (dump_trace) {
		va_start(args, color);
		ret = color_vfprintf(stdout, color, fmt, args);
		va_end(args);
	}

	return ret;
}


void trace_event(event_t *event)
{
	unsigned char *raw_event = (void *)event;
	const char *color = PERF_COLOR_BLUE;
	int i, j;

	if (!dump_trace)
		return;

	dump_printf(".");
	dump_printf_color("\n. ... raw event: size %d bytes\n", color,
			  event->header.size);

	for (i = 0; i < event->header.size; i++) {
		if ((i & 15) == 0) {
			dump_printf(".");
			dump_printf_color("  %04x: ", color, i);
		}

		dump_printf_color(" %02x", color, raw_event[i]);

		if (((i & 15) == 15) || i == event->header.size-1) {
			dump_printf_color("  ", color);
			for (j = 0; j < 15-(i & 15); j++)
				dump_printf_color("   ", color);
			for (j = 0; j < (i & 15); j++) {
				if (isprint(raw_event[i-15+j]))
					dump_printf_color("%c", color,
							  raw_event[i-15+j]);
				else
					dump_printf_color(".", color);
			}
			dump_printf_color("\n", color);
		}
	}
	dump_printf(".\n");
}
