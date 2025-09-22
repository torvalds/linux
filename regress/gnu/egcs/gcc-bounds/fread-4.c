#include <stdio.h>

int main(int argc, char **argv) {
	char buf[100];
	FILE *fp;
	fread(buf, sizeof(uint32_t), sizeof(buf), fp);
	return 1;
}
