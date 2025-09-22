#include <stdio.h>

int main(int argc, char **argv) {
	char *buf;
	snprintf(buf, sizeof(buf), "%s", "foo");
	return 1;
}
