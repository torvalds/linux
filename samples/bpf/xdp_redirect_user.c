// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016 John Fastabend <john.r.fastabend@intel.com>
 */
#include <linux/bpf.h>
#include <linux/if_link.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <net/if.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/resource.h>

#include "bpf_util.h"
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

static int ifindex_in;
static int ifindex_out;
static bool ifindex_out_xdp_dummy_attached = true;
static __u32 prog_id;
static __u32 dummy_prog_id;

static __u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;
static int rxcnt_map_fd;

static void int_exit(int sig)
{
	__u32 curr_prog_id = 0;

	if (bpf_get_link_xdp_id(ifindex_in, &curr_prog_id, xdp_flags)) {
		printf("bpf_get_link_xdp_id failed\n");
		exit(1);
	}
	if (prog_id == curr_prog_id)
		bpf_set_link_xdp_fd(ifindex_in, -1, xdp_flags);
	else if (!curr_prog_id)
		printf("couldn't find a prog id on iface IN\n");
	else
		printf("program on iface IN changed, not removing\n");

	if (ifindex_out_xdp_dummy_attached) {
		curr_prog_id = 0;
		if (bpf_get_link_xdp_id(ifindex_out, &curr_prog_id,
					xdp_flags)) {
			printf("bpf_get_link_xdp_id failed\n");
			exit(1);
		}
		if (dummy_prog_id == curr_prog_id)
			bpf_set_link_xdp_fd(ifindex_out, -1, xdp_flags);
		else if (!curr_prog_id)
			printf("couldn't find a prog id on iface OUT\n");
		else
			printf("program on iface OUT changed, not removing\n");
	}
	exit(0);
}

static void poll_stats(int interval, int ifindex)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	__u64 values[nr_cpus], prev[nr_cpus];

	memset(prev, 0, sizeof(prev));

	while (1) {
		__u64 sum = 0;
		__u32 key = 0;
		int i;

		sleep(interval);
		assert(bpf_map_lookup_elem(rxcnt_map_fd, &key, values) == 0);
		for (i = 0; i < nr_cpus; i++)
			sum += (values[i] - prev[i]);
		if (sum)
			printf("ifindex %i: %10llu pkt/s\n",
			       ifindex, sum / interval);
		memcpy(prev, values, sizeof(values));
	}
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [OPTS] <IFNAME|IFINDEX>_IN <IFNAME|IFINDEX>_OUT\n\n"
		"OPTS:\n"
		"    -S    use skb-mode\n"
		"    -N    enforce native mode\n"
		"    -F    force loading prog\n",
		prog);
}


int main(int argc, char **argv)
{
	struct bpf_prog_load_attr prog_load_attr = {
		.prog_type	= BPF_PROG_TYPE_XDP,
	};
	struct bpf_program *prog, *dummy_prog;
	int prog_fd, tx_port_map_fd, opt;
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	const char *optstr = "FSN";
	struct bpf_object *obj;
	char filename[256];
	int dummy_prog_fd;
	int ret, key = 0;

	while ((opt = getopt(argc, argv, optstr)) != -1) {
		switch (opt) {
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
			usage(basename(argv[0]));
			return 1;
		}
	}

	if (!(xdp_flags & XDP_FLAGS_SKB_MODE))
		xdp_flags |= XDP_FLAGS_DRV_MODE;

	if (optind == argc) {
		printf("usage: %s <IFNAME|IFINDEX>_IN <IFNAME|IFINDEX>_OUT\n", argv[0]);
		return 1;
	}

	ifindex_in = if_nametoindex(argv[optind]);
	if (!ifindex_in)
		ifindex_in = strtoul(argv[optind], NULL, 0);

	ifindex_out = if_nametoindex(argv[optind + 1]);
	if (!ifindex_out)
		ifindex_out = strtoul(argv[optind + 1], NULL, 0);

	printf("input: %d output: %d\n", ifindex_in, ifindex_out);

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	prog_load_attr.file = filename;

	if (bpf_prog_load_xattr(&prog_load_attr, &obj, &prog_fd))
		return 1;

	prog = bpf_program__next(NULL, obj);
	dummy_prog = bpf_program__next(prog, obj);
	if (!prog || !dummy_prog) {
		printf("finding a prog in obj file failed\n");
		return 1;
	}
	/* bpf_prog_load_xattr gives us the pointer to first prog's fd,
	 * so we're missing only the fd for dummy prog
	 */
	dummy_prog_fd = bpf_program__fd(dummy_prog);
	if (prog_fd < 0 || dummy_prog_fd < 0) {
		printf("bpf_prog_load_xattr: %s\n", strerror(errno));
		return 1;
	}

	tx_port_map_fd = bpf_object__find_map_fd_by_name(obj, "tx_port");
	rxcnt_map_fd = bpf_object__find_map_fd_by_name(obj, "rxcnt");
	if (tx_port_map_fd < 0 || rxcnt_map_fd < 0) {
		printf("bpf_object__find_map_fd_by_name failed\n");
		return 1;
	}

	if (bpf_set_link_xdp_fd(ifindex_in, prog_fd, xdp_flags) < 0) {
		printf("ERROR: link set xdp fd failed on %d\n", ifindex_in);
		return 1;
	}

	ret = bpf_obj_get_info_by_fd(prog_fd, &info, &info_len);
	if (ret) {
		printf("can't get prog info - %s\n", strerror(errno));
		return ret;
	}
	prog_id = info.id;

	/* Loading dummy XDP prog on out-device */
	if (bpf_set_link_xdp_fd(ifindex_out, dummy_prog_fd,
			    (xdp_flags | XDP_FLAGS_UPDATE_IF_NOEXIST)) < 0) {
		printf("WARN: link set xdp fd failed on %d\n", ifindex_out);
		ifindex_out_xdp_dummy_attached = false;
	}

	memset(&info, 0, sizeof(info));
	ret = bpf_obj_get_info_by_fd(dummy_prog_fd, &info, &info_len);
	if (ret) {
		printf("can't get prog info - %s\n", strerror(errno));
		return ret;
	}
	dummy_prog_id = info.id;

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	/* bpf redirect port */
	ret = bpf_map_update_elem(tx_port_map_fd, &key, &ifindex_out, 0);
	if (ret) {
		perror("bpf_update_elem");
		goto out;
	}

	poll_stats(2, ifindex_out);

out:
	return 0;
}
