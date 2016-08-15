/*
 * macvtap based virtual network interface feature for LKL
 * Copyright (c) 2016 Hajime Tazaki
 *
 * Author: Hajime Tazaki <thehajime@gmail.com>
 *
 * Current implementation is linux-specific.
 */

/*
 * You need to configure host device in advance.
 *
 * sudo ip link add link eth0 name vtap0 type macvtap mode passthru
 * sudo ip link set dev vtap0 up
 * sudo chown thehajime /dev/tap22
 */

#include <net/if.h>
#include <linux/if_tun.h>

#include "virtio.h"
#include "virtio_net_fd.h"

struct lkl_netdev *lkl_netdev_macvtap_create(const char *path, int offload)
{
	struct ifreq ifr = {
		.ifr_flags = IFF_TAP | IFF_NO_PI,
	};

	return lkl_netdev_tap_init(path, offload, &ifr);
}
