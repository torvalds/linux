#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <md5.h>

int
main(int argc, char **argv)
{
	MD5_CTX ctx;
	unsigned char *data = malloc(10);
	char ret[33];

	strlcpy((char *)data, "123456789", 10);

	MD5Init(&ctx);
	MD5Data(data, sizeof data, ret);
	printf("%s\n", ret);
}
