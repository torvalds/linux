// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 ARM Limited.
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <stdbool.h>

#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/uio.h>

#include <asm/hwcap.h>
#include <asm/mman.h>

#include <linux/compiler.h>

#include "kselftest_harness.h"

#include "gcs-util.h"

#define my_syscall2(num, arg1, arg2)                                          \
({                                                                            \
	register long _num  __asm__ ("x8") = (num);                           \
	register long _arg1 __asm__ ("x0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("x1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("x2") = 0;                               \
	register long _arg4 __asm__ ("x3") = 0;                               \
	register long _arg5 __asm__ ("x4") = 0;                               \
	                                                                      \
	__asm__  volatile (                                                   \
		"svc #0\n"                                                    \
		: "=r"(_arg1)                                                 \
		: "r"(_arg1), "r"(_arg2),                                     \
		  "r"(_arg3), "r"(_arg4),                                     \
		  "r"(_arg5), "r"(_num)					      \
		: "memory", "cc"                                              \
	);                                                                    \
	_arg1;                                                                \
})

static noinline void gcs_recurse(int depth)
{
	if (depth)
		gcs_recurse(depth - 1);

	/* Prevent tail call optimization so we actually recurse */
	asm volatile("dsb sy" : : : "memory");
}

/* Smoke test that a function call and return works*/
TEST(can_call_function)
{
	gcs_recurse(0);
}

static void *gcs_test_thread(void *arg)
{
	int ret;
	unsigned long mode;

	/*
	 * Some libcs don't seem to fill unused arguments with 0 but
	 * the kernel validates this so we supply all 5 arguments.
	 */
	ret = prctl(PR_GET_SHADOW_STACK_STATUS, &mode, 0, 0, 0);
	if (ret != 0) {
		ksft_print_msg("PR_GET_SHADOW_STACK_STATUS failed: %d\n", ret);
		return NULL;
	}

	if (!(mode & PR_SHADOW_STACK_ENABLE)) {
		ksft_print_msg("GCS not enabled in thread, mode is %lu\n",
			       mode);
		return NULL;
	}

	/* Just in case... */
	gcs_recurse(0);

	/* Use a non-NULL value to indicate a pass */
	return &gcs_test_thread;
}

/* Verify that if we start a new thread it has GCS enabled */
TEST(gcs_enabled_thread)
{
	pthread_t thread;
	void *thread_ret;
	int ret;

	ret = pthread_create(&thread, NULL, gcs_test_thread, NULL);
	ASSERT_TRUE(ret == 0);
	if (ret != 0)
		return;

	ret = pthread_join(thread, &thread_ret);
	ASSERT_TRUE(ret == 0);
	if (ret != 0)
		return;

	ASSERT_TRUE(thread_ret != NULL);
}

/* Read the GCS until we find the terminator */
TEST(gcs_find_terminator)
{
	unsigned long *gcs, *cur;

	gcs = get_gcspr();
	cur = gcs;
	while (*cur)
		cur++;

	ksft_print_msg("GCS in use from %p-%p\n", gcs, cur);

	/*
	 * We should have at least whatever called into this test so
	 * the two pointer should differ.
	 */
	ASSERT_TRUE(gcs != cur);
}

/*
 * We can access a GCS via ptrace
 *
 * This could usefully have a fixture but note that each test is
 * fork()ed into a new child whcih causes issues.  Might be better to
 * lift at least some of this out into a separate, non-harness, test
 * program.
 */
TEST(ptrace_read_write)
{
	pid_t child, pid;
	int ret, status;
	siginfo_t si;
	uint64_t val, rval, gcspr;
	struct user_gcs child_gcs;
	struct iovec iov, local_iov, remote_iov;

	child = fork();
	if (child == -1) {
		ksft_print_msg("fork() failed: %d (%s)\n",
			       errno, strerror(errno));
		ASSERT_NE(child, -1);
	}

	if (child == 0) {
		/*
		 * In child, make sure there's something on the stack and
		 * ask to be traced.
		 */
		gcs_recurse(0);
		if (ptrace(PTRACE_TRACEME, -1, NULL, NULL))
			ksft_exit_fail_msg("PTRACE_TRACEME %s",
					   strerror(errno));

		if (raise(SIGSTOP))
			ksft_exit_fail_msg("raise(SIGSTOP) %s",
					   strerror(errno));

		return;
	}

	ksft_print_msg("Child: %d\n", child);

	/* Attach to the child */
	while (1) {
		int sig;

		pid = wait(&status);
		if (pid == -1) {
			ksft_print_msg("wait() failed: %s",
				       strerror(errno));
			goto error;
		}

		/*
		 * This should never happen but it's hard to flag in
		 * the framework.
		 */
		if (pid != child)
			continue;

		if (WIFEXITED(status) || WIFSIGNALED(status))
			ksft_exit_fail_msg("Child died unexpectedly\n");

		if (!WIFSTOPPED(status))
			goto error;

		sig = WSTOPSIG(status);

		if (ptrace(PTRACE_GETSIGINFO, pid, NULL, &si)) {
			if (errno == ESRCH) {
				ASSERT_NE(errno, ESRCH);
				return;
			}

			if (errno == EINVAL) {
				sig = 0; /* bust group-stop */
				goto cont;
			}

			ksft_print_msg("PTRACE_GETSIGINFO: %s\n",
				       strerror(errno));
			goto error;
		}

		if (sig == SIGSTOP && si.si_code == SI_TKILL &&
		    si.si_pid == pid)
			break;

	cont:
		if (ptrace(PTRACE_CONT, pid, NULL, sig)) {
			if (errno == ESRCH) {
				ASSERT_NE(errno, ESRCH);
				return;
			}

			ksft_print_msg("PTRACE_CONT: %s\n", strerror(errno));
			goto error;
		}
	}

	/* Where is the child GCS? */
	iov.iov_base = &child_gcs;
	iov.iov_len = sizeof(child_gcs);
	ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_GCS, &iov);
	if (ret != 0) {
		ksft_print_msg("Failed to read child GCS state: %s (%d)\n",
			       strerror(errno), errno);
		goto error;
	}

	/* We should have inherited GCS over fork(), confirm */
	if (!(child_gcs.features_enabled & PR_SHADOW_STACK_ENABLE)) {
		ASSERT_TRUE(child_gcs.features_enabled &
			    PR_SHADOW_STACK_ENABLE);
		goto error;
	}

	gcspr = child_gcs.gcspr_el0;
	ksft_print_msg("Child GCSPR 0x%lx, flags %llx, locked %llx\n",
		       gcspr, child_gcs.features_enabled,
		       child_gcs.features_locked);

	/* Ideally we'd cross check with the child memory map */

	errno = 0;
	val = ptrace(PTRACE_PEEKDATA, child, (void *)gcspr, NULL);
	ret = errno;
	if (ret != 0)
		ksft_print_msg("PTRACE_PEEKDATA failed: %s (%d)\n",
			       strerror(ret), ret);
	EXPECT_EQ(ret, 0);

	/* The child should be in a function, the GCSPR shouldn't be 0 */
	EXPECT_NE(val, 0);

	/* Same thing via process_vm_readv() */
	local_iov.iov_base = &rval;
	local_iov.iov_len = sizeof(rval);
	remote_iov.iov_base = (void *)gcspr;
	remote_iov.iov_len = sizeof(rval);
	ret = process_vm_readv(child, &local_iov, 1, &remote_iov, 1, 0);
	if (ret == -1)
		ksft_print_msg("process_vm_readv() failed: %s (%d)\n",
			       strerror(errno), errno);
	EXPECT_EQ(ret, sizeof(rval));
	EXPECT_EQ(val, rval);

	/* Write data via a peek */
	ret = ptrace(PTRACE_POKEDATA, child, (void *)gcspr, NULL);
	if (ret == -1)
		ksft_print_msg("PTRACE_POKEDATA failed: %s (%d)\n",
			       strerror(errno), errno);
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(0, ptrace(PTRACE_PEEKDATA, child, (void *)gcspr, NULL));

	/* Restore what we had before */
	ret = ptrace(PTRACE_POKEDATA, child, (void *)gcspr, val);
	if (ret == -1)
		ksft_print_msg("PTRACE_POKEDATA failed: %s (%d)\n",
			       strerror(errno), errno);
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(val, ptrace(PTRACE_PEEKDATA, child, (void *)gcspr, NULL));

	/* That's all, folks */
	kill(child, SIGKILL);
	return;

error:
	kill(child, SIGKILL);
	ASSERT_FALSE(true);
}

