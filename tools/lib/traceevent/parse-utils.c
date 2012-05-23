#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#define __weak __attribute__((weak))

void __vdie(const char *fmt, va_list ap)
{
	int ret = errno;

	if (errno)
		perror("trace-cmd");
	else
		ret = -1;

	fprintf(stderr, "  ");
	vfprintf(stderr, fmt, ap);

	fprintf(stderr, "\n");
	exit(ret);
}

void __die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__vdie(fmt, ap);
	va_end(ap);
}

void __weak die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__vdie(fmt, ap);
	va_end(ap);
}

void __vwarning(const char *fmt, va_list ap)
{
	if (errno)
		perror("trace-cmd");
	errno = 0;

	fprintf(stderr, "  ");
	vfprintf(stderr, fmt, ap);

	fprintf(stderr, "\n");
}

void __warning(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__vwarning(fmt, ap);
	va_end(ap);
}

void __weak warning(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__vwarning(fmt, ap);
	va_end(ap);
}

void __vpr_stat(const char *fmt, va_list ap)
{
	vprintf(fmt, ap);
	printf("\n");
}

void __pr_stat(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__vpr_stat(fmt, ap);
	va_end(ap);
}

void __weak vpr_stat(const char *fmt, va_list ap)
{
	__vpr_stat(fmt, ap);
}

void __weak pr_stat(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__vpr_stat(fmt, ap);
	va_end(ap);
}

void __weak *malloc_or_die(unsigned int size)
{
	void *data;

	data = malloc(size);
	if (!data)
		die("malloc");
	return data;
}
