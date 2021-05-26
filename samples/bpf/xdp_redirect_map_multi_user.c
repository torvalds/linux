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
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "bpf_util.h"
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#define MAX_IFACE_NUM 32

static __u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;
static int ifaces[MAX_IFACE_NUM] = {};
static int rxcnt_map_fd;

static void int_exit(int sig)
{
	__u32 prog_id = 0;
	int i;

	for (i = 0; ifaces[i] > 0; i++) {
		if (bpf_get_link_xdp_id(ifaces[i], &prog_id, xdp_flags)) {
			printf("bpf_get_link_xdp_id failed\n");
			exit(1);
		}
		if (prog_id)
			bpf_set_link_xdp_fd(ifaces[i], -1, xdp_flags);
	}

	exit(0);
}

static void poll_stats(int interval)
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
			printf("Forwarding %10llu pkt/s\n", sum / interval);
		memcpy(prev, values, sizeof(values));
	}
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

static int update_mac_map(struct bpf_object *obj)
{
	int i, ret = -1, mac_map_fd;
	unsigned char mac_addr[6];
	unsigned int ifindex;

	mac_map_fd = bpf_object__find_map_fd_by_name(obj, "mac_map");
	if (mac_map_fd < 0) {
		printf("find mac map fd failed\n");
		return ret;
	}

	for (i = 0; ifaces[i] > 0; i++) {
		ifindex = ifaces[i];

		ret = get_mac_addr(ifindex, mac_addr);
		if (ret < 0) {
			printf("get interface %d mac failed\n", ifindex);
			return ret;
		}

		ret = bpf_map_update_elem(mac_map_fd, &ifindex, mac_addr, 0);
		if (ret) {
			perror("bpf_update_elem mac_map_fd");
			return ret;
		}
	}

	return 0;
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
	int i, ret, opt, forward_map_fd, max_ifindex = 0;
	struct bpf_program *ingress_prog, *egress_prog;
	int ingress_prog_fd, egress_prog_fd = 0;
	struct bpf_devmap_val devmap_val;
	bool attach_egress_prog = false;
	char ifname[IF_NAMESIZE];
	struct bpf_map *mac_map;
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
		return 1;
	}

	if (optind == argc) {
		printf("usage: %s <IFNAME|IFINDEX> <IFNAME|IFINDEX> ...\n", argv[0]);
		return 1;
	}

	printf("Get interfaces");
	for (i = 0; i < MAX_IFACE_NUM && argv[optind + i]; i++) {
		ifaces[i] = if_nametoindex(argv[optind + i]);
		if (!ifaces[i])
			ifaces[i] = strtoul(argv[optind + i], NULL, 0);
		if (!if_indextoname(ifaces[i], ifname)) {
			perror("Invalid interface name or i");
			return 1;
		}

		/* Find the largest index number */
		if (ifaces[i] > max_ifindex)
			max_ifindex = ifaces[i];

		printf(" %d", ifaces[i]);
	}
	printf("\n");

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	obj = bpf_object__open(filename);
	if (libbpf_get_error(obj)) {
		printf("ERROR: opening BPF object file failed\n");
		obj = NULL;
		goto err_out;
	}

	/* Reset the map size to max ifindex + 1 */
	if (attach_egress_prog) {
		mac_map = bpf_object__find_map_by_name(obj, "mac_map");
		ret = bpf_map__resize(mac_map, max_ifindex + 1);
		if (ret < 0) {
			printf("ERROR: reset mac map size failed\n");
			goto err_out;
		}
	}

	/* load BPF program */
	if (bpf_object__load(obj)) {
		printf("ERROR: loading BPF object file failed\n");
		goto err_out;
	}

	if (xdp_flags & XDP_FLAGS_SKB_MODE) {
		ingress_prog = bpf_object__find_program_by_name(obj, "xdp_redirect_map_general");
		forward_map_fd = bpf_object__find_map_fd_by_name(obj, "forward_map_general");
	} else {
		ingress_prog = bpf_object__find_program_by_name(obj, "xdp_redirect_map_native");
		forward_map_fd = bpf_object__find_map_fd_by_name(obj, "forward_map_native");
	}
	if (!ingress_prog || forward_map_fd < 0) {
		printf("finding ingress_prog/forward_map in obj file failed\n");
		goto err_out;
	}

	ingress_prog_fd = bpf_program__fd(ingress_prog);
	if (ingress_prog_fd < 0) {
		printf("find ingress_prog fd failed\n");
		goto err_out;
	}

	rxcnt_map_fd = bpf_object__find_map_fd_by_name(obj, "rxcnt");
	if (rxcnt_map_fd < 0) {
		printf("bpf_object__find_map_fd_by_name failed\n");
		goto err_out;
	}

	if (attach_egress_prog) {
		/* Update mac_map with all egress interfaces' mac addr */
		if (update_mac_map(obj) < 0) {
			printf("Error: update mac map failed");
			goto err_out;
		}

		/* Find egress prog fd */
		egress_prog = bpf_object__find_program_by_name(obj, "xdp_devmap_prog");
		if (!egress_prog) {
			printf("finding egress_prog in obj file failed\n");
			goto err_out;
		}
		egress_prog_fd = bpf_program__fd(egress_prog);
		if (egress_prog_fd < 0) {
			printf("find egress_prog fd failed\n");
			goto err_out;
		}
	}

	/* Remove attached program when program is interrupted or killed */
	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	/* Init forward multicast groups */
	for (i = 0; ifaces[i] > 0; i++) {
		ifindex = ifaces[i];

		/* bind prog_fd to each interface */
		ret = bpf_set_link_xdp_fd(ifindex, ingress_prog_fd, xdp_flags);
		if (ret) {
			printf("Set xdp fd failed on %d\n", ifindex);
			goto err_out;
		}

		/* Add all the interfaces to forward group and attach
		 * egress devmap programe if exist
		 */
		devmap_val.ifindex = ifindex;
		devmap_val.bpf_prog.fd = egress_prog_fd;
		ret = bpf_map_update_elem(forward_map_fd, &ifindex, &devmap_val, 0);
		if (ret) {
			perror("bpf_map_update_elem forward_map");
			goto err_out;
		}
	}

	poll_stats(2);

	return 0;

err_out:
	return 1;
}
