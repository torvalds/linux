#include "util.h"
#include "../debug.h"


/*
 * Default error logging functions
 */
static int perf_stdio__error(const char *format, va_list args)
{
	fprintf(stderr, "Error:\n");
	vfprintf(stderr, format, args);
	return 0;
}

static int perf_stdio__warning(const char *format, va_list args)
{
	fprintf(stderr, "Warning:\n");
	vfprintf(stderr, format, args);
	return 0;
}

static struct perf_error_ops default_eops =
{
	.error		= perf_stdio__error,
	.warning	= perf_stdio__warning,
};

static struct perf_error_ops *perf_eops = &default_eops;


int ui__error(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = perf_eops->error(format, args);
	va_end(args);

	return ret;
}

int ui__warning(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = perf_eops->warning(format, args);
	va_end(args);

	return ret;
}

/**
 * perf_error__register - Register error logging functions
 * @eops: The pointer to error logging function struct
 *
 * Register UI-specific error logging functions. Before calling this,
 * other logging functions should be unregistered, if any.
 */
int perf_error__register(struct perf_error_ops *eops)
{
	if (perf_eops != &default_eops)
		return -1;

	perf_eops = eops;
	return 0;
}

/**
 * perf_error__unregister - Unregister error logging functions
 * @eops: The pointer to error logging function struct
 *
 * Unregister already registered error logging functions.
 */
int perf_error__unregister(struct perf_error_ops *eops)
{
	if (perf_eops != eops)
		return -1;

	perf_eops = &default_eops;
	return 0;
}
