/*
 * video_device_test - Video Device Test
 *
 * Copyright (c) 2016 Shuah Khan <shuahkh@osg.samsung.com>
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *
 * This file is released under the GPLv2.
 */

/*
 * This file adds a test for Video Device. This test should not be included
 * in the Kselftest run. This test should be run when hardware and driver
 * that makes use of V4L2 API is present.
 *
 * This test opens user specified Video Device and calls video ioctls in a
 * loop once every 10 seconds.
 *
 * Usage:
 *	sudo ./video_device_test -d /dev/videoX
 *
 *	While test is running, remove the device or unbind the driver and
 *	ensure there are no use after free errors and other Oops in the
 *	dmesg.
 *	When possible, enable KaSan kernel config option for use-after-free
 *	error detection.
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
#include <linux/videodev2.h>

int main(int argc, char **argv)
{
	int opt;
	char video_dev[256];
	int count;
	struct v4l2_tuner vtuner;
	struct v4l2_capability vcap;
	int ret;
	int fd;

	if (argc < 2) {
		printf("Usage: %s [-d </dev/videoX>]\n", argv[0]);
		exit(-1);
	}

	/* Process arguments */
	while ((opt = getopt(argc, argv, "d:")) != -1) {
		switch (opt) {
		case 'd':
			strncpy(video_dev, optarg, sizeof(video_dev) - 1);
			video_dev[sizeof(video_dev)-1] = '\0';
			break;
		default:
			printf("Usage: %s [-d </dev/videoX>]\n", argv[0]);
			exit(-1);
		}
	}

	/* Generate random number of interations */
	srand((unsigned int) time(NULL));
	count = rand();

	/* Open Video device and keep it open */
	fd = open(video_dev, O_RDWR);
	if (fd == -1) {
		printf("Video Device open errno %s\n", strerror(errno));
		exit(-1);
	}

	printf("\nNote:\n"
	       "While test is running, remove the device or unbind\n"
	       "driver and ensure there are no use after free errors\n"
	       "and other Oops in the dmesg. When possible, enable KaSan\n"
	       "kernel config option for use-after-free error detection.\n\n");

	while (count > 0) {
		ret = ioctl(fd, VIDIOC_QUERYCAP, &vcap);
		if (ret < 0)
			printf("VIDIOC_QUERYCAP errno %s\n", strerror(errno));
		else
			printf("Video device driver %s\n", vcap.driver);

		ret = ioctl(fd, VIDIOC_G_TUNER, &vtuner);
		if (ret < 0)
			printf("VIDIOC_G_TUNER, errno %s\n", strerror(errno));
		else
			printf("type %d rangelow %d rangehigh %d\n",
				vtuner.type, vtuner.rangelow, vtuner.rangehigh);
		sleep(10);
		count--;
	}
}
