// SPDX-License-Identifier: GPL-2.0
static const char *__doc__ =
"XDP multi redirect tool, using BPF_MAP_TYPE_DEVMAP and BPF_F_BROADCAST flag for bpf_redirect_map\n"
"Usage: xdp_redirect_map_multi <IFINDEX|IFNAME> <IFINDEX|IFNAME> ... <IFINDEX|IFNAME>\n";

#include <linux/bpf.h>
#include <linux/if_link.h>
#include <assert.h>
#include <getopt.h>
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
#include <linux/if_ether.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "bpf_util.h"
#include "xdp_sample_user.h"
#include "xdp_redirect_map_multi.skel.h"

#define MAX_IFACE_NUM 32
static int ifaces[MAX_IFACE_NUM] = {};

static int mask = SAMPLE_RX_CNT | SAMPLE_REDIRECT_ERR_MAP_CNT |
		  SAMPLE_EXCEPTION_CNT | SAMPLE_DEVMAP_XMIT_CNT |
		  SAMPLE_DEVMAP_XMIT_CNT_MULTI | SAMPLE_SKIP_HEADING;

DEFINE_SAMPLE_INIT(xdp_redirect_map_multi);

static const struct option long_options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "skb-mode", no_argument, NULL, 'S' },
	{ "force", no_argument, NULL, 'F' },
	{ "load-egress", no_argument, NULL, 'X' },
	{ "stats", no_argument, NULL, 's' },
	{ "interval", required_argument, NULL, 'i' },
	{ "verbose", no_argument, NULL, 'v' },
	{}
};

static int update_mac_map(struct bpf_map *map)
{
	int mac_map_fd = bpf_map__fd(map);
	unsigned char mac_addr[6];
	unsigned int ifindex;
	int i, ret = -1;

	for (i = 0; ifaces[i] > 0; i++) {
		ifindex = ifaces[i];

		ret = get_mac_addr(ifindex, mac_addr);
		if (ret < 0) {
			fprintf(stderr, "get interface %d mac failed\n",
				ifindex);
			return ret;
		}

		ret = bpf_map_update_elem(mac_map_fd, &ifindex, mac_addr, 0);
		if (ret < 0) {
			fprintf(stderr, "Failed to update mac address for ifindex %d\n",
				ifindex);
			return ret;
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct bpf_devmap_val devmap_val = {};
	struct xdp_redirect_map_multi *skel;
	struct bpf_program *ingress_prog;
	bool xdp_devmap_attached = false;
	struct bpf_map *forward_map;
	int ret = EXIT_FAIL_OPTION;
	unsigned long interval = 2;
	char ifname[IF_NAMESIZE];
	unsigned int ifindex;
	bool generic = false;
	bool force = false;
	bool tried = false;
	bool error = true;
	int i, opt;

	while ((opt = getopt_long(argc, argv, "hSFXi:vs",
				  long_options, NULL)) != -1) {
		switch (opt) {
		case 'S':
			generic = true;
			/* devmap_xmit tracepoint not available */
			mask &= ~(SAMPLE_DEVMAP_XMIT_CNT |
				  SAMPLE_DEVMAP_XMIT_CNT_MULTI);
			break;
		case 'F':
			force = true;
			break;
		case 'X':
			xdp_devmap_attached = true;
			break;
		case 'i':
			interval = strtoul(optarg, NULL, 0);
			break;
		case 'v':
			sample_switch_mode();
			break;
		case 's':
			mask |= SAMPLE_REDIRECT_MAP_CNT;
			break;
		case 'h':
			error = false;
		default:
			sample_usage(argv, long_options, __doc__, mask, error);
			return ret;
		}
	}

	if (argc <= optind + 1) {
		sample_usage(argv, long_options, __doc__, mask, error);
		return ret;
	}

	skel = xdp_redirect_map_multi__open();
	if (!skel) {
		fprintf(stderr, "Failed to xdp_redirect_map_multi__open: %s\n",
			strerror(errno));
		ret = EXIT_FAIL_BPF;
		goto end;
	}

	ret = sample_init_pre_load(skel);
	if (ret < 0) {
		fprintf(stderr, "Failed to sample_init_pre_load: %s\n", strerror(-ret));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	ret = EXIT_FAIL_OPTION;
	for (i = 0; i < MAX_IFACE_NUM && argv[optind + i]; i++) {
		ifaces[i] = if_nametoindex(argv[optind + i]);
		if (!ifaces[i])
			ifaces[i] = strtoul(argv[optind + i], NULL, 0);
		if (!if_indextoname(ifaces[i], ifname)) {
			fprintf(stderr, "Bad interface index or name\n");
			sample_usage(argv, long_options, __doc__, mask, true);
			goto end_destroy;
		}

		skel->rodata->from_match[i] = ifaces[i];
		skel->rodata->to_match[i] = ifaces[i];
	}

	ret = xdp_redirect_map_multi__load(skel);
	if (ret < 0) {
		fprintf(stderr, "Failed to xdp_redirect_map_multi__load: %s\n",
			strerror(errno));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	if (xdp_devmap_attached) {
		/* Update mac_map with all egress interfaces' mac addr */
		if (update_mac_map(skel->maps.mac_map) < 0) {
			fprintf(stderr, "Updating mac address failed\n");
			ret = EXIT_FAIL;
			goto end_destroy;
		}
	}

	ret = sample_init(skel, mask);
	if (ret < 0) {
		fprintf(stderr, "Failed to initialize sample: %s\n", strerror(-ret));
		ret = EXIT_FAIL;
		goto end_destroy;
	}

	ingress_prog = skel->progs.xdp_redirect_map_native;
	forward_map = skel->maps.forward_map_native;

	for (i = 0; ifaces[i] > 0; i++) {
		ifindex = ifaces[i];

		ret = EXIT_FAIL_XDP;
restart:
		/* bind prog_fd to each interface */
		if (sample_install_xdp(ingress_prog, ifindex, generic, force) < 0) {
			if (generic && !tried) {
				fprintf(stderr,
					"Trying fallback to sizeof(int) as value_size for devmap in generic mode\n");
				ingress_prog = skel->progs.xdp_redirect_map_general;
				forward_map = skel->maps.forward_map_general;
				tried = true;
				goto restart;
			}
			goto end_destroy;
		}

		/* Add all the interfaces to forward group and attach
		 * egress devmap program if exist
		 */
		devmap_val.ifindex = ifindex;
		if (xdp_devmap_attached)
			devmap_val.bpf_prog.fd = bpf_program__fd(skel->progs.xdp_devmap_prog);
		ret = bpf_map_update_elem(bpf_map__fd(forward_map), &ifindex, &devmap_val, 0);
		if (ret < 0) {
			fprintf(stderr, "Failed to update devmap value: %s\n",
				strerror(errno));
			ret = EXIT_FAIL_BPF;
			goto end_destroy;
		}
	}

	ret = sample_run(interval, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed during sample run: %s\n", strerror(-ret));
		ret = EXIT_FAIL;
		goto end_destroy;
	}
	ret = EXIT_OK;
end_destroy:
	xdp_redirect_map_multi__destroy(skel);
end:
	sample_exit(ret);
}
