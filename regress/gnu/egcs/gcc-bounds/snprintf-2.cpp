#include <stdio.h>

int main(int argc, char **argv) {
	char buf[20];
	snprintf(buf, sizeof(buf) + 10, "%s", "foo");
	return 1;
}
