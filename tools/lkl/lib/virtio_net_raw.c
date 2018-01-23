/*
 * raw socket based virtual network interface feature for LKL
 * Copyright (c) 2015,2016 Ryo Nakamura, Hajime Tazaki
 *
 * Author: Ryo Nakamura <upa@wide.ad.jp>
 *         Hajime Tazaki <thehajime@gmail.com>
 *
 * Current implementation is linux-specific.
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <arpa/inet.h>
#ifdef __linux__
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#elif __FreeBSD__
#include <netinet/in.h>
#endif
#include <fcntl.h>

#include "virtio.h"
#include "virtio_net_fd.h"

/* since Linux 3.14 (man 7 packet) */
#ifndef PACKET_QDISC_BYPASS
#define PACKET_QDISC_BYPASS 20
#endif

struct lkl_netdev *lkl_netdev_raw_create(const char *ifname)
{
#ifdef __linux__
	int ret;
	int ifindex =  if_nametoindex(ifname);
	struct sockaddr_ll ll = {
		.sll_family = PF_PACKET,
		.sll_ifindex = ifindex,
		.sll_protocol = htons(ETH_P_ALL),
	};
	struct packet_mreq mreq = {
		.mr_type = PACKET_MR_PROMISC,
		.mr_ifindex = ifindex,
	};
#endif
	int fd, fd_flags;
#ifdef __linux__
	int val;

	if (ifindex < 0) {
		perror("if_nametoindex");
		return NULL;
	}

	fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
#elif __FreeBSD__
	fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
#endif
	if (fd < 0) {
		perror("socket");
		return NULL;
	}

#ifdef __linux__
	ret = bind(fd, (struct sockaddr *)&ll, sizeof(ll));
	if (ret) {
		perror("bind");
		close(fd);
		return NULL;
	}

	ret = setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq,
			sizeof(mreq));
	if (ret) {
		perror("PACKET_ADD_MEMBERSHIP PACKET_MR_PROMISC");
		close(fd);
		return NULL;
	}

	val = 1;
	ret = setsockopt(fd, SOL_PACKET, PACKET_QDISC_BYPASS, &val,
			 sizeof(val));
	if (ret)
		perror("PACKET_QDISC_BYPASS, ignoring");
#endif

	fd_flags = fcntl(fd, F_GETFD, NULL);
	fcntl(fd, F_SETFL, fd_flags | O_NONBLOCK);

	return lkl_register_netdev_fd(fd, fd);
}