FIXTURE(map_gcs)
{
	unsigned long *stack;
};

FIXTURE_VARIANT(map_gcs)
{
	size_t stack_size;
	unsigned long flags;
};

FIXTURE_VARIANT_ADD(map_gcs, s2k_cap_marker)
{
	.stack_size = 2 * 1024,
	.flags = SHADOW_STACK_SET_MARKER | SHADOW_STACK_SET_TOKEN,
};

FIXTURE_VARIANT_ADD(map_gcs, s2k_cap)
{
	.stack_size = 2 * 1024,
	.flags = SHADOW_STACK_SET_TOKEN,
};

FIXTURE_VARIANT_ADD(map_gcs, s2k_marker)
{
	.stack_size = 2 * 1024,
	.flags = SHADOW_STACK_SET_MARKER,
};

FIXTURE_VARIANT_ADD(map_gcs, s2k)
{
	.stack_size = 2 * 1024,
	.flags = 0,
};

FIXTURE_VARIANT_ADD(map_gcs, s4k_cap_marker)
{
	.stack_size = 4 * 1024,
	.flags = SHADOW_STACK_SET_MARKER | SHADOW_STACK_SET_TOKEN,
};

FIXTURE_VARIANT_ADD(map_gcs, s4k_cap)
{
	.stack_size = 4 * 1024,
	.flags = SHADOW_STACK_SET_TOKEN,
};

