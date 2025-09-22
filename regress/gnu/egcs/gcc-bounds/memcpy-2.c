#include <string.h>

int main(int argc, char **argv) {
	char buf[10];
	char buf2[8] = "1234567";
	memcpy(buf, buf2, sizeof buf);
	return 1;
}
