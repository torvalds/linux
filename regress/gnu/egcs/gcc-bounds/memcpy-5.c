#include <string.h>

int main(int argc, char **argv) {
	int buf[10];
	int buf2[10] = {1,2,3,4,5,6,7,8,9,10};
	memcpy(buf, buf2, sizeof buf2);
	return 1;
}
