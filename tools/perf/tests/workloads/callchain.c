// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "../tests.h"

/*
 * Mark as noinline to establish the call chain, and avoid the static
 * annotation to prevent LTO from renaming the functions.
 */
noinline void callchain_do_syscall(void);
noinline void callchain_foo(void);
noinline int callchain(int argc, const char **argv);

noinline void callchain_do_syscall(void)
{
	syscall(SYS_gettid);
}

noinline void callchain_foo(void)
{
	callchain_do_syscall();
}

noinline int callchain(int argc __maybe_unused,
		       const char **argv __maybe_unused)
{
	callchain_foo();

	return 0;
}

DEFINE_WORKLOAD(callchain);
