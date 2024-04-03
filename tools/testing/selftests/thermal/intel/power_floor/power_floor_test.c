// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#define POWER_FLOOR_ENABLE_ATTRIBUTE "/sys/bus/pci/devices/0000:00:04.0/power_limits/power_floor_enable"
#define POWER_FLOOR_STATUS_ATTRIBUTE  "/sys/bus/pci/devices/0000:00:04.0/power_limits/power_floor_status"

void power_floor_exit(int signum)
{
	int fd;

	/* Disable feature via sysfs knob */

	fd = open(POWER_FLOOR_ENABLE_ATTRIBUTE, O_RDWR);
	if (fd < 0) {
		perror("Unable to open power floor enable file\n");
		exit(1);
	}

	if (write(fd, "0\n", 2) < 0) {
		perror("Can' disable power floor notifications\n");
		exit(1);
	}

	printf("Disabled power floor notifications\n");

	close(fd);
}

int main(int argc, char **argv)
{
	struct pollfd ufd;
	char status_str[3];
	int fd, ret;

	if (signal(SIGINT, power_floor_exit) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
	if (signal(SIGHUP, power_floor_exit) == SIG_IGN)
		signal(SIGHUP, SIG_IGN);
	if (signal(SIGTERM, power_floor_exit) == SIG_IGN)
		signal(SIGTERM, SIG_IGN);

	/* Enable feature via sysfs knob */
	fd = open(POWER_FLOOR_ENABLE_ATTRIBUTE, O_RDWR);
	if (fd < 0) {
		perror("Unable to open power floor enable file\n");
		exit(1);
	}

	if (write(fd, "1\n", 2) < 0) {
		perror("Can' enable power floor notifications\n");
		exit(1);
	}

	close(fd);

	printf("Enabled power floor notifications\n");

	while (1) {
		fd = open(POWER_FLOOR_STATUS_ATTRIBUTE, O_RDONLY);
		if (fd < 0) {
			perror("Unable to power floor status file\n");
			exit(1);
		}

		if ((lseek(fd, 0L, SEEK_SET)) < 0) {
			fprintf(stderr, "Failed to set pointer to beginning\n");
			exit(1);
		}

		if (read(fd, status_str, sizeof(status_str)) < 0) {
			fprintf(stderr, "Failed to read from:%s\n",
			POWER_FLOOR_STATUS_ATTRIBUTE);
			exit(1);
		}

		ufd.fd = fd;
		ufd.events = POLLPRI;

		ret = poll(&ufd, 1, -1);
		if (ret < 0) {
			perror("poll error");
			exit(1);
		} else if (ret == 0) {
			printf("Poll Timeout\n");
		} else {
			if ((lseek(fd, 0L, SEEK_SET)) < 0) {
				fprintf(stderr, "Failed to set pointer to beginning\n");
				exit(1);
			}

			if (read(fd, status_str, sizeof(status_str)) < 0)
				exit(0);

			printf("power floor status: %s\n", status_str);
		}

		close(fd);
	}
}
