/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <libgen.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_qmap.h"
#include "scx_qmap.bpf.skel.h"

const char help_fmt[] =
"A simple five-level FIFO queue sched_ext scheduler.\n"
"\n"
"It also demonstrates hierarchical sub-scheduling: a scheduler can hand some\n"
"of its cpus to a child cgroup that runs its own scheduler. Run one qmap as\n"
"the parent, then run another qmap on a child cgroup with -c to attach it\n"
"beneath the parent.\n"
"\n"
"The policy below is deliberately simplistic and the resulting behavior can\n"
"look odd. qmap is a demo: it exists to exercise every sub-scheduling\n"
"primitive the kernel offers with as little code as possible, not to schedule\n"
"well.\n"
"\n"
"A parent divides the full cpus it holds among itself and its children in\n"
"proportion to cpu.weight. The cpus left over by rounding are time-shared,\n"
"handed to each participant in turn every -R ms. A cpu a scheduler only\n"
"holds a time-share of is never handed further down, and a parent left with\n"
"no full cpu of its own shuts its children down.\n"
"\n"
"See the top-of-file comment in .bpf.c for the design.\n"
"\n"
"Usage: %s [-s SLICE_US] [-e COUNT] [-t COUNT] [-T COUNT] [-l COUNT] [-b COUNT]\n"
"       [-N COUNT] [-P] [-M] [-H] [-c CG_PATH] [-d PID] [-D LEN] [-S] [-p] [-I]\n"
"       [-F COUNT] [-i SEC] [-R MS] [-J MODE] [-v]\n"
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
"  -C MODE       cid-override test (shuffle|bad-dup|bad-range|bad-mono)\n"
"  -i SEC        Stats interval, seconds (default 5)\n"
"  -R MS         Round-robin period for time-shared cpus, ms (default 200)\n"
"  -J MODE       Fault injection (wrong-cid: dispatch to a cid not held,\n"
"                init-fail/cgrp-init-fail: fail init_task/cpuctl_init for\n"
"                \"qmfail*\" comms/cgroups)\n"
"  -B PPT        Rescue bandwidth in parts per thousand, 0 disables (root only, default 20)\n"
"  -q US         Rescue batch quantum in microseconds (root only, default 5000)\n"
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

static void invoke_flush_alloc(struct scx_qmap *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);

	bpf_prog_test_run_opts(bpf_program__fd(skel->progs.flush_alloc), &opts);
}

/* previous counter snapshots for the per-interval hier stats */
struct hier_prev {
	u64 alloc_ns[MAX_SUB_SCHEDS];
	u64 self_alloc_ns;
	u64 alloc_window_ns;
	u64 nr_dsps[MAX_SUB_SCHEDS];
	u64 nr_reenq_cap;
	u64 nr_reenq_immed;
	u64 nr_inject_attempts;
	u64 nr_rescue_dsp;
};

/* current wall-clock time as "HH:MM:SS" for the startup and interval headers */
static const char *tstamp(char *buf, size_t sz)
{
	time_t now = time(NULL);

	strftime(buf, sz, "%H:%M:%S", localtime(&now));
	return buf;
}

/* format the cids whose cid_owner[] matches @owner as "0-3,8", "-" if none */
static void format_cid_ranges(struct qmap_arena *qa, s32 owner, char *buf, size_t sz)
{
	u32 nr = qa->nr_cids, cid;
	size_t off = 0;
	s32 start = -1;

	buf[0] = '\0';
	for (cid = 0; cid <= nr; cid++) {
		bool match = cid < nr && qa->part.cid_owner[cid] == owner;
		int n;

		if (match) {
			if (start < 0)
				start = cid;
			continue;
		}
		if (start < 0)
			continue;

		if (start == (s32)cid - 1)
			n = snprintf(buf + off, sz - off, "%s%d",
				     off ? "," : "", start);
		else
			n = snprintf(buf + off, sz - off, "%s%d-%d",
				     off ? "," : "", start, cid - 1);
		if (n < 0 || (size_t)n >= sz - off) {
			strcpy(&buf[sz - 4], "...");
			return;
		}
		off += n;
		start = -1;
	}
	if (!off)
		strcpy(buf, "-");
}

