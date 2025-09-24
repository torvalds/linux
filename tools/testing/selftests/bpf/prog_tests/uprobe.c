// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Hengqi Chen */

#include <test_progs.h>
#include <asm/ptrace.h>
#include "test_uprobe.skel.h"

static FILE *urand_spawn(int *pid)
{
	FILE *f;

	/* urandom_read's stdout is wired into f */
	f = popen("./urandom_read 1 report-pid", "r");
	if (!f)
		return NULL;

	if (fscanf(f, "%d", pid) != 1) {
		pclose(f);
		errno = EINVAL;
		return NULL;
	}

	return f;
}

static int urand_trigger(FILE **urand_pipe)
{
	int exit_code;

	/* pclose() waits for child process to exit and returns their exit code */
	exit_code = pclose(*urand_pipe);
	*urand_pipe = NULL;

	return exit_code;
}

static void test_uprobe_attach(void)
{
	LIBBPF_OPTS(bpf_uprobe_opts, uprobe_opts);
	struct test_uprobe *skel;
	FILE *urand_pipe = NULL;
	int urand_pid = 0, err;

	skel = test_uprobe__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	urand_pipe = urand_spawn(&urand_pid);
	if (!ASSERT_OK_PTR(urand_pipe, "urand_spawn"))
		goto cleanup;

	skel->bss->my_pid = urand_pid;

	/* Manual attach uprobe to urandlib_api
	 * There are two `urandlib_api` symbols in .dynsym section:
	 *   - urandlib_api@LIBURANDOM_READ_1.0.0
	 *   - urandlib_api@@LIBURANDOM_READ_2.0.0
	 * Both are global bind and would cause a conflict if user
	 * specify the symbol name without a version suffix
	 */
	uprobe_opts.func_name = "urandlib_api";
	skel->links.test4 = bpf_program__attach_uprobe_opts(skel->progs.test4,
							    urand_pid,
							    "./liburandom_read.so",
							    0 /* offset */,
							    &uprobe_opts);
	if (!ASSERT_ERR_PTR(skel->links.test4, "urandlib_api_attach_conflict"))
		goto cleanup;

	uprobe_opts.func_name = "urandlib_api@LIBURANDOM_READ_1.0.0";
	skel->links.test4 = bpf_program__attach_uprobe_opts(skel->progs.test4,
							    urand_pid,
							    "./liburandom_read.so",
							    0 /* offset */,
							    &uprobe_opts);
	if (!ASSERT_OK_PTR(skel->links.test4, "urandlib_api_attach_ok"))
		goto cleanup;

	/* Auto attach 3 u[ret]probes to urandlib_api_sameoffset */
	err = test_uprobe__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	/* trigger urandom_read */
	ASSERT_OK(urand_trigger(&urand_pipe), "urand_exit_code");

	ASSERT_EQ(skel->bss->test1_result, 1, "urandlib_api_sameoffset");
	ASSERT_EQ(skel->bss->test2_result, 1, "urandlib_api_sameoffset@v1");
	ASSERT_EQ(skel->bss->test3_result, 3, "urandlib_api_sameoffset@@v2");
	ASSERT_EQ(skel->bss->test4_result, 1, "urandlib_api");

cleanup:
	if (urand_pipe)
		pclose(urand_pipe);
	test_uprobe__destroy(skel);
}

#ifdef __x86_64__
__naked __maybe_unused unsigned long uprobe_regs_change_trigger(void)
{
	asm volatile (
		"ret\n"
	);
}

