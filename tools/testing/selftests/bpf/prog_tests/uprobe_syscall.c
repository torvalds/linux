// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>

#ifdef __x86_64__

#include <unistd.h>
#include <asm/ptrace.h>
#include <linux/compiler.h>
#include "uprobe_syscall.skel.h"

__naked unsigned long uretprobe_regs_trigger(void)
{
	asm volatile (
		"movq $0xdeadbeef, %rax\n"
		"ret\n"
	);
}

__naked void uretprobe_regs(struct pt_regs *before, struct pt_regs *after)
{
	asm volatile (
		"movq %r15,   0(%rdi)\n"
		"movq %r14,   8(%rdi)\n"
		"movq %r13,  16(%rdi)\n"
		"movq %r12,  24(%rdi)\n"
		"movq %rbp,  32(%rdi)\n"
		"movq %rbx,  40(%rdi)\n"
		"movq %r11,  48(%rdi)\n"
		"movq %r10,  56(%rdi)\n"
		"movq  %r9,  64(%rdi)\n"
		"movq  %r8,  72(%rdi)\n"
		"movq %rax,  80(%rdi)\n"
		"movq %rcx,  88(%rdi)\n"
		"movq %rdx,  96(%rdi)\n"
		"movq %rsi, 104(%rdi)\n"
		"movq %rdi, 112(%rdi)\n"
		"movq   $0, 120(%rdi)\n" /* orig_rax */
		"movq   $0, 128(%rdi)\n" /* rip      */
		"movq   $0, 136(%rdi)\n" /* cs       */
		"pushf\n"
		"pop %rax\n"
		"movq %rax, 144(%rdi)\n" /* eflags   */
		"movq %rsp, 152(%rdi)\n" /* rsp      */
		"movq   $0, 160(%rdi)\n" /* ss       */

		/* save 2nd argument */
		"pushq %rsi\n"
		"call uretprobe_regs_trigger\n"

		/* save  return value and load 2nd argument pointer to rax */
		"pushq %rax\n"
		"movq 8(%rsp), %rax\n"

		"movq %r15,   0(%rax)\n"
		"movq %r14,   8(%rax)\n"
		"movq %r13,  16(%rax)\n"
		"movq %r12,  24(%rax)\n"
		"movq %rbp,  32(%rax)\n"
		"movq %rbx,  40(%rax)\n"
		"movq %r11,  48(%rax)\n"
		"movq %r10,  56(%rax)\n"
		"movq  %r9,  64(%rax)\n"
		"movq  %r8,  72(%rax)\n"
		"movq %rcx,  88(%rax)\n"
		"movq %rdx,  96(%rax)\n"
		"movq %rsi, 104(%rax)\n"
		"movq %rdi, 112(%rax)\n"
		"movq   $0, 120(%rax)\n" /* orig_rax */
		"movq   $0, 128(%rax)\n" /* rip      */
		"movq   $0, 136(%rax)\n" /* cs       */

		/* restore return value and 2nd argument */
		"pop %rax\n"
		"pop %rsi\n"

		"movq %rax,  80(%rsi)\n"

		"pushf\n"
		"pop %rax\n"

		"movq %rax, 144(%rsi)\n" /* eflags   */
		"movq %rsp, 152(%rsi)\n" /* rsp      */
		"movq   $0, 160(%rsi)\n" /* ss       */
		"ret\n"
);
}

static void test_uretprobe_regs_equal(void)
{
	struct uprobe_syscall *skel = NULL;
	struct pt_regs before = {}, after = {};
	unsigned long *pb = (unsigned long *) &before;
	unsigned long *pa = (unsigned long *) &after;
	unsigned long *pp;
	unsigned int i, cnt;
	int err;

	skel = uprobe_syscall__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_syscall__open_and_load"))
		goto cleanup;

	err = uprobe_syscall__attach(skel);
	if (!ASSERT_OK(err, "uprobe_syscall__attach"))
		goto cleanup;

	uretprobe_regs(&before, &after);

	pp = (unsigned long *) &skel->bss->regs;
	cnt = sizeof(before)/sizeof(*pb);

	for (i = 0; i < cnt; i++) {
		unsigned int offset = i * sizeof(unsigned long);

		/*
		 * Check register before and after uretprobe_regs_trigger call
		 * that triggers the uretprobe.
		 */
		switch (offset) {
		case offsetof(struct pt_regs, rax):
			ASSERT_EQ(pa[i], 0xdeadbeef, "return value");
			break;
		default:
			if (!ASSERT_EQ(pb[i], pa[i], "register before-after value check"))
				fprintf(stdout, "failed register offset %u\n", offset);
		}

		/*
		 * Check register seen from bpf program and register after
		 * uretprobe_regs_trigger call
		 */
		switch (offset) {
		/*
		 * These values will be different (not set in uretprobe_regs),
		 * we don't care.
		 */
		case offsetof(struct pt_regs, orig_rax):
		case offsetof(struct pt_regs, rip):
		case offsetof(struct pt_regs, cs):
		case offsetof(struct pt_regs, rsp):
		case offsetof(struct pt_regs, ss):
			break;
		default:
			if (!ASSERT_EQ(pp[i], pa[i], "register prog-after value check"))
				fprintf(stdout, "failed register offset %u\n", offset);
		}
	}

cleanup:
	uprobe_syscall__destroy(skel);
}
#else
static void test_uretprobe_regs_equal(void)
{
	test__skip();
}
#endif

void test_uprobe_syscall(void)
{
	if (test__start_subtest("uretprobe_regs_equal"))
		test_uretprobe_regs_equal();
}
