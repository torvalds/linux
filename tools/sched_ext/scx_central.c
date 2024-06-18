/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <libgen.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_central.bpf.skel.h"

const char help_fmt[] =
"A central FIFO sched_ext scheduler.\n"
"\n"
"See the top-level comment in .bpf.c for more details.\n"
"\n"
"Usage: %s [-s SLICE_US] [-c CPU]\n"
"\n"
"  -s SLICE_US   Override slice duration\n"
"  -c CPU        Override the central CPU (default: 0)\n"
"  -v            Print libbpf debug messages\n"
"  -h            Display this help and exit\n";

static bool verbose;
static volatile int exit_req;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG && !verbose)
		return 0;
	return vfprintf(stderr, format, args);
}

static void sigint_handler(int dummy)
{
	exit_req = 1;
}

int main(int argc, char **argv)
{
	struct scx_central *skel;
	struct bpf_link *link;
	__u64 seq = 0;
	__s32 opt;

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	skel = SCX_OPS_OPEN(central_ops, scx_central);

	skel->rodata->central_cpu = 0;
	skel->rodata->nr_cpu_ids = libbpf_num_possible_cpus();

	while ((opt = getopt(argc, argv, "s:c:pvh")) != -1) {
		switch (opt) {
		case 's':
			skel->rodata->slice_ns = strtoull(optarg, NULL, 0) * 1000;
			break;
		case 'c':
			skel->rodata->central_cpu = strtoul(optarg, NULL, 0);
			break;
		case 'v':
			verbose = true;
			break;
		default:
			fprintf(stderr, help_fmt, basename(argv[0]));
			return opt != 'h';
		}
	}

	/* Resize arrays so their element count is equal to cpu count. */
	RESIZE_ARRAY(skel, data, cpu_gimme_task, skel->rodata->nr_cpu_ids);

	SCX_OPS_LOAD(skel, central_ops, scx_central, uei);
	link = SCX_OPS_ATTACH(skel, central_ops, scx_central);

	while (!exit_req && !UEI_EXITED(skel, uei)) {
		printf("[SEQ %llu]\n", seq++);
		printf("total   :%10" PRIu64 "    local:%10" PRIu64 "   queued:%10" PRIu64 "  lost:%10" PRIu64 "\n",
		       skel->bss->nr_total,
		       skel->bss->nr_locals,
		       skel->bss->nr_queued,
		       skel->bss->nr_lost_pids);
		printf("                    dispatch:%10" PRIu64 " mismatch:%10" PRIu64 " retry:%10" PRIu64 "\n",
		       skel->bss->nr_dispatches,
		       skel->bss->nr_mismatches,
		       skel->bss->nr_retries);
		printf("overflow:%10" PRIu64 "\n",
		       skel->bss->nr_overflows);
		fflush(stdout);
		sleep(1);
	}

	bpf_link__destroy(link);
	UEI_REPORT(skel, uei);
	scx_central__destroy(skel);
	return 0;
}