FIXTURE_VARIANT_ADD(map_gcs, s3k_marker)
{
	.stack_size = 4 * 1024,
	.flags = SHADOW_STACK_SET_MARKER,
};

FIXTURE_VARIANT_ADD(map_gcs, s4k)
{
	.stack_size = 4 * 1024,
	.flags = 0,
};

FIXTURE_VARIANT_ADD(map_gcs, s16k_cap_marker)
{
	.stack_size = 16 * 1024,
	.flags = SHADOW_STACK_SET_MARKER | SHADOW_STACK_SET_TOKEN,
};

FIXTURE_VARIANT_ADD(map_gcs, s16k_cap)
{
	.stack_size = 16 * 1024,
	.flags = SHADOW_STACK_SET_TOKEN,
};

FIXTURE_VARIANT_ADD(map_gcs, s16k_marker)
{
	.stack_size = 16 * 1024,
	.flags = SHADOW_STACK_SET_MARKER,
};

FIXTURE_VARIANT_ADD(map_gcs, s16k)
{
	.stack_size = 16 * 1024,
	.flags = 0,
};

FIXTURE_VARIANT_ADD(map_gcs, s64k_cap_marker)
{
	.stack_size = 64 * 1024,
	.flags = SHADOW_STACK_SET_MARKER | SHADOW_STACK_SET_TOKEN,
};

FIXTURE_VARIANT_ADD(map_gcs, s64k_cap)
{
	.stack_size = 64 * 1024,
	.flags = SHADOW_STACK_SET_TOKEN,
};

FIXTURE_VARIANT_ADD(map_gcs, s64k_marker)
{
	.stack_size = 64 * 1024,
	.flags = SHADOW_STACK_SET_MARKER,
};

FIXTURE_VARIANT_ADD(map_gcs, s64k)
{
	.stack_size = 64 * 1024,
	.flags = 0,
};

