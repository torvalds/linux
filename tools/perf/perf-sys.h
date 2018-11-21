/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_SYS_H
#define _PERF_SYS_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/perf_event.h>
#include <asm/barrier.h>

#ifdef __powerpc__
#define CPUINFO_PROC	{"cpu"}
#endif

#ifdef __s390__
#define CPUINFO_PROC	{"vendor_id"}
#endif

#ifdef __sh__
#define CPUINFO_PROC	{"cpu type"}
#endif

#ifdef __hppa__
#define CPUINFO_PROC	{"cpu"}
#endif

#ifdef __sparc__
#define CPUINFO_PROC	{"cpu"}
#endif

#ifdef __alpha__
#define CPUINFO_PROC	{"cpu model"}
#endif

#ifdef __arm__
#define CPUINFO_PROC	{"model name", "Processor"}
#endif

#ifdef __mips__
#define CPUINFO_PROC	{"cpu model"}
#endif

#ifdef __arc__
#define CPUINFO_PROC	{"Processor"}
#endif

#ifdef __xtensa__
#define CPUINFO_PROC	{"core ID"}
#endif

#ifndef CPUINFO_PROC
#define CPUINFO_PROC	{ "model name", }
#endif

static inline int
sys_perf_event_open(struct perf_event_attr *attr,
		      pid_t pid, int cpu, int group_fd,
		      unsigned long flags)
{
	int fd;

	fd = syscall(__NR_perf_event_open, attr, pid, cpu,
		     group_fd, flags);

#ifdef HAVE_ATTR_TEST
	if (unlikely(test_attr__enabled))
		test_attr__open(attr, pid, cpu, fd, group_fd, flags);
#endif
	return fd;
}

#endif /* _PERF_SYS_H */
