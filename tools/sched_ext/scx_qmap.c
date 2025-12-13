/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <libgen.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_qmap.bpf.skel.h"

const char help_fmt[] =
"A simple five-level FIFO queue sched_ext scheduler.\n"
"\n"
"See the top-level comment in .bpf.c for more details.\n"
"\n"
"Usage: %s [-s SLICE_US] [-e COUNT] [-t COUNT] [-T COUNT] [-l COUNT] [-b COUNT]\n"
"       [-P] [-M] [-d PID] [-D LEN] [-p] [-v]\n"
"\n"
"  -s SLICE_US   Override slice duration\n"
"  -e COUNT      Trigger scx_bpf_error() after COUNT enqueues\n"
"  -t COUNT      Stall every COUNT'th user thread\n"
"  -T COUNT      Stall every COUNT'th kernel thread\n"
"  -l COUNT      Trigger dispatch infinite looping after COUNT dispatches\n"
"  -b COUNT      Dispatch upto COUNT tasks together\n"
"  -P            Print out DSQ content and event counters to trace_pipe every second\n"
"  -M            Print out debug messages to trace_pipe\n"
"  -H            Boost nice -20 tasks in SHARED_DSQ, use with -b\n"
"  -d PID        Disallow a process from switching into SCHED_EXT (-1 for self)\n"
"  -D LEN        Set scx_exit_info.dump buffer length\n"
"  -S            Suppress qmap-specific debug dump\n"
"  -p            Switch only tasks on SCHED_EXT policy instead of all\n"
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
	struct scx_qmap *skel;
	struct bpf_link *link;
	int opt;

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	skel = SCX_OPS_OPEN(qmap_ops, scx_qmap);

	skel->rodata->slice_ns = __COMPAT_ENUM_OR_ZERO("scx_public_consts", "SCX_SLICE_DFL");

	while ((opt = getopt(argc, argv, "s:e:t:T:l:b:PMHd:D:Spvh")) != -1) {
		switch (opt) {
		case 's':
			skel->rodata->slice_ns = strtoull(optarg, NULL, 0) * 1000;
			break;
		case 'e':
			skel->bss->test_error_cnt = strtoul(optarg, NULL, 0);
			break;
		case 't':
			skel->rodata->stall_user_nth = strtoul(optarg, NULL, 0);
			break;
		case 'T':
			skel->rodata->stall_kernel_nth = strtoul(optarg, NULL, 0);
			break;
		case 'l':
			skel->rodata->dsp_inf_loop_after = strtoul(optarg, NULL, 0);
			break;
		case 'b':
			skel->rodata->dsp_batch = strtoul(optarg, NULL, 0);
			break;
		case 'P':
			skel->rodata->print_dsqs_and_events = true;
			break;
		case 'M':
			skel->rodata->print_msgs = true;
			break;
		case 'H':
			skel->rodata->highpri_boosting = true;
			break;
		case 'd':
			skel->rodata->disallow_tgid = strtol(optarg, NULL, 0);
			if (skel->rodata->disallow_tgid < 0)
				skel->rodata->disallow_tgid = getpid();
			break;
		case 'D':
			skel->struct_ops.qmap_ops->exit_dump_len = strtoul(optarg, NULL, 0);
			break;
		case 'S':
			skel->rodata->suppress_dump = true;
			break;
		case 'p':
			skel->struct_ops.qmap_ops->flags |= SCX_OPS_SWITCH_PARTIAL;
			break;
		case 'v':
			verbose = true;
			break;
		default:
			fprintf(stderr, help_fmt, basename(argv[0]));
			return opt != 'h';
		}
	}

	SCX_OPS_LOAD(skel, qmap_ops, scx_qmap, uei);
	link = SCX_OPS_ATTACH(skel, qmap_ops, scx_qmap);

	while (!exit_req && !UEI_EXITED(skel, uei)) {
		long nr_enqueued = skel->bss->nr_enqueued;
		long nr_dispatched = skel->bss->nr_dispatched;

		printf("stats  : enq=%lu dsp=%lu delta=%ld reenq=%"PRIu64" deq=%"PRIu64" core=%"PRIu64" enq_ddsp=%"PRIu64"\n",
		       nr_enqueued, nr_dispatched, nr_enqueued - nr_dispatched,
		       skel->bss->nr_reenqueued, skel->bss->nr_dequeued,
		       skel->bss->nr_core_sched_execed,
		       skel->bss->nr_ddsp_from_enq);
		printf("         exp_local=%"PRIu64" exp_remote=%"PRIu64" exp_timer=%"PRIu64" exp_lost=%"PRIu64"\n",
		       skel->bss->nr_expedited_local,
		       skel->bss->nr_expedited_remote,
		       skel->bss->nr_expedited_from_timer,
		       skel->bss->nr_expedited_lost);
		if (__COMPAT_has_ksym("scx_bpf_cpuperf_cur"))
			printf("cpuperf: cur min/avg/max=%u/%u/%u target min/avg/max=%u/%u/%u\n",
			       skel->bss->cpuperf_min,
			       skel->bss->cpuperf_avg,
			       skel->bss->cpuperf_max,
			       skel->bss->cpuperf_target_min,
			       skel->bss->cpuperf_target_avg,
			       skel->bss->cpuperf_target_max);
		fflush(stdout);
		sleep(1);
	}

	bpf_link__destroy(link);
	UEI_REPORT(skel, uei);
	scx_qmap__destroy(skel);
	/*
	 * scx_qmap implements ops.cpu_on/offline() and doesn't need to restart
	 * on CPU hotplug events.
	 */
	return 0;
}
