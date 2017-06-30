/*
 * SO2 Lab - Linux device drivers (#4)
 * User-space test file
 */

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "../include/so2_cdev.h"

#define DEVICE_PATH	"/dev/so2_cdev"

/*
 * prints error message and exits
 */

static void error(const char *message)
{
	perror(message);
	exit(EXIT_FAILURE);
}

/*
 * print use case
 */

static void usage(const char *argv0)
{
	printf("Usage: %s <options>\n options:\n"
			"\tp - print\n"
			"\ts string - set buffer\n"
			"\tg - get buffer\n"
			"\td - down\n"
			"\tu - up\n"
			"\tn - open with O_NONBLOCK and read data\n", argv0);
	exit(EXIT_FAILURE);
}

/*
 * Sample run:
 *  ./so2_cdev_test p		; print ioctl message
 *  ./so2_cdev_test d		; wait on wait_queue
 *  ./so2_cdev_test u		; wait on wait_queue
 */

int main(int argc, char **argv)
{
	int fd;
	char buffer[BUFFER_SIZE];

	if (argc < 2)
		usage(argv[0]);

	if (strlen(argv[1]) != 1)
		usage(argv[0]);

	fd = open(DEVICE_PATH, O_RDONLY);
	if (fd < 0) {
		perror("open");
		exit(EXIT_FAILURE);
	}

	switch (argv[1][0]) {
	case 'p':				/* print */
		if (ioctl(fd, MY_IOCTL_PRINT, 0) < 0) {
			perror("ioctl");
			exit(EXIT_FAILURE);
		}

		break;
	case 's':				/* set buffer */
		if (argc < 3)
			usage(argv[0]);
		memset(buffer, 0, BUFFER_SIZE);
		strncpy(buffer, argv[2], BUFFER_SIZE);
		if (ioctl(fd, MY_IOCTL_SET_BUFFER, buffer) < 0) {
			perror("ioctl");
			exit(EXIT_FAILURE);
		}
		break;
	case 'g':				/* get buffer */
		if (ioctl(fd, MY_IOCTL_GET_BUFFER, buffer) < 0) {
			perror("ioctl");
			exit(EXIT_FAILURE);
		}
		buffer[BUFFER_SIZE-1] = 0;
		printf("IOCTL buffer contains %s\n", buffer);
		break;
	case 'd':				/* down */
		if (ioctl(fd, MY_IOCTL_DOWN, 0) < 0) {
			perror("ioctl");
			exit(EXIT_FAILURE);
		}
		break;
	case 'u':				/* up */
		if (ioctl(fd, MY_IOCTL_UP, 0) < 0) {
			perror("ioctl");
			exit(EXIT_FAILURE);
		}
		break;
	case 'n':
		if (fcntl(fd, F_SETFL, O_RDONLY | O_NONBLOCK) < 0) {
			perror("fcntl");
			exit(EXIT_FAILURE);
		}

		if (read(fd, buffer, BUFFER_SIZE) < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		}
		buffer[BUFFER_SIZE-1] = 0;
		printf("Device buffer contains %s\n", buffer);
		break;
	default:
		error("Wrong parameter");
	}

	close(fd);

	return 0;
}
