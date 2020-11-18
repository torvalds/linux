// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <sched.h>
#include "util.h" // for sched_getcpu()
#include "../perf-sys.h"
#include "cloexec.h"
#include "event.h"
#include "asm/bug.h"
#include "debug.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/string.h>

static unsigned long flag = PERF_FLAG_FD_CLOEXEC;

int __weak sched_getcpu(void)
{
#ifdef __NR_getcpu
	unsigned cpu;
	int err = syscall(__NR_getcpu, &cpu, NULL, NULL);
	if (!err)
		return cpu;
#else
	errno = ENOSYS;
#endif
	return -1;
}

static int perf_flag_probe(void)
{
	/* use 'safest' configuration as used in evsel__fallback() */
	struct perf_event_attr attr = {
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_CPU_CLOCK,
		.exclude_kernel = 1,
	};
	int fd;
	int err;
	int cpu;
	pid_t pid = -1;
	char sbuf[STRERR_BUFSIZE];

	cpu = sched_getcpu();
	if (cpu < 0)
		cpu = 0;

	/*
	 * Using -1 for the pid is a workaround to avoid gratuitous jump label
	 * changes.
	 */
	while (1) {
		/* check cloexec flag */
		fd = sys_perf_event_open(&attr, pid, cpu, -1,
					 PERF_FLAG_FD_CLOEXEC);
		if (fd < 0 && pid == -1 && errno == EACCES) {
			pid = 0;
			continue;
		}
		break;
	}
	err = errno;

	if (fd >= 0) {
		close(fd);
		return 1;
	}

	WARN_ONCE(err != EINVAL && err != EBUSY && err != EACCES,
		  "perf_event_open(..., PERF_FLAG_FD_CLOEXEC) failed with unexpected error %d (%s)\n",
		  err, str_error_r(err, sbuf, sizeof(sbuf)));

	/* not supported, confirm error related to PERF_FLAG_FD_CLOEXEC */
	while (1) {
		fd = sys_perf_event_open(&attr, pid, cpu, -1, 0);
		if (fd < 0 && pid == -1 && errno == EACCES) {
			pid = 0;
			continue;
		}
		break;
	}
	err = errno;

	if (fd >= 0)
		close(fd);

	if (WARN_ONCE(fd < 0 && err != EBUSY && err != EACCES,
		      "perf_event_open(..., 0) failed unexpectedly with error %d (%s)\n",
		      err, str_error_r(err, sbuf, sizeof(sbuf))))
		return -1;

	return 0;
}

unsigned long perf_event_open_cloexec_flag(void)
{
	static bool probed;

	if (!probed) {
		if (perf_flag_probe() <= 0)
			flag = 0;
		probed = true;
	}

	return flag;
}
