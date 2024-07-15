// SPDX-License-Identifier: GPL-2.0+

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "dexcr.h"
#include "utils.h"

static int require_nphie(void)
{
	SKIP_IF_MSG(!dexcr_exists(), "DEXCR not supported");

	pr_set_dexcr(PR_PPC_DEXCR_NPHIE, PR_PPC_DEXCR_CTRL_SET | PR_PPC_DEXCR_CTRL_SET_ONEXEC);

	if (get_dexcr(EFFECTIVE) & DEXCR_PR_NPHIE)
		return 0;

	SKIP_IF_MSG(!(get_dexcr(EFFECTIVE) & DEXCR_PR_NPHIE),
		    "Failed to enable DEXCR[NPHIE]");

	return 0;
}

static jmp_buf hashchk_detected_buf;
static const char *hashchk_failure_msg;

static void hashchk_handler(int signum, siginfo_t *info, void *context)
{
	if (signum != SIGILL)
		hashchk_failure_msg = "wrong signal received";
	else if (info->si_code != ILL_ILLOPN)
		hashchk_failure_msg = "wrong signal code received";

	longjmp(hashchk_detected_buf, 0);
}

/*
 * Check that hashchk triggers when DEXCR[NPHIE] is enabled
 * and is detected as such by the kernel exception handler
 */
static int hashchk_detected_test(void)
{
	struct sigaction old;
	int err;

	err = require_nphie();
	if (err)
		return err;

	old = push_signal_handler(SIGILL, hashchk_handler);
	if (setjmp(hashchk_detected_buf))
		goto out;

	hashchk_failure_msg = NULL;
	do_bad_hashchk();
	hashchk_failure_msg = "hashchk failed to trigger";

out:
	pop_signal_handler(SIGILL, old);
	FAIL_IF_MSG(hashchk_failure_msg, hashchk_failure_msg);
	return 0;
}

#define HASH_COUNT 8

static unsigned long hash_values[HASH_COUNT + 1];

static void fill_hash_values(void)
{
	for (unsigned long i = 0; i < HASH_COUNT; i++)
		hashst(i, &hash_values[i]);

	/* Used to ensure the checks uses the same addresses as the hashes */
	hash_values[HASH_COUNT] = (unsigned long)&hash_values;
}

static unsigned int count_hash_values_matches(void)
{
	unsigned long matches = 0;

	for (unsigned long i = 0; i < HASH_COUNT; i++) {
		unsigned long orig_hash = hash_values[i];
		hash_values[i] = 0;

		hashst(i, &hash_values[i]);

		if (hash_values[i] == orig_hash)
			matches++;
	}

	return matches;
}

static int hashchk_exec_child(void)
{
	ssize_t count;

	fill_hash_values();

	count = write(STDOUT_FILENO, hash_values, sizeof(hash_values));
	return count == sizeof(hash_values) ? 0 : EOVERFLOW;
}

static char *hashchk_exec_child_args[] = { "hashchk_exec_child", NULL };

/*
 * Check that new programs get different keys so a malicious process
 * can't recreate a victim's hash values.
 */
static int hashchk_exec_random_key_test(void)
{
	pid_t pid;
	int err;
	int pipefd[2];

	err = require_nphie();
	if (err)
		return err;

	FAIL_IF_MSG(pipe(pipefd), "failed to create pipe");

	pid = fork();
	if (pid == 0) {
		if (dup2(pipefd[1], STDOUT_FILENO) == -1)
			_exit(errno);

		execve("/proc/self/exe", hashchk_exec_child_args, NULL);
		_exit(errno);
	}

	await_child_success(pid);
	FAIL_IF_MSG(read(pipefd[0], hash_values, sizeof(hash_values)) != sizeof(hash_values),
		    "missing expected child output");

	/* Verify the child used the same hash_values address */
	FAIL_IF_EXIT_MSG(hash_values[HASH_COUNT] != (unsigned long)&hash_values,
			 "bad address check");

	/* If all hashes are the same it means (most likely) same key */
	FAIL_IF_MSG(count_hash_values_matches() == HASH_COUNT, "shared key detected");

	return 0;
}

/*
 * Check that forks share the same key so that existing hash values
 * remain valid.
 */
static int hashchk_fork_share_key_test(void)
{
	pid_t pid;
	int err;

	err = require_nphie();
	if (err)
		return err;

	fill_hash_values();

	pid = fork();
	if (pid == 0) {
		if (count_hash_values_matches() != HASH_COUNT)
			_exit(1);
		_exit(0);
	}

	await_child_success(pid);
	return 0;
}

#define STACK_SIZE (1024 * 1024)

static int hashchk_clone_child_fn(void *args)
{
	fill_hash_values();
	return 0;
}

/*
 * Check that threads share the same key so that existing hash values
 * remain valid.
 */
static int hashchk_clone_share_key_test(void)
{
	void *child_stack;
	pid_t pid;
	int err;

	err = require_nphie();
	if (err)
		return err;

	child_stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);

	FAIL_IF_MSG(child_stack == MAP_FAILED, "failed to map child stack");

	pid = clone(hashchk_clone_child_fn, child_stack + STACK_SIZE,
		    CLONE_VM | SIGCHLD, NULL);

	await_child_success(pid);
	FAIL_IF_MSG(count_hash_values_matches() != HASH_COUNT,
		    "different key detected");

	return 0;
}

int main(int argc, char *argv[])
{
	int err = 0;

	if (argc >= 1 && !strcmp(argv[0], hashchk_exec_child_args[0]))
		return hashchk_exec_child();

	err |= test_harness(hashchk_detected_test, "hashchk_detected");
	err |= test_harness(hashchk_exec_random_key_test, "hashchk_exec_random_key");
	err |= test_harness(hashchk_fork_share_key_test, "hashchk_fork_share_key");
	err |= test_harness(hashchk_clone_share_key_test, "hashchk_clone_share_key");

	return err;
}
