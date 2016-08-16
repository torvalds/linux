/*
 * POSIX file descriptor based virtual network interface feature for
 * LKL Copyright (c) 2015,2016 Ryo Nakamura, Hajime Tazaki
 *
 * Author: Ryo Nakamura <upa@wide.ad.jp>
 *         Hajime Tazaki <thehajime@gmail.com>
 *         Octavian Purdila <octavian.purdila@intel.com>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/uio.h>

#include "virtio.h"
#include "virtio_net_fd.h"


/* The following tx() and rx() code assume struct lkl_dev_buf matches
 * sruct iovec so we can safely cast iov to (struct iovec *). (If
 * BUILD_BUG_ON() were supported in LKL, I would have added
 *
 * "BUILD_BUG_ON(sizeof(struct lkl_dev_buf) == sizeof(struct iovec));"
 */
static int fd_net_tx(struct lkl_netdev *nd, struct lkl_dev_buf *iov, int cnt)
{
	int ret;
	struct lkl_netdev_fd *nd_fd =
		container_of(nd, struct lkl_netdev_fd, dev);

	do {
		ret = writev(nd_fd->fd, (struct iovec *)iov, cnt);
	} while (ret == -1 && errno == EINTR);

	if (ret < 0) {
		if (errno != EAGAIN) {
			perror("write to fd netdev fails");
		} else {
			char tmp;

			nd_fd->poll_tx = 1;
			if (write(nd_fd->pipe[1], &tmp, 1) <= 0)
				perror("virtio net fd pipe write");
		}
	}
	return ret;
}

static int fd_net_rx(struct lkl_netdev *nd, struct lkl_dev_buf *iov, int cnt)
{
	int ret;
	struct lkl_netdev_fd *nd_fd =
		container_of(nd, struct lkl_netdev_fd, dev);

	do {
		ret = readv(nd_fd->fd, (struct iovec *)iov, cnt);
	} while (ret == -1 && errno == EINTR);

	if (ret < 0) {
		if (errno != EAGAIN) {
			perror("virtio net fd read");
		} else {
			char tmp;

			nd_fd->poll_rx = 1;
			if (write(nd_fd->pipe[1], &tmp, 1) < 0)
				perror("virtio net fd pipe write");
		}
	}
	return ret;
}

static int fd_net_poll(struct lkl_netdev *nd)
{
	struct lkl_netdev_fd *nd_fd =
		container_of(nd, struct lkl_netdev_fd, dev);
	struct pollfd pfds[2] = {
		{
			.fd = nd_fd->fd,
		},
		{
			.fd = nd_fd->pipe[0],
			.events = POLLIN,
		},
	};
	int ret;

	if (nd_fd->poll_rx)
		pfds[0].events |= POLLIN|POLLPRI;
	if (nd_fd->poll_tx)
		pfds[0].events |= POLLOUT;

	do {
		ret = poll(pfds, 2, -1);
	} while (ret == -1 && errno == EINTR);

	if (ret < 0) {
		perror("virtio net fd poll");
		return 0;
	}

	if (pfds[1].revents & (POLLHUP|POLLNVAL))
		return LKL_DEV_NET_POLL_HUP;

	if (pfds[1].revents & POLLIN) {
		char tmp[PIPE_BUF];

		ret = read(nd_fd->pipe[0], tmp, PIPE_BUF);
		if (ret == 0)
			return LKL_DEV_NET_POLL_HUP;
		if (ret < 0)
			perror("virtio net fd pipe read");
	}

	ret = 0;

	if (pfds[0].revents & (POLLIN|POLLPRI)) {
		nd_fd->poll_rx = 0;
		ret |= LKL_DEV_NET_POLL_RX;
	}

	if (pfds[0].revents & POLLOUT) {
		nd_fd->poll_tx = 0;
		ret |= LKL_DEV_NET_POLL_TX;
	}

	return ret;
}

static int fd_net_close(struct lkl_netdev *nd)
{
	struct lkl_netdev_fd *nd_fd =
		container_of(nd, struct lkl_netdev_fd, dev);

	/* this will cause a POLLHUP in the poll function */
	close(nd_fd->pipe[1]);
	close(nd_fd->pipe[0]);
	close(nd_fd->fd);

	return 0;
}

struct lkl_dev_net_ops fd_net_ops =  {
	.tx = fd_net_tx,
	.rx = fd_net_rx,
	.poll = fd_net_poll,
	.close = fd_net_close,
};

struct lkl_netdev_fd *lkl_register_netdev_fd(int fd)
{
	struct lkl_netdev_fd *nd;

	nd = malloc(sizeof(*nd));
	if (!nd) {
		fprintf(stderr, "fdnet: failed to allocate memory\n");
		/* TODO: propagate the error state, maybe use errno for that? */
		return NULL;
	}

	memset(nd, 0, sizeof(*nd));

	nd->fd = fd;
	if (pipe(nd->pipe) < 0) {
		perror("pipe");
		lkl_unregister_netdev_fd(nd);
		return NULL;
	}

	if (fcntl(nd->pipe[0], F_SETFL, O_NONBLOCK) < 0) {
		perror("fnctl");
		close(nd->pipe[0]);
		close(nd->pipe[1]);
		lkl_unregister_netdev_fd(nd);
	}

	nd->dev.ops = &fd_net_ops;
	return nd;
}

void lkl_unregister_netdev_fd(struct lkl_netdev_fd *nd)
{
	fd_net_close(&nd->dev);
	free(nd);
}
