// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>

#ifdef __x86_64__

#include <unistd.h>
#include <asm/ptrace.h>
#include <linux/compiler.h>
#include <linux/stringify.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <asm/prctl.h>
#include "uprobe_syscall.skel.h"
#include "uprobe_syscall_executed.skel.h"

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

#define BPF_TESTMOD_UPROBE_TEST_FILE "/sys/kernel/bpf_testmod_uprobe"

static int write_bpf_testmod_uprobe(unsigned long offset)
{
	size_t n, ret;
	char buf[30];
	int fd;

	n = sprintf(buf, "%lu", offset);

	fd = open(BPF_TESTMOD_UPROBE_TEST_FILE, O_WRONLY);
	if (fd < 0)
		return -errno;

	ret = write(fd, buf, n);
	close(fd);
	return ret != n ? (int) ret : 0;
}

static void test_uretprobe_regs_change(void)
{
	struct pt_regs before = {}, after = {};
	unsigned long *pb = (unsigned long *) &before;
	unsigned long *pa = (unsigned long *) &after;
	unsigned long cnt = sizeof(before)/sizeof(*pb);
	unsigned int i, err, offset;

	offset = get_uprobe_offset(uretprobe_regs_trigger);

	err = write_bpf_testmod_uprobe(offset);
	if (!ASSERT_OK(err, "register_uprobe"))
		return;

	uretprobe_regs(&before, &after);

	err = write_bpf_testmod_uprobe(0);
	if (!ASSERT_OK(err, "unregister_uprobe"))
		return;

	for (i = 0; i < cnt; i++) {
		unsigned int offset = i * sizeof(unsigned long);

		switch (offset) {
		case offsetof(struct pt_regs, rax):
			ASSERT_EQ(pa[i], 0x12345678deadbeef, "rax");
			break;
		case offsetof(struct pt_regs, rcx):
			ASSERT_EQ(pa[i], 0x87654321feebdaed, "rcx");
			break;
		case offsetof(struct pt_regs, r11):
			ASSERT_EQ(pa[i], (__u64) -1, "r11");
			break;
		default:
			if (!ASSERT_EQ(pa[i], pb[i], "register before-after value check"))
				fprintf(stdout, "failed register offset %u\n", offset);
		}
	}
}

#ifndef __NR_uretprobe
#define __NR_uretprobe 335
#endif

__naked unsigned long uretprobe_syscall_call_1(void)
{
	/*
	 * Pretend we are uretprobe trampoline to trigger the return
	 * probe invocation in order to verify we get SIGILL.
	 */
	asm volatile (
		"pushq %rax\n"
		"pushq %rcx\n"
		"pushq %r11\n"
		"movq $" __stringify(__NR_uretprobe) ", %rax\n"
		"syscall\n"
		"popq %r11\n"
		"popq %rcx\n"
		"retq\n"
	);
}

__naked unsigned long uretprobe_syscall_call(void)
{
	asm volatile (
		"call uretprobe_syscall_call_1\n"
		"retq\n"
	);
}

static void test_uretprobe_syscall_call(void)
{
	LIBBPF_OPTS(bpf_uprobe_multi_opts, opts,
		.retprobe = true,
	);
	struct uprobe_syscall_executed *skel;
	int pid, status, err, go[2], c = 0;

	if (!ASSERT_OK(pipe(go), "pipe"))
		return;

	skel = uprobe_syscall_executed__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_syscall_executed__open_and_load"))
		goto cleanup;

	pid = fork();
	if (!ASSERT_GE(pid, 0, "fork"))
		goto cleanup;

	/* child */
	if (pid == 0) {
		close(go[1]);

		/* wait for parent's kick */
		err = read(go[0], &c, 1);
		if (err != 1)
			exit(-1);

		uretprobe_syscall_call();
		_exit(0);
	}

	skel->links.test = bpf_program__attach_uprobe_multi(skel->progs.test, pid,
							    "/proc/self/exe",
							    "uretprobe_syscall_call", &opts);
	if (!ASSERT_OK_PTR(skel->links.test, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	/* kick the child */
	write(go[1], &c, 1);
	err = waitpid(pid, &status, 0);
	ASSERT_EQ(err, pid, "waitpid");

	/* verify the child got killed with SIGILL */
	ASSERT_EQ(WIFSIGNALED(status), 1, "WIFSIGNALED");
	ASSERT_EQ(WTERMSIG(status), SIGILL, "WTERMSIG");

	/* verify the uretprobe program wasn't called */
	ASSERT_EQ(skel->bss->executed, 0, "executed");

cleanup:
	uprobe_syscall_executed__destroy(skel);
	close(go[1]);
	close(go[0]);
}

/*
 * Borrowed from tools/testing/selftests/x86/test_shadow_stack.c.
 *
 * For use in inline enablement of shadow stack.
 *
 * The program can't return from the point where shadow stack gets enabled
 * because there will be no address on the shadow stack. So it can't use
 * syscall() for enablement, since it is a function.
 *
 * Based on code from nolibc.h. Keep a copy here because this can't pull
 * in all of nolibc.h.
 */
#define ARCH_PRCTL(arg1, arg2)					\
({								\
	long _ret;						\
	register long _num  asm("eax") = __NR_arch_prctl;	\
	register long _arg1 asm("rdi") = (long)(arg1);		\
	register long _arg2 asm("rsi") = (long)(arg2);		\
								\
	asm volatile (						\
		"syscall\n"					\
		: "=a"(_ret)					\
		: "r"(_arg1), "r"(_arg2),			\
		  "0"(_num)					\
		: "rcx", "r11", "memory", "cc"			\
	);							\
	_ret;							\
})

#ifndef ARCH_SHSTK_ENABLE
#define ARCH_SHSTK_ENABLE	0x5001
#define ARCH_SHSTK_DISABLE	0x5002
#define ARCH_SHSTK_SHSTK	(1ULL <<  0)
#endif

static void test_uretprobe_shadow_stack(void)
{
	if (ARCH_PRCTL(ARCH_SHSTK_ENABLE, ARCH_SHSTK_SHSTK)) {
		test__skip();
		return;
	}

	/* Run all of the uretprobe tests. */
	test_uretprobe_regs_equal();
	test_uretprobe_regs_change();
	test_uretprobe_syscall_call();

	ARCH_PRCTL(ARCH_SHSTK_DISABLE, ARCH_SHSTK_SHSTK);
}
#else
static void test_uretprobe_regs_equal(void)
{
	test__skip();
}

static void test_uretprobe_regs_change(void)
{
	test__skip();
}

static void test_uretprobe_syscall_call(void)
{
	test__skip();
}

static void test_uretprobe_shadow_stack(void)
{
	test__skip();
}
#endif

void test_uprobe_syscall(void)
{
	if (test__start_subtest("uretprobe_regs_equal"))
		test_uretprobe_regs_equal();
	if (test__start_subtest("uretprobe_regs_change"))
		test_uretprobe_regs_change();
	if (test__start_subtest("uretprobe_syscall_call"))
		test_uretprobe_syscall_call();
	if (test__start_subtest("uretprobe_shadow_stack"))
		test_uretprobe_shadow_stack();
}
