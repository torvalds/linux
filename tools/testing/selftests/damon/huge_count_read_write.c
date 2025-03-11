// SPDX-License-Identifier: GPL-2.0
/*
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#pragma GCC diagnostic push
#if __GNUC__ >= 11 && __GNUC_MINOR__ >= 1
/* Ignore read(2) overflow and write(2) overread compile warnings */
#pragma GCC diagnostic ignored "-Wstringop-overread"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

void write_read_with_huge_count(char *file)
{
	int filedesc = open(file, O_RDWR);
	char buf[256];
	int ret;

	printf("%s %s\n", __func__, file);
	if (filedesc < 0) {
		fprintf(stderr, "failed opening %s\n", file);
		exit(1);
	}

	write(filedesc, "", 0xfffffffful);
	ret = read(filedesc, buf, 0xfffffffful);
	close(filedesc);
}

#pragma GCC diagnostic pop

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <file>\n", argv[0]);
		exit(1);
	}
	write_read_with_huge_count(argv[1]);

	return 0;
}
