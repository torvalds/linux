#include <stdio.h>

int main(int argc, char **argv) {
	char buf[20];
	int a[2], b;
	sscanf(buf, "%3d %d %3d %30s %50d %20c %21c", &a[0], &a[1], &b, buf, buf, buf, buf);
	return 0;
}
