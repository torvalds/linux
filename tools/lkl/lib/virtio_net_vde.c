#ifdef CONFIG_AUTO_LKL_VIRTIO_NET_VDE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <lkl.h>
#include <lkl_host.h>

#include "virtio.h"

#include <libvdeplug.h>

struct lkl_netdev_vde {
	struct lkl_netdev dev;
	VDECONN *conn;
};

struct lkl_netdev *nuse_vif_vde_create(char *switch_path);
static int net_vde_tx(struct lkl_netdev *nd, struct iovec *iov, int cnt);
static int net_vde_rx(struct lkl_netdev *nd, struct iovec *iov, int cnt);
static int net_vde_poll_with_timeout(struct lkl_netdev *nd, int timeout);
static int net_vde_poll(struct lkl_netdev *nd);
static void net_vde_poll_hup(struct lkl_netdev *nd);
static void net_vde_free(struct lkl_netdev *nd);

struct lkl_dev_net_ops vde_net_ops = {
	.tx = net_vde_tx,
	.rx = net_vde_rx,
	.poll = net_vde_poll,
	.poll_hup = net_vde_poll_hup,
	.free = net_vde_free,
};

int net_vde_tx(struct lkl_netdev *nd, struct iovec *iov, int cnt)
{
	int ret;
	struct lkl_netdev_vde *nd_vde =
		container_of(nd, struct lkl_netdev_vde, dev);
	void *data = iov[0].iov_base;
	int len = (int)iov[0].iov_len;

	ret = vde_send(nd_vde->conn, data, len, 0);
	if (ret <= 0 && errno == EAGAIN)
		return -1;
	return ret;
}

int net_vde_rx(struct lkl_netdev *nd, struct iovec *iov, int cnt)
{
	int ret;
	struct lkl_netdev_vde *nd_vde =
		container_of(nd, struct lkl_netdev_vde, dev);
	void *data = iov[0].iov_base;
	int len = (int)iov[0].iov_len;

	/*
	 * Due to a bug in libvdeplug we have to first poll to make sure
	 * that there is data available.
	 * The correct solution would be to just use
	 *   ret = vde_recv(nd_vde->conn, data, len, MSG_DONTWAIT);
	 * This should be changed once libvdeplug is fixed.
	 */
	ret = 0;
	if (net_vde_poll_with_timeout(nd, 0) & LKL_DEV_NET_POLL_RX)
		ret = vde_recv(nd_vde->conn, data, len, 0);
	if (ret <= 0)
		return -1;
	return ret;
}

int net_vde_poll_with_timeout(struct lkl_netdev *nd, int timeout)
{
	int ret;
	struct lkl_netdev_vde *nd_vde =
		container_of(nd, struct lkl_netdev_vde, dev);
	struct pollfd pollfds[] = {
			{
					.fd = vde_datafd(nd_vde->conn),
					.events = POLLIN | POLLOUT,
			},
			{
					.fd = vde_ctlfd(nd_vde->conn),
					.events = POLLHUP | POLLIN
			}
	};

	while (poll(pollfds, 2, timeout) < 0 && errno == EINTR)
		;

	ret = 0;

	if (pollfds[1].revents & (POLLHUP | POLLNVAL | POLLIN))
		return LKL_DEV_NET_POLL_HUP;
	if (pollfds[0].revents & (POLLHUP | POLLNVAL))
		return LKL_DEV_NET_POLL_HUP;

	if (pollfds[0].revents & POLLIN)
		ret |= LKL_DEV_NET_POLL_RX;
	if (pollfds[0].revents & POLLOUT)
		ret |= LKL_DEV_NET_POLL_TX;

	return ret;
}

int net_vde_poll(struct lkl_netdev *nd)
{
	return net_vde_poll_with_timeout(nd, -1);
}

void net_vde_poll_hup(struct lkl_netdev *nd)
{
	struct lkl_netdev_vde *nd_vde =
		container_of(nd, struct lkl_netdev_vde, dev);

	vde_close(nd_vde->conn);
}

void net_vde_free(struct lkl_netdev *nd)
{
	struct lkl_netdev_vde *nd_vde =
		container_of(nd, struct lkl_netdev_vde, dev);

	free(nd_vde);
}

struct lkl_netdev *lkl_netdev_vde_create(char const *switch_path)
{
	struct lkl_netdev_vde *nd;
	struct vde_open_args open_args = {.port = 0, .group = 0, .mode = 0700 };
	char *switch_path_copy = 0;

	nd = malloc(sizeof(*nd));
	if (!nd) {
		fprintf(stderr, "Failed to allocate memory.\n");
		/* TODO: propagate the error state, maybe use errno? */
		return 0;
	}
	nd->dev.ops = &vde_net_ops;

	/* vde_open() allows the null pointer as path which means
	 * "VDE default path"
	 */
	if (switch_path != 0) {
		/* vde_open() takes a non-const char * which is a bug in their
		 * function declaration. Even though the implementation does not
		 * modify the string, we shouldn't just cast away the const.
		 */
		size_t switch_path_length = strlen(switch_path);

		switch_path_copy = calloc(switch_path_length + 1, sizeof(char));
		if (!switch_path_copy) {
			fprintf(stderr, "Failed to allocate memory.\n");
			/* TODO: propagate the error state, maybe use errno? */
			return 0;
		}
		strncpy(switch_path_copy, switch_path, switch_path_length);
	}
	nd->conn = vde_open(switch_path_copy, "lkl-virtio-net", &open_args);
	free(switch_path_copy);
	if (nd->conn == 0) {
		fprintf(stderr, "Failed to connect to vde switch.\n");
		/* TODO: propagate the error state, maybe use errno? */
		return 0;
	}

	return &nd->dev;
}

#else /* CONFIG_AUTO_LKL_VIRTIO_NET_VDE */

struct lkl_netdev *lkl_netdev_vde_create(char const *switch_path)
{
	fprintf(stderr, "lkl: The host library was compiled without support for VDE networking. Please rebuild with VDE enabled.\n");
	return 0;
}

#endif /* CONFIG_AUTO_LKL_VIRTIO_NET_VDE */
