/*
 * tun/tap based virtual network interface feature for LKL
 * Copyright (c) 2015,2016 Ryo Nakamura, Hajime Tazaki
 *
 * Author: Ryo Nakamura <upa@wide.ad.jp>
 *         Hajime Tazaki <thehajime@gmail.com>
 *         Octavian Purdila <octavian.purdila@intel.com>
 *
 * Current implementation is linux-specific.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>

#include "virtio.h"
#include "virtio_net_linux_fdnet.h"

struct lkl_netdev *lkl_netdev_tap_create(const char *ifname)
{
	struct lkl_netdev_linux_fdnet *nd;
	int fd, ret;

	struct ifreq ifr = {
		.ifr_flags = IFF_TAP | IFF_NO_PI,
	};

	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

	fd = open("/dev/net/tun", O_RDWR|O_NONBLOCK);
	if (fd < 0) {
		perror("tap: failed to open tap");
		return NULL;
	}

	ret = ioctl(fd, TUNSETIFF, &ifr);
	if (ret < 0) {
		fprintf(stderr, "tap: failed to attach to %s: %s\n",
			ifr.ifr_name, strerror(errno));
		close(fd);
		return NULL;
	}

	nd = lkl_register_netdev_linux_fdnet(fd);
	if (!nd) {
		perror("failed to register to.");
		return NULL;
	}

	return (struct lkl_netdev *)nd;
}
