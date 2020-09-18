// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#include <sys/auxv.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <setjmp.h>

#include "../../kselftest_harness.h"
#include "helper.h"

#define PAC_COLLISION_ATTEMPTS 10
/*
 * The kernel sets TBID by default. So bits 55 and above should remain
 * untouched no matter what.
 * The VA space size is 48 bits. Bigger is opt-in.
 */
#define PAC_MASK (~0xff80ffffffffffff)
#define ASSERT_PAUTH_ENABLED() \
do { \
	unsigned long hwcaps = getauxval(AT_HWCAP); \
	/* data key instructions are not in NOP space. This prevents a SIGILL */ \
	ASSERT_NE(0, hwcaps & HWCAP_PACA) TH_LOG("PAUTH not enabled"); \
} while (0)
#define ASSERT_GENERIC_PAUTH_ENABLED() \
do { \
	unsigned long hwcaps = getauxval(AT_HWCAP); \
	/* generic key instructions are not in NOP space. This prevents a SIGILL */ \
	ASSERT_NE(0, hwcaps & HWCAP_PACG) TH_LOG("Generic PAUTH not enabled"); \
} while (0)

void sign_specific(struct signatures *sign, size_t val)
{
	sign->keyia = keyia_sign(val);
	sign->keyib = keyib_sign(val);
	sign->keyda = keyda_sign(val);
	sign->keydb = keydb_sign(val);
}

void sign_all(struct signatures *sign, size_t val)
{
	sign->keyia = keyia_sign(val);
	sign->keyib = keyib_sign(val);
	sign->keyda = keyda_sign(val);
	sign->keydb = keydb_sign(val);
	sign->keyg  = keyg_sign(val);
}

int n_same(struct signatures *old, struct signatures *new, int nkeys)
{
	int res = 0;

	res += old->keyia == new->keyia;
	res += old->keyib == new->keyib;
	res += old->keyda == new->keyda;
	res += old->keydb == new->keydb;
	if (nkeys == NKEYS)
		res += old->keyg == new->keyg;

	return res;
}

int exec_sign_all(struct signatures *signed_vals, size_t val)
{
	int new_stdin[2];
	int new_stdout[2];
	int status;
	ssize_t ret;
	pid_t pid;

	ret = pipe(new_stdin);
	if (ret == -1) {
		perror("pipe returned error");
		return -1;
	}

	ret = pipe(new_stdout);
	if (ret == -1) {
		perror("pipe returned error");
		return -1;
	}

	pid = fork();
	// child
	if (pid == 0) {
		dup2(new_stdin[0], STDIN_FILENO);
		if (ret == -1) {
			perror("dup2 returned error");
			exit(1);
		}

		dup2(new_stdout[1], STDOUT_FILENO);
		if (ret == -1) {
			perror("dup2 returned error");
			exit(1);
		}

		close(new_stdin[0]);
		close(new_stdin[1]);
		close(new_stdout[0]);
		close(new_stdout[1]);

		ret = execl("exec_target", "exec_target", (char *)NULL);
		if (ret == -1) {
			perror("exec returned error");
			exit(1);
		}
	}

	close(new_stdin[0]);
	close(new_stdout[1]);

	ret = write(new_stdin[1], &val, sizeof(size_t));
	if (ret == -1) {
		perror("write returned error");
		return -1;
	}

	/*
	 * wait for the worker to finish, so that read() reads all data
	 * will also context switch with worker so that this function can be used
	 * for context switch tests
	 */
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) == 0) {
		fprintf(stderr, "worker exited unexpectedly\n");
		return -1;
	}
	if (WEXITSTATUS(status) != 0) {
		fprintf(stderr, "worker exited with error\n");
		return -1;
	}

	ret = read(new_stdout[0], signed_vals, sizeof(struct signatures));
	if (ret == -1) {
		perror("read returned error");
		return -1;
	}

	return 0;
}

sigjmp_buf jmpbuf;
void pac_signal_handler(int signum, siginfo_t *si, void *uc)
{
	if (signum == SIGSEGV || signum == SIGILL)
		siglongjmp(jmpbuf, 1);
}

/* check that a corrupted PAC results in SIGSEGV or SIGILL */
TEST(corrupt_pac)
{
	struct sigaction sa;

	ASSERT_PAUTH_ENABLED();
	if (sigsetjmp(jmpbuf, 1) == 0) {
		sa.sa_sigaction = pac_signal_handler;
		sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
		sigemptyset(&sa.sa_mask);

		sigaction(SIGSEGV, &sa, NULL);
		sigaction(SIGILL, &sa, NULL);

		pac_corruptor();
		ASSERT_TRUE(0) TH_LOG("SIGSEGV/SIGILL signal did not occur");
	}
}

/*
 * There are no separate pac* and aut* controls so checking only the pac*
 * instructions is sufficient
 */
TEST(pac_instructions_not_nop)
{
	size_t keyia = 0;
	size_t keyib = 0;
	size_t keyda = 0;
	size_t keydb = 0;

	ASSERT_PAUTH_ENABLED();

	for (int i = 0; i < PAC_COLLISION_ATTEMPTS; i++) {
		keyia |= keyia_sign(i) & PAC_MASK;
		keyib |= keyib_sign(i) & PAC_MASK;
		keyda |= keyda_sign(i) & PAC_MASK;
		keydb |= keydb_sign(i) & PAC_MASK;
	}

	ASSERT_NE(0, keyia) TH_LOG("keyia instructions did nothing");
	ASSERT_NE(0, keyib) TH_LOG("keyib instructions did nothing");
	ASSERT_NE(0, keyda) TH_LOG("keyda instructions did nothing");
	ASSERT_NE(0, keydb) TH_LOG("keydb instructions did nothing");
}

TEST(pac_instructions_not_nop_generic)
{
	size_t keyg = 0;

	ASSERT_GENERIC_PAUTH_ENABLED();

	for (int i = 0; i < PAC_COLLISION_ATTEMPTS; i++)
		keyg |= keyg_sign(i) & PAC_MASK;

	ASSERT_NE(0, keyg)  TH_LOG("keyg instructions did nothing");
}

/*
 * fork() does not change keys. Only exec() does so call a worker program.
 * Its only job is to sign a value and report back the resutls
 */
TEST(exec_changed_keys)
{
	struct signatures new_keys;
	struct signatures old_keys;
	int ret;
	int same = 10;
	int nkeys = NKEYS;
	unsigned long hwcaps = getauxval(AT_HWCAP);

	/* generic and data key instructions are not in NOP space. This prevents a SIGILL */
	ASSERT_NE(0, hwcaps & HWCAP_PACA) TH_LOG("PAUTH not enabled");
	if (!(hwcaps & HWCAP_PACG)) {
		TH_LOG("WARNING: Generic PAUTH not enabled. Skipping generic key checks");
		nkeys = NKEYS - 1;
	}

	for (int i = 0; i < PAC_COLLISION_ATTEMPTS; i++) {
		ret = exec_sign_all(&new_keys, i);
		ASSERT_EQ(0, ret) TH_LOG("failed to run worker");

		if (nkeys == NKEYS)
			sign_all(&old_keys, i);
		else
			sign_specific(&old_keys, i);

		ret = n_same(&old_keys, &new_keys, nkeys);
		if (ret < same)
			same = ret;
	}

	ASSERT_EQ(0, same) TH_LOG("exec() did not change %d keys", same);
}

TEST_HARNESS_MAIN
