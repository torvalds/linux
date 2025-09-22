#include <stdio.h>

int
main(int argc, char **argv)
{
	char buf[10];

	/* This expression can be folded. */
	sprintf(buf, "%s", "foo");

	return (1);
}
