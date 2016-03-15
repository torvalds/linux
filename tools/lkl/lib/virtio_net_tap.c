/*
 * tun/tap based virtual network interface feature for LKL
 * Copyright (c) 2015,2016 Ryo Nakamura, Hajime Tazaki
 *
 * Author: Ryo Nakamura <upa@wide.ad.jp>
 *         Hajime Tazaki <thehajime@gmail.com>
 *         Octavian Purdila <octavian.purdila@intel.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>

#include <lkl_host.h>

#include "virtio.h"

struct lkl_netdev_tap {
	struct lkl_netdev dev;
	int fd;
};

static int net_tx(struct lkl_netdev *nd, void *data, int len)
{
	int ret;
	struct lkl_netdev_tap *nd_tap = (struct lkl_netdev_tap *) nd;

	ret = write(nd_tap->fd, data, len);
	if (ret <= 0 && errno == EAGAIN)
		return -1;
	return 0;
}

static int net_rx(struct lkl_netdev *nd, void *data, int *len)
{
	int ret;
	struct lkl_netdev_tap *nd_tap = (struct lkl_netdev_tap *) nd;

	ret = read(nd_tap->fd, data, *len);
	if (ret <= 0)
		return -1;
	*len = ret;
	return 0;
}

static int net_poll(struct lkl_netdev *nd, int events)
{
	struct lkl_netdev_tap *nd_tap = (struct lkl_netdev_tap *) nd;

	struct pollfd pfd = {
		.fd = nd_tap->fd,
	};
	int ret = 0;

	if (events & LKL_DEV_NET_POLL_RX)
		pfd.events |= POLLIN | POLLPRI;
	if (events & LKL_DEV_NET_POLL_TX)
		pfd.events |= POLLOUT;

	while (poll(&pfd, 1, -1) < 0 && errno == EINTR)
		;

	if (pfd.revents & (POLLHUP | POLLNVAL))
		return -1;

	if (pfd.revents & POLLIN)
		ret |= LKL_DEV_NET_POLL_RX;
	if (pfd.revents & POLLOUT)
		ret |= LKL_DEV_NET_POLL_TX;

	return ret;
}

struct lkl_dev_net_ops tap_net_ops = {
	.tx = net_tx,
	.rx = net_rx,
	.poll = net_poll,
};

struct lkl_netdev *lkl_netdev_tap_create(const char *ifname)
{
	struct lkl_netdev_tap *nd;
	int ret;

	nd = (struct lkl_netdev_tap *) malloc(sizeof(struct lkl_netdev_tap));
	if (!nd) {
		fprintf(stderr, "tap: failed to allocate memory\n");
		/* TODO: propagate the error state, maybe use errno for that? */
		return NULL;
	}
	nd->dev.ops = &tap_net_ops;

	struct ifreq ifr = {
		.ifr_flags = IFF_TAP | IFF_NO_PI,
	};

	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

	nd->fd = open("/dev/net/tun", O_RDWR|O_NONBLOCK);
	if (nd->fd < 0) {
		fprintf(stderr, "tap: failed to open tap: %s\n",
			strerror(errno));
		free(nd);
		return NULL;
	}

	ret = ioctl(nd->fd, TUNSETIFF, &ifr);
	if (ret < 0) {
		fprintf(stderr, "tap: failed to attach to %s: %s\n",
			ifr.ifr_name, strerror(errno));
		close(nd->fd);
		free(nd);
		return NULL;
	}

	return (struct lkl_netdev *)nd;
}
