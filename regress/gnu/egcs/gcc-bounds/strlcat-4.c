#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
	char *foo;
	strlcat(foo, "bar", -(-sizeof(foo)));
	return 1;
}
