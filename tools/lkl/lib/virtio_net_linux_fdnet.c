/*
 * Linux File descripter based virtual network interface feature for LKL
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
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "virtio.h"
#include "virtio_net_linux_fdnet.h"

struct lkl_netdev_linux_fdnet_ops lkl_netdev_linux_fdnet_ops = {
	/*
	 * /dev/net/tun is Linux specific so we know our host is some
	 * flavor of Linux, but this allows graceful support if we're
	 * on a kernel that's < 2.6.22.
	 */
	#ifdef __NR_eventfd
	/* This sigature was recently (9/2014) changed in glibc. */
	.eventfd = (int (*)(unsigned int, int))eventfd,
	#endif /* __NR_eventfd */
};

static int linux_fdnet_net_tx(struct lkl_netdev *nd, void *data, int len)
{
	int ret;
	struct lkl_netdev_linux_fdnet *nd_fdnet =
		container_of(nd, struct lkl_netdev_linux_fdnet, dev);

	do {
		ret = write(nd_fdnet->fd, data, len);
	} while (ret == -1 && errno == EINVAL);
	if (ret > 0)
		return 0;
	if (ret < 0 && errno != EAGAIN)
		perror("write to Linux fd netdev fails");

	return -1;
}

static int linux_fdnet_net_rx(struct lkl_netdev *nd, void *data, int *len)
{
	int ret;
	struct lkl_netdev_linux_fdnet *nd_fdnet =
		container_of(nd, struct lkl_netdev_linux_fdnet, dev);

	do {
		ret = read(nd_fdnet->fd, data, *len);
	} while (ret == -1 && errno == EINVAL);
	if (ret > 0) {
		*len = ret;
		return 0;
	}
	if (ret < 0 && errno != EAGAIN)
		perror("read from fdnet device fails");

	return -1;
}

static int linux_fdnet_net_poll(struct lkl_netdev *nd, int events)
{
	struct lkl_netdev_linux_fdnet *nd_fdnet =
		container_of(nd, struct lkl_netdev_linux_fdnet, dev);
	int epoll_fd = -1;
	struct epoll_event ev[2];
	int ret;
	const int is_rx = events & LKL_DEV_NET_POLL_RX;
	const int is_tx = events & LKL_DEV_NET_POLL_TX;
	int i;
	int ret_ev = 0;
	unsigned int event;

	if (is_rx && is_tx) {
		fprintf(stderr, "both LKL_DEV_NET_POLL_RX and "
			"LKL_DEV_NET_POLL_TX are set\n");
		lkl_host_ops.panic();
		return -1;
	}
	if (!is_rx && !is_tx) {
		fprintf(stderr, "Neither LKL_DEV_NET_POLL_RX nor"
			" LKL_DEV_NET_POLL_TX are set.\n");
		lkl_host_ops.panic();
		return -1;
	}

	if (is_rx)
		epoll_fd = nd_fdnet->epoll_rx_fd;
	else if (is_tx)
		epoll_fd = nd_fdnet->epoll_tx_fd;

	do {
		ret = epoll_wait(epoll_fd, ev, 2, -1);
	} while (ret == -1 && errno == EINTR);
	if (ret < 0) {
		perror("epoll_wait");
		return -1;
	}

	for (i = 0; i < ret; ++i) {
		if (ev[i].data.fd == nd_fdnet->eventfd)
			return -1;
		if (ev[i].data.fd == nd_fdnet->fd) {
			event = ev[i].events;
			if (event & (EPOLLIN | EPOLLPRI))
				ret_ev = LKL_DEV_NET_POLL_RX;
			else if (event & EPOLLOUT)
				ret_ev = LKL_DEV_NET_POLL_TX;
			else
				return -1;
		}
	}
	return ret_ev;
}

