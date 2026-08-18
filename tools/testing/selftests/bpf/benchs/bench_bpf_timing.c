// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bench_bpf_timing.h"
#include "bpf_util.h"

struct timing_stats {
	double min, max;
	double median, p99;
	double mean, stddev;
	int count;
};

static int cmp_double(const void *a, const void *b)
{
	double da = *(const double *)a;
	double db = *(const double *)b;

	if (da < db)
		return -1;
	if (da > db)
		return 1;
	return 0;
}

static double percentile(const double *sorted, int n, double pct)
{
	int idx = (int)(n * pct / 100.0);

	if (idx >= n)
		idx = n - 1;
	return sorted[idx];
}

static int collect_samples(struct bpf_bench_timing *t,
			   double *out, int max_out)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	__u32 timed_iters = t->batch_iters;
	int total = 0;

	if (nr_cpus > BENCH_NR_CPUS)
		nr_cpus = BENCH_NR_CPUS;

	for (unsigned int cpu = 0; cpu < nr_cpus; cpu++) {
		__u32 count = t->idx[cpu];

		if (count > BENCH_NR_SAMPLES)
			count = BENCH_NR_SAMPLES;

		for (__u32 i = 0; i < count && total < max_out; i++) {
			__u64 sample = t->samples[cpu][i];

			if (sample == 0)
				continue;
			out[total++] = (double)sample / timed_iters;
		}
	}

	qsort(out, total, sizeof(double), cmp_double);
	return total;
}

static int filter_outliers_iqr(double *sorted, int n)
{
	double q1, q3, iqr, lo, hi;
	int start = 0, end = n;

	if (n < 8)
		return n;

	q1 = sorted[n / 4];
	q3 = sorted[3 * n / 4];
	iqr = q3 - q1;
	lo = q1 - 1.5 * iqr;
	hi = q3 + 1.5 * iqr;

	while (start < end && sorted[start] < lo)
		start++;
	while (end > start && sorted[end - 1] > hi)
		end--;

	if (start > 0)
		memmove(sorted, sorted + start, (end - start) * sizeof(double));

	return end - start;
}

static void compute_stats(const double *sorted, int n,
			  struct timing_stats *s)
{
	double sum = 0, var_sum = 0;

	memset(s, 0, sizeof(*s));
	s->count = n;

	if (n == 0)
		return;

	s->min    = sorted[0];
	s->max    = sorted[n - 1];
	s->median = sorted[n / 2];
	s->p99    = percentile(sorted, n, 99);

	for (int i = 0; i < n; i++)
		sum += sorted[i];
	s->mean = sum / n;

	for (int i = 0; i < n; i++) {
		double d = sorted[i] - s->mean;

		var_sum += d * d;
	}
	s->stddev = n > 1 ? sqrt(var_sum / (n - 1)) : 0;
}

void bpf_bench_timing_measure(struct bpf_bench_timing *t, struct bench_res *res)
{
	unsigned int nr_cpus;
	__u32 total_samples;
	int i;

	t->warmup_ticks++;

	if (t->warmup_ticks < env.warmup_sec)
		return;

	if (t->warmup_ticks == env.warmup_sec) {
		*t->timing_enabled = 1;
		return;
	}

	nr_cpus = bpf_num_possible_cpus();
	if (nr_cpus > BENCH_NR_CPUS)
		nr_cpus = BENCH_NR_CPUS;

	total_samples = 0;
	for (i = 0; i < (int)nr_cpus; i++) {
		__u32 cnt = t->idx[i];

		if (cnt > BENCH_NR_SAMPLES)
			cnt = BENCH_NR_SAMPLES;
		total_samples += cnt;
	}

	if (total_samples >= (__u32)env.producer_cnt * t->target_samples && !t->done) {
		t->done = true;
		*t->timing_enabled = 0;
		bench_force_done();
	}
}

void bpf_bench_timing_report(struct bpf_bench_timing *t, const char *name, const char *description)
{
	int max_out = BENCH_NR_CPUS * BENCH_NR_SAMPLES;
	struct timing_stats s;
	double *all;
	int total;

	all = calloc(max_out, sizeof(*all));
	if (!all) {
		fprintf(stderr, "failed to allocate timing buffer\n");
		return;
	}

	total = collect_samples(t, all, max_out);

	if (total == 0) {
		printf("No timing samples collected.\n");
		free(all);
		return;
	}

	total = filter_outliers_iqr(all, total);
	compute_stats(all, total, &s);

	if (t->machine_readable) {
		printf("RESULT scenario=%s samples=%d median=%.2f stddev=%.2f cv=%.2f min=%.2f "
		       "p99=%.2f max=%.2f\n", name, total, s.median, s.stddev,
		       s.mean > 0 ? s.stddev / s.mean * 100.0 : 0.0, s.min, s.p99, s.max);
	} else {
		printf("%s: median %.2f ns/op, stddev %.2f, p99 %.2f (%d samples)\n", name,
		       s.median, s.stddev, s.p99, total);
	}

	free(all);
}

