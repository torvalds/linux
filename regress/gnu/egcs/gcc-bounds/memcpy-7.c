#include <string.h>

extern char buf[];

int main(int argc, char **argv) {
	char buf2[10] = "123456789";
	memcpy(buf, buf2, sizeof buf2);
	return 1;
}
