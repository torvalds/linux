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
#ifdef __linux__
#include <linux/if_tun.h>
#elif __FreeBSD__
#include <net/if_tun.h>
#endif
#include <sys/ioctl.h>

#include "virtio.h"
#include "virtio_net_fd.h"

#define BIT(x) (1ULL << x)

struct lkl_netdev *lkl_netdev_tap_init(const char *path, int offload,
				       struct ifreq *ifr)
{
	struct lkl_netdev *nd;
	int fd, vnet_hdr_sz = 0;
#ifdef __linux__
	int ret, tap_arg = 0;

	if (offload & BIT(LKL_VIRTIO_NET_F_GUEST_CSUM))
		tap_arg |= TUN_F_CSUM;
	if (offload & (BIT(LKL_VIRTIO_NET_F_GUEST_TSO4) |
	    BIT(LKL_VIRTIO_NET_F_MRG_RXBUF)))
		tap_arg |= TUN_F_TSO4 | TUN_F_CSUM;
	if (offload & (BIT(LKL_VIRTIO_NET_F_GUEST_TSO6)))
		tap_arg |= TUN_F_TSO6 | TUN_F_CSUM;

	if (tap_arg || (offload & (BIT(LKL_VIRTIO_NET_F_CSUM) |
				   BIT(LKL_VIRTIO_NET_F_HOST_TSO4) |
				   BIT(LKL_VIRTIO_NET_F_HOST_TSO6)))) {
		ifr->ifr_flags |= IFF_VNET_HDR;
		vnet_hdr_sz = sizeof(struct lkl_virtio_net_hdr_v1);
	}
#endif
	fd = open(path, O_RDWR|O_NONBLOCK);
	if (fd < 0) {
		perror("open");
		return NULL;
	}

#ifdef __linux__
	ret = ioctl(fd, TUNSETIFF, ifr);
	if (ret < 0) {
		fprintf(stderr, "%s: failed to attach to: %s\n",
			path, strerror(errno));
		close(fd);
		return NULL;
	}
	if (vnet_hdr_sz && ioctl(fd, TUNSETVNETHDRSZ, &vnet_hdr_sz) != 0) {
		fprintf(stderr, "%s: failed to TUNSETVNETHDRSZ to: %s\n",
			path, strerror(errno));
		close(fd);
		return NULL;
	}
	if (ioctl(fd, TUNSETOFFLOAD, tap_arg) != 0) {
		fprintf(stderr, "%s: failed to TUNSETOFFLOAD: %s\n",
			path, strerror(errno));
		close(fd);
		return NULL;
	}
#endif
	nd = lkl_register_netdev_fd(fd, fd);
	if (!nd) {
		perror("failed to register to.");
		close(fd);
		return NULL;
	}

	nd->has_vnet_hdr = (vnet_hdr_sz != 0);
	return nd;
}

struct lkl_netdev *lkl_netdev_tap_create(const char *ifname, int offload)
{
#ifdef __linux__
	char *path = "/dev/net/tun";
#elif __FreeBSD__
	char path[32];

	sprintf(path, "/dev/%s", ifname);
#endif

	struct ifreq ifr = {
#ifdef __linux__
		.ifr_flags = IFF_TAP | IFF_NO_PI,
#endif
	};

	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

	return lkl_netdev_tap_init(path, offload, &ifr);
}
