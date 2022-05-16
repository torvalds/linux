// SPDX-License-Identifier: GPL-2.0
/*
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

void write_read_with_huge_count(char *file)
{
	int filedesc = open(file, O_RDWR);
	char buf[25];
	int ret;

	printf("%s %s\n", __func__, file);
	if (filedesc < 0) {
		fprintf(stderr, "failed opening %s\n", file);
		exit(1);
	}

	write(filedesc, "", 0xfffffffful);
	perror("after write: ");
	ret = read(filedesc, buf, 0xfffffffful);
	perror("after read: ");
	close(filedesc);
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <file>\n", argv[0]);
		exit(1);
	}
	write_read_with_huge_count(argv[1]);

	return 0;
}
