// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc. */
static const char *__doc__=
"XDP monitor tool, based on tracepoints\n";

static const char *__doc_err_only__=
" NOTICE: Only tracking XDP redirect errors\n"
"         Enable redirect success stats via '-s/--stats'\n"
"         (which comes with a per packet processing overhead)\n";

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <locale.h>
#include <getopt.h>
#include <net/if.h>
#include <time.h>
#include <signal.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "bpf_util.h"
#include "xdp_sample_user.h"
#include "xdp_monitor.skel.h"

static int mask = SAMPLE_REDIRECT_ERR_CNT | SAMPLE_CPUMAP_ENQUEUE_CNT |
		  SAMPLE_CPUMAP_KTHREAD_CNT | SAMPLE_EXCEPTION_CNT |
		  SAMPLE_DEVMAP_XMIT_CNT | SAMPLE_DEVMAP_XMIT_CNT_MULTI;

DEFINE_SAMPLE_INIT(xdp_monitor);

static const struct option long_options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "stats", no_argument, NULL, 's' },
	{ "interval", required_argument, NULL, 'i' },
	{ "verbose", no_argument, NULL, 'v' },
	{}
};

int main(int argc, char **argv)
{
	unsigned long interval = 2;
	int ret = EXIT_FAIL_OPTION;
	struct xdp_monitor *skel;
	bool errors_only = true;
	int longindex = 0, opt;
	bool error = true;

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "si:vh",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 's':
			errors_only = false;
			mask |= SAMPLE_REDIRECT_CNT;
			break;
		case 'i':
			interval = strtoul(optarg, NULL, 0);
			break;
		case 'v':
			sample_switch_mode();
			break;
		case 'h':
			error = false;
		default:
			sample_usage(argv, long_options, __doc__, mask, error);
			return ret;
		}
	}

	skel = xdp_monitor__open();
	if (!skel) {
		fprintf(stderr, "Failed to xdp_monitor__open: %s\n",
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

	ret = xdp_monitor__load(skel);
	if (ret < 0) {
		fprintf(stderr, "Failed to xdp_monitor__load: %s\n", strerror(errno));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	ret = sample_init(skel, mask);
	if (ret < 0) {
		fprintf(stderr, "Failed to initialize sample: %s\n", strerror(-ret));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	if (errors_only)
		printf("%s", __doc_err_only__);

	ret = sample_run(interval, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed during sample run: %s\n", strerror(-ret));
		ret = EXIT_FAIL;
		goto end_destroy;
	}
	ret = EXIT_OK;
end_destroy:
	xdp_monitor__destroy(skel);
end:
	sample_exit(ret);
}
