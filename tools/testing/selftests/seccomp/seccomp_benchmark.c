/*
 * Strictly speaking, this is not a test. But it can report during test
 * runs so relative performace can be measured.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
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

#define ARRAY_SIZE(a)    (sizeof(a) / sizeof(a[0]))

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

	printf("%lu.%09lu - %lu.%09lu = %llu (%.1fs)\n",
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

	printf("Calibrating sample size for %d seconds worth of syscalls ...\n", seconds);

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
	double one = i_one, one_bump = one * 0.01;
	double two = i_two, two_bump = two * 0.01;

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
	     unsigned long long one, bool (*eval)(int, int), unsigned long long two)
{
	bool good;

	printf("\t%s %s %s (%lld %s %lld): ", name_one, name_eval, name_two,
	       (long long)one, name_eval, (long long)two);
	if (one > INT_MAX) {
		printf("Miscalculation! Measurement went negative: %lld\n", (long long)one);
		return 1;
	}
	if (two > INT_MAX) {
		printf("Miscalculation! Measurement went negative: %lld\n", (long long)two);
		return 1;
	}

	good = eval(one, two);
	printf("%s\n", good ? "✔️" : "❌");

	return good ? 0 : 1;
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

	printf("Current BPF sysctl settings:\n");
	system("sysctl net.core.bpf_jit_enable");
	system("sysctl net.core.bpf_jit_harden");

	if (argc > 1)
		samples = strtoull(argv[1], NULL, 0);
	else
		samples = calibrate();

	printf("Benchmarking %llu syscalls...\n", samples);

	/* Native call */
	native = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	printf("getpid native: %llu ns\n", native);

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	assert(ret == 0);

	/* One filter resulting in a bitmap */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &bitmap_prog);
	assert(ret == 0);

	bitmap1 = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	printf("getpid RET_ALLOW 1 filter (bitmap): %llu ns\n", bitmap1);

	/* Second filter resulting in a bitmap */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &bitmap_prog);
	assert(ret == 0);

	bitmap2 = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	printf("getpid RET_ALLOW 2 filters (bitmap): %llu ns\n", bitmap2);

	/* Third filter, can no longer be converted to bitmap */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	assert(ret == 0);

	filter1 = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	printf("getpid RET_ALLOW 3 filters (full): %llu ns\n", filter1);

	/* Fourth filter, can not be converted to bitmap because of filter 3 */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &bitmap_prog);
	assert(ret == 0);

	filter2 = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	printf("getpid RET_ALLOW 4 filters (full): %llu ns\n", filter2);

	/* Estimations */
#define ESTIMATE(fmt, var, what)	do {			\
		var = (what);					\
		printf("Estimated " fmt ": %llu ns\n", var);	\
		if (var > INT_MAX)				\
			goto more_samples;			\
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

	printf("Expectations:\n");
	ret |= compare("native", "≤", "1 bitmap", native, le, bitmap1);
	bits = compare("native", "≤", "1 filter", native, le, filter1);
	if (bits)
		goto more_samples;

	ret |= compare("per-filter (last 2 diff)", "≈", "per-filter (filters / 4)",
			per_filter1, approx, per_filter2);

	bits = compare("1 bitmapped", "≈", "2 bitmapped",
			bitmap1 - native, approx, bitmap2 - native);
	if (bits) {
		printf("Skipping constant action bitmap expectations: they appear unsupported.\n");
		goto out;
	}

	ret |= compare("entry", "≈", "1 bitmapped", entry, approx, bitmap1 - native);
	ret |= compare("entry", "≈", "2 bitmapped", entry, approx, bitmap2 - native);
	ret |= compare("native + entry + (per filter * 4)", "≈", "4 filters total",
			entry + (per_filter1 * 4) + native, approx, filter2);
	if (ret == 0)
		goto out;

more_samples:
	printf("Saw unexpected benchmark result. Try running again with more samples?\n");
out:
	return 0;
}
