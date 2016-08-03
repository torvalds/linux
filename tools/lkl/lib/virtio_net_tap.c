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

#define BIT(x) (1ULL << x)

struct lkl_netdev *lkl_netdev_tap_create(const char *ifname, int offload)
{
	struct lkl_netdev_linux_fdnet *nd;
	int fd, ret, tap_arg = 0;
	int vnet_hdr_sz = 0;

	struct ifreq ifr = {
		.ifr_flags = IFF_TAP | IFF_NO_PI,
	};

	if (offload & BIT(LKL_VIRTIO_NET_F_GUEST_CSUM))
		tap_arg |= TUN_F_CSUM;
	if (offload & (BIT(LKL_VIRTIO_NET_F_GUEST_TSO4) |
	    BIT(LKL_VIRTIO_NET_F_MRG_RXBUF)))
		tap_arg |= TUN_F_TSO4 | TUN_F_CSUM;

	if (tap_arg || (offload & (BIT(LKL_VIRTIO_NET_F_CSUM) |
	    BIT(LKL_VIRTIO_NET_F_HOST_TSO4)))) {
		ifr.ifr_flags |= IFF_VNET_HDR;
		vnet_hdr_sz = sizeof(struct lkl_virtio_net_hdr_v1);
	}

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
	if (vnet_hdr_sz && ioctl(fd, TUNSETVNETHDRSZ, &vnet_hdr_sz) != 0) {
		fprintf(stderr, "tap: failed to TUNSETVNETHDRSZ to %s: %s\n",
			ifr.ifr_name, strerror(errno));
		close(fd);
		return NULL;
	}
	if (ioctl(fd, TUNSETOFFLOAD, tap_arg) != 0) {
		fprintf(stderr, "tap: failed to TUNSETOFFLOAD to %s: %s\n",
			ifr.ifr_name, strerror(errno));
		close(fd);
		return NULL;
	}
	nd = lkl_register_netdev_linux_fdnet(fd);
	if (!nd) {
		perror("failed to register to.");
		close(fd);
		return NULL;
	}
	nd->dev.has_vnet_hdr = (vnet_hdr_sz != 0);
	return (struct lkl_netdev *)nd;
}
