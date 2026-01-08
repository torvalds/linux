// SPDX-License-Identifier: GPL-2.0
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <sys/ioctl.h>

int main(int argc, char **argv)
{
	struct logical_block_metadata_cap cap = {};
	const char *filename;
	int fd;
	int result;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s BLOCK_DEVICE\n", argv[0]);
		return 1;
	}

	filename = argv[1];
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror(filename);
		return 1;
	}

	result = ioctl(fd, FS_IOC_GETLBMD_CAP, &cap);
	if (result < 0) {
		perror("ioctl");
		return 1;
	}

	printf("metadata_size: %u\n", cap.lbmd_size);
	printf("pi_offset: %u\n", cap.lbmd_pi_offset);
	printf("pi_tuple_size: %u\n", cap.lbmd_pi_size);
	return 0;
}
