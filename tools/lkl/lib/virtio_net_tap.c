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
#include <sys/eventfd.h>
#include <linux/unistd.h>

#include <lkl_host.h>

#include "virtio.h"
#include "virtio_net_tap.h"

struct lkl_netdev_tap {
	struct lkl_netdev dev;
	/* TAP device */
	int fd;
	/* Needed to initiate shutdown */
	int eventfd;
};

struct lkl_netdev_tap_ops lkl_netdev_tap_ops = {
	/* /dev/net/tun is Linux specific so we know our host is some
	 * flavor of Linux, but this allows graceful support if we're
	 * on a kernel that's < 2.6.22. */
	#ifdef __NR_eventfd
	/* This sigature was recently (9/2014) changed in glibc. */
	.eventfd = (int (*)(unsigned int, int))eventfd,
	#endif /* __NR_eventfd */
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

static inline int pfds_should_close(struct pollfd *pfds, int closeable)
{
	if (pfds[0].revents & (POLLHUP | POLLNVAL))
		return 1;

	if (closeable && pfds[1].revents & POLLIN)
		return 1;

	return 0;
}

/* pfds must be zeroed out by the caller */
static inline void config_pfds(struct pollfd *pfds, struct lkl_netdev_tap *nd_tap,
			int events, int closeable)
{
	pfds[0].fd = nd_tap->fd;

	if (events & LKL_DEV_NET_POLL_RX)
		pfds[0].events |= POLLIN | POLLPRI;
	if (events & LKL_DEV_NET_POLL_TX)
		pfds[0].events |= POLLOUT;

	if (closeable) {
		pfds[1].fd = nd_tap->eventfd;
		pfds[1].events = POLLIN;
	}
}

static inline void poll_pfds(struct pollfd *pfds, int closeable)
{
	while (poll(pfds, closeable ? 2 : 1, -1) < 0 && errno == EINTR)
		; 		/* spin */
}

static int net_poll(struct lkl_netdev *nd, int events, int closeable)
{
	struct lkl_netdev_tap *nd_tap = container_of(nd, struct lkl_netdev_tap, dev);
	struct pollfd pfds[2] = {{0}};
	int ret = 0;

	config_pfds(pfds, nd_tap, events, closeable);

	poll_pfds(pfds, closeable);

	if (pfds_should_close(pfds, closeable))
		return -1;

	if (pfds[0].revents & POLLIN)
		ret |= LKL_DEV_NET_POLL_RX;
	if (pfds[0].revents & POLLOUT)
		ret |= LKL_DEV_NET_POLL_TX;

	return ret;
}

static int net_poll_uncloseable(struct lkl_netdev *nd, int events)
{
	return net_poll(nd, events, 0);
}

static int net_poll_closeable(struct lkl_netdev *nd, int events)
{
	return net_poll(nd, events, 1);
}

static int net_close(struct lkl_netdev *nd)
{
	long buf = 1;
	struct lkl_netdev_tap *nd_tap = container_of(nd, struct lkl_netdev_tap, dev);

	if (write(nd_tap->eventfd, &buf, sizeof(buf)) < 0) {
		perror("tap: failed to close tap");
		/* This should never happen. */
		return -1;
	}

	/* The order that we join in doesn't matter. */
	if (lkl_host_ops.thread_join(nd->rx_tid) ||
		lkl_host_ops.thread_join(nd->tx_tid))
		return -1;

	/* nor does the order that we close */
	if (close(nd_tap->fd) || close(nd_tap->eventfd)) {
		perror("tap net_close TAP fd");
		return -1;
	}

	return 0;
}

struct lkl_dev_net_ops tap_net_ops_uncloseable = {
	.tx = net_tx,
	.rx = net_rx,
	.poll = net_poll_uncloseable,
};


struct lkl_dev_net_ops tap_net_ops_closeable = {
	.tx = net_tx,
	.rx = net_rx,
	.poll = net_poll_closeable,
	.close = net_close,
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

	struct ifreq ifr = {
		.ifr_flags = IFF_TAP | IFF_NO_PI,
	};

	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

	nd->fd = open("/dev/net/tun", O_RDWR|O_NONBLOCK);
	if (nd->fd < 0) {
		perror("tap: failed to open tap");
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

	if (lkl_netdev_tap_ops.eventfd) {
		/* eventfd is supported by the host, all is well */
		nd->dev.ops = &tap_net_ops_closeable;
		nd->eventfd = lkl_netdev_tap_ops.eventfd(
			0, EFD_NONBLOCK | EFD_SEMAPHORE);

		if (nd->eventfd < 0) {
			perror("lkl_netdev_tap_create eventfd");
			close(nd->fd);
			free(nd);
			return NULL;
		}
	} else {
		/* no host eventfd support */
		nd->dev.ops = &tap_net_ops_uncloseable;
		nd->eventfd = -1;
	}


	return (struct lkl_netdev *)nd;
}
