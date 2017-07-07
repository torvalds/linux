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

#ifndef SYS_getcpu
# ifdef __x86_64__
#  define SYS_getcpu 309
# else
#  define SYS_getcpu 318
# endif
#endif

int nerrs = 0;

#ifdef __x86_64__
# define VSYS(x) (x)
#else
# define VSYS(x) 0
#endif

typedef long (*getcpu_t)(unsigned *, unsigned *, void *);

const getcpu_t vgetcpu = (getcpu_t)VSYS(0xffffffffff600800);
getcpu_t vdso_getcpu;

void fill_function_pointers()
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
}

static long sys_getcpu(unsigned * cpu, unsigned * node,
		       void* cache)
{
	return syscall(__NR_getcpu, cpu, node, cache);
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

int main(int argc, char **argv)
{
	fill_function_pointers();

	test_getcpu();

	return nerrs ? 1 : 0;
}
