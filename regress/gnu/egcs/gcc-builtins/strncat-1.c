#include <string.h>

int
main(int argc, char **argv)
{
	char foo[10];
	const char bar[] = "bar";

	/* The compiler should not simplify this into strcat. */
	strncat(foo, bar, sizeof(foo));

	return (1);
}
