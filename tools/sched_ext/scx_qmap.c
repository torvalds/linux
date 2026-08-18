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
#include <sys/mman.h>
#include <sys/stat.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_qmap.h"
#include "scx_qmap.bpf.skel.h"

const char help_fmt[] =
"A simple five-level FIFO queue sched_ext scheduler.\n"
"\n"
"See the top-level comment in .bpf.c for more details.\n"
"\n"
"Usage: %s [-s SLICE_US] [-e COUNT] [-t COUNT] [-T COUNT] [-l COUNT] [-b COUNT]\n"
"       [-N COUNT] [-P] [-M] [-H] [-c CG_PATH] [-d PID] [-D LEN] [-S] [-p] [-I]\n"
"       [-F COUNT] [-v]\n"
"\n"
"  -s SLICE_US   Override slice duration\n"
"  -e COUNT      Trigger scx_bpf_error() after COUNT enqueues\n"
"  -t COUNT      Stall every COUNT'th user thread\n"
"  -T COUNT      Stall every COUNT'th kernel thread\n"
"  -N COUNT      Size of the task_ctx arena slab (default 16384)\n"
"  -l COUNT      Trigger dispatch infinite looping after COUNT dispatches\n"
"  -b COUNT      Dispatch upto COUNT tasks together\n"
"  -P            Print out DSQ content and event counters to trace_pipe every second\n"
"  -M            Print out debug messages to trace_pipe\n"
"  -H            Boost nice -20 tasks in SHARED_DSQ, use with -b\n"
"  -c CG_PATH    Cgroup path to attach as sub-scheduler, must run parent scheduler first\n"
"  -d PID        Disallow a process from switching into SCHED_EXT (-1 for self)\n"
"  -D LEN        Set scx_exit_info.dump buffer length\n"
"  -S            Suppress qmap-specific debug dump\n"
"  -p            Switch only tasks on SCHED_EXT policy instead of all\n"
"  -I            Turn on SCX_OPS_ALWAYS_ENQ_IMMED\n"
"  -F COUNT      IMMED stress: force every COUNT'th enqueue to a busy local DSQ (use with -I)\n"
"  -C MODE       cid-override test (shuffle|bad-dup|bad-range)\n"
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
	struct qmap_arena *qa;
	__u32 test_error_cnt = 0;
	__u64 ecode;
	int opt;

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	if (libbpf_num_possible_cpus() > SCX_QMAP_MAX_CPUS) {
		fprintf(stderr,
			"scx_qmap: %d possible CPUs exceeds compile-time cap %d; "
			"rebuild with larger SCX_QMAP_MAX_CPUS\n",
			libbpf_num_possible_cpus(), SCX_QMAP_MAX_CPUS);
		return 1;
	}
restart:
	optind = 1;
	skel = SCX_OPS_OPEN(qmap_ops, scx_qmap);

	skel->rodata->slice_ns = __COMPAT_ENUM_OR_ZERO("scx_public_consts", "SCX_SLICE_DFL");
	skel->rodata->max_tasks = 16384;

	while ((opt = getopt(argc, argv, "s:e:t:T:l:b:N:PMHc:d:D:SpIF:C:vh")) != -1) {
		switch (opt) {
		case 's':
			skel->rodata->slice_ns = strtoull(optarg, NULL, 0) * 1000;
			break;
		case 'e':
			test_error_cnt = strtoul(optarg, NULL, 0);
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
		case 'N':
			skel->rodata->max_tasks = strtoul(optarg, NULL, 0);
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
		case 'c': {
			struct stat st;
			if (stat(optarg, &st) < 0) {
				perror("stat");
				return 1;
			}
			skel->struct_ops.qmap_ops->sub_cgroup_id = st.st_ino;
			skel->rodata->sub_cgroup_id = st.st_ino;
			break;
		}
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
		case 'I':
			skel->rodata->always_enq_immed = true;
			skel->struct_ops.qmap_ops->flags |= SCX_OPS_ALWAYS_ENQ_IMMED;
			break;
		case 'F':
			skel->rodata->immed_stress_nth = strtoul(optarg, NULL, 0);
			break;
		case 'C': {
			u32 nr_cpus = libbpf_num_possible_cpus();
			u32 mode, i;

			if (!strcmp(optarg, "shuffle"))
				mode = 1;
			else if (!strcmp(optarg, "bad-dup"))
				mode = 2;
			else if (!strcmp(optarg, "bad-range"))
				mode = 3;
			else {
				fprintf(stderr, "unknown cid-override mode '%s'\n", optarg);
				return 1;
			}
			skel->rodata->cid_override_mode = mode;

			/* shuffle: reversed cpu_to_cid, bad-dup: dup cid 0, bad-range: identity */
			for (i = 0; i < nr_cpus; i++) {
				if (mode == 1)
					skel->bss->cid_override_cpu_to_cid[i] = nr_cpus - 1 - i;
				else
					skel->bss->cid_override_cpu_to_cid[i] = i;
			}
			if (mode == 2 && nr_cpus >= 2)
				skel->bss->cid_override_cpu_to_cid[1] = 0;
			if (mode == 3)
				skel->bss->cid_override_cpu_to_cid[0] = (s32)nr_cpus;
			break;
		}
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

	qa = &skel->arena->qa;
	qa->test_error_cnt = test_error_cnt;

	while (!exit_req && !UEI_EXITED(skel, uei)) {
		long nr_enqueued = qa->nr_enqueued;
		long nr_dispatched = qa->nr_dispatched;

		printf("stats  : enq=%lu dsp=%lu delta=%ld reenq/cid0=%llu/%llu deq=%llu core=%llu enq_ddsp=%llu\n",
		       nr_enqueued, nr_dispatched, nr_enqueued - nr_dispatched,
		       qa->nr_reenqueued, qa->nr_reenqueued_cid0,
		       qa->nr_dequeued,
		       qa->nr_core_sched_execed,
		       qa->nr_ddsp_from_enq);
		printf("         exp_local=%llu exp_remote=%llu exp_timer=%llu exp_lost=%llu\n",
		       qa->nr_expedited_local,
		       qa->nr_expedited_remote,
		       qa->nr_expedited_from_timer,
		       qa->nr_expedited_lost);
		if (__COMPAT_has_ksym("scx_bpf_cidperf_cur"))
			printf("cpuperf: cur min/avg/max=%u/%u/%u target min/avg/max=%u/%u/%u\n",
			       qa->cpuperf_min,
			       qa->cpuperf_avg,
			       qa->cpuperf_max,
			       qa->cpuperf_target_min,
			       qa->cpuperf_target_avg,
			       qa->cpuperf_target_max);
		fflush(stdout);
		sleep(1);
	}

	bpf_link__destroy(link);
	ecode = UEI_REPORT(skel, uei);
	scx_qmap__destroy(skel);

	if (UEI_ECODE_RESTART(ecode))
		goto restart;
	return 0;
}