FIXTURE_VARIANT_ADD(map_gcs, s128k_cap_marker)
{
	.stack_size = 128 * 1024,
	.flags = SHADOW_STACK_SET_MARKER | SHADOW_STACK_SET_TOKEN,
};

FIXTURE_VARIANT_ADD(map_gcs, s128k_cap)
{
	.stack_size = 128 * 1024,
	.flags = SHADOW_STACK_SET_TOKEN,
};

FIXTURE_VARIANT_ADD(map_gcs, s128k_marker)
{
	.stack_size = 128 * 1024,
	.flags = SHADOW_STACK_SET_MARKER,
};

FIXTURE_VARIANT_ADD(map_gcs, s128k)
{
	.stack_size = 128 * 1024,
	.flags = 0,
};

FIXTURE_SETUP(map_gcs)
{
	self->stack = (void *)syscall(__NR_map_shadow_stack, 0,
				      variant->stack_size, 
				      variant->flags);
	ASSERT_FALSE(self->stack == MAP_FAILED);
	ksft_print_msg("Allocated stack from %p-%p\n", self->stack,
		       self->stack + variant->stack_size);
}

FIXTURE_TEARDOWN(map_gcs)
{
	int ret;

	if (self->stack != MAP_FAILED) {
		ret = munmap(self->stack, variant->stack_size);
		ASSERT_EQ(ret, 0);
	}
}

/* The stack has a cap token */
TEST_F(map_gcs, stack_capped)
{
	unsigned long *stack = self->stack;
	size_t cap_index;

	cap_index = (variant->stack_size / sizeof(unsigned long));

	switch (variant->flags & (SHADOW_STACK_SET_MARKER | SHADOW_STACK_SET_TOKEN)) {
	case SHADOW_STACK_SET_MARKER | SHADOW_STACK_SET_TOKEN:
		cap_index -= 2;
		break;
	case SHADOW_STACK_SET_TOKEN:
		cap_index -= 1;
		break;
	case SHADOW_STACK_SET_MARKER:
	case 0:
		/* No cap, no test */
		return;
	}

	ASSERT_EQ(stack[cap_index], GCS_CAP(&stack[cap_index]));
}

/* The top of the stack is 0 */
TEST_F(map_gcs, stack_terminated)
{
	unsigned long *stack = self->stack;
	size_t term_index;

	if (!(variant->flags & SHADOW_STACK_SET_MARKER))
		return;

	term_index = (variant->stack_size / sizeof(unsigned long)) - 1;

	ASSERT_EQ(stack[term_index], 0);
}

/* Writes should fault */
TEST_F_SIGNAL(map_gcs, not_writeable, SIGSEGV)
{
	self->stack[0] = 0;
}

/* Put it all together, we can safely switch to and from the stack */
TEST_F(map_gcs, stack_switch)
{
	size_t cap_index;
	cap_index = (variant->stack_size / sizeof(unsigned long));
	unsigned long *orig_gcspr_el0, *pivot_gcspr_el0;

	/* Skip over the stack terminator and point at the cap */
	switch (variant->flags & (SHADOW_STACK_SET_MARKER | SHADOW_STACK_SET_TOKEN)) {
	case SHADOW_STACK_SET_MARKER | SHADOW_STACK_SET_TOKEN:
		cap_index -= 2;
		break;
	case SHADOW_STACK_SET_TOKEN:
		cap_index -= 1;
		break;
	case SHADOW_STACK_SET_MARKER:
	case 0:
		/* No cap, no test */
		return;
	}
	pivot_gcspr_el0 = &self->stack[cap_index];

	/* Pivot to the new GCS */
	ksft_print_msg("Pivoting to %p from %p, target has value 0x%lx\n",
		       pivot_gcspr_el0, get_gcspr(),
		       *pivot_gcspr_el0);
	gcsss1(pivot_gcspr_el0);
	orig_gcspr_el0 = gcsss2();
	ksft_print_msg("Pivoted to %p from %p, target has value 0x%lx\n",
		       get_gcspr(), orig_gcspr_el0,
		       *pivot_gcspr_el0);

	ksft_print_msg("Pivoted, GCSPR_EL0 now %p\n", get_gcspr());

	/* New GCS must be in the new buffer */
	ASSERT_TRUE((unsigned long)get_gcspr() > (unsigned long)self->stack);
	ASSERT_TRUE((unsigned long)get_gcspr() <=
		    (unsigned long)self->stack + variant->stack_size);

	/* We should be able to use all but 2 slots of the new stack */
	ksft_print_msg("Recursing %zu levels\n", cap_index - 1);
	gcs_recurse(cap_index - 1);

	/* Pivot back to the original GCS */
	gcsss1(orig_gcspr_el0);
	pivot_gcspr_el0 = gcsss2();

	gcs_recurse(0);
	ksft_print_msg("Pivoted back to GCSPR_EL0 0x%p\n", get_gcspr());
}