#define CALIBRATE_SEED_BATCH	100
#define CALIBRATE_MIN_BATCH	100
#define CALIBRATE_MAX_BATCH	10000000
#define CALIBRATE_TARGET_MS	10
#define CALIBRATE_RUNS		5
#define PROPORTIONALITY_TOL	0.05	/* 5% */

static void reset_timing(struct bpf_bench_timing *t)
{
	*t->timing_enabled = 0;
	memset(t->samples, 0, sizeof(__u64) * BENCH_NR_CPUS * BENCH_NR_SAMPLES);
	memset(t->idx, 0, sizeof(__u32) * BENCH_NR_CPUS);
}

static __u64 measure_elapsed(struct bpf_bench_timing *t, bpf_bench_run_fn run_fn, void *run_ctx,
			     __u32 iters, int runs)
{
	__u64 buf[CALIBRATE_RUNS];
	int n = 0, i, j;

	reset_timing(t);
	*t->batch_iters_bss = iters;
	*t->timing_enabled = 1;

	for (i = 0; i < runs; i++)
		run_fn(run_ctx);

	*t->timing_enabled = 0;

	for (i = 0; i < BENCH_NR_CPUS && n < runs; i++) {
		__u32 cnt = t->idx[i];

		for (j = 0; j < (int)cnt && n < runs; j++)
			buf[n++] = t->samples[i][j];
	}

	if (n == 0)
		return 0;

	for (i = 1; i < n; i++) {
		__u64 key = buf[i];

		j = i - 1;
		while (j >= 0 && buf[j] > key) {
			buf[j + 1] = buf[j];
			j--;
		}
		buf[j + 1] = key;
	}

	return buf[n / 2];
}

static __u32 compute_batch_iters(__u64 per_op_ns)
{
	__u64 target_ns = (__u64)CALIBRATE_TARGET_MS * 1000000ULL;
	__u32 iters;

	if (per_op_ns == 0)
		return CALIBRATE_MIN_BATCH;

	iters = target_ns / per_op_ns;

	if (iters < CALIBRATE_MIN_BATCH)
		iters = CALIBRATE_MIN_BATCH;
	if (iters > CALIBRATE_MAX_BATCH)
		iters = CALIBRATE_MAX_BATCH;

	return iters;
}

void bpf_bench_calibrate(struct bpf_bench_timing *t, bpf_bench_run_fn run_fn, void *run_ctx)
{
	__u64 elapsed, per_op_ns;
	__u64 time_n, time_2n;
	double ratio;

	elapsed = measure_elapsed(t, run_fn, run_ctx, CALIBRATE_SEED_BATCH, CALIBRATE_RUNS);
	if (elapsed == 0) {
		fprintf(stderr, "calibration: no timing samples, using default\n");
		t->batch_iters = 10000;
		*t->batch_iters_bss = t->batch_iters;
		reset_timing(t);
		return;
	}

	per_op_ns = elapsed / CALIBRATE_SEED_BATCH;
	t->batch_iters = compute_batch_iters(per_op_ns);

	time_n = measure_elapsed(t, run_fn, run_ctx, t->batch_iters, CALIBRATE_RUNS);
	time_2n = measure_elapsed(t, run_fn, run_ctx, t->batch_iters * 2, CALIBRATE_RUNS);

	if (time_n > 0 && time_2n > 0) {
		ratio = (double)time_2n / (double)time_n;

		if (fabs(ratio - 2.0) / 2.0 > PROPORTIONALITY_TOL)
			fprintf(stderr,
				"WARNING: proportionality check failed (2N/N ratio=%.3f, "
				"expected=2.000, error=%.1f%%)\n  System noise may be affecting "
				"results.\n",
				ratio, fabs(ratio - 2.0) / 2.0 * 100.0);
	}

	*t->batch_iters_bss = t->batch_iters;
	reset_timing(t);
}
