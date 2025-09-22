#include <stdio.h>

extern char buf1[];

int main(int argc, char **argv) {
	char q[10], buf1[10];
	sscanf(q,"%[.0-9]",buf1);
	return 1;
}
