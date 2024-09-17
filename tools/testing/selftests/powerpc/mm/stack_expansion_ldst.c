// SPDX-License-Identifier: GPL-2.0
/*
 * Test that loads/stores expand the stack segment, or trigger a SEGV, in
 * various conditions.
 *
 * Based on test code by Tom Lane.
 */

#undef NDEBUG
#include <assert.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define _KB (1024)
#define _MB (1024 * 1024)

volatile char *stack_top_ptr;
volatile unsigned long stack_top_sp;
volatile char c;

enum access_type {
	LOAD,
	STORE,
};

/*
 * Consume stack until the stack pointer is below @target_sp, then do an access
 * (load or store) at offset @delta from either the base of the stack or the
 * current stack pointer.
 */
__attribute__ ((noinline))
int consume_stack(unsigned long target_sp, unsigned long stack_high, int delta, enum access_type type)
{
	unsigned long target;
	char stack_cur;

	if ((unsigned long)&stack_cur > target_sp)
		return consume_stack(target_sp, stack_high, delta, type);
	else {
		// We don't really need this, but without it GCC might not
		// generate a recursive call above.
		stack_top_ptr = &stack_cur;

#ifdef __powerpc__
		asm volatile ("mr %[sp], %%r1" : [sp] "=r" (stack_top_sp));
#else
		asm volatile ("mov %%rsp, %[sp]" : [sp] "=r" (stack_top_sp));
#endif
		target = stack_high - delta + 1;
		volatile char *p = (char *)target;

		if (type == STORE)
			*p = c;
		else
			c = *p;

		// Do something to prevent the stack frame being popped prior to
		// our access above.
		getpid();
	}

	return 0;
}

static int search_proc_maps(char *needle, unsigned long *low, unsigned long *high)
{
	unsigned long start, end;
	static char buf[4096];
	char name[128];
	FILE *f;
	int rc;

	f = fopen("/proc/self/maps", "r");
	if (!f) {
		perror("fopen");
		return -1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		rc = sscanf(buf, "%lx-%lx %*c%*c%*c%*c %*x %*d:%*d %*d %127s\n",
			    &start, &end, name);
		if (rc == 2)
			continue;

		if (rc != 3) {
			printf("sscanf errored\n");
			rc = -1;
			break;
		}

		if (strstr(name, needle)) {
			*low = start;
			*high = end - 1;
			rc = 0;
			break;
		}
	}

	fclose(f);

	return rc;
}

int child(unsigned int stack_used, int delta, enum access_type type)
{
	unsigned long low, stack_high;

	assert(search_proc_maps("[stack]", &low, &stack_high) == 0);

	assert(consume_stack(stack_high - stack_used, stack_high, delta, type) == 0);

	printf("Access OK: %s delta %-7d used size 0x%06x stack high 0x%lx top_ptr %p top sp 0x%lx actual used 0x%lx\n",
	       type == LOAD ? "load" : "store", delta, stack_used, stack_high,
	       stack_top_ptr, stack_top_sp, stack_high - stack_top_sp + 1);

	return 0;
}

static int test_one(unsigned int stack_used, int delta, enum access_type type)
{
	pid_t pid;
	int rc;

	pid = fork();
	if (pid == 0)
		exit(child(stack_used, delta, type));

	assert(waitpid(pid, &rc, 0) != -1);

	if (WIFEXITED(rc) && WEXITSTATUS(rc) == 0)
		return 0;

	// We don't expect a non-zero exit that's not a signal
	assert(!WIFEXITED(rc));

	printf("Faulted:   %s delta %-7d used size 0x%06x signal %d\n",
	       type == LOAD ? "load" : "store", delta, stack_used,
	       WTERMSIG(rc));

	return 1;
}

// This is fairly arbitrary but is well below any of the targets below,
// so that the delta between the stack pointer and the target is large.
#define DEFAULT_SIZE	(32 * _KB)

static void test_one_type(enum access_type type, unsigned long page_size, unsigned long rlim_cur)
{
	unsigned long delta;

	// We should be able to access anywhere within the rlimit
	for (delta = page_size; delta <= rlim_cur; delta += page_size)
		assert(test_one(DEFAULT_SIZE, delta, type) == 0);

	assert(test_one(DEFAULT_SIZE, rlim_cur, type) == 0);

	// But if we go past the rlimit it should fail
	assert(test_one(DEFAULT_SIZE, rlim_cur + 1, type) != 0);
}

static int test(void)
{
	unsigned long page_size;
	struct rlimit rlimit;

	page_size = getpagesize();
	getrlimit(RLIMIT_STACK, &rlimit);
	printf("Stack rlimit is 0x%lx\n", rlimit.rlim_cur);

	printf("Testing loads ...\n");
	test_one_type(LOAD, page_size, rlimit.rlim_cur);
	printf("Testing stores ...\n");
	test_one_type(STORE, page_size, rlimit.rlim_cur);

	printf("All OK\n");

	return 0;
}

#ifdef __powerpc__
#include "utils.h"

int main(void)
{
	return test_harness(test, "stack_expansion_ldst");
}
#else
int main(void)
{
	return test();
}
#endif
