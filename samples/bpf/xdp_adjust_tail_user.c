/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/bpf.h>
#include <linux/if_link.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#include <sys/resource.h>
#include <arpa/inet.h>
#include <netinet/ether.h>
#include <unistd.h>
#include <time.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#define STATS_INTERVAL_S 2U
#define MAX_PCKT_SIZE 600

static int ifindex = -1;
static __u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;
static __u32 prog_id;

static void int_exit(int sig)
{
	__u32 curr_prog_id = 0;

	if (ifindex > -1) {
		if (bpf_get_link_xdp_id(ifindex, &curr_prog_id, xdp_flags)) {
			printf("bpf_get_link_xdp_id failed\n");
			exit(1);
		}
		if (prog_id == curr_prog_id)
			bpf_set_link_xdp_fd(ifindex, -1, xdp_flags);
		else if (!curr_prog_id)
			printf("couldn't find a prog id on a given iface\n");
		else
			printf("program on interface changed, not removing\n");
	}
	exit(0);
}

/* simple "icmp packet too big sent" counter
 */
static void poll_stats(unsigned int map_fd, unsigned int kill_after_s)
{
	time_t started_at = time(NULL);
	__u64 value = 0;
	int key = 0;


	while (!kill_after_s || time(NULL) - started_at <= kill_after_s) {
		sleep(STATS_INTERVAL_S);

		assert(bpf_map_lookup_elem(map_fd, &key, &value) == 0);

		printf("icmp \"packet too big\" sent: %10llu pkts\n", value);
	}
}

static void usage(const char *cmd)
{
	printf("Start a XDP prog which send ICMP \"packet too big\" \n"
		"messages if ingress packet is bigger then MAX_SIZE bytes\n");
	printf("Usage: %s [...]\n", cmd);
	printf("    -i <ifname|ifindex> Interface\n");
	printf("    -T <stop-after-X-seconds> Default: 0 (forever)\n");
	printf("    -P <MAX_PCKT_SIZE> Default: %u\n", MAX_PCKT_SIZE);
	printf("    -S use skb-mode\n");
	printf("    -N enforce native mode\n");
	printf("    -F force loading prog\n");
	printf("    -h Display this help\n");
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	struct bpf_prog_load_attr prog_load_attr = {
		.prog_type	= BPF_PROG_TYPE_XDP,
	};
	unsigned char opt_flags[256] = {};
	const char *optstr = "i:T:P:SNFh";
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	unsigned int kill_after_s = 0;
	int i, prog_fd, map_fd, opt;
	struct bpf_object *obj;
	__u32 max_pckt_size = 0;
	__u32 key = 0;
	char filename[256];
	int err;

	for (i = 0; i < strlen(optstr); i++)
		if (optstr[i] != 'h' && 'a' <= optstr[i] && optstr[i] <= 'z')
			opt_flags[(unsigned char)optstr[i]] = 1;

	while ((opt = getopt(argc, argv, optstr)) != -1) {

		switch (opt) {
		case 'i':
			ifindex = if_nametoindex(optarg);
			if (!ifindex)
				ifindex = atoi(optarg);
			break;
		case 'T':
			kill_after_s = atoi(optarg);
			break;
		case 'P':
			max_pckt_size = atoi(optarg);
			break;
		case 'S':
			xdp_flags |= XDP_FLAGS_SKB_MODE;
			break;
		case 'N':
			/* default, set below */
			break;
		case 'F':
			xdp_flags &= ~XDP_FLAGS_UPDATE_IF_NOEXIST;
			break;
		default:
			usage(argv[0]);
			return 1;
		}
		opt_flags[opt] = 0;
	}

	if (!(xdp_flags & XDP_FLAGS_SKB_MODE))
		xdp_flags |= XDP_FLAGS_DRV_MODE;

	for (i = 0; i < strlen(optstr); i++) {
		if (opt_flags[(unsigned int)optstr[i]]) {
			fprintf(stderr, "Missing argument -%c\n", optstr[i]);
			usage(argv[0]);
			return 1;
		}
	}

	if (setrlimit(RLIMIT_MEMLOCK, &r)) {
		perror("setrlimit(RLIMIT_MEMLOCK, RLIM_INFINITY)");
		return 1;
	}

	if (!ifindex) {
		fprintf(stderr, "Invalid ifname\n");
		return 1;
	}

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	prog_load_attr.file = filename;

	if (bpf_prog_load_xattr(&prog_load_attr, &obj, &prog_fd))
		return 1;

	/* static global var 'max_pcktsz' is accessible from .data section */
	if (max_pckt_size) {
		map_fd = bpf_object__find_map_fd_by_name(obj, "xdp_adju.data");
		if (map_fd < 0) {
			printf("finding a max_pcktsz map in obj file failed\n");
			return 1;
		}
		bpf_map_update_elem(map_fd, &key, &max_pckt_size, BPF_ANY);
	}

	/* fetch icmpcnt map */
	map_fd = bpf_object__find_map_fd_by_name(obj, "icmpcnt");
	if (map_fd < 0) {
		printf("finding a icmpcnt map in obj file failed\n");
		return 1;
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	if (bpf_set_link_xdp_fd(ifindex, prog_fd, xdp_flags) < 0) {
		printf("link set xdp fd failed\n");
		return 1;
	}

	err = bpf_obj_get_info_by_fd(prog_fd, &info, &info_len);
	if (err) {
		printf("can't get prog info - %s\n", strerror(errno));
		return 1;
	}
	prog_id = info.id;

	poll_stats(map_fd, kill_after_s);
	int_exit(0);

	return 0;
}
