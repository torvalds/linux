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
	__u64 seq = 0, ecode;
	__s32 opt;
	cpu_set_t *cpuset;

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);
restart:
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
	RESIZE_ARRAY(skel, data, cpu_started_at, skel->rodata->nr_cpu_ids);

	SCX_OPS_LOAD(skel, central_ops, scx_central, uei);

	/*
	 * Affinitize the loading thread to the central CPU, as:
	 * - That's where the BPF timer is first invoked in the BPF program.
	 * - We probably don't want this user space component to take up a core
	 *   from a task that would benefit from avoiding preemption on one of
	 *   the tickless cores.
	 *
	 * Until BPF supports pinning the timer, it's not guaranteed that it
	 * will always be invoked on the central CPU. In practice, this
	 * suffices the majority of the time.
	 */
	cpuset = CPU_ALLOC(skel->rodata->nr_cpu_ids);
	SCX_BUG_ON(!cpuset, "Failed to allocate cpuset");
	CPU_ZERO(cpuset);
	CPU_SET(skel->rodata->central_cpu, cpuset);
	SCX_BUG_ON(sched_setaffinity(0, sizeof(*cpuset), cpuset),
		   "Failed to affinitize to central CPU %d (max %d)",
		   skel->rodata->central_cpu, skel->rodata->nr_cpu_ids - 1);
	CPU_FREE(cpuset);

	link = SCX_OPS_ATTACH(skel, central_ops, scx_central);

	if (!skel->data->timer_pinned)
		printf("WARNING : BPF_F_TIMER_CPU_PIN not available, timer not pinned to central\n");

	while (!exit_req && !UEI_EXITED(skel, uei)) {
		printf("[SEQ %llu]\n", seq++);
		printf("total   :%10" PRIu64 "    local:%10" PRIu64 "   queued:%10" PRIu64 "  lost:%10" PRIu64 "\n",
		       skel->bss->nr_total,
		       skel->bss->nr_locals,
		       skel->bss->nr_queued,
		       skel->bss->nr_lost_pids);
		printf("timer   :%10" PRIu64 " dispatch:%10" PRIu64 " mismatch:%10" PRIu64 " retry:%10" PRIu64 "\n",
		       skel->bss->nr_timers,
		       skel->bss->nr_dispatches,
		       skel->bss->nr_mismatches,
		       skel->bss->nr_retries);
		printf("overflow:%10" PRIu64 "\n",
		       skel->bss->nr_overflows);
		fflush(stdout);
		sleep(1);
	}

	bpf_link__destroy(link);
	ecode = UEI_REPORT(skel, uei);
	scx_central__destroy(skel);

	if (UEI_ECODE_RESTART(ecode))
		goto restart;
	return 0;
}
