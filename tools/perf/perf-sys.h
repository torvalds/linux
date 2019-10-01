/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_SYS_H
#define _PERF_SYS_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <linux/compiler.h>

struct perf_event_attr;

extern bool test_attr__enabled;
void test_attr__ready(void);
void test_attr__init(void);
void test_attr__open(struct perf_event_attr *attr, pid_t pid, int cpu,
		     int fd, int group_fd, unsigned long flags);

#ifndef HAVE_ATTR_TEST
#define HAVE_ATTR_TEST 1
#endif

static inline int
sys_perf_event_open(struct perf_event_attr *attr,
		      pid_t pid, int cpu, int group_fd,
		      unsigned long flags)
{
	int fd;

	fd = syscall(__NR_perf_event_open, attr, pid, cpu,
		     group_fd, flags);

#if HAVE_ATTR_TEST
	if (unlikely(test_attr__enabled))
		test_attr__open(attr, pid, cpu, fd, group_fd, flags);
#endif
	return fd;
}

#endif /* _PERF_SYS_H */
