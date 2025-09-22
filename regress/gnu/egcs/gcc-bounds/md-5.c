#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <rmd160.h>

int
main(int argc, char **argv)
{
	RMD160_CTX ctx;
	unsigned char *data = malloc(10);
	char ret[32];

	strlcpy((char *)data, "123456789", 10);

	RMD160Init(&ctx);
	RMD160Data(data, sizeof data, ret);
	printf("%s\n", ret);
}
