/* For general debugging purposes */

#include "../perf.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

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
