/* For general debugging purposes */

#include "../perf.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <api/debug.h>

#include "cache.h"
#include "color.h"
#include "event.h"
#include "debug.h"
#include "util.h"
#include "target.h"

#define NSECS_PER_SEC  1000000000ULL
#define NSECS_PER_USEC 1000ULL

int verbose;
bool dump_trace = false, quiet = false;
int debug_ordered_events;
static int redirect_to_stderr;
int debug_data_convert;

int veprintf(int level, int var, const char *fmt, va_list args)
{
	int ret = 0;

	if (var >= level) {
		if (use_browser >= 1 && !redirect_to_stderr)
			ui_helpline__vshow(fmt, args);
		else
			ret = vfprintf(stderr, fmt, args);
	}

	return ret;
}

int eprintf(int level, int var, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = veprintf(level, var, fmt, args);
	va_end(args);

	return ret;
}

static int veprintf_time(u64 t, const char *fmt, va_list args)
{
	int ret = 0;
	u64 secs, usecs, nsecs = t;

	secs   = nsecs / NSECS_PER_SEC;
	nsecs -= secs  * NSECS_PER_SEC;
	usecs  = nsecs / NSECS_PER_USEC;

	ret = fprintf(stderr, "[%13" PRIu64 ".%06" PRIu64 "] ",
		      secs, usecs);
	ret += vfprintf(stderr, fmt, args);
	return ret;
}

int eprintf_time(int level, int var, u64 t, const char *fmt, ...)
{
	int ret = 0;
	va_list args;

	if (var >= level) {
		va_start(args, fmt);
		ret = veprintf_time(t, fmt, args);
		va_end(args);
	}

	return ret;
}

/*
 * Overloading libtraceevent standard info print
 * function, display with -v in perf.
 */
void pr_stat(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	veprintf(1, verbose, fmt, args);
	va_end(args);
	eprintf(1, verbose, "\n");
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

static void trace_event_printer(enum binary_printer_ops op,
				unsigned int val, void *extra)
{
	const char *color = PERF_COLOR_BLUE;
	union perf_event *event = (union perf_event *)extra;
	unsigned char ch = (unsigned char)val;

	switch (op) {
	case BINARY_PRINT_DATA_BEGIN:
		printf(".");
		color_fprintf(stdout, color, "\n. ... raw event: size %d bytes\n",
				event->header.size);
		break;
	case BINARY_PRINT_LINE_BEGIN:
		printf(".");
		break;
	case BINARY_PRINT_ADDR:
		color_fprintf(stdout, color, "  %04x: ", val);
		break;
	case BINARY_PRINT_NUM_DATA:
		color_fprintf(stdout, color, " %02x", val);
		break;
	case BINARY_PRINT_NUM_PAD:
		color_fprintf(stdout, color, "   ");
		break;
	case BINARY_PRINT_SEP:
		color_fprintf(stdout, color, "  ");
		break;
	case BINARY_PRINT_CHAR_DATA:
		color_fprintf(stdout, color, "%c",
			      isprint(ch) ? ch : '.');
		break;
	case BINARY_PRINT_CHAR_PAD:
		color_fprintf(stdout, color, " ");
		break;
	case BINARY_PRINT_LINE_END:
		color_fprintf(stdout, color, "\n");
		break;
	case BINARY_PRINT_DATA_END:
		printf("\n");
		break;
	default:
		break;
	}
}

void trace_event(union perf_event *event)
{
	unsigned char *raw_event = (void *)event;

	if (!dump_trace)
		return;

	print_binary(raw_event, event->header.size, 16,
		     trace_event_printer, event);
}

static struct debug_variable {
	const char *name;
	int *ptr;
} debug_variables[] = {
	{ .name = "verbose",		.ptr = &verbose },
	{ .name = "ordered-events",	.ptr = &debug_ordered_events},
	{ .name = "stderr",		.ptr = &redirect_to_stderr},
	{ .name = "data-convert",	.ptr = &debug_data_convert },
	{ .name = NULL, }
};

int perf_debug_option(const char *str)
{
	struct debug_variable *var = &debug_variables[0];
	char *vstr, *s = strdup(str);
	int v = 1;

	vstr = strchr(s, '=');
	if (vstr)
		*vstr++ = 0;

	while (var->name) {
		if (!strcmp(s, var->name))
			break;
		var++;
	}

	if (!var->name) {
		pr_err("Unknown debug variable name '%s'\n", s);
		free(s);
		return -1;
	}

	if (vstr) {
		v = atoi(vstr);
		/*
		 * Allow only values in range (0, 10),
		 * otherwise set 0.
		 */
		v = (v < 0) || (v > 10) ? 0 : v;
	}

	*var->ptr = v;
	free(s);
	return 0;
}

#define DEBUG_WRAPPER(__n, __l)				\
static int pr_ ## __n ## _wrapper(const char *fmt, ...)	\
{							\
	va_list args;					\
	int ret;					\
							\
	va_start(args, fmt);				\
	ret = veprintf(__l, verbose, fmt, args);	\
	va_end(args);					\
	return ret;					\
}

DEBUG_WRAPPER(warning, 0);
DEBUG_WRAPPER(debug, 1);

void perf_debug_setup(void)
{
	libapi_set_print(pr_warning_wrapper, pr_warning_wrapper, pr_debug_wrapper);
}
