#include <string.h>

int
main(int argc, char **argv)
{
	char buf[512];
	volatile char *rv;

	/* This expression cannot be folded. */
	rv = stpcpy(buf, argv[0]);

	return (1);
}
