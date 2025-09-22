#include <stdio.h>

#if defined(__LP64__)
#define	DUMMYSIZE	104
#else
#define	DUMMYSIZE	100
#endif

int main(int argc, char **argv) {
	char *buf;
	char buf2[10];
	snprintf(buf2, -sizeof(buf) + DUMMYSIZE, "%s", "foo");
	return 1;
}
