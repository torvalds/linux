/*
 * pipe based virtual network interface feature for LKL
 * Copyright (c) 2017,2016 Motomu Utsumi
 *
 * Author: Motomu Utsumi <motomuman@gmail.com>
 *
 * Current implementation is linux-specific.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include "virtio.h"
#include "virtio_net_fd.h"

struct lkl_netdev *lkl_netdev_pipe_create(const char *_ifname, int offload)
{
	struct lkl_netdev *nd;
	int fd_rx, fd_tx;
	char *ifname = strdup(_ifname), *ifname_rx = NULL, *ifname_tx = NULL;

	ifname_rx = strtok(ifname, "|");
	if (ifname_rx == NULL) {
		fprintf(stderr, "invalid ifname format: %s\n", ifname);
		free(ifname);
		return NULL;
	}

	ifname_tx = strtok(NULL, "|");
	if (ifname_tx == NULL) {
		fprintf(stderr, "invalid ifname format: %s\n", ifname);
		free(ifname);
		return NULL;
	}

	if (strtok(NULL, "|") != NULL) {
		fprintf(stderr, "invalid ifname format: %s\n", ifname);
		free(ifname);
		return NULL;
	}

	fd_rx = open(ifname_rx, O_RDWR|O_NONBLOCK);
	if (fd_rx < 0) {
		perror("can not open ifname_rx pipe");
		free(ifname);
		return NULL;
	}

	fd_tx = open(ifname_tx, O_RDWR|O_NONBLOCK);
	if (fd_tx < 0) {
		perror("can not open ifname_tx pipe");
		close(fd_rx);
		free(ifname);
		return NULL;
	}

	nd = lkl_register_netdev_fd(fd_rx, fd_tx);
	if (!nd) {
		perror("failed to register to.");
		close(fd_rx);
		close(fd_tx);
		free(ifname);
		return NULL;
	}

	free(ifname);
	/*
	 * To avoid mismatch with LKL otherside,
	 * we always enabled vnet hdr
	 */
	nd->has_vnet_hdr = 1;
	return nd;
}
