#include <string.h>

int
main(int argc, char **argv)
{
	char buf[10];

	/* This expression can be folded. */
	strcpy(buf, "foo");

	return (1);
}
