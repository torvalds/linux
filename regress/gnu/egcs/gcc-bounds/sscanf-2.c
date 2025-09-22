#include <stdio.h>

int main(int argc, char **argv) {
	char buf[10];
	char buf2[10];
	sscanf("foooo baaar", "%20s %10s", buf, buf2);
	return 1;
}
