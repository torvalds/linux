#include <stdio.h>

int main(int argc, char **argv) {
	char buf[10];
	char buf2[10];
	sscanf("foooo baaar", "%9s %9s", buf, buf2);
	return 1;
}
