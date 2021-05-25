/*
 * lab008 - sync disk writes to a file, with (syscall) latency outliers.
 *
 * 21-May-2015	Brendan Gregg	Created this.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <strings.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

const char *datafile = "lab008.data";

#define BUFSIZE		(8 * 1024)
#define BIGSIZE		(10 * 1024 * 1024)
#define FILESIZE	(10 * 1024 * 1024)

void
write_log(int fd)
{
	char *buf, *big;
	long long i;
	int ret, j;

	buf = malloc(BUFSIZE);
	big = malloc(BIGSIZE);
	if (buf == NULL || big == NULL) {
		printf("ERROR: malloc buffers.\n");
		exit(1);
	}
	bzero(buf, BUFSIZE);
	bzero(big, BIGSIZE);

	for (;;) {
		for (i = 0, j = 0; i < FILESIZE;) {
			if ((j++ % 100) == 0) {
				ret = write(fd, big, BIGSIZE);
				i += BIGSIZE;
			} else {
				ret = write(fd, buf, BUFSIZE);
				i += BUFSIZE;
			}

			if (ret < 0) {
				printf("ERROR: write error.\n");
				exit(2);
			}
		}

		if (lseek(fd, 0, SEEK_SET) < 0) {
			printf("ERROR: seek() failed.\n");
			exit(3);
		}
	}

	free(buf);
	free(big);
}

int
main()
{
	int fd;

	if ((fd = open(datafile, O_CREAT | O_WRONLY | O_SYNC, 0644)) < 0) {
		printf("ERROR: writing to %s\n", datafile);
		exit(1);
	}

	write_log(fd);

	return (0);
}
