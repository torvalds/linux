// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv)
{
	const char *path;
	char buf[4];
	int fd, rc;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <path>\n", argv[0]);
		return EXIT_FAILURE;
	}

	path = argv[1];

	/* create a test variable */
	fd = open(path, O_RDWR | O_CREAT, 0600);
	if (fd < 0) {
		perror("open(O_WRONLY)");
		return EXIT_FAILURE;
	}

	rc = read(fd, buf, sizeof(buf));
	if (rc != 0) {
		fprintf(stderr, "Reading a new var should return EOF\n");
		close(fd);
		return EXIT_FAILURE;
	}

	close(fd);
	return EXIT_SUCCESS;
}
