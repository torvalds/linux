// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <x86intrin.h>

#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "helpers.h"
#include "xstate.h"

#ifndef __x86_64__
# error This test is 64-bit only
#endif

/* err() exits and will not return */
#define fatal_error(msg, ...)	err(1, "[FAIL]\t" msg, ##__VA_ARGS__)

#define XFEATURE_MASK_XTILECFG	(1 << XFEATURE_XTILECFG)
#define XFEATURE_MASK_XTILEDATA	(1 << XFEATURE_XTILEDATA)
#define XFEATURE_MASK_XTILE	(XFEATURE_MASK_XTILECFG | XFEATURE_MASK_XTILEDATA)

struct xstate_info xtiledata;

/* The helpers for managing XSAVE buffer and tile states: */

struct xsave_buffer *stashed_xsave;

static void init_stashed_xsave(void)
{
	stashed_xsave = alloc_xbuf();
	if (!stashed_xsave)
		fatal_error("failed to allocate stashed_xsave\n");
	clear_xstate_header(stashed_xsave);
}

static void free_stashed_xsave(void)
{
	free(stashed_xsave);
}

/* Work around printf() being unsafe in signals: */
#define SIGNAL_BUF_LEN 1000
char signal_message_buffer[SIGNAL_BUF_LEN];
void sig_print(char *msg)
{
	int left = SIGNAL_BUF_LEN - strlen(signal_message_buffer) - 1;

	strncat(signal_message_buffer, msg, left);
}

static volatile bool noperm_signaled;
static int noperm_errs;
/*
 * Signal handler for when AMX is used but
 * permission has not been obtained.
 */
static void handle_noperm(int sig, siginfo_t *si, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t *)ctx_void;
	void *xbuf = ctx->uc_mcontext.fpregs;
	struct _fpx_sw_bytes *sw_bytes;
	uint64_t features;

	/* Reset the signal message buffer: */
	signal_message_buffer[0] = '\0';
	sig_print("\tAt SIGILL handler,\n");

	if (si->si_code != ILL_ILLOPC) {
		noperm_errs++;
		sig_print("[FAIL]\tInvalid signal code.\n");
	} else {
		sig_print("[OK]\tValid signal code (ILL_ILLOPC).\n");
	}

	sw_bytes = get_fpx_sw_bytes(xbuf);
	/*
	 * Without permission, the signal XSAVE buffer should not
	 * have room for AMX register state (aka. xtiledata).
	 * Check that the size does not overlap with where xtiledata
	 * will reside.
	 *
	 * This also implies that no state components *PAST*
	 * XTILEDATA (features >=19) can be present in the buffer.
	 */
	if (sw_bytes->xstate_size <= xtiledata.xbuf_offset) {
		sig_print("[OK]\tValid xstate size\n");
	} else {
		noperm_errs++;
		sig_print("[FAIL]\tInvalid xstate size\n");
	}

	features = get_fpx_sw_bytes_features(xbuf);
	/*
	 * Without permission, the XTILEDATA feature
	 * bit should not be set.
	 */
	if ((features & XFEATURE_MASK_XTILEDATA) == 0) {
		sig_print("[OK]\tValid xstate mask\n");
	} else {
		noperm_errs++;
		sig_print("[FAIL]\tInvalid xstate mask\n");
	}

	noperm_signaled = true;
	ctx->uc_mcontext.gregs[REG_RIP] += 3; /* Skip the faulting XRSTOR */
}

/* Return true if XRSTOR is successful; otherwise, false. */
static inline bool xrstor_safe(struct xsave_buffer *xbuf, uint64_t mask)
{
	noperm_signaled = false;
	xrstor(xbuf, mask);

	/* Print any messages produced by the signal code: */
	printf("%s", signal_message_buffer);
	/*
	 * Reset the buffer to make sure any future printing
	 * only outputs new messages:
	 */
	signal_message_buffer[0] = '\0';

	if (noperm_errs)
		fatal_error("saw %d errors in noperm signal handler\n", noperm_errs);

	return !noperm_signaled;
}

/*
 * Use XRSTOR to populate the XTILEDATA registers with
 * random data.
 *
 * Return true if successful; otherwise, false.
 */
static inline bool load_rand_tiledata(struct xsave_buffer *xbuf)
{
	clear_xstate_header(xbuf);
	set_xstatebv(xbuf, XFEATURE_MASK_XTILEDATA);
	set_rand_data(&xtiledata, xbuf);
	return xrstor_safe(xbuf, XFEATURE_MASK_XTILEDATA);
}

enum expected_result { FAIL_EXPECTED, SUCCESS_EXPECTED };

/* arch_prctl() and sigaltstack() test */

#define ARCH_GET_XCOMP_SUPP	0x1021
#define ARCH_GET_XCOMP_PERM	0x1022
#define ARCH_REQ_XCOMP_PERM	0x1023

static void req_xtiledata_perm(void)
{
	syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA);
}

