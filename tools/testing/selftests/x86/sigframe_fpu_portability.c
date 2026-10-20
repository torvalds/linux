// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/ucontext.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <cpuid.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stddef.h>
#include <setjmp.h>

#include "helpers.h"
#include "xstate.h"

#ifndef FP_XSTATE_MAGIC2_SIZE
#define FP_XSTATE_MAGIC2_SIZE	sizeof(FP_XSTATE_MAGIC2)
#endif

/*
 * This test verifies the FPU portability and consistency of the signal frame.
 *
 * - test_valid_shrunk_xstate_size:
 *   Verifies that the kernel restores state from a frame with xstate_size
 *   shrunk to only include active features.
 *
 * - test_invalid_shrunk_xstate_size:
 *   Verifies that the kernel rejects a frame if xstate_size is too small for
 *   the features enabled in xfeatures.
 */

#define SIGFRAME_XSTATE_HDR_OFFSET	512
#define XSTATE_SSE_ONLY_SIZE	(SIGFRAME_XSTATE_HDR_OFFSET + XSAVE_HDR_SIZE)
#define XFEATURE_MASK_FPSSE	((1 << XFEATURE_FP) | (1 << XFEATURE_SSE))

static uint32_t ymm_offset;
static uint32_t xstate_size_ymm;
static pid_t self_pid;

/*
 * Load %ymm0 from @v, invoke SYS_kill to deliver @sig, and store the
 * restored %ymm0 state back into @v within a single inline assembly
 * block so the compiler cannot clobber %xmm0/%ymm0 between steps.
 */
__attribute__((target("avx")))
static void raise_with_ymm0(int sig, uint64_t *v)
{
	register long rax asm("rax") = SYS_kill;
	register long rdi asm("rdi") = self_pid;
	register long rsi asm("rsi") = sig;

	asm volatile ("vmovdqu %0, %%ymm0\n\t"
		      "syscall\n\t"
		      "vmovdqu %%ymm0, %0"
		      : "+m" (*(char (*)[32])v), "+r" (rax)
		      : "r" (rdi), "r" (rsi)
		      : "rcx", "r11", "ymm0", "memory");
}

/*
 * Avoid using printf() in signal handlers as it is not
 * async-signal-safe.
 */
#define SIGNAL_BUF_LEN 1024
static char sig_err_buf[SIGNAL_BUF_LEN];

static void sig_print(const char *msg)
{
	int left = SIGNAL_BUF_LEN - strlen(sig_err_buf) - 1;

	strncat(sig_err_buf, msg, left);
}

static void check_avx_support(void)
{
	uint32_t eax, ebx, ecx, edx;
	struct xstate_info xstate;

	/* Check CPUID.01H:ECX.OSXSAVE[bit 27] before calling xgetbv to avoid #UD */
	__cpuid(1, eax, ebx, ecx, edx);
	if (!(ecx & (1 << 27)))
		ksft_exit_skip("OSXSAVE not enabled by OS\n");

	/* Check XCR0[2] (YMM) is enabled by OS */
	if (!(xgetbv(0) & (1 << XFEATURE_YMM)))
		ksft_exit_skip("AVX (YMM) not enabled in XCR0\n");

	xstate = get_xstate_info(XFEATURE_YMM);
	if (!xstate.size)
		ksft_exit_skip("AVX not supported by hardware\n");

	ymm_offset = xstate.xbuf_offset;
	xstate_size_ymm = xstate.xbuf_offset + xstate.size;
}

#define TEST_YMMH_VAL (0x5656565656565656UL)

