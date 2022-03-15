// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "../../../../include/uapi/linux/rk-iomux.h"

static int rk_iomux_ioctl_set(int fd, int bank, int pin, int mux)
{
	struct iomux_ioctl_data data = {
		.bank = bank,
		.pin = pin,
		.mux = mux,
	};
	int ret;

	if (!fd)
		return -EINVAL;

	ret = ioctl(fd, IOMUX_IOC_MUX_SET, &data);
	if (ret < 0) {
		perror("fail to ioctl");
		return ret;
	}

	return 0;
}

static int rk_iomux_ioctl_get(int fd, int bank, int pin, int *mux)
{
	struct iomux_ioctl_data data = {
		.bank = bank,
		.pin = pin,
	};
	int ret;

	if (!fd)
		return -EINVAL;

	ret = ioctl(fd, IOMUX_IOC_MUX_GET, &data);
	if (ret < 0) {
		perror("fail to ioctl");
		return ret;
	}
	*mux = data.mux;

	return 0;
}

static void usage(void)
{
	printf("%s:\n"
		"set iomux:\n"
		"iomux [bank index] [pin index] [mux value]\n"
		"get iomux:\n"
		"iomux [bank index] [pin index]\n",
		__func__);
}

int main(int argc, char *argv[])
{
	const char *name = "/dev/iomux";
	int fd;
	int bank, pin, mux;
	int ret;

	if ((argc != 3) && (argc != 4)) {
		usage();
		return -1;
	}

	bank = atoi(argv[1]);
	pin = atoi(argv[2]);

	fd = open(name, O_RDWR);
	if (fd < 0) {
		printf("open %s failed!\n", name);
		return fd;
	}

	if (argc == 4) {
		mux = atoi(argv[3]);
		ret = rk_iomux_ioctl_set(fd, bank, pin, mux);
		if (ret)
			goto err;
	} else if (argc == 3) {
		ret = rk_iomux_ioctl_get(fd, bank, pin, &mux);
		if (ret)
			goto err;
		printf("mux get (GPIO%d-%d) = %d\n", bank, pin, mux);
	}

err:
	close(fd);
	return 0;
}
