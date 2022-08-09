// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Covalent IO, Inc. http://covalent.io
 */
static const char *__doc__ =
"XDP redirect tool, using BPF_MAP_TYPE_DEVMAP\n"
"Usage: xdp_redirect_map <IFINDEX|IFNAME>_IN <IFINDEX|IFNAME>_OUT\n";

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
#include <getopt.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "bpf_util.h"
#include "xdp_sample_user.h"
#include "xdp_redirect_map.skel.h"

static int mask = SAMPLE_RX_CNT | SAMPLE_REDIRECT_ERR_MAP_CNT |
		  SAMPLE_EXCEPTION_CNT | SAMPLE_DEVMAP_XMIT_CNT_MULTI;

DEFINE_SAMPLE_INIT(xdp_redirect_map);

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

int main(int argc, char **argv)
{
	struct bpf_devmap_val devmap_val = {};
	bool xdp_devmap_attached = false;
	struct xdp_redirect_map *skel;
	char str[2 * IF_NAMESIZE + 1];
	char ifname_out[IF_NAMESIZE];
	struct bpf_map *tx_port_map;
	char ifname_in[IF_NAMESIZE];
	int ifindex_in, ifindex_out;
	unsigned long interval = 2;
	int ret = EXIT_FAIL_OPTION;
	struct bpf_program *prog;
	bool generic = false;
	bool force = false;
	bool tried = false;
	bool error = true;
	int opt, key = 0;

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
		sample_usage(argv, long_options, __doc__, mask, true);
		goto end;
	}

	ifindex_in = if_nametoindex(argv[optind]);
	if (!ifindex_in)
		ifindex_in = strtoul(argv[optind], NULL, 0);

	ifindex_out = if_nametoindex(argv[optind + 1]);
	if (!ifindex_out)
		ifindex_out = strtoul(argv[optind + 1], NULL, 0);

	if (!ifindex_in || !ifindex_out) {
		fprintf(stderr, "Bad interface index or name\n");
		sample_usage(argv, long_options, __doc__, mask, true);
		goto end;
	}

	skel = xdp_redirect_map__open();
	if (!skel) {
		fprintf(stderr, "Failed to xdp_redirect_map__open: %s\n",
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

	/* Load 2nd xdp prog on egress. */
	if (xdp_devmap_attached) {
		ret = get_mac_addr(ifindex_out, skel->rodata->tx_mac_addr);
		if (ret < 0) {
			fprintf(stderr, "Failed to get interface %d mac address: %s\n",
				ifindex_out, strerror(-ret));
			ret = EXIT_FAIL;
			goto end_destroy;
		}
	}

	skel->rodata->from_match[0] = ifindex_in;
	skel->rodata->to_match[0] = ifindex_out;

	ret = xdp_redirect_map__load(skel);
	if (ret < 0) {
		fprintf(stderr, "Failed to xdp_redirect_map__load: %s\n",
			strerror(errno));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	ret = sample_init(skel, mask);
	if (ret < 0) {
		fprintf(stderr, "Failed to initialize sample: %s\n", strerror(-ret));
		ret = EXIT_FAIL;
		goto end_destroy;
	}

	prog = skel->progs.xdp_redirect_map_native;
	tx_port_map = skel->maps.tx_port_native;
restart:
	if (sample_install_xdp(prog, ifindex_in, generic, force) < 0) {
		/* First try with struct bpf_devmap_val as value for generic
		 * mode, then fallback to sizeof(int) for older kernels.
		 */
		fprintf(stderr,
			"Trying fallback to sizeof(int) as value_size for devmap in generic mode\n");
		if (generic && !tried) {
			prog = skel->progs.xdp_redirect_map_general;
			tx_port_map = skel->maps.tx_port_general;
			tried = true;
			goto restart;
		}
		ret = EXIT_FAIL_XDP;
		goto end_destroy;
	}

	/* Loading dummy XDP prog on out-device */
	sample_install_xdp(skel->progs.xdp_redirect_dummy_prog, ifindex_out, generic, force);

	devmap_val.ifindex = ifindex_out;
	if (xdp_devmap_attached)
		devmap_val.bpf_prog.fd = bpf_program__fd(skel->progs.xdp_redirect_map_egress);
	ret = bpf_map_update_elem(bpf_map__fd(tx_port_map), &key, &devmap_val, 0);
	if (ret < 0) {
		fprintf(stderr, "Failed to update devmap value: %s\n",
			strerror(errno));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	ret = EXIT_FAIL;
	if (!if_indextoname(ifindex_in, ifname_in)) {
		fprintf(stderr, "Failed to if_indextoname for %d: %s\n", ifindex_in,
			strerror(errno));
		goto end_destroy;
	}

	if (!if_indextoname(ifindex_out, ifname_out)) {
		fprintf(stderr, "Failed to if_indextoname for %d: %s\n", ifindex_out,
			strerror(errno));
		goto end_destroy;
	}

	safe_strncpy(str, get_driver_name(ifindex_in), sizeof(str));
	printf("Redirecting from %s (ifindex %d; driver %s) to %s (ifindex %d; driver %s)\n",
	       ifname_in, ifindex_in, str, ifname_out, ifindex_out, get_driver_name(ifindex_out));
	snprintf(str, sizeof(str), "%s->%s", ifname_in, ifname_out);

	ret = sample_run(interval, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed during sample run: %s\n", strerror(-ret));
		ret = EXIT_FAIL;
		goto end_destroy;
	}
	ret = EXIT_OK;
end_destroy:
	xdp_redirect_map__destroy(skel);
end:
	sample_exit(ret);
}
