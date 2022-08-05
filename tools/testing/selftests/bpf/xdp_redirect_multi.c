// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/if_link.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "bpf_util.h"
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#define MAX_IFACE_NUM 32
#define MAX_INDEX_NUM 1024

static __u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;
static int ifaces[MAX_IFACE_NUM] = {};

static void int_exit(int sig)
{
	__u32 prog_id = 0;
	int i;

	for (i = 0; ifaces[i] > 0; i++) {
		if (bpf_xdp_query_id(ifaces[i], xdp_flags, &prog_id)) {
			printf("bpf_xdp_query_id failed\n");
			exit(1);
		}
		if (prog_id)
			bpf_xdp_detach(ifaces[i], xdp_flags, NULL);
	}

	exit(0);
}

static int get_mac_addr(unsigned int ifindex, void *mac_addr)
{
	char ifname[IF_NAMESIZE];
	struct ifreq ifr;
	int fd, ret = -1;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return ret;

	if (!if_indextoname(ifindex, ifname))
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
		"usage: %s [OPTS] <IFNAME|IFINDEX> <IFNAME|IFINDEX> ...\n"
		"OPTS:\n"
		"    -S    use skb-mode\n"
		"    -N    enforce native mode\n"
		"    -F    force loading prog\n"
		"    -X    load xdp program on egress\n",
		prog);
}

int main(int argc, char **argv)
{
	int prog_fd, group_all, mac_map;
	struct bpf_program *ingress_prog, *egress_prog;
	int i, err, ret, opt, egress_prog_fd = 0;
	struct bpf_devmap_val devmap_val;
	bool attach_egress_prog = false;
	unsigned char mac_addr[6];
	char ifname[IF_NAMESIZE];
	struct bpf_object *obj;
	unsigned int ifindex;
	char filename[256];

	while ((opt = getopt(argc, argv, "SNFX")) != -1) {
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
			attach_egress_prog = true;
			break;
		default:
			usage(basename(argv[0]));
			return 1;
		}
	}

	if (!(xdp_flags & XDP_FLAGS_SKB_MODE)) {
		xdp_flags |= XDP_FLAGS_DRV_MODE;
	} else if (attach_egress_prog) {
		printf("Load xdp program on egress with SKB mode not supported yet\n");
		goto err_out;
	}

	if (optind == argc) {
		printf("usage: %s <IFNAME|IFINDEX> <IFNAME|IFINDEX> ...\n", argv[0]);
		goto err_out;
	}

	printf("Get interfaces:");
	for (i = 0; i < MAX_IFACE_NUM && argv[optind + i]; i++) {
		ifaces[i] = if_nametoindex(argv[optind + i]);
		if (!ifaces[i])
			ifaces[i] = strtoul(argv[optind + i], NULL, 0);
		if (!if_indextoname(ifaces[i], ifname)) {
			perror("Invalid interface name or i");
			goto err_out;
		}
		if (ifaces[i] > MAX_INDEX_NUM) {
			printf(" interface index too large\n");
			goto err_out;
		}
		printf(" %d", ifaces[i]);
	}
	printf("\n");

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	obj = bpf_object__open_file(filename, NULL);
	err = libbpf_get_error(obj);
	if (err)
		goto err_out;
	err = bpf_object__load(obj);
	if (err)
		goto err_out;
	prog_fd = bpf_program__fd(bpf_object__next_program(obj, NULL));

	if (attach_egress_prog)
		group_all = bpf_object__find_map_fd_by_name(obj, "map_egress");
	else
		group_all = bpf_object__find_map_fd_by_name(obj, "map_all");
	mac_map = bpf_object__find_map_fd_by_name(obj, "mac_map");

	if (group_all < 0 || mac_map < 0) {
		printf("bpf_object__find_map_fd_by_name failed\n");
		goto err_out;
	}

	if (attach_egress_prog) {
		/* Find ingress/egress prog for 2nd xdp prog */
		ingress_prog = bpf_object__find_program_by_name(obj, "xdp_redirect_map_all_prog");
		egress_prog = bpf_object__find_program_by_name(obj, "xdp_devmap_prog");
		if (!ingress_prog || !egress_prog) {
			printf("finding ingress/egress_prog in obj file failed\n");
			goto err_out;
		}
		prog_fd = bpf_program__fd(ingress_prog);
		egress_prog_fd = bpf_program__fd(egress_prog);
		if (prog_fd < 0 || egress_prog_fd < 0) {
			printf("find egress_prog fd failed\n");
			goto err_out;
		}
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	/* Init forward multicast groups and exclude group */
	for (i = 0; ifaces[i] > 0; i++) {
		ifindex = ifaces[i];

		if (attach_egress_prog) {
			ret = get_mac_addr(ifindex, mac_addr);
			if (ret < 0) {
				printf("get interface %d mac failed\n", ifindex);
				goto err_out;
			}
			ret = bpf_map_update_elem(mac_map, &ifindex, mac_addr, 0);
			if (ret) {
				perror("bpf_update_elem mac_map failed\n");
				goto err_out;
			}
		}

		/* Add all the interfaces to group all */
		devmap_val.ifindex = ifindex;
		devmap_val.bpf_prog.fd = egress_prog_fd;
		ret = bpf_map_update_elem(group_all, &ifindex, &devmap_val, 0);
		if (ret) {
			perror("bpf_map_update_elem");
			goto err_out;
		}

		/* bind prog_fd to each interface */
		ret = bpf_xdp_attach(ifindex, prog_fd, xdp_flags, NULL);
		if (ret) {
			printf("Set xdp fd failed on %d\n", ifindex);
			goto err_out;
		}
	}

	/* sleep some time for testing */
	sleep(999);

	return 0;

err_out:
	return 1;
}
