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
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <fcntl.h>
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
	/* epoll fds for rx and tx */
	int epoll_rx_fd;
	int epoll_tx_fd;
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

	do {
		ret = write(nd_tap->fd, data, len);
	} while (ret == -1 && errno == EINVAL);
	if (ret > 0) return 0;
	if (ret < 0 && errno != EAGAIN) {
		perror("write to tap fails");
	}
	return -1;
}

static int net_rx(struct lkl_netdev *nd, void *data, int *len)
{
	int ret;
	struct lkl_netdev_tap *nd_tap = (struct lkl_netdev_tap *) nd;

	do {
		ret = read(nd_tap->fd, data, *len);
	} while (ret == -1 && errno == EINVAL);
	if (ret > 0) {
		*len = ret;
		return 0; 
	}
	if (ret < 0 && errno != EAGAIN) {
		perror("read from tap fails");
	}
	return -1;
}

static int net_poll(struct lkl_netdev *nd, int events)
{
	struct lkl_netdev_tap *nd_tap = container_of(nd, struct lkl_netdev_tap, dev);
	int epoll_fd = -1;
	struct epoll_event ev[2];
	int ret;
	const int is_rx = events & LKL_DEV_NET_POLL_RX;
	const int is_tx = events & LKL_DEV_NET_POLL_TX;
	int i;
	int ret_ev = 0;
	unsigned int event;

	if (is_rx && is_tx) {
		fprintf(stderr, "both LKL_DEV_NET_POLL_RX and LKL_DEV_NET_POLL_TX"
			"are set\n");
		lkl_host_ops.panic();
		return -1;
	}
	if (!is_rx && !is_tx) {
		fprintf(stderr, "Neither LKL_DEV_NET_POLL_RX nor"
			"LKL_DEV_NET_POLL_TX are set.\n");
		lkl_host_ops.panic();
		return -1;
	}

	if (is_rx)
		epoll_fd = nd_tap->epoll_rx_fd;
	else if (is_tx)
		epoll_fd = nd_tap->epoll_tx_fd;

	do {
		ret = epoll_wait(epoll_fd, ev, 2, -1);
	} while (ret == -1 && errno == EINTR);
	if (ret < 0) {
		perror("epoll_wait");
		return -1;
	}

	for (i = 0; i < ret; ++i) {
		if (ev[i].data.fd == nd_tap->eventfd) {
			return -1;
		}
		if (ev[i].data.fd == nd_tap->fd) {
			event = ev[i].events;
			if(event & (EPOLLIN | EPOLLPRI))
				ret_ev = LKL_DEV_NET_POLL_RX;
			else if (event & EPOLLOUT)
				ret_ev = LKL_DEV_NET_POLL_TX;
			else
				return -1;
		}
	}
	return ret_ev;
}

static int net_close(struct lkl_netdev *nd)
{
	long buf = 1;
	struct lkl_netdev_tap *nd_tap = container_of(nd, struct lkl_netdev_tap, dev);

	if (nd_tap->eventfd == -1) {
		/* No eventfd support. */
		return 0;
	}

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
	if (close(nd_tap->fd) || close(nd_tap->eventfd) ||
		close(nd_tap->epoll_rx_fd) || close(nd_tap->epoll_tx_fd)) {
		perror("tap net_close TAP fd");
		return -1;
	}

	return 0;
}

struct lkl_dev_net_ops tap_net_ops =  {
	.tx = net_tx,
	.rx = net_rx,
	.poll = net_poll,
	.close = net_close,
};

static int add_to_epoll(int epoll_fd, int fd, unsigned int events)
{
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = events;
	ev.data.fd = fd;
	int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
	if (ret) {
		perror("EPOLL_CTL_ADD fails");
		return -1;
	}
	return 0;
}

static int create_epoll_fd(int fd, unsigned int events) {
	int ret = epoll_create1(0);
	if (ret < 0) {
		perror("epoll_create1");
		return -1;
	}
	if (add_to_epoll(ret, fd, events)) {
		close(ret);
		return -1;
	}
	return ret;
}

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
	/* Making them edge-triggered to save CPU. */
	nd->epoll_rx_fd  = create_epoll_fd(nd->fd, EPOLLIN | EPOLLPRI | EPOLLET);
	nd->epoll_tx_fd  = create_epoll_fd(nd->fd, EPOLLOUT | EPOLLET);
	if (nd->epoll_rx_fd < 0 || nd->epoll_tx_fd < 0) {
		if (nd->epoll_rx_fd >= 0) close(nd->epoll_rx_fd);
		if (nd->epoll_tx_fd >= 0) close(nd->epoll_tx_fd);
		close(nd->fd);
		free(nd);
		return NULL;
	}

	nd->dev.ops = &tap_net_ops;
	if (lkl_netdev_tap_ops.eventfd) {
		/* eventfd is supported by the host, all is well */
		nd->eventfd = lkl_netdev_tap_ops.eventfd(
			0, EFD_NONBLOCK | EFD_SEMAPHORE);

		if (nd->eventfd < 0) {
			perror("lkl_netdev_tap_create eventfd");
			goto fail;
		}
		if (add_to_epoll(nd->epoll_rx_fd, nd->eventfd, EPOLLIN) ||
			add_to_epoll(nd->epoll_tx_fd, nd->eventfd, EPOLLIN)) {
			close(nd->eventfd);
			goto fail;
		}
	} else {
		/* no host eventfd support */
		nd->eventfd = -1;
	}
	return (struct lkl_netdev *)nd;
fail:
	close(nd->epoll_rx_fd);
	close(nd->epoll_tx_fd);
	close(nd->fd);
	free(nd);
	return NULL;


}
