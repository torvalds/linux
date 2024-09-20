// SPDX-License-Identifier: GPL-2.0-only
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <linux/ptrace.h>
#include "../../kselftest_harness.h"

#define RISCV_V_MAGIC		0x53465457
#define DEFAULT_VALUE		2
#define SIGNAL_HANDLER_OVERRIDE	3

static void simple_handle(int sig_no, siginfo_t *info, void *vcontext)
{
	ucontext_t *context = vcontext;

	context->uc_mcontext.__gregs[REG_PC] = context->uc_mcontext.__gregs[REG_PC] + 4;
}

static void vector_override(int sig_no, siginfo_t *info, void *vcontext)
{
	ucontext_t *context = vcontext;

	// vector state
	struct __riscv_extra_ext_header *ext;
	struct __riscv_v_ext_state *v_ext_state;

	/* Find the vector context. */
	ext = (void *)(&context->uc_mcontext.__fpregs);
	if (ext->hdr.magic != RISCV_V_MAGIC) {
		fprintf(stderr, "bad vector magic: %x\n", ext->hdr.magic);
		abort();
	}

	v_ext_state = (void *)((char *)(ext) + sizeof(*ext));

	*(int *)v_ext_state->datap = SIGNAL_HANDLER_OVERRIDE;

	context->uc_mcontext.__gregs[REG_PC] = context->uc_mcontext.__gregs[REG_PC] + 4;
}

static int vector_sigreturn(int data, void (*handler)(int, siginfo_t *, void *))
{
	int after_sigreturn;
	struct sigaction sig_action = {
		.sa_sigaction = handler,
		.sa_flags = SA_SIGINFO
	};

	sigaction(SIGSEGV, &sig_action, 0);

	asm(".option push				\n\
		.option		arch, +v		\n\
		vsetivli	x0, 1, e32, m1, ta, ma	\n\
		vmv.s.x		v0, %1			\n\
		# Generate SIGSEGV			\n\
		lw		a0, 0(x0)		\n\
		vmv.x.s		%0, v0			\n\
		.option pop" : "=r" (after_sigreturn) : "r" (data));

	return after_sigreturn;
}

TEST(vector_restore)
{
	int result;

	result = vector_sigreturn(DEFAULT_VALUE, &simple_handle);

	EXPECT_EQ(DEFAULT_VALUE, result);
}

TEST(vector_restore_signal_handler_override)
{
	int result;

	result = vector_sigreturn(DEFAULT_VALUE, &vector_override);

	EXPECT_EQ(SIGNAL_HANDLER_OVERRIDE, result);
}

TEST_HARNESS_MAIN