static int linux_fdnet_net_close(struct lkl_netdev *nd)
{
	long buf = 1;
	struct lkl_netdev_linux_fdnet *nd_fdnet =
		container_of(nd, struct lkl_netdev_linux_fdnet, dev);

	if (nd_fdnet->eventfd == -1) {
		/* No eventfd support. */
		return 0;
	}

	if (write(nd_fdnet->eventfd, &buf, sizeof(buf)) < 0) {
		perror("linux-fdnet: failed to close fd");
		/* This should never happen. */
		return -1;
	}

	/* The order that we join in doesn't matter. */
	if (lkl_host_ops.thread_join(nd->rx_tid) ||
		lkl_host_ops.thread_join(nd->tx_tid))
		return -1;

	/* nor does the order that we close */
	if (close(nd_fdnet->fd) || close(nd_fdnet->eventfd) ||
		close(nd_fdnet->epoll_rx_fd) || close(nd_fdnet->epoll_tx_fd)) {
		perror("linux-fdnet net_close fd");
		return -1;
	}

	return 0;
}

struct lkl_dev_net_ops linux_fdnet_net_ops =  {
	.tx = linux_fdnet_net_tx,
	.rx = linux_fdnet_net_rx,
	.poll = linux_fdnet_net_poll,
	.close = linux_fdnet_net_close,
};

static int add_to_epoll(int epoll_fd, int fd, unsigned int events)
{
	struct epoll_event ev;
	int ret;

	memset(&ev, 0, sizeof(ev));
	ev.events = events;
	ev.data.fd = fd;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
	if (ret) {
		perror("EPOLL_CTL_ADD fails");
		return -1;
	}
	return 0;
}

static int create_epoll_fd(int fd, unsigned int events)
{
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


struct lkl_netdev_linux_fdnet *lkl_register_netdev_linux_fdnet(int fd)
{
	struct lkl_netdev_linux_fdnet *nd;

	nd = (struct lkl_netdev_linux_fdnet *)
		malloc(sizeof(struct lkl_netdev_linux_fdnet));
	if (!nd) {
		fprintf(stderr, "fdnet: failed to allocate memory\n");
		/* TODO: propagate the error state, maybe use errno for that? */
		return NULL;
	}

	nd->fd = fd;
	/* Making them edge-triggered to save CPU. */
	nd->epoll_rx_fd = create_epoll_fd(nd->fd, EPOLLIN | EPOLLPRI | EPOLLET);
	nd->epoll_tx_fd = create_epoll_fd(nd->fd, EPOLLOUT | EPOLLET);
	if (nd->epoll_rx_fd < 0 || nd->epoll_tx_fd < 0) {
		if (nd->epoll_rx_fd >= 0)
			close(nd->epoll_rx_fd);
		if (nd->epoll_tx_fd >= 0)
			close(nd->epoll_tx_fd);
		lkl_unregister_netdev_linux_fdnet(nd);
		return NULL;
	}

	if (lkl_netdev_linux_fdnet_ops.eventfd) {
		/* eventfd is supported by the host, all is well */
		nd->eventfd = lkl_netdev_linux_fdnet_ops.eventfd(
			0, EFD_NONBLOCK | EFD_SEMAPHORE);

		if (nd->eventfd < 0) {
			perror("fdnet: create eventfd");
			lkl_unregister_netdev_linux_fdnet(nd);
			return NULL;
		}
		if (add_to_epoll(nd->epoll_rx_fd, nd->eventfd, EPOLLIN) ||
			add_to_epoll(nd->epoll_tx_fd, nd->eventfd, EPOLLIN)) {
			lkl_unregister_netdev_linux_fdnet(nd);
			return NULL;
		}
	} else {
		/* no host eventfd support */
		nd->eventfd = -1;
	}

	nd->dev.ops = &linux_fdnet_net_ops;
	return nd;
}

void lkl_unregister_netdev_linux_fdnet(struct lkl_netdev_linux_fdnet *nd)
{
	close(nd->eventfd);
	close(nd->epoll_rx_fd);
	close(nd->epoll_tx_fd);
	free(nd);
}
