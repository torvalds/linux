// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

int main(int argc, char *argv[])
{
	int fd;
	size_t size;
	ssize_t spliced;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s INPUT [BYTES]\n", argv[0]);
		return EXIT_FAILURE;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror(argv[1]);
		return EXIT_FAILURE;
	}

	if (argc == 3)
		size = atol(argv[2]);
	else {
		struct stat statbuf;

		if (fstat(fd, &statbuf) < 0) {
			perror(argv[1]);
			return EXIT_FAILURE;
		}

		if (statbuf.st_size > INT_MAX) {
			fprintf(stderr, "%s: Too big\n", argv[1]);
			return EXIT_FAILURE;
		}

		size = statbuf.st_size;
	}

	/* splice(2) file to stdout. */
	spliced = splice(fd, NULL, STDOUT_FILENO, NULL,
		      size, SPLICE_F_MOVE);
	if (spliced < 0) {
		perror("splice");
		return EXIT_FAILURE;
	}

	close(fd);
	return EXIT_SUCCESS;
}
