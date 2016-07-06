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
#include <lkl.h>
#include <lkl_host.h>

#include "xlate.h"
#include "../virtio_net_linux_fdnet.h"

#define __USE_GNU
#include <dlfcn.h>

/* Mount points are named after filesystem types so they should never
 * be longer than ~6 characters. */
#define MAX_FSTYPE_LEN 50

int parse_mac_str(char *mac_str, __lkl__u8 mac[LKL_ETH_ALEN])
{
	char delim[] = ":";
	char *saveptr = NULL, *token = NULL;
	int i = 0;
	if (!mac_str) {
		return 0;
	}

	for (token = strtok_r(mac_str, delim, &saveptr); i < LKL_ETH_ALEN; i++) {
		if (!token) {
			/* The address is too short */
			return -1;
		} else {
			mac[i] = (__lkl__u8) strtol(token, NULL, 16);
		}

		token = strtok_r(NULL, delim, &saveptr);
	}

	if (strtok_r(NULL, delim, &saveptr)) {
		/* The address is too long */
		return -1;
	}

	return 1;
}

/* Add permanent arp entries in the form of "ip|mac;ip|mac;..." */
static void add_arp(int ifindex, char* entries) {
	char *saveptr = NULL, *token = NULL;
	char *ip = NULL, *mac_str = NULL;
	int ret = 0;
	__lkl__u8 mac[LKL_ETH_ALEN];
	unsigned int ip_addr;

	for (token = strtok_r(entries, ";", &saveptr); token;
	     token = strtok_r(NULL, ";", &saveptr)) {
		ip = strtok(token, "|");
		mac_str = strtok(NULL, "|");
		if (ip == NULL || mac_str == NULL || strtok(NULL, "|") != NULL) {
			return;
		}
		ip_addr = inet_addr(ip);
		ret = parse_mac_str(mac_str, mac);
		if (ret != 1) {
			fprintf(stderr, "Failed to parse mac: %s\n", mac_str);
			return;
		}
		ret = lkl_add_arp_entry(ifindex, ip_addr, mac);
		if (ret) {
			fprintf(stderr, "Failed to add arp entry: %s\n", lkl_strerror(ret));
			return;
		}
	}
	return;
}

/* We don't have an easy way to make FILE*s out of our fds, so we
 * can't use e.g. fgets */
static int dump_file(char *path)
{
	int ret = -1, bytes_read = 0;
	char str[1024] = { 0 };
	int fd;

	fd = lkl_sys_open(path, O_RDONLY, 0);

	if (fd < 0) {
		fprintf(stderr, "dump_file lkl_sys_open %s: %s\n",
			path, lkl_strerror(fd));
		return -1;
	}

	/* Need to print this out in order to make sense of the output */
	printf("Reading from %s:\n==========\n", path);
	while ((ret = lkl_sys_read(fd, str, sizeof(str) - 1)) > 0)
		bytes_read += printf("%s", str);
	printf("==========\n");

	if (ret) {
		fprintf(stderr, "dump_file lkl_sys_read %s: %s\n",
			path, lkl_strerror(ret));
		return -1;
	}

	return 0;
}

/* For simplicity, if we want to mount a filesystem of a particular
 * type, we'll create a directory under / with the name of the type;
 * e.g. we'll have our sysfs as /sysfs */
static int mount_fs(char *fstype)
{
	char dir[MAX_FSTYPE_LEN] = "/";
	int flags = 0, ret = 0;

	strncat(dir, fstype, MAX_FSTYPE_LEN - 1);

	/* Create with regular umask */
	ret = lkl_sys_mkdir(dir, 0xff);
	if (ret) {
		fprintf(stderr, "mount_fs mkdir %s: %s\n", dir,
			lkl_strerror(ret));
		return -1;
	}

	/* We have no use for nonzero flags right now */
	ret = lkl_sys_mount(dir, dir, fstype, flags, NULL);
	if (ret) {
		fprintf(stderr, "mount_fs mount %s as %s: %s\n",
			dir, fstype, strerror(ret));
		return -1;
	}

	return 0;
}

