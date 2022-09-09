// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016 John Fastabend <john.r.fastabend@intel.com>
 */
static const char *__doc__ =
"XDP redirect tool, using bpf_redirect helper\n"
"Usage: xdp_redirect <IFINDEX|IFNAME>_IN <IFINDEX|IFNAME>_OUT\n";

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
#include "xdp_redirect.skel.h"

static int mask = SAMPLE_RX_CNT | SAMPLE_REDIRECT_ERR_CNT |
		  SAMPLE_EXCEPTION_CNT | SAMPLE_DEVMAP_XMIT_CNT_MULTI;

DEFINE_SAMPLE_INIT(xdp_redirect);

static const struct option long_options[] = {
	{"help",	no_argument,		NULL, 'h' },
	{"skb-mode",	no_argument,		NULL, 'S' },
	{"force",	no_argument,		NULL, 'F' },
	{"stats",	no_argument,		NULL, 's' },
	{"interval",	required_argument,	NULL, 'i' },
	{"verbose",	no_argument,		NULL, 'v' },
	{}
};

int main(int argc, char **argv)
{
	int ifindex_in, ifindex_out, opt;
	char str[2 * IF_NAMESIZE + 1];
	char ifname_out[IF_NAMESIZE];
	char ifname_in[IF_NAMESIZE];
	int ret = EXIT_FAIL_OPTION;
	unsigned long interval = 2;
	struct xdp_redirect *skel;
	bool generic = false;
	bool force = false;
	bool error = true;

	while ((opt = getopt_long(argc, argv, "hSFi:vs",
				  long_options, NULL)) != -1) {
		switch (opt) {
		case 'S':
			generic = true;
			mask &= ~(SAMPLE_DEVMAP_XMIT_CNT |
				  SAMPLE_DEVMAP_XMIT_CNT_MULTI);
			break;
		case 'F':
			force = true;
			break;
		case 'i':
			interval = strtoul(optarg, NULL, 0);
			break;
		case 'v':
			sample_switch_mode();
			break;
		case 's':
			mask |= SAMPLE_REDIRECT_CNT;
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
		return ret;
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

	skel = xdp_redirect__open();
	if (!skel) {
		fprintf(stderr, "Failed to xdp_redirect__open: %s\n", strerror(errno));
		ret = EXIT_FAIL_BPF;
		goto end;
	}

	ret = sample_init_pre_load(skel);
	if (ret < 0) {
		fprintf(stderr, "Failed to sample_init_pre_load: %s\n", strerror(-ret));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	skel->rodata->from_match[0] = ifindex_in;
	skel->rodata->to_match[0] = ifindex_out;
	skel->rodata->ifindex_out = ifindex_out;

	ret = xdp_redirect__load(skel);
	if (ret < 0) {
		fprintf(stderr, "Failed to xdp_redirect__load: %s\n", strerror(errno));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	ret = sample_init(skel, mask);
	if (ret < 0) {
		fprintf(stderr, "Failed to initialize sample: %s\n", strerror(-ret));
		ret = EXIT_FAIL;
		goto end_destroy;
	}

	ret = EXIT_FAIL_XDP;
	if (sample_install_xdp(skel->progs.xdp_redirect_prog, ifindex_in,
			       generic, force) < 0)
		goto end_destroy;

	/* Loading dummy XDP prog on out-device */
	sample_install_xdp(skel->progs.xdp_redirect_dummy_prog, ifindex_out,
			   generic, force);

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
	xdp_redirect__destroy(skel);
end:
	sample_exit(ret);
}
