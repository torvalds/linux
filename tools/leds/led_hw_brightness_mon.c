// SPDX-License-Identifier: GPL-2.0
/*
 * led_hw_brightness_mon.c
 *
 * This program monitors LED brightness level changes having its origin
 * in hardware/firmware, i.e. outside of kernel control.
 * A timestamp and brightness value is printed each time the brightness changes.
 *
 * Usage: led_hw_brightness_mon <device-name>
 *
 * <device-name> is the name of the LED class device to be monitored. Pressing
 * CTRL+C will exit.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/uleds.h>

int main(int argc, char const *argv[])
{
	int fd, ret;
	char brightness_file_path[LED_MAX_NAME_SIZE + 11];
	struct pollfd pollfd;
	struct timespec ts;
	char buf[11];

	if (argc != 2) {
		fprintf(stderr, "Requires <device-name> argument\n");
		return 1;
	}

	snprintf(brightness_file_path, LED_MAX_NAME_SIZE,
		 "/sys/class/leds/%s/brightness_hw_changed", argv[1]);

	fd = open(brightness_file_path, O_RDONLY);
	if (fd == -1) {
		printf("Failed to open %s file\n", brightness_file_path);
		return 1;
	}

	/*
	 * read may fail if no hw brightness change has occurred so far,
	 * but it is required to avoid spurious poll notifications in
	 * the opposite case.
	 */
	read(fd, buf, sizeof(buf));

	pollfd.fd = fd;
	pollfd.events = POLLPRI;

	while (1) {
		ret = poll(&pollfd, 1, -1);
		if (ret == -1) {
			printf("Failed to poll %s file (%d)\n",
				brightness_file_path, ret);
			ret = 1;
			break;
		}

		clock_gettime(CLOCK_MONOTONIC, &ts);

		ret = read(fd, buf, sizeof(buf));
		if (ret < 0)
			break;

		ret = lseek(pollfd.fd, 0, SEEK_SET);
		if (ret < 0) {
			printf("lseek failed (%d)\n", ret);
			break;
		}

		printf("[%ld.%09ld] %d\n", ts.tv_sec, ts.tv_nsec, atoi(buf));
	}

	close(fd);

	return ret;
}
