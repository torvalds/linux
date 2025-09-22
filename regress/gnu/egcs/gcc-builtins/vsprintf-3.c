#include <stdio.h>
#include <stdarg.h>

void
test_vsprintf(int unused, ...)
{
	char buf[10];
	volatile int rv;
	va_list ap;

	va_start(ap, unused);

	/* This expression can be folded. */
	rv = vsprintf(buf, "bar", ap);

	va_end(ap);
}

int
main(int argc, char **argv)
{
	test_vsprintf(0);

	return (1);
}
