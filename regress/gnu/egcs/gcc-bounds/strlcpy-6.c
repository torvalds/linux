#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
	char buf[10];
	char *buf2;
	strlcpy(buf2, "foo", sizeof buf);
	return 1;
}