static void __handle_shrunk_xstate_size(int sig, siginfo_t *si, void *ucp, bool valid_size)
{
	uint64_t xfeatures, *ymmh_p;
	struct xsave_buffer *xbuf;
	struct _fpx_sw_bytes *sw;
	ucontext_t *uc = ucp;
	void *fp;

	fp = uc->uc_mcontext.fpregs;
	if (!fp) {
		sig_print("fpregs is NULL\n");
		return;
	}

	sw = get_fpx_sw_bytes(fp);
	if (sw->magic1 != FP_XSTATE_MAGIC1) {
		sig_print("magic1 is not valid\n");
		return;
	}

	xbuf = (struct xsave_buffer *)fp;

	/*
	 * Both test cases shrink the frame to contain only AVX (FP + SSE + YMM).
	 * If valid_size is true, set xstate_size to match the enabled features.
	 * If valid_size is false, set xstate_size too small (SSE only), which
	 * the kernel must reject.
	 */
	if (valid_size)
		sw->xstate_size = xstate_size_ymm;
	else
		sw->xstate_size = XSTATE_SSE_ONLY_SIZE;

	xfeatures = get_xstatebv(xbuf);
	xfeatures &= XFEATURE_MASK_FPSSE | (1 << XFEATURE_YMM);
	set_xstatebv(xbuf, xfeatures);
	set_fpx_sw_bytes_features(fp, xfeatures);

	*(uint32_t *)(fp + sw->xstate_size) = FP_XSTATE_MAGIC2;

	if (valid_size) {
		ymmh_p = (uint64_t *)(fp + ymm_offset);
		ymmh_p[0] = TEST_YMMH_VAL;
		ymmh_p[1] = TEST_YMMH_VAL + 1;
	}

	/* clear everything after MAGIC2. */
	if (sw->xstate_size + FP_XSTATE_MAGIC2_SIZE < sw->extended_size)
		memset(fp + sw->xstate_size + FP_XSTATE_MAGIC2_SIZE, 0,
		       sw->extended_size - sw->xstate_size - FP_XSTATE_MAGIC2_SIZE);
}

static void handle_valid_shrunk_xstate_size(int sig, siginfo_t *si, void *ucp)
{
	__handle_shrunk_xstate_size(sig, si, ucp, true);
}

static void handle_invalid_shrunk_xstate_size(int sig, siginfo_t *si, void *ucp)
{
	__handle_shrunk_xstate_size(sig, si, ucp, false);
}

static void test_valid_shrunk_xstate_size(void)
{
	uint64_t v[4];

	sig_err_buf[0] = 0;
	sethandler(SIGUSR1, handle_valid_shrunk_xstate_size, 0);

	v[0] = 0x1111111111111111ULL;
	v[1] = 0x2222222222222222ULL;
	v[2] = 0x3333333333333333ULL;
	v[3] = 0x4444444444444444ULL;
	raise_with_ymm0(SIGUSR1, v);

	if (sig_err_buf[0])
		ksft_test_result_fail("%s\n", sig_err_buf);
	else if (v[2] == TEST_YMMH_VAL && v[3] == (TEST_YMMH_VAL + 1))
		ksft_test_result_pass("YMM state restored correctly from shrunk frame\n");
	else
		ksft_test_result_fail(
				"Got upper bits: 0x%lx 0x%lx (expected %lx %lx)\n",
			       v[2], v[3], TEST_YMMH_VAL, TEST_YMMH_VAL + 1);

	clearhandler(SIGUSR1);
}

static sigjmp_buf segv_jmpbuf;

static void handle_segv(int sig, siginfo_t *si, void *ucp)
{
	siglongjmp(segv_jmpbuf, 1);
}

static void test_invalid_shrunk_xstate_size(void)
{
	uint64_t v[4];

	sig_err_buf[0] = 0;
	sethandler(SIGUSR1, handle_invalid_shrunk_xstate_size, 0);
	sethandler(SIGSEGV, handle_segv, 0);

	if (sigsetjmp(segv_jmpbuf, 1) == 0) {
		v[0] = 0x1111111111111111ULL;
		v[1] = 0x2222222222222222ULL;
		v[2] = 0x3333333333333333ULL;
		v[3] = 0x4444444444444444ULL;
		raise_with_ymm0(SIGUSR1, v);
		sig_print("Inconsistent size was NOT rejected\n");
	}

	clearhandler(SIGUSR1);
	clearhandler(SIGSEGV);

	if (sig_err_buf[0])
		ksft_test_result_fail("%s\n", sig_err_buf);
	else
		ksft_test_result_pass("Inconsistent size correctly rejected\n");
}

int main(void)
{
	ksft_print_header();
	ksft_set_plan(2);

	self_pid = getpid();

	check_avx_support();

	test_valid_shrunk_xstate_size();
	test_invalid_shrunk_xstate_size();

	ksft_finished();
	return 0;
}
