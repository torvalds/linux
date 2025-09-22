#include <string.h>

int
main(int argc, char **argv)
{
	char buf[10];

	/* This expression can be folded. */
	strcat(buf, "foo");

	return (1);
}
