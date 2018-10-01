// SPDX-License-Identifier: GPL-2.0
/*
 * ldt_gdt.c - Test cases for LDT and GDT access
 * Copyright (c) 2011-2015 Andrew Lutomirski
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <dlfcn.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <limits.h>

#ifndef SYS_getcpu
# ifdef __x86_64__
#  define SYS_getcpu 309
# else
#  define SYS_getcpu 318
# endif
#endif

/* max length of lines in /proc/self/maps - anything longer is skipped here */
#define MAPS_LINE_LEN 128

int nerrs = 0;

typedef int (*vgettime_t)(clockid_t, struct timespec *);

vgettime_t vdso_clock_gettime;

typedef long (*getcpu_t)(unsigned *, unsigned *, void *);

getcpu_t vgetcpu;
getcpu_t vdso_getcpu;

static void *vsyscall_getcpu(void)
{
#ifdef __x86_64__
	FILE *maps;
	char line[MAPS_LINE_LEN];
	bool found = false;

	maps = fopen("/proc/self/maps", "r");
	if (!maps) /* might still be present, but ignore it here, as we test vDSO not vsyscall */
		return NULL;

	while (fgets(line, MAPS_LINE_LEN, maps)) {
		char r, x;
		void *start, *end;
		char name[MAPS_LINE_LEN];

		/* sscanf() is safe here as strlen(name) >= strlen(line) */
		if (sscanf(line, "%p-%p %c-%cp %*x %*x:%*x %*u %s",
			   &start, &end, &r, &x, name) != 5)
			continue;

		if (strcmp(name, "[vsyscall]"))
			continue;

		/* assume entries are OK, as we test vDSO here not vsyscall */
		found = true;
		break;
	}

	fclose(maps);

	if (!found) {
		printf("Warning: failed to find vsyscall getcpu\n");
		return NULL;
	}
	return (void *) (0xffffffffff600800);
#else
	return NULL;
#endif
}


