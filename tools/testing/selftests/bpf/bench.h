/* SPDX-License-Identifier: GPL-2.0 */
#pragma once
#include <stdlib.h>
#include <stdbool.h>
#include <linux/err.h>
#include <errno.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <math.h>
#include <time.h>
#include <sys/syscall.h>

struct cpu_set {
	bool *cpus;
	int cpus_len;
	int next_cpu;
};

struct env {
	char *bench_name;
	int duration_sec;
	int warmup_sec;
	bool verbose;
	bool list;
	bool affinity;
	bool quiet;
	int consumer_cnt;
	int producer_cnt;
	struct cpu_set prod_cpus;
	struct cpu_set cons_cpus;
};

struct basic_stats {
	double mean;
	double stddev;
};

struct bench_res {
	long hits;
	long drops;
	long false_hits;
	long important_hits;
	unsigned long gp_ns;
	unsigned long gp_ct;
	unsigned int stime;
};

struct bench {
	const char *name;
	const struct argp *argp;
	void (*validate)(void);
	void (*setup)(void);
	void *(*producer_thread)(void *ctx);
	void *(*consumer_thread)(void *ctx);
	void (*measure)(struct bench_res* res);
	void (*report_progress)(int iter, struct bench_res* res, long delta_ns);
	void (*report_final)(struct bench_res res[], int res_cnt);
};

struct counter {
	long value;
} __attribute__((aligned(128)));

extern struct env env;
extern const struct bench *bench;

void setup_libbpf(void);
void hits_drops_report_progress(int iter, struct bench_res *res, long delta_ns);
void hits_drops_report_final(struct bench_res res[], int res_cnt);
void false_hits_report_progress(int iter, struct bench_res *res, long delta_ns);
void false_hits_report_final(struct bench_res res[], int res_cnt);
void ops_report_progress(int iter, struct bench_res *res, long delta_ns);
void ops_report_final(struct bench_res res[], int res_cnt);
void local_storage_report_progress(int iter, struct bench_res *res,
				   long delta_ns);
void local_storage_report_final(struct bench_res res[], int res_cnt);
void grace_period_latency_basic_stats(struct bench_res res[], int res_cnt,
				      struct basic_stats *gp_stat);
void grace_period_ticks_basic_stats(struct bench_res res[], int res_cnt,
				    struct basic_stats *gp_stat);

static inline __u64 get_time_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);

	return (u64)t.tv_sec * 1000000000 + t.tv_nsec;
}

static inline void atomic_inc(long *value)
{
	(void)__atomic_add_fetch(value, 1, __ATOMIC_RELAXED);
}

static inline void atomic_add(long *value, long n)
{
	(void)__atomic_add_fetch(value, n, __ATOMIC_RELAXED);
}

static inline long atomic_swap(long *value, long n)
{
	return __atomic_exchange_n(value, n, __ATOMIC_RELAXED);
}
