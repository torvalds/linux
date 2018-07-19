#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#define BUF_SIZE 256

int main(int argc, char *argv[])
{
	int fd = open("/dev/urandom", O_RDONLY);
	int i;
	char buf[BUF_SIZE];
	int count = 4;

	if (fd < 0)
		return 1;

	if (argc == 2)
		count = atoi(argv[1]);

	for (i = 0; i < count; ++i)
		read(fd, buf, BUF_SIZE);

	close(fd);
	return 0;
}
