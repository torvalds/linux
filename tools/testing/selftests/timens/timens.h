/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TIMENS_H__
#define __TIMENS_H__

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include "../kselftest.h"

#ifndef CLONE_NEWTIME
# define CLONE_NEWTIME	0x00000080
#endif

static int config_posix_timers = true;

static inline void check_config_posix_timers(void)
{
	if (timer_create(-1, 0, 0) == -1 && errno == ENOSYS)
		config_posix_timers = false;
}

static inline bool check_skip(int clockid)
{
	if (config_posix_timers)
		return false;

	switch (clockid) {
	/* Only these clocks are supported without CONFIG_POSIX_TIMERS. */
	case CLOCK_BOOTTIME:
	case CLOCK_MONOTONIC:
	case CLOCK_REALTIME:
		return false;
	default:
		ksft_test_result_skip("Posix Clocks & timers are not supported\n");
		return true;
	}

	return false;
}

static inline int unshare_timens(void)
{
	if (unshare(CLONE_NEWTIME)) {
		if (errno == EPERM)
			ksft_exit_skip("need to run as root\n");
		return pr_perror("Can't unshare() timens");
	}
	return 0;
}

static inline int _settime(clockid_t clk_id, time_t offset)
{
	int fd, len;
	char buf[4096];

	if (clk_id == CLOCK_MONOTONIC_COARSE || clk_id == CLOCK_MONOTONIC_RAW)
		clk_id = CLOCK_MONOTONIC;

	len = snprintf(buf, sizeof(buf), "%d %ld 0", clk_id, offset);

	fd = open("/proc/self/timens_offsets", O_WRONLY);
	if (fd < 0)
		return pr_perror("/proc/self/timens_offsets");

	if (write(fd, buf, len) != len)
		return pr_perror("/proc/self/timens_offsets");

	close(fd);

	return 0;
}

static inline int _gettime(clockid_t clk_id, struct timespec *res, bool raw_syscall)
{
	int err;

	if (!raw_syscall) {
		if (clock_gettime(clk_id, res)) {
			pr_perror("clock_gettime(%d)", (int)clk_id);
			return -1;
		}
		return 0;
	}

	err = syscall(SYS_clock_gettime, clk_id, res);
	if (err)
		pr_perror("syscall(SYS_clock_gettime(%d))", (int)clk_id);

	return err;
}

static inline void nscheck(void)
{
	if (access("/proc/self/ns/time", F_OK) < 0)
		ksft_exit_skip("Time namespaces are not supported\n");
}

#endif