static void fill_function_pointers()
{
	void *vdso = dlopen("linux-vdso.so.1",
			    RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
	if (!vdso)
		vdso = dlopen("linux-gate.so.1",
			      RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
	if (!vdso) {
		printf("[WARN]\tfailed to find vDSO\n");
		return;
	}

	vdso_getcpu = (getcpu_t)dlsym(vdso, "__vdso_getcpu");
	if (!vdso_getcpu)
		printf("Warning: failed to find getcpu in vDSO\n");

	vgetcpu = (getcpu_t) vsyscall_getcpu();

	vdso_clock_gettime = (vgettime_t)dlsym(vdso, "__vdso_clock_gettime");
	if (!vdso_clock_gettime)
		printf("Warning: failed to find clock_gettime in vDSO\n");
}

static long sys_getcpu(unsigned * cpu, unsigned * node,
		       void* cache)
{
	return syscall(__NR_getcpu, cpu, node, cache);
}

static inline int sys_clock_gettime(clockid_t id, struct timespec *ts)
{
	return syscall(__NR_clock_gettime, id, ts);
}

static void test_getcpu(void)
{
	printf("[RUN]\tTesting getcpu...\n");

	for (int cpu = 0; ; cpu++) {
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(cpu, &cpuset);
		if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
			return;

		unsigned cpu_sys, cpu_vdso, cpu_vsys,
			node_sys, node_vdso, node_vsys;
		long ret_sys, ret_vdso = 1, ret_vsys = 1;
		unsigned node;

		ret_sys = sys_getcpu(&cpu_sys, &node_sys, 0);
		if (vdso_getcpu)
			ret_vdso = vdso_getcpu(&cpu_vdso, &node_vdso, 0);
		if (vgetcpu)
			ret_vsys = vgetcpu(&cpu_vsys, &node_vsys, 0);

		if (!ret_sys)
			node = node_sys;
		else if (!ret_vdso)
			node = node_vdso;
		else if (!ret_vsys)
			node = node_vsys;

		bool ok = true;
		if (!ret_sys && (cpu_sys != cpu || node_sys != node))
			ok = false;
		if (!ret_vdso && (cpu_vdso != cpu || node_vdso != node))
			ok = false;
		if (!ret_vsys && (cpu_vsys != cpu || node_vsys != node))
			ok = false;

		printf("[%s]\tCPU %u:", ok ? "OK" : "FAIL", cpu);
		if (!ret_sys)
			printf(" syscall: cpu %u, node %u", cpu_sys, node_sys);
		if (!ret_vdso)
			printf(" vdso: cpu %u, node %u", cpu_vdso, node_vdso);
		if (!ret_vsys)
			printf(" vsyscall: cpu %u, node %u", cpu_vsys,
			       node_vsys);
		printf("\n");

		if (!ok)
			nerrs++;
	}
}

static bool ts_leq(const struct timespec *a, const struct timespec *b)
{
	if (a->tv_sec != b->tv_sec)
		return a->tv_sec < b->tv_sec;
	else
		return a->tv_nsec <= b->tv_nsec;
}

static char const * const clocknames[] = {
	[0] = "CLOCK_REALTIME",
	[1] = "CLOCK_MONOTONIC",
	[2] = "CLOCK_PROCESS_CPUTIME_ID",
	[3] = "CLOCK_THREAD_CPUTIME_ID",
	[4] = "CLOCK_MONOTONIC_RAW",
	[5] = "CLOCK_REALTIME_COARSE",
	[6] = "CLOCK_MONOTONIC_COARSE",
	[7] = "CLOCK_BOOTTIME",
	[8] = "CLOCK_REALTIME_ALARM",
	[9] = "CLOCK_BOOTTIME_ALARM",
	[10] = "CLOCK_SGI_CYCLE",
	[11] = "CLOCK_TAI",
};

static void test_one_clock_gettime(int clock, const char *name)
{
	struct timespec start, vdso, end;
	int vdso_ret, end_ret;

	printf("[RUN]\tTesting clock_gettime for clock %s (%d)...\n", name, clock);

	if (sys_clock_gettime(clock, &start) < 0) {
		if (errno == EINVAL) {
			vdso_ret = vdso_clock_gettime(clock, &vdso);
			if (vdso_ret == -EINVAL) {
				printf("[OK]\tNo such clock.\n");
			} else {
				printf("[FAIL]\tNo such clock, but __vdso_clock_gettime returned %d\n", vdso_ret);
				nerrs++;
			}
		} else {
			printf("[WARN]\t clock_gettime(%d) syscall returned error %d\n", clock, errno);
		}
		return;
	}

	vdso_ret = vdso_clock_gettime(clock, &vdso);
	end_ret = sys_clock_gettime(clock, &end);

	if (vdso_ret != 0 || end_ret != 0) {
		printf("[FAIL]\tvDSO returned %d, syscall errno=%d\n",
		       vdso_ret, errno);
		nerrs++;
		return;
	}

	printf("\t%llu.%09ld %llu.%09ld %llu.%09ld\n",
	       (unsigned long long)start.tv_sec, start.tv_nsec,
	       (unsigned long long)vdso.tv_sec, vdso.tv_nsec,
	       (unsigned long long)end.tv_sec, end.tv_nsec);

	if (!ts_leq(&start, &vdso) || !ts_leq(&vdso, &end)) {
		printf("[FAIL]\tTimes are out of sequence\n");
		nerrs++;
	}
}

static void test_clock_gettime(void)
{
	for (int clock = 0; clock < sizeof(clocknames) / sizeof(clocknames[0]);
	     clock++) {
		test_one_clock_gettime(clock, clocknames[clock]);
	}

	/* Also test some invalid clock ids */
	test_one_clock_gettime(-1, "invalid");
	test_one_clock_gettime(INT_MIN, "invalid");
	test_one_clock_gettime(INT_MAX, "invalid");
}

int main(int argc, char **argv)
{
	fill_function_pointers();

	test_clock_gettime();

	/*
	 * Test getcpu() last so that, if something goes wrong setting affinity,
	 * we still run the other tests.
	 */
	test_getcpu();

	return nerrs ? 1 : 0;
}
