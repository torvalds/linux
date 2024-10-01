/*
 * Strictly speaking, this is not a test. But it can report during test
 * runs so relative performace can be measured.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <err.h>
#include <limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/param.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "../kselftest.h"

unsigned long long timing(clockid_t clk_id, unsigned long long samples)
{
	struct timespec start, finish;
	unsigned long long i;
	pid_t pid, ret;

	pid = getpid();
	assert(clock_gettime(clk_id, &start) == 0);
	for (i = 0; i < samples; i++) {
		ret = syscall(__NR_getpid);
		assert(pid == ret);
	}
	assert(clock_gettime(clk_id, &finish) == 0);

	i = finish.tv_sec - start.tv_sec;
	i *= 1000000000ULL;
	i += finish.tv_nsec - start.tv_nsec;

	ksft_print_msg("%lu.%09lu - %lu.%09lu = %llu (%.1fs)\n",
		       finish.tv_sec, finish.tv_nsec,
		       start.tv_sec, start.tv_nsec,
		       i, (double)i / 1000000000.0);

	return i;
}

unsigned long long calibrate(void)
{
	struct timespec start, finish;
	unsigned long long i, samples, step = 9973;
	pid_t pid, ret;
	int seconds = 15;

	ksft_print_msg("Calibrating sample size for %d seconds worth of syscalls ...\n", seconds);

	samples = 0;
	pid = getpid();
	assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
	do {
		for (i = 0; i < step; i++) {
			ret = syscall(__NR_getpid);
			assert(pid == ret);
		}
		assert(clock_gettime(CLOCK_MONOTONIC, &finish) == 0);

		samples += step;
		i = finish.tv_sec - start.tv_sec;
		i *= 1000000000ULL;
		i += finish.tv_nsec - start.tv_nsec;
	} while (i < 1000000000ULL);

	return samples * seconds;
}

bool approx(int i_one, int i_two)
{
	/*
	 * This continues to be a noisy test. Instead of a 1% comparison
	 * go with 10%.
	 */
	double one = i_one, one_bump = one * 0.1;
	double two = i_two, two_bump = two * 0.1;

	one_bump = one + MAX(one_bump, 2.0);
	two_bump = two + MAX(two_bump, 2.0);

	/* Equal to, or within 1% or 2 digits */
	if (one == two ||
	    (one > two && one <= two_bump) ||
	    (two > one && two <= one_bump))
		return true;
	return false;
}

bool le(int i_one, int i_two)
{
	if (i_one <= i_two)
		return true;
	return false;
}

long compare(const char *name_one, const char *name_eval, const char *name_two,
	     unsigned long long one, bool (*eval)(int, int), unsigned long long two,
	     bool skip)
{
	bool good;

	if (skip) {
		ksft_test_result_skip("%s %s %s\n", name_one, name_eval,
				      name_two);
		return 0;
	}

	ksft_print_msg("\t%s %s %s (%lld %s %lld): ", name_one, name_eval, name_two,
		       (long long)one, name_eval, (long long)two);
	if (one > INT_MAX) {
		ksft_print_msg("Miscalculation! Measurement went negative: %lld\n", (long long)one);
		good = false;
		goto out;
	}
	if (two > INT_MAX) {
		ksft_print_msg("Miscalculation! Measurement went negative: %lld\n", (long long)two);
		good = false;
		goto out;
	}

	good = eval(one, two);
	printf("%s\n", good ? "✔️" : "❌");

out:
	ksft_test_result(good, "%s %s %s\n", name_one, name_eval, name_two);

	return good ? 0 : 1;
}

/* Pin to a single CPU so the benchmark won't bounce around the system. */
void affinity(void)
{
	long cpu;
	ulong ncores = sysconf(_SC_NPROCESSORS_CONF);
	cpu_set_t *setp = CPU_ALLOC(ncores);
	ulong setsz = CPU_ALLOC_SIZE(ncores);

	/*
	 * Totally unscientific way to avoid CPUs that might be busier:
	 * choose the highest CPU instead of the lowest.
	 */
	for (cpu = ncores - 1; cpu >= 0; cpu--) {
		CPU_ZERO_S(setsz, setp);
		CPU_SET_S(cpu, setsz, setp);
		if (sched_setaffinity(getpid(), setsz, setp) == -1)
			continue;
		printf("Pinned to CPU %lu of %lu\n", cpu + 1, ncores);
		goto out;
	}
	fprintf(stderr, "Could not set CPU affinity -- calibration may not work well");

out:
	CPU_FREE(setp);
}