/* partition summary + one row per sched: weight, cpus, dispatch rate, cids */
static void print_hier(struct qmap_arena *qa, struct hier_prev *prev, u64 own_cgid)
{
	char ranges[128], who[16];
	const char *rr = "-";
	double secs;
	u32 i;

	/*
	 * account_alloc() bumps alloc_window_ns together with the per-owner
	 * counters, so dividing by the same window yields exact cid counts.
	 */
	secs = (qa->alloc_window_ns - prev->alloc_window_ns) / 1e9;
	prev->alloc_window_ns = qa->alloc_window_ns;

	/* resolve the live shared-pool holder */
	if (qa->part.nr_shared && qa->part.nr_rr) {
		u64 cgid = qa->part.rr_slots[qa->part.rr_pos];

		rr = "self";
		if (cgid) {
			rr = "?";
			for (i = 0; i < MAX_SUB_SCHEDS; i++) {
				if (qa->sub_sched_ctxs[i].cgroup_id == cgid) {
					snprintf(who, sizeof(who), "sub%u", i);
					rr = who;
					break;
				}
			}
		}
	}

	format_cid_ranges(qa, CID_SHARED, ranges, sizeof(ranges));
	printf("hier   : nsub=%llu excl=%u shared=%s rr=%s reenq cap/immed +%llu/+%llu inj=+%llu rescue=+%llu\n",
	       (unsigned long long)qa->nr_sub_scheds, qa->part.nr_excl, ranges, rr,
	       (unsigned long long)(qa->nr_reenq_cap - prev->nr_reenq_cap),
	       (unsigned long long)(qa->nr_reenq_immed - prev->nr_reenq_immed),
	       (unsigned long long)(qa->nr_inject_attempts - prev->nr_inject_attempts),
	       (unsigned long long)(qa->nr_rescue_dsp - prev->nr_rescue_dsp));
	prev->nr_reenq_cap = qa->nr_reenq_cap;
	prev->nr_reenq_immed = qa->nr_reenq_immed;
	prev->nr_inject_attempts = qa->nr_inject_attempts;
	prev->nr_rescue_dsp = qa->nr_rescue_dsp;

	printf("hier   : %-4s %10s %4s %6s %8s  %s\n",
	       "", "cgroup", "w", "alloc", "disp/s", "cids");

	format_cid_ranges(qa, CID_SELF, ranges, sizeof(ranges));
	printf("hier   : %-4s %10llu %4u %6.2f %8s  %s\n", "self",
	       (unsigned long long)own_cgid, 100,
	       secs > 0 ? (qa->self_alloc_ns - prev->self_alloc_ns) / (secs * 1e9) : 0.0,
	       "-", ranges);
	prev->self_alloc_ns = qa->self_alloc_ns;

	for (i = 0; i < MAX_SUB_SCHEDS; i++) {
		struct sub_sched_ctx *sc = &qa->sub_sched_ctxs[i];

		if (!sc->cgroup_id)
			continue;

		snprintf(who, sizeof(who), "sub%u", i);
		format_cid_ranges(qa, i, ranges, sizeof(ranges));
		printf("hier   : %-4s %10llu %4u %6.2f %8.1f  %s\n", who,
		       (unsigned long long)sc->cgroup_id, sc->weight,
		       secs > 0 ? (qa->alloc_ns[i] - prev->alloc_ns[i]) / (secs * 1e9) : 0.0,
		       secs > 0 ? (sc->nr_dsps - prev->nr_dsps[i]) / secs : 0.0,
		       ranges);
		prev->alloc_ns[i] = qa->alloc_ns[i];
		prev->nr_dsps[i] = sc->nr_dsps;
	}
}

