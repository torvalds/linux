#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
	char buf[10];
	strlcat(buf, "foo", -1);
	return 1;
}
