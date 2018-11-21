#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#define BUF_SIZE 256
int main(void)
{
	int fd = open("/dev/urandom", O_RDONLY);
	int i;
	char buf[BUF_SIZE];

	if (fd < 0)
		return 1;
	for (i = 0; i < 4; ++i)
		read(fd, buf, BUF_SIZE);

	close(fd);
	return 0;
}
