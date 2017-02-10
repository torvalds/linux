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

#define __USE_GNU
#include <dlfcn.h>

#define _GNU_SOURCE
#include <sched.h>

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

/* Add permanent neighbor entries in the form of "ip|mac;ip|mac;..." */
static void add_neighbor(int ifindex, char* entries) {
	char *saveptr = NULL, *token = NULL;
	char *ip = NULL, *mac_str = NULL;
	int ret = 0;
	__lkl__u8 mac[LKL_ETH_ALEN];
	char ip_addr[16];
	int af;

	for (token = strtok_r(entries, ";", &saveptr); token;
	     token = strtok_r(NULL, ";", &saveptr)) {
		ip = strtok(token, "|");
		mac_str = strtok(NULL, "|");
		if (ip == NULL || mac_str == NULL || strtok(NULL, "|") != NULL) {
			return;
		}
		af = LKL_AF_INET;
		ret = inet_pton(AF_INET, ip, ip_addr);
		if (ret == 0) {
			ret = inet_pton(AF_INET6, ip, ip_addr);
			af = LKL_AF_INET6;
		}
		if (ret != 1) {
			fprintf(stderr, "Bad ip address: %s\n", ip);
			return;
		}

		ret = parse_mac_str(mac_str, mac);
		if (ret != 1) {
			fprintf(stderr, "Failed to parse mac: %s\n", mac_str);
			return;
		}
		ret = lkl_add_neighbor(ifindex, af, ip_addr, mac);
		if (ret) {
			fprintf(stderr, "Failed to add neighbor entry: %s\n", lkl_strerror(ret));
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

static void mount_cmds_exec(char *_cmds, int (*callback)(char*))
{
	char *saveptr = NULL, *token;
	int ret = 0;
	char *cmds = strdup(_cmds);
	token = strtok_r(cmds, ",", &saveptr);

	while (token && ret >= 0) {
		ret = callback(token);
		token = strtok_r(NULL, ",", &saveptr);
	}

	if (ret < 0)
		fprintf(stderr, "mount_cmds_exec: failed parsing %s\n", _cmds);

	free(cmds);
}

static void PinToCpus(const cpu_set_t* cpus)
{
	if (sched_setaffinity(0, sizeof(cpu_set_t), cpus)) {
		perror("sched_setaffinity");
	}
}

static void PinToFirstCpu(const cpu_set_t* cpus)
{
	int j;
	cpu_set_t pinto;
	CPU_ZERO(&pinto);
	for (j = 0; j < CPU_SETSIZE; j++) {
		if (CPU_ISSET(j, cpus)) {
			lkl_printf("LKL: Pin To CPU %d\n", j);
			CPU_SET(j, &pinto);
			PinToCpus(&pinto);
			return;
		}
	}
}

int lkl_debug, lkl_running;

static int nd_id = -1;
static struct lkl_netdev *nd;

void __attribute__((constructor(102)))
hijack_init(void)
{
	int ret, i, dev_null, nd_ifindex = -1;
	/* OBSOLETE: should use IFTYPE and IFPARAMS */
	char *tap = getenv("LKL_HIJACK_NET_TAP");
	char *iftype = getenv("LKL_HIJACK_NET_IFTYPE");
	char *ifparams = getenv("LKL_HIJACK_NET_IFPARAMS");
	char *mtu_str = getenv("LKL_HIJACK_NET_MTU");
	__lkl__u8 mac[LKL_ETH_ALEN] = {0};
	char *ip = getenv("LKL_HIJACK_NET_IP");
	char *ipv6 = getenv("LKL_HIJACK_NET_IPV6");
	char *mac_str = getenv("LKL_HIJACK_NET_MAC");
	char *netmask_len = getenv("LKL_HIJACK_NET_NETMASK_LEN");
	char *netmask6_len = getenv("LKL_HIJACK_NET_NETMASK6_LEN");
	char *gateway = getenv("LKL_HIJACK_NET_GATEWAY");
	char *gateway6 = getenv("LKL_HIJACK_NET_GATEWAY6");
	char *debug = getenv("LKL_HIJACK_DEBUG");
	char *mount = getenv("LKL_HIJACK_MOUNT");
	struct lkl_netdev_args nd_args;
	char *neigh_entries = getenv("LKL_HIJACK_NET_NEIGHBOR");
	char *qdisc_entries = getenv("LKL_HIJACK_NET_QDISC");
	/* single_cpu mode:
	 * 0: Don't pin to single CPU (default).
	 * 1: Pin only LKL kernel threads to single CPU.
	 * 2: Pin all LKL threads to single CPU including all LKL kernel threads
	 * and device polling threads. Avoid this mode if having busy polling
	 * threads.
	 *
	 * mode 2 can achieve better TCP_RR but worse TCP_STREAM than mode 1.
	 * You should choose the best for your application and virtio device
	 * type.
	 */
	char *single_cpu= getenv("LKL_HIJACK_SINGLE_CPU");
	int single_cpu_mode = 0;
	cpu_set_t ori_cpu;
	char *offload1 = getenv("LKL_HIJACK_OFFLOAD");
	int offload = 0;
	char *sysctls = getenv("LKL_HIJACK_SYSCTL");
	char *boot_cmdline = getenv("LKL_HIJACK_BOOT_CMDLINE") ? : "";

	memset(&nd_args, 0, sizeof(struct lkl_netdev_args));
	if (!debug) {
		lkl_host_ops.print = NULL;
	} else {
		lkl_register_dbg_handler();
		lkl_debug = strtol(debug, NULL, 0);
	}
	if (offload1)
		offload = strtol(offload1, NULL, 0);

	if (lkl_debug & 0x200) {
		char c;

		printf("press 'enter' to continue\n");
		if (scanf("%c", &c) <= 0) {
			fprintf(stderr, "scanf() fails\n");
			return;
		}
	}
	if (single_cpu) {
		single_cpu_mode = atoi(single_cpu);
		switch (single_cpu_mode) {
			case 0:
			case 1:
			case 2: break;
			default:
				fprintf(stderr, "single cpu mode must be 0~2.\n");
				single_cpu_mode = 0;
				break;
		}
	}

	if (single_cpu_mode) {
		if (sched_getaffinity(0, sizeof(cpu_set_t), &ori_cpu)) {
			perror("sched_getaffinity");
			single_cpu_mode = 0;
		}
	}

	/* Pin to a single cpu.
	 * Any children thread created after it are pinned to the same CPU.
	 */
	if (single_cpu_mode == 2)
		PinToFirstCpu(&ori_cpu);

	if (tap) {
		fprintf(stderr,
			"WARN: variable LKL_HIJACK_NET_TAP is now obsoleted.\n"
			"      please use LKL_HIJACK_NET_IFTYPE and "
			"LKL_HIJACK_NET_IFPARAMS instead.\n");
		nd = lkl_netdev_tap_create(tap, offload);
	}

	if (!nd && iftype && ifparams) {
		if ((strcmp(iftype, "tap") == 0)) {
			nd = lkl_netdev_tap_create(ifparams, offload);
		} else if ((strcmp(iftype, "macvtap") == 0)) {
			nd = lkl_netdev_macvtap_create(ifparams, offload);
		} else {
			if (offload) {
				fprintf(stderr,
					"WARN: LKL_HIJACK_OFFLOAD is only "
					"supported on "
					"tap and macvtap devices"
					" (for now)!\n"
					"No offload features will be "
					"enabled.\n");
			}
			offload = 0;
			if (strcmp(iftype, "dpdk") == 0)
				nd = lkl_netdev_dpdk_create(ifparams);
			else if (strcmp(iftype, "vde") == 0)
				nd = lkl_netdev_vde_create(ifparams);
			else if (strcmp(iftype, "raw") == 0)
				nd = lkl_netdev_raw_create(ifparams);
		}
	}

	if (nd) {
		ret = parse_mac_str(mac_str, mac);

		if (ret < 0) {
			fprintf(stderr, "failed to parse mac\n");
			return;
		} else if (ret > 0) {
			nd_args.mac = mac;
		} else {
			nd_args.mac = NULL;
		}

		nd_args.offload = offload;
		ret = lkl_netdev_add(nd, &nd_args);

		if (ret < 0) {
			fprintf(stderr, "failed to add netdev: %s\n",
				lkl_strerror(ret));
			return;
		}
		nd_id = ret;
	}

	if (single_cpu_mode == 1)
		PinToFirstCpu(&ori_cpu);

	ret = lkl_start_kernel(&lkl_host_ops, boot_cmdline);
	if (ret) {
		fprintf(stderr, "can't start kernel: %s\n", lkl_strerror(ret));
		return;
	}

	lkl_running = 1;

	/* restore cpu affinity */
	if (single_cpu_mode)
		PinToCpus(&ori_cpu);

	ret = lkl_set_fd_limit(65535);
	if (ret)
		fprintf(stderr, "lkl_set_fd_limit failed: %s\n",
			lkl_strerror(ret));

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

	if (nd_ifindex >= 0 && ipv6 && netmask6_len) {
		struct in6_addr addr;
		unsigned int pflen = atoi(netmask6_len);

		if (inet_pton(AF_INET6, ipv6, &addr) != 1) {
			fprintf(stderr, "Invalid ipv6 addr: %s\n", ipv6);
		}  else {
			ret = lkl_if_set_ipv6(nd_ifindex, &addr, pflen);
			if (ret < 0)
				fprintf(stderr, "failed to set IPv6address: %s\n",
					lkl_strerror(ret));
		}
	}

	if (nd_ifindex >= 0 && gateway6) {
		char gw[16];

		if (inet_pton(AF_INET6, gateway6, gw) != 1) {
			fprintf(stderr, "Invalid ipv6 gateway: %s\n", gateway6);
		} else {
			ret = lkl_set_ipv6_gateway(gw);
			if (ret< 0)
				fprintf(stderr, "failed to set IPv6 gateway: %s\n",
					lkl_strerror(ret));
		}
	}

	if (mount)
		mount_cmds_exec(mount, lkl_mount_fs);

	if (nd_ifindex >= 0 && neigh_entries)
		add_neighbor(nd_ifindex, neigh_entries);

	if (nd_ifindex >= 0 && qdisc_entries)
		lkl_qdisc_parse_add(nd_ifindex, qdisc_entries);

	if (sysctls)
		lkl_sysctl_parse_write(sysctls);
}

void __attribute__((destructor))
hijack_fini(void)
{
	int i;
	char *dump = getenv("LKL_HIJACK_DUMP");
	int err;

	/* The following pauses the kernel before exiting allowing one
	 * to debug or collect stattistics/diagnosis info from it.
	 */
	if (lkl_debug & 0x100) {
		while (1)
			pause();
	}
	if (dump)
		mount_cmds_exec(dump, dump_file);

	for (i = 0; i < LKL_FD_OFFSET; i++)
		lkl_sys_close(i);

	if (nd_id >= 0)
		lkl_netdev_remove(nd_id);

	if (nd)
		lkl_netdev_free(nd);

	err = lkl_sys_halt();
	if (err)
		fprintf(stderr, "lkl_sys_halt: %s\n", lkl_strerror(err));
}
