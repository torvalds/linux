// SPDX-License-Identifier: GPL-2.0
/*
 * clone3(CLONE_VM|CLONE_PIDFD) with an unwritable pidfd pointer must
 * not leak a GCS VMA. copy_thread() maps one, then put_user() fails
 * and copy_process() calls exit_thread(). Without HAVE_EXIT_THREAD
 * that free is a no-op and the VMA stays in the parent's mm.
 *
 * After enabling GCS, do not return from main(): libc frames above
 * that point have no GCS entries.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <linux/sched.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <asm/hwcap.h>

#include "kselftest.h"
#include "gcs-util.h"

#ifndef HWCAP_GCS
#define HWCAP_GCS		(1UL << 32)
#endif

#ifndef __NR_clone3
#define __NR_clone3 435
#endif

#define NCLONE			16
#define STACK_SIZE		(64 * 1024)

static int count_ss_vmas(void)
{
	FILE *f;
	char line[256];
	int vmas = 0;

	f = fopen("/proc/self/smaps", "r");
	if (!f)
		ksft_exit_fail_msg("fopen /proc/self/smaps: %s\n",
				   strerror(errno));

	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "VmFlags:", 8) && strstr(line, " ss"))
			vmas++;
	}
	fclose(f);
	return vmas;
}

/*
 * CLONE_VM maps a new GCS. CLONE_PIDFD lets put_user() fail after that.
 * Use a raw svc: the child resumes on an empty GCS, so a libc return
 * after clone3 would fault; exit the same way.
 */
static long clone_pidfd(void *stack, size_t stack_size, int *pidfd_ptr)
{
	struct clone_args args = {
		.flags = CLONE_VM | CLONE_PIDFD,
		.pidfd = (uint64_t)(uintptr_t)pidfd_ptr,
		.exit_signal = SIGCHLD,
		.stack = (uint64_t)(uintptr_t)stack,
		.stack_size = stack_size,
	};
	register long num __asm__("x8") = __NR_clone3;
	register long ret __asm__("x0") = (long)&args;
	register long sz __asm__("x1") = sizeof(args);
	int st;

	asm volatile("svc #0"
		     : "+r"(ret)
		     : "r"(num), "r"(sz)
		     : "memory", "cc");
	if (ret == 0) {
		register long n __asm__("x8") = __NR_exit;
		register long e __asm__("x0") = 0;

		asm volatile("svc #0" : : "r"(n), "r"(e) : "memory");
		__builtin_unreachable();
	}
	if (ret < 0)
		return ret;
	if (waitpid((pid_t)ret, &st, 0) < 0)
		return -errno;
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
		return 1;
	return 0;
}

int main(void)
{
	void *stack;
	void *bad_pidfd;
	unsigned long gcs_mode;
	long rc;
	int pidfd = -1;
	int vmas0, vmas1;
	int i, n_efault = 0;

	ksft_print_header();

	if (!(getauxval(AT_HWCAP) & HWCAP_GCS))
		ksft_exit_skip("GCS not supported\n");

	stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	if (stack == MAP_FAILED)
		ksft_exit_fail_msg("mmap stack: %s\n", strerror(errno));

	bad_pidfd = mmap(NULL, 4096, PROT_NONE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (bad_pidfd == MAP_FAILED)
		ksft_exit_fail_msg("mmap pidfd page: %s\n", strerror(errno));

	rc = my_syscall2(__NR_prctl, PR_GET_SHADOW_STACK_STATUS, &gcs_mode);
	if (rc)
		ksft_exit_fail_msg("PR_GET_SHADOW_STACK_STATUS: %ld\n", rc);

	if (!(gcs_mode & PR_SHADOW_STACK_ENABLE)) {
		rc = my_syscall2(__NR_prctl, PR_SET_SHADOW_STACK_STATUS,
				 PR_SHADOW_STACK_ENABLE);
		if (rc == -EOPNOTSUPP || rc == -EINVAL || rc == -EBUSY)
			ksft_exit_skip("cannot enable GCS: %ld\n", rc);
		if (rc)
			ksft_exit_fail_msg("PR_SET_SHADOW_STACK_STATUS: %ld\n",
					   rc);
	}

	rc = clone_pidfd(stack, STACK_SIZE, &pidfd);
	if (rc == -ENOSYS || rc == -EINVAL || rc == -EPERM)
		ksft_exit_skip("clone3 valid pidfd unavailable: %s\n",
			       strerror(-rc));

	ksft_set_plan(3);

	ksft_test_result(rc == 0 && pidfd >= 0,
			 "clone3 valid pidfd (rc=%ld pidfd=%d)\n",
			 rc, pidfd);
	if (pidfd >= 0)
		close(pidfd);

	vmas0 = count_ss_vmas();

	for (i = 0; i < NCLONE; i++) {
		rc = clone_pidfd(stack, STACK_SIZE, bad_pidfd);
		if (rc == -EFAULT)
			n_efault++;
	}
	ksft_test_result(n_efault == NCLONE,
			 "clone3 bad pidfd returns -EFAULT x%d\n", NCLONE);

	vmas1 = count_ss_vmas();
	ksft_test_result(vmas1 == vmas0,
			 "no GCS VMA leak (%d -> %d)\n", vmas0, vmas1);

	ksft_finished();
}