static __naked void uprobe_regs_change(struct pt_regs *before, struct pt_regs *after)
{
	asm volatile (
		"movq %r11,  48(%rdi)\n"
		"movq %r10,  56(%rdi)\n"
		"movq  %r9,  64(%rdi)\n"
		"movq  %r8,  72(%rdi)\n"
		"movq %rax,  80(%rdi)\n"
		"movq %rcx,  88(%rdi)\n"
		"movq %rdx,  96(%rdi)\n"
		"movq %rsi, 104(%rdi)\n"
		"movq %rdi, 112(%rdi)\n"

		/* save 2nd argument */
		"pushq %rsi\n"
		"call uprobe_regs_change_trigger\n"

		/* save  return value and load 2nd argument pointer to rax */
		"pushq %rax\n"
		"movq 8(%rsp), %rax\n"

		"movq %r11,  48(%rax)\n"
		"movq %r10,  56(%rax)\n"
		"movq  %r9,  64(%rax)\n"
		"movq  %r8,  72(%rax)\n"
		"movq %rcx,  88(%rax)\n"
		"movq %rdx,  96(%rax)\n"
		"movq %rsi, 104(%rax)\n"
		"movq %rdi, 112(%rax)\n"

		/* restore return value and 2nd argument */
		"pop %rax\n"
		"pop %rsi\n"

		"movq %rax,  80(%rsi)\n"
		"ret\n"
	);
}

static void regs_common(void)
{
	struct pt_regs before = {}, after = {}, expected = {
		.rax = 0xc0ffe,
		.rcx = 0xbad,
		.rdx = 0xdead,
		.r8  = 0x8,
		.r9  = 0x9,
		.r10 = 0x10,
		.r11 = 0x11,
		.rdi = 0x12,
		.rsi = 0x13,
	};
	LIBBPF_OPTS(bpf_uprobe_opts, uprobe_opts);
	struct test_uprobe *skel;

	skel = test_uprobe__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->bss->my_pid = getpid();
	skel->bss->regs = expected;

	uprobe_opts.func_name = "uprobe_regs_change_trigger";
	skel->links.test_regs_change = bpf_program__attach_uprobe_opts(skel->progs.test_regs_change,
							    -1,
							    "/proc/self/exe",
							    0 /* offset */,
							    &uprobe_opts);
	if (!ASSERT_OK_PTR(skel->links.test_regs_change, "bpf_program__attach_uprobe_opts"))
		goto cleanup;

	uprobe_regs_change(&before, &after);

	ASSERT_EQ(after.rax, expected.rax, "ax");
	ASSERT_EQ(after.rcx, expected.rcx, "cx");
	ASSERT_EQ(after.rdx, expected.rdx, "dx");
	ASSERT_EQ(after.r8,  expected.r8,  "r8");
	ASSERT_EQ(after.r9,  expected.r9,  "r9");
	ASSERT_EQ(after.r10, expected.r10, "r10");
	ASSERT_EQ(after.r11, expected.r11, "r11");
	ASSERT_EQ(after.rdi, expected.rdi, "rdi");
	ASSERT_EQ(after.rsi, expected.rsi, "rsi");

cleanup:
	test_uprobe__destroy(skel);
}

static noinline unsigned long uprobe_regs_change_ip_1(void)
{
	return 0xc0ffee;
}

static noinline unsigned long uprobe_regs_change_ip_2(void)
{
	return 0xdeadbeef;
}

static void regs_ip(void)
{
	LIBBPF_OPTS(bpf_uprobe_opts, uprobe_opts);
	struct test_uprobe *skel;
	unsigned long ret;

	skel = test_uprobe__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->bss->my_pid = getpid();
	skel->bss->ip = (unsigned long) uprobe_regs_change_ip_2;

	uprobe_opts.func_name = "uprobe_regs_change_ip_1";
	skel->links.test_regs_change_ip = bpf_program__attach_uprobe_opts(
						skel->progs.test_regs_change_ip,
						-1,
						"/proc/self/exe",
						0 /* offset */,
						&uprobe_opts);
	if (!ASSERT_OK_PTR(skel->links.test_regs_change_ip, "bpf_program__attach_uprobe_opts"))
		goto cleanup;

	ret = uprobe_regs_change_ip_1();
	ASSERT_EQ(ret, 0xdeadbeef, "ret");

cleanup:
	test_uprobe__destroy(skel);
}

static void test_uprobe_regs_change(void)
{
	if (test__start_subtest("regs_change_common"))
		regs_common();
	if (test__start_subtest("regs_change_ip"))
		regs_ip();
}
#else
static void test_uprobe_regs_change(void) { }
#endif

void test_uprobe(void)
{
	if (test__start_subtest("attach"))
		test_uprobe_attach();
	test_uprobe_regs_change();
}
