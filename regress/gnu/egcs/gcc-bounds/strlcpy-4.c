#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
	char *buf;
	strlcpy(buf, "foo", sizeof buf);
	return 1;
}
