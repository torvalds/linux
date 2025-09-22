#include <string.h>

int main(int argc, char **argv) {
	char buf[10];
	char buf2[10] = "123456789";
	memcpy(buf, buf2, sizeof buf);
	return 1;
}
