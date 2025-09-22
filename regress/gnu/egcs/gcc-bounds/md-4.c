#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <md5.h>

const unsigned char data[] = "1234567890abcdefghijklmnopqrstuvwxyz";

int
main(int argc, char **argv)
{
	MD5_CTX ctx;
	char ret[10];

	MD5Init(&ctx);
	MD5Data(data, sizeof data - 1, ret);
	printf("%s\n", ret);
}