static void validate_req_xcomp_perm(enum expected_result exp)
{
	unsigned long bitmask, expected_bitmask;
	long rc;

	rc = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_PERM, &bitmask);
	if (rc) {
		fatal_error("prctl(ARCH_GET_XCOMP_PERM) error: %ld", rc);
	} else if (!(bitmask & XFEATURE_MASK_XTILECFG)) {
		fatal_error("ARCH_GET_XCOMP_PERM returns XFEATURE_XTILECFG off.");
	}

	rc = syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA);
	if (exp == FAIL_EXPECTED) {
		if (rc) {
			printf("[OK]\tARCH_REQ_XCOMP_PERM saw expected failure..\n");
			return;
		}

		fatal_error("ARCH_REQ_XCOMP_PERM saw unexpected success.\n");
	} else if (rc) {
		fatal_error("ARCH_REQ_XCOMP_PERM saw unexpected failure.\n");
	}

	expected_bitmask = bitmask | XFEATURE_MASK_XTILEDATA;

	rc = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_PERM, &bitmask);
	if (rc) {
		fatal_error("prctl(ARCH_GET_XCOMP_PERM) error: %ld", rc);
	} else if (bitmask != expected_bitmask) {
		fatal_error("ARCH_REQ_XCOMP_PERM set a wrong bitmask: %lx, expected: %lx.\n",
			    bitmask, expected_bitmask);
	} else {
		printf("\tARCH_REQ_XCOMP_PERM is successful.\n");
	}
}

static void validate_xcomp_perm(enum expected_result exp)
{
	bool load_success = load_rand_tiledata(stashed_xsave);

	if (exp == FAIL_EXPECTED) {
		if (load_success) {
			noperm_errs++;
			printf("[FAIL]\tLoad tiledata succeeded.\n");
		} else {
			printf("[OK]\tLoad tiledata failed.\n");
		}
	} else if (exp == SUCCESS_EXPECTED) {
		if (load_success) {
			printf("[OK]\tLoad tiledata succeeded.\n");
		} else {
			noperm_errs++;
			printf("[FAIL]\tLoad tiledata failed.\n");
		}
	}
}

#ifndef AT_MINSIGSTKSZ
#  define AT_MINSIGSTKSZ	51
#endif

static void *alloc_altstack(unsigned int size)
{
	void *altstack;

	altstack = mmap(NULL, size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);

	if (altstack == MAP_FAILED)
		fatal_error("mmap() for altstack");

	return altstack;
}

static void setup_altstack(void *addr, unsigned long size, enum expected_result exp)
{
	stack_t ss;
	int rc;

	memset(&ss, 0, sizeof(ss));
	ss.ss_size = size;
	ss.ss_sp = addr;

	rc = sigaltstack(&ss, NULL);

	if (exp == FAIL_EXPECTED) {
		if (rc) {
			printf("[OK]\tsigaltstack() failed.\n");
		} else {
			fatal_error("sigaltstack() succeeded unexpectedly.\n");
		}
	} else if (rc) {
		fatal_error("sigaltstack()");
	}
}

static void test_dynamic_sigaltstack(void)
{
	unsigned int small_size, enough_size;
	unsigned long minsigstksz;
	void *altstack;

	minsigstksz = getauxval(AT_MINSIGSTKSZ);
	printf("\tAT_MINSIGSTKSZ = %lu\n", minsigstksz);
	/*
	 * getauxval() itself can return 0 for failure or
	 * success.  But, in this case, AT_MINSIGSTKSZ
	 * will always return a >=0 value if implemented.
	 * Just check for 0.
	 */
	if (minsigstksz == 0) {
		printf("no support for AT_MINSIGSTKSZ, skipping sigaltstack tests\n");
		return;
	}

	enough_size = minsigstksz * 2;

	altstack = alloc_altstack(enough_size);
	printf("\tAllocate memory for altstack (%u bytes).\n", enough_size);

	/*
	 * Try setup_altstack() with a size which can not fit
	 * XTILEDATA.  ARCH_REQ_XCOMP_PERM should fail.
	 */
	small_size = minsigstksz - xtiledata.size;
	printf("\tAfter sigaltstack() with small size (%u bytes).\n", small_size);
	setup_altstack(altstack, small_size, SUCCESS_EXPECTED);
	validate_req_xcomp_perm(FAIL_EXPECTED);

	/*
	 * Try setup_altstack() with a size derived from
	 * AT_MINSIGSTKSZ.  It should be more than large enough
	 * and thus ARCH_REQ_XCOMP_PERM should succeed.
	 */
	printf("\tAfter sigaltstack() with enough size (%u bytes).\n", enough_size);
	setup_altstack(altstack, enough_size, SUCCESS_EXPECTED);
	validate_req_xcomp_perm(SUCCESS_EXPECTED);

	/*
	 * Try to coerce setup_altstack() to again accept a
	 * too-small altstack.  This ensures that big-enough
	 * sigaltstacks can not shrink to a too-small value
	 * once XTILEDATA permission is established.
	 */
	printf("\tThen, sigaltstack() with small size (%u bytes).\n", small_size);
	setup_altstack(altstack, small_size, FAIL_EXPECTED);
}

