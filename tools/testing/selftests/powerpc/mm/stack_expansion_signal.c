// SPDX-License-Identifier: GPL-2.0
/*
 * Test that signal delivery is able to expand the stack segment without
 * triggering a SEGV.
 *
 * Based on test code by Tom Lane.
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include "../pmu/lib.h"
#include "utils.h"

#define _KB (1024)
#define _MB (1024 * 1024)

static char *stack_base_ptr;
static char *stack_top_ptr;

static volatile sig_atomic_t sig_occurred = 0;

static void sigusr1_handler(int signal_arg)
{
	sig_occurred = 1;
}

static int consume_stack(unsigned int stack_size, union pipe write_pipe)
{
	char stack_cur;

	if ((stack_base_ptr - &stack_cur) < stack_size)
		return consume_stack(stack_size, write_pipe);
	else {
		stack_top_ptr = &stack_cur;

		FAIL_IF(notify_parent(write_pipe));

		while (!sig_occurred)
			barrier();
	}

	return 0;
}

static int child(unsigned int stack_size, union pipe write_pipe)
{
	struct sigaction act;
	char stack_base;

	act.sa_handler = sigusr1_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	if (sigaction(SIGUSR1, &act, NULL) < 0)
		err(1, "sigaction");

	stack_base_ptr = (char *) (((size_t) &stack_base + 65535) & ~65535UL);

	FAIL_IF(consume_stack(stack_size, write_pipe));

	printf("size 0x%06x: OK, stack base %p top %p (%zx used)\n",
		stack_size, stack_base_ptr, stack_top_ptr,
		stack_base_ptr - stack_top_ptr);

	return 0;
}

static int test_one_size(unsigned int stack_size)
{
	union pipe read_pipe, write_pipe;
	pid_t pid;

	FAIL_IF(pipe(read_pipe.fds) == -1);
	FAIL_IF(pipe(write_pipe.fds) == -1);

	pid = fork();
	if (pid == 0) {
		close(read_pipe.read_fd);
		close(write_pipe.write_fd);
		exit(child(stack_size, read_pipe));
	}

	close(read_pipe.write_fd);
	close(write_pipe.read_fd);
	FAIL_IF(sync_with_child(read_pipe, write_pipe));

	kill(pid, SIGUSR1);

	FAIL_IF(wait_for_child(pid));

	close(read_pipe.read_fd);
	close(write_pipe.write_fd);

	return 0;
}

int test(void)
{
	unsigned int i, size;

	// Test with used stack from 1MB - 64K to 1MB + 64K
	// Increment by 64 to get more coverage of odd sizes
	for (i = 0; i < (128 * _KB); i += 64) {
		size = i + (1 * _MB) - (64 * _KB);
		FAIL_IF(test_one_size(size));
	}

	return 0;
}

int main(void)
{
	return test_harness(test, "stack_expansion_signal");
}
