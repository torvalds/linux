/*
 * system calls hijack code
 * Copyright (c) 2015 Hajime Tazaki
 *
 * Author: Hajime Tazaki <tazaki@sfc.wide.ad.jp>
 *
 * Note: some of the code is picked from rumpkernel, written by Antti Kantee.
 */

#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#undef st_atime
#undef st_mtime
#undef st_ctime
#include <lkl.h>
#include <lkl_host.h>

#include "xlate.h"

void __attribute__((constructor(102)))
hijack_init(void)
{
	int ret, i, dev_null, nd_id = -1, nd_ifindex = -1;
	char *tap = getenv("LKL_HIJACK_NET_TAP");
	char *ip = getenv("LKL_HIJACK_NET_IP");
	char *netmask_len = getenv("LKL_HIJACK_NET_NETMASK_LEN");
	char *gateway = getenv("LKL_HIJACK_NET_GATEWAY");
	char *debug = getenv("LKL_HIJACK_DEBUG");

	if (tap) {
		struct ifreq ifr = {
			.ifr_flags = IFF_TAP | IFF_NO_PI,
		};
		union lkl_netdev nd;

		strncpy(ifr.ifr_name, tap, IFNAMSIZ);

		nd.fd = open("/dev/net/tun", O_RDWR|O_NONBLOCK);
		if (nd.fd < 0) {
			fprintf(stderr, "failed to open tap: %s\n", strerror(errno));
			goto no_tap;
		}

		ret = ioctl(nd.fd, TUNSETIFF, &ifr);
		if (ret < 0) {
			fprintf(stderr, "failed to attach to %s: %s\n",
				ifr.ifr_name, strerror(errno));
			goto no_tap;
		}

		ret = lkl_netdev_add(nd, NULL);
		if (ret < 0) {
			fprintf(stderr, "failed to add netdev: %s\n",
				lkl_strerror(ret));
			goto no_tap;
		}

		nd_id = ret;
	}

no_tap:
	if (!debug)
		lkl_host_ops.print = NULL;


	ret = lkl_start_kernel(&lkl_host_ops, 64 * 1024 * 1024, "");
	if (ret) {
		fprintf(stderr, "can't start kernel: %s\n", lkl_strerror(ret));
		return;
	}

	/* fillup FDs up to LKL_FD_OFFSET */
	ret = lkl_sys_mknod("/dev_null", LKL_S_IFCHR | 0600, LKL_MKDEV(1, 3));
	dev_null = lkl_sys_open("/dev_null", LKL_O_RDONLY, 0);
	if (dev_null < 0) {
		fprintf(stderr, "failed to open /dev/null: %s\n", lkl_strerror(dev_null));
		return;
	}

	for (i = 1; i < LKL_FD_OFFSET; i++)
		lkl_sys_dup(dev_null);

	/* lo iff_up */
	lkl_if_up(1);

	if (nd_id >= 0) {
		nd_ifindex = lkl_netdev_get_ifindex(nd_id);
		if (nd_ifindex > 0)
			lkl_if_up(nd_ifindex);
		else
			fprintf(stderr, "failed to get ifindex for netdev id %d: %s\n",
				nd_id, lkl_strerror(nd_ifindex));
	}

	if (nd_ifindex >= 0 && ip && netmask_len) {
		unsigned int addr = inet_addr(ip);
		int nmlen = atoi(netmask_len);

		if (addr != INADDR_NONE && nmlen > 0 && nmlen < 32) {
			ret = lkl_if_set_ipv4(nd_ifindex, addr, nmlen);
			if (ret < 0)
				fprintf(stderr, "failed to set IPv4 address: %s\n",
					lkl_strerror(ret));
		}
	}

	if (nd_ifindex >= 0 && gateway) {
		unsigned int addr = inet_addr(gateway);

		if (addr != INADDR_NONE) {
			ret = lkl_set_ipv4_gateway(addr);
			if (ret< 0)
				fprintf(stderr, "failed to set IPv4 gateway: %s\n",
					lkl_strerror(ret));
		}
	}
}

void __attribute__((destructor))
hijack_fini(void)
{
	int i;

	for (i = 0; i < LKL_FD_OFFSET; i++)
		lkl_sys_close(i);


	lkl_sys_halt();
}
