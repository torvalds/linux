#include <stdio.h>

int main(int argc, char **argv) {
	long buf[100];
	FILE *fp;
	fread(buf, sizeof(char), sizeof(buf), fp);
	return 1;
}
