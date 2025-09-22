#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <md5.h>

const unsigned char data[] = "1234567890abcdefghijklmnopqrstuvwxyz";

int
main(int argc, char **argv)
{
	MD5_CTX ctx;
	char *ret;

	MD5Init(&ctx);
	ret = MD5Data(data, sizeof data - 1, NULL);
	printf("%s\n", ret);
	free(ret);
}
