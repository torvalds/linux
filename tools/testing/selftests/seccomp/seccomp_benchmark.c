/*
 * Strictly speaking, this is not a test. But it can report during test
 * runs so relative performace can be measured.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
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

int main(int argc, char *argv[])
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;
	unsigned long long samples;
	unsigned long long native, filter1, filter2;

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

	/* One filter */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	assert(ret == 0);

	filter1 = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	printf("getpid RET_ALLOW 1 filter: %llu ns\n", filter1);

	if (filter1 == native)
		printf("No overhead measured!? Try running again with more samples.\n");

	/* Two filters */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	assert(ret == 0);

	filter2 = timing(CLOCK_PROCESS_CPUTIME_ID, samples) / samples;
	printf("getpid RET_ALLOW 2 filters: %llu ns\n", filter2);

	/* Calculations */
	printf("Estimated total seccomp overhead for 1 filter: %llu ns\n",
		filter1 - native);

	printf("Estimated total seccomp overhead for 2 filters: %llu ns\n",
		filter2 - native);

	printf("Estimated seccomp per-filter overhead: %llu ns\n",
		filter2 - filter1);

	printf("Estimated seccomp entry overhead: %llu ns\n",
		filter1 - native - (filter2 - filter1));

	return 0;
}