static void mount_cmds_exec(char *_cmds, int (*callback)(char*))
{
	char *saveptr = NULL, *token;
	int ret = 0;
	char *cmds = strdup(_cmds);
	token = strtok_r(cmds, ",", &saveptr);

	while (token && !ret) {
		ret = callback(token);
		token = strtok_r(NULL, ",", &saveptr);
	}

	if (ret)
		fprintf(stderr, "mount_cmds_exec: failed parsing %s\n", _cmds);

	free(cmds);
}

void fixup_netdev_linux_fdnet_ops(void)
{
	/* It's okay if this is NULL, because then netdev close will
	 * fall back onto an uncloseable implementation. */
	lkl_netdev_linux_fdnet_ops.eventfd = dlsym(RTLD_NEXT, "eventfd");
}

void __attribute__((constructor(102)))
hijack_init(void)
{
	int ret, i, dev_null, nd_id = -1, nd_ifindex = -1;
	/* OBSOLETE: should use IFTYPE and IFPARAMS */
	char *tap = getenv("LKL_HIJACK_NET_TAP");
	char *iftype = getenv("LKL_HIJACK_NET_IFTYPE");
	char *ifparams = getenv("LKL_HIJACK_NET_IFPARAMS");
	char *mtu_str = getenv("LKL_HIJACK_NET_MTU");
	__lkl__u8 mac[LKL_ETH_ALEN] = {0};
	char *ip = getenv("LKL_HIJACK_NET_IP");
	char *mac_str = getenv("LKL_HIJACK_NET_MAC");
	char *netmask_len = getenv("LKL_HIJACK_NET_NETMASK_LEN");
	char *gateway = getenv("LKL_HIJACK_NET_GATEWAY");
	char *debug = getenv("LKL_HIJACK_DEBUG");
	char *mount = getenv("LKL_HIJACK_MOUNT");
	struct lkl_netdev *nd = NULL;
	char *arp_entries = getenv("LKL_HIJACK_NET_ARP");

	/* Must be run before lkl_netdev_tap_create */
	fixup_netdev_linux_fdnet_ops();

	if (tap) {
		fprintf(stderr,
			"WARN: variable LKL_HIJACK_NET_TAP is now obsoleted.\n"
			"      please use LKL_HIJACK_NET_IFTYPE and "
			"LKL_HIJACK_NET_IFPARAMS instead.\n");
		nd = lkl_netdev_tap_create(tap);
	}

	if (!nd && iftype && ifparams) {
		if ((strcmp(iftype, "tap") == 0))
			nd = lkl_netdev_tap_create(ifparams);
		else if (strcmp(iftype, "dpdk") == 0)
			nd = lkl_netdev_dpdk_create(ifparams);
		else if (strcmp(iftype, "vde") == 0)
			nd = lkl_netdev_vde_create(ifparams);
		else if (strcmp(iftype, "raw") == 0)
			nd = lkl_netdev_raw_create(ifparams);
	}

	if (nd) {
		ret = parse_mac_str(mac_str, mac);

		if (ret < 0) {
			fprintf(stderr, "failed to parse mac\n");
			return;
		} else if (ret > 0) {
			ret = lkl_netdev_add(nd, mac);
		} else {
			ret = lkl_netdev_add(nd, NULL);
		}

		if (ret < 0) {
			fprintf(stderr, "failed to add netdev: %s\n",
				lkl_strerror(ret));
			return;
		}
		nd_id = ret;
	}

	if (!debug)
		lkl_host_ops.print = NULL;
	else
		lkl_register_dbg_handler();

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

	if (nd_ifindex >= 0 && mtu_str) {
		int mtu = atoi(mtu_str);

		ret = lkl_if_set_mtu(nd_ifindex, mtu);
		if (ret < 0)
			fprintf(stderr, "failed to set MTU: %s\n", lkl_strerror(ret));
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

	if (mount)
		mount_cmds_exec(mount, mount_fs);

	if (nd_ifindex >=0 && arp_entries)
		add_arp(nd_ifindex, arp_entries);
}

void __attribute__((destructor))
hijack_fini(void)
{
	int i;
	char *dump = getenv("LKL_HIJACK_DUMP");

	if (dump)
		mount_cmds_exec(dump, dump_file);

	for (i = 0; i < LKL_FD_OFFSET; i++)
		lkl_sys_close(i);


	lkl_sys_halt();
}
