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
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <lkl.h>
#include <lkl_host.h>

#include "xlate.h"
#include "init.h"
#include "../config.h"

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

static int nd_id[LKL_IF_MAX];
static struct lkl_netdev *nd[LKL_IF_MAX];
static struct lkl_config *cfg;

static int config_load(void)
{
	int len, ret = -1;
	char *buf;
	int fd;
	char *path = getenv("LKL_HIJACK_CONFIG_FILE");

	cfg = (struct lkl_config *)malloc(sizeof(struct lkl_config));
	if (!cfg) {
		perror("config malloc");
		return -1;
	}
	init_config(cfg);

	ret = load_config_env(cfg);
	if (ret < 0)
		return ret;

	if (path)
		fd = open(path, O_RDONLY, 0);
	else if (access("lkl-hijack.json", R_OK) == 0)
		fd = open("lkl-hijack.json", O_RDONLY, 0);
	else
		return 0;
	if (fd < 0) {
		fprintf(stderr, "config_file open %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	len = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	if (len < 0) {
		perror("config size check (lseek)");
		return -1;
	} else if (len == 0) {
		return 0;
	}
	buf = (char *)malloc(len * sizeof(char) + 1);
	if (!buf) {
		perror("config buf malloc");
		return -1;
	}
	ret = read(fd, buf, len);
	if (ret < 0) {
		perror("config file read");
		free(buf);
		return -1;
	}
	ret = load_config_json(cfg, buf);
	free(buf);
	return ret;
}

static int lkl_hijack_netdev_create(struct lkl_config *cfg, int ifidx)
{
	int ret, offload = 0;
	struct lkl_netdev_args nd_args;
	__lkl__u8 mac[LKL_ETH_ALEN] = {0};

	if (cfg->ifoffload_str[ifidx])
		offload = strtol(cfg->ifoffload_str[ifidx], NULL, 0);
	memset(&nd_args, 0, sizeof(struct lkl_netdev_args));
	if (cfg->iftap[ifidx]) {
		fprintf(stderr, "WARN: LKL_HIJACK_NET_TAP is now obsoleted.\n");
		fprintf(stderr, "use LKL_HIJACK_NET_IFTYPE and PARAMS\n");
		nd[ifidx] = lkl_netdev_tap_create(cfg->iftap[ifidx], offload);
	}

	if (!nd[ifidx] && cfg->iftype[ifidx] && cfg->ifparams[ifidx]) {
		if ((strcmp(cfg->iftype[ifidx], "tap") == 0)) {
			nd[ifidx] =
				lkl_netdev_tap_create(cfg->ifparams[ifidx],
							offload);
		} else if ((strcmp(cfg->iftype[ifidx], "macvtap") == 0)) {
			nd[ifidx] =
				lkl_netdev_macvtap_create(cfg->ifparams[ifidx],
							offload);
		} else if ((strcmp(cfg->iftype[ifidx], "dpdk") == 0)) {
			nd[ifidx] =
				lkl_netdev_dpdk_create(cfg->ifparams[ifidx],
							offload, mac);
		} else if ((strcmp(cfg->iftype[ifidx], "pipe") == 0)) {
			nd[ifidx] =
				lkl_netdev_pipe_create(cfg->ifparams[ifidx],
							offload);
		} else {
			if (offload) {
				fprintf(stderr,
					"WARN: %s isn't supported on %s\n",
					"LKL_HIJACK_OFFLOAD",
					cfg->iftype[ifidx]);
				fprintf(stderr,
					"WARN: Disabling offload features.\n");
			}
			offload = 0;
		}
		if (strcmp(cfg->iftype[ifidx], "vde") == 0)
			nd[ifidx] = lkl_netdev_vde_create(cfg->ifparams[ifidx]);
		if (strcmp(cfg->iftype[ifidx], "raw") == 0)
			nd[ifidx] = lkl_netdev_raw_create(cfg->ifparams[ifidx]);
	}

	if (nd[ifidx]) {
		if ((mac[0] != 0) || (mac[1] != 0) ||
				(mac[2] != 0) || (mac[3] != 0) ||
				(mac[4] != 0) || (mac[5] != 0)) {
			nd_args.mac = mac;
		} else {
			ret = parse_mac_str(cfg->ifmac_str[ifidx], mac);

			if (ret < 0) {
				fprintf(stderr, "failed to parse mac\n");
				return -1;
			} else if (ret > 0) {
				nd_args.mac = mac;
			} else {
				nd_args.mac = NULL;
			}
		}

		nd_args.offload = offload;
		ret = lkl_netdev_add(nd[ifidx], &nd_args);
		if (ret < 0) {
			fprintf(stderr, "failed to add netdev: %s\n",
					lkl_strerror(ret));
			return -1;
		}
		nd_id[ifidx] = ret;
	}
	return 0;
}

static int lkl_hijack_netdev_configure(struct lkl_config *cfg, int ifidx)
{
	int ret, nd_ifindex = -1;

	if (nd_id[ifidx] >= 0) {
		nd_ifindex = lkl_netdev_get_ifindex(nd_id[ifidx]);
		if (nd_ifindex > 0)
			lkl_if_up(nd_ifindex);
		else
			fprintf(stderr,
				"failed to get ifindex for netdev id %d: %s\n",
				nd_id[ifidx], lkl_strerror(nd_ifindex));
	}

	if (nd_ifindex >= 0 && cfg->ifmtu_str[ifidx]) {
		int mtu = atoi(cfg->ifmtu_str[ifidx]);

		ret = lkl_if_set_mtu(nd_ifindex, mtu);
		if (ret < 0)
			fprintf(stderr, "failed to set MTU: %s\n",
					lkl_strerror(ret));
	}

	if (nd_ifindex >= 0 && cfg->ifip[ifidx] && cfg->ifnetmask_len[ifidx]) {
		unsigned int addr = inet_addr(cfg->ifip[ifidx]);
		int nmlen = atoi(cfg->ifnetmask_len[ifidx]);

		if (addr != INADDR_NONE && nmlen > 0 && nmlen < 32) {
			ret = lkl_if_set_ipv4(nd_ifindex, addr, nmlen);
			if (ret < 0)
				fprintf(stderr,
					"failed to set IPv4 address: %s\n",
					lkl_strerror(ret));
		}
		if (cfg->ifgateway[ifidx]) {
			unsigned int gwaddr = inet_addr(cfg->ifgateway[ifidx]);

			if (gwaddr != INADDR_NONE) {
				ret = lkl_if_set_ipv4_gateway(nd_ifindex,
						addr, nmlen, gwaddr);
				if (ret < 0)
					fprintf(stderr,
							"failed to set v4 if gw: %s\n",
							lkl_strerror(ret));
			}
		}
	}

	if (nd_ifindex >= 0 && cfg->ifipv6[ifidx] &&
			cfg->ifnetmask6_len[ifidx]) {
		struct in6_addr addr;
		unsigned int pflen = atoi(cfg->ifnetmask6_len[ifidx]);

		if (inet_pton(AF_INET6, cfg->ifipv6[ifidx], &addr) != 1) {
			fprintf(stderr, "Invalid ipv6 addr: %s\n",
					cfg->ifipv6[ifidx]);
		}  else {
			ret = lkl_if_set_ipv6(nd_ifindex, &addr, pflen);
			if (ret < 0)
				fprintf(stderr,
					"failed to set IPv6 address: %s\n",
					lkl_strerror(ret));
		}
		if (cfg->ifgateway6[ifidx]) {
			char gwaddr[16];

			if (inet_pton(AF_INET6, cfg->ifgateway6[ifidx],
								gwaddr) != 1) {
				fprintf(stderr, "Invalid ipv6 gateway: %s\n",
						cfg->ifgateway6[ifidx]);
			} else {
				ret = lkl_if_set_ipv6_gateway(nd_ifindex,
						&addr, pflen, gwaddr);
				if (ret < 0)
					fprintf(stderr,
							"failed to set v6 if gw: %s\n",
							lkl_strerror(ret));
			}
		}
	}

	if (nd_ifindex >= 0 && cfg->ifneigh_entries[ifidx])
		add_neighbor(nd_ifindex, cfg->ifneigh_entries[ifidx]);

	if (nd_ifindex >= 0 && cfg->ifqdisc_entries[ifidx])
		lkl_qdisc_parse_add(nd_ifindex, cfg->ifqdisc_entries[ifidx]);

	return 0;
}

void __attribute__((constructor))
hijack_init(void)
{
	int ret, i, dev_null;
	int single_cpu_mode = 0;
	int ifidx;
	cpu_set_t ori_cpu;

	ret = config_load();
	if (ret < 0)
		return;
	for (i = 0; i < LKL_IF_MAX; i++)
		nd_id[i] = -1;

	if (cfg->debug) {
		lkl_register_dbg_handler();
		lkl_debug = strtol(cfg->debug, NULL, 0);
	}

	if (!cfg->debug || (lkl_debug == 0))
		lkl_host_ops.print = NULL;

	if (lkl_debug & 0x200) {
		char c;

		printf("press 'enter' to continue\n");
		if (scanf("%c", &c) <= 0) {
			fprintf(stderr, "scanf() fails\n");
			return;
		}
	}
	if (cfg->single_cpu) {
		single_cpu_mode = atoi(cfg->single_cpu);
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

	for (ifidx = 0; ifidx < cfg->ifnum; ifidx++) {
		ret = lkl_hijack_netdev_create(cfg, ifidx);
		if (ret < 0)
			return;
	}

	if (single_cpu_mode == 1)
		PinToFirstCpu(&ori_cpu);

#ifdef __ANDROID__
	struct sigaction sa;

	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	if (sigaction(32, &sa, 0) == -1) {
		perror("sigaction");
		exit(1);
	}
#endif

	ret = lkl_start_kernel(&lkl_host_ops, cfg->boot_cmdline);
	if (ret) {
		fprintf(stderr, "can't start kernel: %s\n", lkl_strerror(ret));
		return;
	}

	lkl_running = 1;

	/* initialize epoll manage list */
	memset(dual_fds, -1, sizeof(int) * LKL_FD_OFFSET);

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
		fprintf(stderr, "failed to open /dev/null: %s\n",
				lkl_strerror(dev_null));
		return;
	}

	for (i = 1; i < LKL_FD_OFFSET; i++)
		lkl_sys_dup(dev_null);

	/* lo iff_up */
	lkl_if_up(1);

	for (ifidx = 0; ifidx < cfg->ifnum; ifidx++) {
		ret = lkl_hijack_netdev_configure(cfg, ifidx);
		if (ret < 0)
			return;
	}

	if (cfg->gateway) {
		unsigned int gwaddr = inet_addr(cfg->gateway);

		if (gwaddr != INADDR_NONE) {
			ret = lkl_set_ipv4_gateway(gwaddr);
			if (ret< 0)
				fprintf(stderr, "failed to set IPv4 gateway: %s\n",
					lkl_strerror(ret));
		}
	}

	if (cfg->gateway6) {
		char gw[16];

		if (inet_pton(AF_INET6, cfg->gateway6, gw) != 1) {
			fprintf(stderr, "Invalid ipv6 gateway: %s\n",
					cfg->gateway6);
		} else {
			ret = lkl_set_ipv6_gateway(gw);
			if (ret < 0)
				fprintf(stderr,
					"failed to set IPv6 gateway: %s\n",
					lkl_strerror(ret));
		}
	}

	if (cfg->mount)
		mount_cmds_exec(cfg->mount, lkl_mount_fs);

	if (cfg->sysctls)
		lkl_sysctl_parse_write(cfg->sysctls);

	/* put a delay before calling main() */
	if (cfg->delay_main) {
		unsigned long delay = strtoul(cfg->delay_main, NULL, 10);

		if (delay == ~0UL)
			fprintf(stderr, "got invalid delay_main value (%s)\n",
				cfg->delay_main);
		else {
			lkl_printf("sleeping %lu usec\n", delay);
			usleep(delay);
		}
	}
}

void __attribute__((destructor))
hijack_fini(void)
{
	int i;
	int err;

	/* The following pauses the kernel before exiting allowing one
	 * to debug or collect stattistics/diagnosis info from it.
	 */
	if (lkl_debug & 0x100) {
		while (1)
			pause();
	}

	if (cfg) {
		if (cfg->dump)
			mount_cmds_exec(cfg->dump, dump_file);
		for (i = 0; i < cfg->ifnum; i++)
			if (nd_id[i] >= 0)
				lkl_netdev_remove(nd_id[i]);
		for (i = 0; i < cfg->ifnum; i++)
			if (nd[i])
				lkl_netdev_free(nd[i]);
		clean_config(cfg);
		free(cfg);
	}
	if (!lkl_running)
		return;

	for (i = 0; i < LKL_FD_OFFSET; i++)
		lkl_sys_close(i);

	err = lkl_sys_halt();
	if (err)
		fprintf(stderr, "lkl_sys_halt: %s\n", lkl_strerror(err));
}
