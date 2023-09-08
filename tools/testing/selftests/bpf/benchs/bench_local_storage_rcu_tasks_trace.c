// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <argp.h>

#include <sys/prctl.h>
#include "local_storage_rcu_tasks_trace_bench.skel.h"
#include "bench.h"

#include <signal.h>

static struct {
	__u32 nr_procs;
	__u32 kthread_pid;
} args = {
	.nr_procs = 1000,
	.kthread_pid = 0,
};

enum {
	ARG_NR_PROCS = 7000,
	ARG_KTHREAD_PID = 7001,
};

static const struct argp_option opts[] = {
	{ "nr_procs", ARG_NR_PROCS, "NR_PROCS", 0,
		"Set number of user processes to spin up"},
	{ "kthread_pid", ARG_KTHREAD_PID, "PID", 0,
		"Pid of rcu_tasks_trace kthread for ticks tracking"},
	{},
};

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	long ret;

	switch (key) {
	case ARG_NR_PROCS:
		ret = strtol(arg, NULL, 10);
		if (ret < 1 || ret > UINT_MAX) {
			fprintf(stderr, "invalid nr_procs\n");
			argp_usage(state);
		}
		args.nr_procs = ret;
		break;
	case ARG_KTHREAD_PID:
		ret = strtol(arg, NULL, 10);
		if (ret < 1) {
			fprintf(stderr, "invalid kthread_pid\n");
			argp_usage(state);
		}
		args.kthread_pid = ret;
		break;
break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

const struct argp bench_local_storage_rcu_tasks_trace_argp = {
	.options = opts,
	.parser = parse_arg,
};

#define MAX_SLEEP_PROCS 150000

static void validate(void)
{
	if (env.producer_cnt != 1) {
		fprintf(stderr, "benchmark doesn't support multi-producer!\n");
		exit(1);
	}
	if (env.consumer_cnt != 1) {
		fprintf(stderr, "benchmark doesn't support multi-consumer!\n");
		exit(1);
	}

	if (args.nr_procs > MAX_SLEEP_PROCS) {
		fprintf(stderr, "benchmark supports up to %u sleeper procs!\n",
			MAX_SLEEP_PROCS);
		exit(1);
	}
}

static long kthread_pid_ticks(void)
{
	char procfs_path[100];
	long stime;
	FILE *f;

	if (!args.kthread_pid)
		return -1;

	sprintf(procfs_path, "/proc/%u/stat", args.kthread_pid);
	f = fopen(procfs_path, "r");
	if (!f) {
		fprintf(stderr, "couldn't open %s, exiting\n", procfs_path);
		goto err_out;
	}
	if (fscanf(f, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %ld", &stime) != 1) {
		fprintf(stderr, "fscanf of %s failed, exiting\n", procfs_path);
		goto err_out;
	}
	fclose(f);
	return stime;

err_out:
	if (f)
		fclose(f);
	exit(1);
	return 0;
}

static struct {
	struct local_storage_rcu_tasks_trace_bench *skel;
	long prev_kthread_stime;
} ctx;

static void sleep_and_loop(void)
{
	while (true) {
		sleep(rand() % 4);
		syscall(__NR_getpgid);
	}
}

static void local_storage_tasks_trace_setup(void)
{
	int i, err, forkret, runner_pid;

	runner_pid = getpid();

	for (i = 0; i < args.nr_procs; i++) {
		forkret = fork();
		if (forkret < 0) {
			fprintf(stderr, "Error forking sleeper proc %u of %u, exiting\n", i,
				args.nr_procs);
			goto err_out;
		}

		if (!forkret) {
			err = prctl(PR_SET_PDEATHSIG, SIGKILL);
			if (err < 0) {
				fprintf(stderr, "prctl failed with err %d, exiting\n", errno);
				goto err_out;
			}

			if (getppid() != runner_pid) {
				fprintf(stderr, "Runner died while spinning up procs, exiting\n");
				goto err_out;
			}
			sleep_and_loop();
		}
	}
	printf("Spun up %u procs (our pid %d)\n", args.nr_procs, runner_pid);

	setup_libbpf();

	ctx.skel = local_storage_rcu_tasks_trace_bench__open_and_load();
	if (!ctx.skel) {
		fprintf(stderr, "Error doing open_and_load, exiting\n");
		goto err_out;
	}

	ctx.prev_kthread_stime = kthread_pid_ticks();

	if (!bpf_program__attach(ctx.skel->progs.get_local)) {
		fprintf(stderr, "Error attaching bpf program\n");
		goto err_out;
	}

	if (!bpf_program__attach(ctx.skel->progs.pregp_step)) {
		fprintf(stderr, "Error attaching bpf program\n");
		goto err_out;
	}

	if (!bpf_program__attach(ctx.skel->progs.postgp)) {
		fprintf(stderr, "Error attaching bpf program\n");
		goto err_out;
	}

	return;
err_out:
	exit(1);
}

static void measure(struct bench_res *res)
{
	long ticks;

	res->gp_ct = atomic_swap(&ctx.skel->bss->gp_hits, 0);
	res->gp_ns = atomic_swap(&ctx.skel->bss->gp_times, 0);
	ticks = kthread_pid_ticks();
	res->stime = ticks - ctx.prev_kthread_stime;
	ctx.prev_kthread_stime = ticks;
}

static void *consumer(void *input)
{
	return NULL;
}

static void *producer(void *input)
{
	while (true)
		syscall(__NR_getpgid);
	return NULL;
}

static void report_progress(int iter, struct bench_res *res, long delta_ns)
{
	if (ctx.skel->bss->unexpected) {
		fprintf(stderr, "Error: Unexpected order of bpf prog calls (postgp after pregp).");
		fprintf(stderr, "Data can't be trusted, exiting\n");
		exit(1);
	}

	if (env.quiet)
		return;

	printf("Iter %d\t avg tasks_trace grace period latency\t%lf ns\n",
	       iter, res->gp_ns / (double)res->gp_ct);
	printf("Iter %d\t avg ticks per tasks_trace grace period\t%lf\n",
	       iter, res->stime / (double)res->gp_ct);
}

static void report_final(struct bench_res res[], int res_cnt)
{
	struct basic_stats gp_stat;

	grace_period_latency_basic_stats(res, res_cnt, &gp_stat);
	printf("SUMMARY tasks_trace grace period latency");
	printf("\tavg %.3lf us\tstddev %.3lf us\n", gp_stat.mean, gp_stat.stddev);
	grace_period_ticks_basic_stats(res, res_cnt, &gp_stat);
	printf("SUMMARY ticks per tasks_trace grace period");
	printf("\tavg %.3lf\tstddev %.3lf\n", gp_stat.mean, gp_stat.stddev);
}

/* local-storage-tasks-trace: Benchmark performance of BPF local_storage's use
 * of RCU Tasks-Trace.
 *
 * Stress RCU Tasks Trace by forking many tasks, all of which do no work aside
 * from sleep() loop, and creating/destroying BPF task-local storage on wakeup.
 * The number of forked tasks is configurable.
 *
 * exercising code paths which call call_rcu_tasks_trace while there are many
 * thousands of tasks on the system should result in RCU Tasks-Trace having to
 * do a noticeable amount of work.
 *
 * This should be observable by measuring rcu_tasks_trace_kthread CPU usage
 * after the grace period has ended, or by measuring grace period latency.
 *
 * This benchmark uses both approaches, attaching to rcu_tasks_trace_pregp_step
 * and rcu_tasks_trace_postgp functions to measure grace period latency and
 * using /proc/PID/stat to measure rcu_tasks_trace_kthread kernel ticks
 */
const struct bench bench_local_storage_tasks_trace = {
	.name = "local-storage-tasks-trace",
	.argp = &bench_local_storage_rcu_tasks_trace_argp,
	.validate = validate,
	.setup = local_storage_tasks_trace_setup,
	.producer_thread = producer,
	.consumer_thread = consumer,
	.measure = measure,
	.report_progress = report_progress,
	.report_final = report_final,
};