/* We fault if we try to go beyond the end of the stack */
TEST_F_SIGNAL(map_gcs, stack_overflow, SIGSEGV)
{
	size_t cap_index;
	cap_index = (variant->stack_size / sizeof(unsigned long));
	unsigned long *orig_gcspr_el0, *pivot_gcspr_el0;

	/* Skip over the stack terminator and point at the cap */
	switch (variant->flags & (SHADOW_STACK_SET_MARKER | SHADOW_STACK_SET_TOKEN)) {
	case SHADOW_STACK_SET_MARKER | SHADOW_STACK_SET_TOKEN:
		cap_index -= 2;
		break;
	case SHADOW_STACK_SET_TOKEN:
		cap_index -= 1;
		break;
	case SHADOW_STACK_SET_MARKER:
	case 0:
		/* No cap, no test but we need to SEGV to avoid a false fail */
		orig_gcspr_el0 = get_gcspr();
		*orig_gcspr_el0 = 0;
		return;
	}
	pivot_gcspr_el0 = &self->stack[cap_index];

	/* Pivot to the new GCS */
	ksft_print_msg("Pivoting to %p from %p, target has value 0x%lx\n",
		       pivot_gcspr_el0, get_gcspr(),
		       *pivot_gcspr_el0);
	gcsss1(pivot_gcspr_el0);
	orig_gcspr_el0 = gcsss2();
	ksft_print_msg("Pivoted to %p from %p, target has value 0x%lx\n",
		       pivot_gcspr_el0, orig_gcspr_el0,
		       *pivot_gcspr_el0);

	ksft_print_msg("Pivoted, GCSPR_EL0 now %p\n", get_gcspr());

	/* New GCS must be in the new buffer */
	ASSERT_TRUE((unsigned long)get_gcspr() > (unsigned long)self->stack);
	ASSERT_TRUE((unsigned long)get_gcspr() <=
		    (unsigned long)self->stack + variant->stack_size);

	/* Now try to recurse, we should fault doing this. */
	ksft_print_msg("Recursing %zu levels...\n", cap_index + 1);
	gcs_recurse(cap_index + 1);
	ksft_print_msg("...done\n");

	/* Clean up properly to try to guard against spurious passes. */
	gcsss1(orig_gcspr_el0);
	pivot_gcspr_el0 = gcsss2();
	ksft_print_msg("Pivoted back to GCSPR_EL0 0x%p\n", get_gcspr());
}

FIXTURE(map_invalid_gcs)
{
};

FIXTURE_VARIANT(map_invalid_gcs)
{
	size_t stack_size;
};

FIXTURE_SETUP(map_invalid_gcs)
{
}

FIXTURE_TEARDOWN(map_invalid_gcs)
{
}

/* GCS must be larger than 16 bytes */
FIXTURE_VARIANT_ADD(map_invalid_gcs, too_small)
{
	.stack_size = 8,
};

