#include <string.h>

int
main(int argc, char **argv)
{
	char buf[10];

	/* This expression can be folded. */
	stpcpy(buf, "foo");

	return (1);
}
