// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Covalent IO, Inc. http://covalent.io
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
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "bpf_util.h"
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

static int ifindex_in;
static int ifindex_out;
static bool ifindex_out_xdp_dummy_attached = true;
static bool xdp_devmap_attached;
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

static int get_mac_addr(unsigned int ifindex_out, void *mac_addr)
{
	char ifname[IF_NAMESIZE];
	struct ifreq ifr;
	int fd, ret = -1;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return ret;

	if (!if_indextoname(ifindex_out, ifname))
		goto err_out;

	strcpy(ifr.ifr_name, ifname);

	if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0)
		goto err_out;

	memcpy(mac_addr, ifr.ifr_hwaddr.sa_data, 6 * sizeof(char));
	ret = 0;

err_out:
	close(fd);
	return ret;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [OPTS] <IFNAME|IFINDEX>_IN <IFNAME|IFINDEX>_OUT\n\n"
		"OPTS:\n"
		"    -S    use skb-mode\n"
		"    -N    enforce native mode\n"
		"    -F    force loading prog\n"
		"    -X    load xdp program on egress\n",
		prog);
}

int main(int argc, char **argv)
{
	struct bpf_prog_load_attr prog_load_attr = {
		.prog_type	= BPF_PROG_TYPE_UNSPEC,
	};
	struct bpf_program *prog, *dummy_prog, *devmap_prog;
	int prog_fd, dummy_prog_fd, devmap_prog_fd = 0;
	int tx_port_map_fd, tx_mac_map_fd;
	struct bpf_devmap_val devmap_val;
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	const char *optstr = "FSNX";
	struct bpf_object *obj;
	int ret, opt, key = 0;
	char filename[256];

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
		case 'X':
			xdp_devmap_attached = true;
			break;
		default:
			usage(basename(argv[0]));
			return 1;
		}
	}

	if (!(xdp_flags & XDP_FLAGS_SKB_MODE)) {
		xdp_flags |= XDP_FLAGS_DRV_MODE;
	} else if (xdp_devmap_attached) {
		printf("Load xdp program on egress with SKB mode not supported yet\n");
		return 1;
	}

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

	if (xdp_flags & XDP_FLAGS_SKB_MODE) {
		prog = bpf_object__find_program_by_name(obj, "xdp_redirect_map_general");
		tx_port_map_fd = bpf_object__find_map_fd_by_name(obj, "tx_port_general");
	} else {
		prog = bpf_object__find_program_by_name(obj, "xdp_redirect_map_native");
		tx_port_map_fd = bpf_object__find_map_fd_by_name(obj, "tx_port_native");
	}
	dummy_prog = bpf_object__find_program_by_name(obj, "xdp_redirect_dummy_prog");
	if (!prog || dummy_prog < 0 || tx_port_map_fd < 0) {
		printf("finding prog/dummy_prog/tx_port_map in obj file failed\n");
		goto out;
	}
	prog_fd = bpf_program__fd(prog);
	dummy_prog_fd = bpf_program__fd(dummy_prog);
	if (prog_fd < 0 || dummy_prog_fd < 0 || tx_port_map_fd < 0) {
		printf("bpf_prog_load_xattr: %s\n", strerror(errno));
		return 1;
	}

	tx_mac_map_fd = bpf_object__find_map_fd_by_name(obj, "tx_mac");
	rxcnt_map_fd = bpf_object__find_map_fd_by_name(obj, "rxcnt");
	if (tx_mac_map_fd < 0 || rxcnt_map_fd < 0) {
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

	/* Load 2nd xdp prog on egress. */
	if (xdp_devmap_attached) {
		unsigned char mac_addr[6];

		devmap_prog = bpf_object__find_program_by_name(obj, "xdp_redirect_map_egress");
		if (!devmap_prog) {
			printf("finding devmap_prog in obj file failed\n");
			goto out;
		}
		devmap_prog_fd = bpf_program__fd(devmap_prog);
		if (devmap_prog_fd < 0) {
			printf("finding devmap_prog fd failed\n");
			goto out;
		}

		if (get_mac_addr(ifindex_out, mac_addr) < 0) {
			printf("get interface %d mac failed\n", ifindex_out);
			goto out;
		}

		ret = bpf_map_update_elem(tx_mac_map_fd, &key, mac_addr, 0);
		if (ret) {
			perror("bpf_update_elem tx_mac_map_fd");
			goto out;
		}
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	devmap_val.ifindex = ifindex_out;
	devmap_val.bpf_prog.fd = devmap_prog_fd;
	ret = bpf_map_update_elem(tx_port_map_fd, &key, &devmap_val, 0);
	if (ret) {
		perror("bpf_update_elem");
		goto out;
	}

	poll_stats(2, ifindex_out);

out:
	return 0;
}