/* GCS size must be 16 byte aligned */
FIXTURE_VARIANT_ADD(map_invalid_gcs, unligned_1)  { .stack_size = 1024 + 1  };
FIXTURE_VARIANT_ADD(map_invalid_gcs, unligned_2)  { .stack_size = 1024 + 2  };
FIXTURE_VARIANT_ADD(map_invalid_gcs, unligned_3)  { .stack_size = 1024 + 3  };
FIXTURE_VARIANT_ADD(map_invalid_gcs, unligned_4)  { .stack_size = 1024 + 4  };
FIXTURE_VARIANT_ADD(map_invalid_gcs, unligned_5)  { .stack_size = 1024 + 5  };
FIXTURE_VARIANT_ADD(map_invalid_gcs, unligned_6)  { .stack_size = 1024 + 6  };
FIXTURE_VARIANT_ADD(map_invalid_gcs, unligned_7)  { .stack_size = 1024 + 7  };

TEST_F(map_invalid_gcs, do_map)
{
	void *stack;

	stack = (void *)syscall(__NR_map_shadow_stack, 0,
				variant->stack_size, 0);
	ASSERT_TRUE(stack == MAP_FAILED);
	if (stack != MAP_FAILED)
		munmap(stack, variant->stack_size);
}

FIXTURE(invalid_mprotect)
{
	unsigned long *stack;
	size_t stack_size;
};

FIXTURE_VARIANT(invalid_mprotect)
{
	unsigned long flags;
};

FIXTURE_SETUP(invalid_mprotect)
{
	self->stack_size = sysconf(_SC_PAGE_SIZE);
	self->stack = (void *)syscall(__NR_map_shadow_stack, 0,
				      self->stack_size, 0);
	ASSERT_FALSE(self->stack == MAP_FAILED);
	ksft_print_msg("Allocated stack from %p-%p\n", self->stack,
		       self->stack + self->stack_size);
}

FIXTURE_TEARDOWN(invalid_mprotect)
{
	int ret;

	if (self->stack != MAP_FAILED) {
		ret = munmap(self->stack, self->stack_size);
		ASSERT_EQ(ret, 0);
	}
}

FIXTURE_VARIANT_ADD(invalid_mprotect, exec)
{
	.flags = PROT_EXEC,
};

TEST_F(invalid_mprotect, do_map)
{
	int ret;

	ret = mprotect(self->stack, self->stack_size, variant->flags);
	ASSERT_EQ(ret, -1);
}

TEST_F(invalid_mprotect, do_map_read)
{
	int ret;

	ret = mprotect(self->stack, self->stack_size,
		       variant->flags | PROT_READ);
	ASSERT_EQ(ret, -1);
}

int main(int argc, char **argv)
{
	unsigned long gcs_mode;
	int ret;

	if (!(getauxval(AT_HWCAP) & HWCAP_GCS))
		ksft_exit_skip("SKIP GCS not supported\n");

	/* 
	 * Force shadow stacks on, our tests *should* be fine with or
	 * without libc support and with or without this having ended
	 * up tagged for GCS and enabled by the dynamic linker.  We
	 * can't use the libc prctl() function since we can't return
	 * from enabling the stack.
	 */
	ret = my_syscall2(__NR_prctl, PR_GET_SHADOW_STACK_STATUS, &gcs_mode);
	if (ret) {
		ksft_print_msg("Failed to read GCS state: %d\n", ret);
		return EXIT_FAILURE;
	}
	
	if (!(gcs_mode & PR_SHADOW_STACK_ENABLE)) {
		gcs_mode = PR_SHADOW_STACK_ENABLE;
		ret = my_syscall2(__NR_prctl, PR_SET_SHADOW_STACK_STATUS,
				  gcs_mode);
		if (ret) {
			ksft_print_msg("Failed to configure GCS: %d\n", ret);
			return EXIT_FAILURE;
		}
	}

	/* Avoid returning in case libc doesn't understand GCS */
	exit(test_harness_run(argc, argv));
}
