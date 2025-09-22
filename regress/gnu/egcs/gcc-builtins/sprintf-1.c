#include <stdio.h>

int
main(int argc, char **argv)
{
	char buf[512];
	volatile int rv;

	/* This expression cannot be folded. */
	rv = sprintf(buf, "%s", argv[0]);

	return (1);
}
