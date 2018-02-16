// SPDX-License-Identifier: GPL-2.0

/*
 * media_device_test.c - Media Controller Device ioctl loop Test
 *
 * Copyright (c) 2016 Shuah Khan <shuahkh@osg.samsung.com>
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *
 */

/*
 * This file adds a test for Media Controller API.
 * This test should be run as root and should not be
 * included in the Kselftest run. This test should be
 * run when hardware and driver that makes use Media
 * Controller API are present in the system.
 *
 * This test opens user specified Media Device and calls
 * MEDIA_IOC_DEVICE_INFO ioctl in a loop once every 10
 * seconds.
 *
 * Usage:
 *	sudo ./media_device_test -d /dev/mediaX
 *
 *	While test is running, remove the device and
 *	ensure there are no use after free errors and
 *	other Oops in the dmesg. Enable KaSan kernel
 *	config option for use-after-free error detection.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <linux/media.h>

int main(int argc, char **argv)
{
	int opt;
	char media_device[256];
	int count;
	struct media_device_info mdi;
	int ret;
	int fd;

	if (argc < 2) {
		printf("Usage: %s [-d </dev/mediaX>]\n", argv[0]);
		exit(-1);
	}

	/* Process arguments */
	while ((opt = getopt(argc, argv, "d:")) != -1) {
		switch (opt) {
		case 'd':
			strncpy(media_device, optarg, sizeof(media_device) - 1);
			media_device[sizeof(media_device)-1] = '\0';
			break;
		default:
			printf("Usage: %s [-d </dev/mediaX>]\n", argv[0]);
			exit(-1);
		}
	}

	if (getuid() != 0) {
		printf("Please run the test as root - Exiting.\n");
		exit(-1);
	}

	/* Generate random number of interations */
	srand((unsigned int) time(NULL));
	count = rand();

	/* Open Media device and keep it open */
	fd = open(media_device, O_RDWR);
	if (fd == -1) {
		printf("Media Device open errno %s\n", strerror(errno));
		exit(-1);
	}

	printf("\nNote:\n"
	       "While test is running, remove the device and\n"
	       "ensure there are no use after free errors and\n"
	       "other Oops in the dmesg. Enable KaSan kernel\n"
	       "config option for use-after-free error detection.\n\n");

	printf("Running test for %d iternations\n", count);

	while (count > 0) {
		ret = ioctl(fd, MEDIA_IOC_DEVICE_INFO, &mdi);
		if (ret < 0)
			printf("Media Device Info errno %s\n", strerror(errno));
		else
			printf("Media device model %s driver %s - count %d\n",
				mdi.model, mdi.driver, count);
		sleep(10);
		count--;
	}
}
