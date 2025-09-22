#include <string.h>

int main(int argc, char **argv) {
	char buf[0];
	char buf2[0];
	memcpy(buf, buf2, sizeof buf);
	return 1;
}
