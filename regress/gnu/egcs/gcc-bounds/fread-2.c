#include <stdio.h>

int main(int argc, char **argv) {
	char buf[100];
	FILE *fp;
	fread(buf, sizeof(char), sizeof(buf) + 10, fp);
	return 1;
}