int main(int argc, char **argv)
{
	struct scx_qmap *skel;
	struct bpf_link *link;
	struct qmap_arena *qa;
	u32 test_error_cnt = 0;
	u64 ecode;
	int opt, stats_intv = 5, i, round_robin_ms = 200;
	struct hier_prev hprev = {};
	const char *sub_cg_path = NULL;
	char tbuf[32];
	u32 inject_mode = 0;
	u64 own_cgid = 0;
	s32 cid_override_shard_sz = 4;

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
	skel = SCX_OPS_CID_OPEN(qmap_ops, scx_qmap);

	skel->rodata->slice_ns = __COMPAT_ENUM_OR_ZERO("scx_public_consts", "SCX_SLICE_DFL");
	skel->rodata->max_tasks = 16384;

	while ((opt = getopt(argc, argv,
			     "s:e:t:T:l:b:N:PMHc:d:D:SpIF:C:i:R:J:B:q:vh")) != -1) {
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
			own_cgid = st.st_ino;
			sub_cg_path = optarg;
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
			skel->struct_ops.qmap_ops->flags |= SCX_OPS_ALWAYS_ENQ_IMMED;
			break;
		case 'F':
			skel->rodata->immed_stress_nth = strtoul(optarg, NULL, 0);
			break;
		case 'C': {
			u32 nr_cpus = libbpf_num_possible_cpus();
			u32 mode;

			if (!strcmp(optarg, "shuffle"))
				mode = QMAP_CID_OVR_SHUFFLE;
			else if (!strcmp(optarg, "bad-dup"))
				mode = QMAP_CID_OVR_BAD_DUP;
			else if (!strcmp(optarg, "bad-range"))
				mode = QMAP_CID_OVR_BAD_RANGE;
			else if (!strcmp(optarg, "bad-mono"))
				mode = QMAP_CID_OVR_BAD_MONO;
			else {
				fprintf(stderr, "unknown cid-override mode '%s'\n", optarg);
				return 1;
			}
			skel->rodata->cid_override_mode = mode;
			cid_override_shard_sz = 4;

			/*
			 * bad-mono needs >= 3 shards to build a 0-based but
			 * non-monotonic shard_start. Shrink the shard size so
			 * the test runs on any machine with >= 3 cpus.
			 */
			if (mode == QMAP_CID_OVR_BAD_MONO) {
				if (nr_cpus < 3) {
					fprintf(stderr, "bad-mono needs >= 3 cpus (have %u)\n",
						nr_cpus);
					return 1;
				}
				cid_override_shard_sz = nr_cpus / 3;
			}

			/* shards of shard_sz each */
			skel->rodata->cid_override_nr_shards =
				(nr_cpus + cid_override_shard_sz - 1) / cid_override_shard_sz;
			break;
		}
		case 'i':
			stats_intv = atoi(optarg);
			if (stats_intv < 1)
				stats_intv = 1;
			break;
		case 'R':
			round_robin_ms = atoi(optarg);
			if (round_robin_ms < 10)
				round_robin_ms = 10;
			break;
		case 'J':
			if (!strcmp(optarg, "wrong-cid"))
				inject_mode = QMAP_INJ_WRONG_CID;
			else if (!strcmp(optarg, "init-fail"))
				inject_mode = QMAP_INJ_INIT_FAIL;
			else if (!strcmp(optarg, "cgrp-init-fail"))
				inject_mode = QMAP_INJ_CGRP_INIT_FAIL;
			else
				inject_mode = strtoul(optarg, NULL, 0);
			break;
		case 'B': {
			u32 ppt = strtoul(optarg, NULL, 0);

			if (!ppt)
				ppt = __COMPAT_ENUM_OR_ZERO("scx_consts", "SCX_RESCUE_DISABLE");
			skel->struct_ops.qmap_ops->rescue_bandwidth_ppt = ppt;
			break;
		}
		case 'q':
			skel->struct_ops.qmap_ops->rescue_quantum_us = strtoul(optarg, NULL, 0);
			break;
		case 'v':
			verbose = true;
			break;
		default:
			fprintf(stderr, help_fmt, basename(argv[0]));
			return opt != 'h';
		}
	}

	skel->rodata->round_robin_ns = (u64)round_robin_ms * 1000000;

	SCX_OPS_LOAD(skel, qmap_ops, scx_qmap, uei);

	qa = &skel->arena->qa;

	/*
	 * The cid-override arrays live in the arena, which is mmapped at load.
	 * Populate them before qmap_init_cids() consumes them at attach.
	 */
	if (skel->rodata->cid_override_mode) {
		u32 mode = skel->rodata->cid_override_mode;
		u32 nr_cpus = libbpf_num_possible_cpus();
		u32 i;

		/* shuffle: reversed cpu_to_cid; others: identity */
		for (i = 0; i < nr_cpus; i++) {
			if (mode == QMAP_CID_OVR_SHUFFLE)
				qa->cid_override_cpu_to_cid[i] = nr_cpus - 1 - i;
			else
				qa->cid_override_cpu_to_cid[i] = i;
		}
		if (mode == QMAP_CID_OVR_BAD_DUP && nr_cpus >= 2)
			qa->cid_override_cpu_to_cid[1] = 0;
		if (mode == QMAP_CID_OVR_BAD_RANGE)
			qa->cid_override_cpu_to_cid[0] = (s32)nr_cpus;

		for (i = 0; i < skel->rodata->cid_override_nr_shards; i++)
			qa->cid_override_shard_start[i] = i * cid_override_shard_sz;

		if (mode == QMAP_CID_OVR_BAD_MONO) {
			/* swap [1] and [2] to break monotonicity */
			s32 tmp = qa->cid_override_shard_start[1];
			qa->cid_override_shard_start[1] = qa->cid_override_shard_start[2];
			qa->cid_override_shard_start[2] = tmp;
		}
	}

	link = SCX_OPS_ATTACH(skel, qmap_ops, scx_qmap);

	qa->test_error_cnt = test_error_cnt;
	qa->inject_mode = inject_mode;

	if (sub_cg_path)
		printf("%s scx_qmap started: sub-scheduler on %s, stats every %ds\n",
		       tstamp(tbuf, sizeof(tbuf)), sub_cg_path, stats_intv);
	else
		printf("%s scx_qmap started: root scheduler, stats every %ds\n",
		       tstamp(tbuf, sizeof(tbuf)), stats_intv);
	fflush(stdout);

	while (!exit_req && !UEI_EXITED(skel, uei)) {
		long nr_enqueued = qa->nr_enqueued;
		long nr_dispatched = qa->nr_dispatched;

		printf("---- %s ----\n",
		       tstamp(tbuf, sizeof(tbuf)));
		printf("stats  : enq=%lu dsp=%lu delta=%ld reenq/cid0=%llu/%llu deq=%llu core=%llu enq_ddsp=%llu\n",
		       nr_enqueued, nr_dispatched, nr_enqueued - nr_dispatched,
		       (unsigned long long)qa->nr_reenqueued,
		       (unsigned long long)qa->nr_reenqueued_cid0,
		       (unsigned long long)qa->nr_dequeued,
		       (unsigned long long)qa->nr_core_sched_execed,
		       (unsigned long long)qa->nr_ddsp_from_enq);
		printf("         exp_local=%llu exp_remote=%llu exp_timer=%llu exp_lost=%llu\n",
		       (unsigned long long)qa->nr_expedited_local,
		       (unsigned long long)qa->nr_expedited_remote,
		       (unsigned long long)qa->nr_expedited_from_timer,
		       (unsigned long long)qa->nr_expedited_lost);
		if (__COMPAT_has_ksym("scx_bpf_cidperf_cur"))
			printf("cpuperf: cur min/avg/max=%u/%u/%u target min/avg/max=%u/%u/%u\n",
			       qa->cpuperf_min,
			       qa->cpuperf_avg,
			       qa->cpuperf_max,
			       qa->cpuperf_target_min,
			       qa->cpuperf_target_avg,
			       qa->cpuperf_target_max);

		invoke_flush_alloc(skel);
		print_hier(qa, &hprev, own_cgid);
		fflush(stdout);

		for (i = 0; i < stats_intv && !exit_req && !UEI_EXITED(skel, uei); i++)
			sleep(1);
	}

	bpf_link__destroy(link);
	ecode = UEI_REPORT(skel, uei);
	scx_qmap__destroy(skel);

	if (!exit_req && UEI_ECODE_RESTART(ecode))
		goto restart;
	return 0;
}
