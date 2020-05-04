/*
 * SO2 - Networking Lab (#11)
 *
 * Test filter module for exercise #2
 *
 * Sends MY_IOCTL_FILTER_ADDRESS to filter module.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "../kernel/filter.h"

#define MY_DEVICE	"/dev/filter"


static void print_usage(char *argv0)
{
	fprintf(stderr, "Usage: %s <address>\n"
			"\taddress must be a string containing "
			"an IP dotted address\n", argv0);
}

int main(int argc, char **argv)
{
	int fd;
	unsigned int addr;

	if (argc != 2) {
		print_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	/* get address */
	addr = inet_addr(argv[1]);

	/* make device node */
	if (mknod(MY_DEVICE, 0644 | S_IFCHR, makedev(MY_MAJOR, 0)) < 0) {
		if (errno != EEXIST) {
			perror("mknod " MY_DEVICE);
			exit(EXIT_FAILURE);
		}
	}

	/* open device */
	fd = open(MY_DEVICE, O_RDONLY);
	if (fd < 0) {
		perror("open " MY_DEVICE);
	} else {
		/* send ioctl */
		if (ioctl(fd, MY_IOCTL_FILTER_ADDRESS, &addr) < 0)
			perror("ioctl MY_IOCTL_FILTER_ADDRESS");

		/* close device */
		if (close(fd) < 0)
			perror("close");
	}

	/* cleanup device node */
	if (unlink(MY_DEVICE) < 0)
		perror("unlink " MY_DEVICE);

	return 0;
}
