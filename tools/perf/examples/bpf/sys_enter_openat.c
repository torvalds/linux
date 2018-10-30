// SPDX-License-Identifier: GPL-2.0
/*
 * Hook into 'openat' syscall entry tracepoint
 *
 * Test it with:
 *
 * perf trace -e tools/perf/examples/bpf/sys_enter_openat.c cat /etc/passwd > /dev/null
 *
 * It'll catch some openat syscalls related to the dynamic linked and
 * the last one should be the one for '/etc/passwd'.
 *
 * The syscall_enter_openat_args can be used to get the syscall fields
 * and use them for filtering calls, i.e. use in expressions for
 * the return value.
 */

#include <bpf.h>

struct syscall_enter_openat_args {
	unsigned long long unused;
	long		   syscall_nr;
	long		   dfd;
	char		   *filename_ptr;
	long		   flags;
	long		   mode;
};

int syscall_enter(openat)(struct syscall_enter_openat_args *args)
{
	return 1;
}

license(GPL);
