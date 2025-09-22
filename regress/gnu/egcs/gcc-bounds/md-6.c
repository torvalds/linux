#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sha1.h>

const unsigned char data[] = "1234567890abcdefghijklmnopqrstuvwxyz";

int
main(int argc, char **argv)
{
	SHA1_CTX ctx;
	char ret[20];

	SHA1Init(&ctx);
	SHA1Data(data, sizeof data - 1, ret);
	printf("%s\n", ret);
}