int main(int argc, char *argv[])
{
	struct sock_filter bitmap_filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS, offsetof(struct seccomp_data, nr)),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog bitmap_prog = {
		.len = (unsigned short)ARRAY_SIZE(bitmap_filter),
		.filter = bitmap_filter,
	};
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS, offsetof(struct seccomp_data, args[0])),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};

	long ret, bits;
	unsigned long long samples, calc;
	unsigned long long native, filter1, filter2, bitmap1, bitmap2;
	unsigned long long entry, per_filter1, per_filter2;
	bool skip = false;

	setbuf(stdout, NULL);

	ksft_print_header();
	ksft_set_plan(7);

	ksft_print_msg("Running on:\n");
	ksft_print_msg("%s", "");
	system("uname -a");

	ksft_print_msg("Current BPF sysctl settings:\n");
	/* Avoid using "sysctl" which may not be installed. */
	ksft_print_msg("%s", "");
	system("grep -H . /proc/sys/net/core/bpf_jit_enable");
	ksft_print_msg("%s", "");
	system("grep -H . /proc/sys/net/core/bpf_jit_harden");

	affinity();

	if (argc > 1)
		samples = strtoull(argv[1], NULL, 0);
	else
		samples = calibrate();

	ksft_print_msg("Benchmarking %llu syscalls...\n", samples);

	/* Native call */
	native = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	ksft_print_msg("getpid native: %llu ns\n", native);

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	assert(ret == 0);

	/* One filter resulting in a bitmap */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &bitmap_prog);
	assert(ret == 0);

	bitmap1 = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	ksft_print_msg("getpid RET_ALLOW 1 filter (bitmap): %llu ns\n", bitmap1);

	/* Second filter resulting in a bitmap */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &bitmap_prog);
	assert(ret == 0);

	bitmap2 = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	ksft_print_msg("getpid RET_ALLOW 2 filters (bitmap): %llu ns\n", bitmap2);

	/* Third filter, can no longer be converted to bitmap */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	assert(ret == 0);

	filter1 = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	ksft_print_msg("getpid RET_ALLOW 3 filters (full): %llu ns\n", filter1);

	/* Fourth filter, can not be converted to bitmap because of filter 3 */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &bitmap_prog);
	assert(ret == 0);

	filter2 = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	ksft_print_msg("getpid RET_ALLOW 4 filters (full): %llu ns\n", filter2);

	/* Estimations */
#define ESTIMATE(fmt, var, what)	do {			\
		var = (what);					\
		ksft_print_msg("Estimated " fmt ": %llu ns\n", var);	\
		if (var > INT_MAX) {				\
			skip = true;				\
			ret |= 1;				\
		}						\
	} while (0)

	ESTIMATE("total seccomp overhead for 1 bitmapped filter", calc,
		 bitmap1 - native);
	ESTIMATE("total seccomp overhead for 2 bitmapped filters", calc,
		 bitmap2 - native);
	ESTIMATE("total seccomp overhead for 3 full filters", calc,
		 filter1 - native);
	ESTIMATE("total seccomp overhead for 4 full filters", calc,
		 filter2 - native);
	ESTIMATE("seccomp entry overhead", entry,
		 bitmap1 - native - (bitmap2 - bitmap1));
	ESTIMATE("seccomp per-filter overhead (last 2 diff)", per_filter1,
		 filter2 - filter1);
	ESTIMATE("seccomp per-filter overhead (filters / 4)", per_filter2,
		 (filter2 - native - entry) / 4);

	ksft_print_msg("Expectations:\n");
	ret |= compare("native", "≤", "1 bitmap", native, le, bitmap1,
		       skip);
	bits = compare("native", "≤", "1 filter", native, le, filter1,
		       skip);
	if (bits)
		skip = true;

	ret |= compare("per-filter (last 2 diff)", "≈", "per-filter (filters / 4)",
		       per_filter1, approx, per_filter2, skip);

	bits = compare("1 bitmapped", "≈", "2 bitmapped",
		       bitmap1 - native, approx, bitmap2 - native, skip);
	if (bits) {
		ksft_print_msg("Skipping constant action bitmap expectations: they appear unsupported.\n");
		skip = true;
	}

	ret |= compare("entry", "≈", "1 bitmapped", entry, approx,
		       bitmap1 - native, skip);
	ret |= compare("entry", "≈", "2 bitmapped", entry, approx,
		       bitmap2 - native, skip);
	ret |= compare("native + entry + (per filter * 4)", "≈", "4 filters total",
		       entry + (per_filter1 * 4) + native, approx, filter2,
		       skip);

	if (ret)
		ksft_print_msg("Saw unexpected benchmark result. Try running again with more samples?\n");

	ksft_finished();
}