static void test_dynamic_state(void)
{
	pid_t parent, child, grandchild;

	parent = fork();
	if (parent < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (parent > 0) {
		int status;
		/* fork() succeeded.  Now in the parent. */

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("arch_prctl test parent exit");
		return;
	}
	/* fork() succeeded.  Now in the child . */

	printf("[RUN]\tCheck ARCH_REQ_XCOMP_PERM around process fork() and sigaltack() test.\n");

	printf("\tFork a child.\n");
	child = fork();
	if (child < 0) {
		fatal_error("fork");
	} else if (child > 0) {
		int status;

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("arch_prctl test child exit");
		_exit(0);
	}

	/*
	 * The permission request should fail without an
	 * XTILEDATA-compatible signal stack
	 */
	printf("\tTest XCOMP_PERM at child.\n");
	validate_xcomp_perm(FAIL_EXPECTED);

	/*
	 * Set up an XTILEDATA-compatible signal stack and
	 * also obtain permission to populate XTILEDATA.
	 */
	printf("\tTest dynamic sigaltstack at child:\n");
	test_dynamic_sigaltstack();

	/* Ensure that XTILEDATA can be populated. */
	printf("\tTest XCOMP_PERM again at child.\n");
	validate_xcomp_perm(SUCCESS_EXPECTED);

	printf("\tFork a grandchild.\n");
	grandchild = fork();
	if (grandchild < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (!grandchild) {
		/* fork() succeeded.  Now in the (grand)child. */
		printf("\tTest XCOMP_PERM at grandchild.\n");

		/*
		 * Ensure that the grandchild inherited
		 * permission and a compatible sigaltstack:
		 */
		validate_xcomp_perm(SUCCESS_EXPECTED);
	} else {
		int status;
		/* fork() succeeded.  Now in the parent. */

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("fork test grandchild");
	}

	_exit(0);
}

static inline int __compare_tiledata_state(struct xsave_buffer *xbuf1, struct xsave_buffer *xbuf2)
{
	return memcmp(&xbuf1->bytes[xtiledata.xbuf_offset],
		      &xbuf2->bytes[xtiledata.xbuf_offset],
		      xtiledata.size);
}

/*
 * Save current register state and compare it to @xbuf1.'
 *
 * Returns false if @xbuf1 matches the registers.
 * Returns true  if @xbuf1 differs from the registers.
 */
static inline bool __validate_tiledata_regs(struct xsave_buffer *xbuf1)
{
	struct xsave_buffer *xbuf2;
	int ret;

	xbuf2 = alloc_xbuf();
	if (!xbuf2)
		fatal_error("failed to allocate XSAVE buffer\n");

	xsave(xbuf2, XFEATURE_MASK_XTILEDATA);
	ret = __compare_tiledata_state(xbuf1, xbuf2);

	free(xbuf2);

	if (ret == 0)
		return false;
	return true;
}

static inline void validate_tiledata_regs_changed(struct xsave_buffer *xbuf)
{
	int ret = __validate_tiledata_regs(xbuf);

	if (ret == 0)
		fatal_error("TILEDATA registers did not change");
}

/* tiledata inheritance test */

static void test_fork(void)
{
	pid_t child, grandchild;

	child = fork();
	if (child < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (child > 0) {
		/* fork() succeeded.  Now in the parent. */
		int status;

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("fork test child");
		return;
	}
	/* fork() succeeded.  Now in the child. */
	printf("[RUN]\tCheck tile data inheritance.\n\tBefore fork(), load tiledata\n");

	load_rand_tiledata(stashed_xsave);

	grandchild = fork();
	if (grandchild < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (grandchild > 0) {
		/* fork() succeeded.  Still in the first child. */
		int status;

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("fork test grand child");
		_exit(0);
	}
	/* fork() succeeded.  Now in the (grand)child. */

	/*
	 * TILEDATA registers are not preserved across fork().
	 * Ensure that their value has changed:
	 */
	validate_tiledata_regs_changed(stashed_xsave);

	_exit(0);
}

int main(void)
{
	unsigned long features;
	long rc;

	rc = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_SUPP, &features);
	if (rc || (features & XFEATURE_MASK_XTILE) != XFEATURE_MASK_XTILE) {
		ksft_print_msg("no AMX support\n");
		return KSFT_SKIP;
	}

	xtiledata = get_xstate_info(XFEATURE_XTILEDATA);
	if (!xtiledata.size || !xtiledata.xbuf_offset) {
		fatal_error("xstate cpuid: invalid tile data size/offset: %d/%d",
			    xtiledata.size, xtiledata.xbuf_offset);
	}

	init_stashed_xsave();
	sethandler(SIGILL, handle_noperm, 0);

	test_dynamic_state();

	/* Request permission for the following tests */
	req_xtiledata_perm();

	test_fork();

	/*
	 * Perform generic xstate tests for context switching, ptrace,
	 * and signal.
	 */
	test_xstate(XFEATURE_XTILEDATA);

	clearhandler(SIGILL);
	free_stashed_xsave();

	return 0;
}
