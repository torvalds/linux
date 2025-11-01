// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>

#define _SDT_HAS_SEMAPHORES 1
#include "../sdt.h"

#include "test_usdt.skel.h"
#include "test_urandom_usdt.skel.h"

int lets_test_this(int);

static volatile int idx = 2;
static volatile __u64 bla = 0xFEDCBA9876543210ULL;
static volatile short nums[] = {-1, -2, -3, -4};

static volatile struct {
	int x;
	signed char y;
} t1 = { 1, -127 };

#define SEC(name) __attribute__((section(name), used))

unsigned short test_usdt0_semaphore SEC(".probes");
unsigned short test_usdt3_semaphore SEC(".probes");
unsigned short test_usdt12_semaphore SEC(".probes");

static void __always_inline trigger_func(int x) {
	long y = 42;

	if (test_usdt0_semaphore)
		STAP_PROBE(test, usdt0);
	if (test_usdt3_semaphore)
		STAP_PROBE3(test, usdt3, x, y, &bla);
	if (test_usdt12_semaphore) {
		STAP_PROBE12(test, usdt12,
			     x, x + 1, y, x + y, 5,
			     y / 7, bla, &bla, -9, nums[x],
			     nums[idx], t1.y);
	}
}

#if defined(__x86_64__) || defined(__i386__)
/*
 * SIB (Scale-Index-Base) addressing format: "size@(base_reg, index_reg, scale)"
 * - 'size' is the size in bytes of the array element, and its sign indicates
 *   whether the type is signed (negative) or unsigned (positive).
 * - 'base_reg' is the register holding the base address, normally rdx or edx
 * - 'index_reg' is the register holding the index, normally rax or eax
 * - 'scale' is the scaling factor (typically 1, 2, 4, or 8), which matches the
 *    size of the element type.
 *
 * For example, for an array of 'short' (signed 2-byte elements), the SIB spec would be:
 * - size: -2 (negative because 'short' is signed)
 * - scale: 2 (since sizeof(short) == 2)
 *
 * The resulting SIB format: "-2@(%%rdx,%%rax,2)" for x86_64, "-2@(%%edx,%%eax,2)" for i386
 */
static volatile short array[] = {-1, -2, -3, -4};

#if defined(__x86_64__)
#define USDT_SIB_ARG_SPEC -2@(%%rdx,%%rax,2)
#else
#define USDT_SIB_ARG_SPEC -2@(%%edx,%%eax,2)
#endif

unsigned short test_usdt_sib_semaphore SEC(".probes");

static void trigger_sib_spec(void)
{
	/*
	 * Force SIB addressing with inline assembly.
	 *
	 * You must compile with -std=gnu99 or -std=c99 to use the
	 * STAP_PROBE_ASM macro.
	 *
	 * The STAP_PROBE_ASM macro generates a quoted string that gets
	 * inserted between the surrounding assembly instructions. In this
	 * case, USDT_SIB_ARG_SPEC is embedded directly into the instruction
	 * stream, creating a probe point between the asm statement boundaries.
	 * It works fine with gcc/clang.
	 *
	 * Register constraints:
	 * - "d"(array): Binds the 'array' variable to %rdx or %edx register
	 * - "a"(0): Binds the constant 0 to %rax or %eax register
	 * These ensure that when USDT_SIB_ARG_SPEC references %%rdx(%edx) and
	 * %%rax(%eax), they contain the expected values for SIB addressing.
	 *
	 * The "memory" clobber prevents the compiler from reordering memory
	 * accesses around the probe point, ensuring that the probe behavior
	 * is predictable and consistent.
	 */
	asm volatile(
		STAP_PROBE_ASM(test, usdt_sib, USDT_SIB_ARG_SPEC)
		:
		: "d"(array), "a"(0)
		: "memory"
	);
}
#endif

static void subtest_basic_usdt(bool optimized)
{
	LIBBPF_OPTS(bpf_usdt_opts, opts);
	struct test_usdt *skel;
	struct test_usdt__bss *bss;
	int err, i, called;
	const __u64 expected_cookie = 0xcafedeadbeeffeed;

#define TRIGGER(x) ({			\
	trigger_func(x);		\
	if (optimized)			\
		trigger_func(x);	\
	optimized ? 2 : 1;		\
	})

	skel = test_usdt__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	bss = skel->bss;
	bss->my_pid = getpid();

	err = test_usdt__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	/* usdt0 won't be auto-attached */
	opts.usdt_cookie = expected_cookie;
	skel->links.usdt0 = bpf_program__attach_usdt(skel->progs.usdt0,
						     0 /*self*/, "/proc/self/exe",
						     "test", "usdt0", &opts);
	if (!ASSERT_OK_PTR(skel->links.usdt0, "usdt0_link"))
		goto cleanup;

#if defined(__x86_64__) || defined(__i386__)
	opts.usdt_cookie = expected_cookie;
	skel->links.usdt_sib = bpf_program__attach_usdt(skel->progs.usdt_sib,
							 0 /*self*/, "/proc/self/exe",
							 "test", "usdt_sib", &opts);
	if (!ASSERT_OK_PTR(skel->links.usdt_sib, "usdt_sib_link"))
		goto cleanup;
#endif

	called = TRIGGER(1);

	ASSERT_EQ(bss->usdt0_called, called, "usdt0_called");
	ASSERT_EQ(bss->usdt3_called, called, "usdt3_called");
	ASSERT_EQ(bss->usdt12_called, called, "usdt12_called");

	ASSERT_EQ(bss->usdt0_cookie, expected_cookie, "usdt0_cookie");
	ASSERT_EQ(bss->usdt0_arg_cnt, 0, "usdt0_arg_cnt");
	ASSERT_EQ(bss->usdt0_arg_ret, -ENOENT, "usdt0_arg_ret");
	ASSERT_EQ(bss->usdt0_arg_size, -ENOENT, "usdt0_arg_size");

	/* auto-attached usdt3 gets default zero cookie value */
	ASSERT_EQ(bss->usdt3_cookie, 0, "usdt3_cookie");
	ASSERT_EQ(bss->usdt3_arg_cnt, 3, "usdt3_arg_cnt");

	ASSERT_EQ(bss->usdt3_arg_rets[0], 0, "usdt3_arg1_ret");
	ASSERT_EQ(bss->usdt3_arg_rets[1], 0, "usdt3_arg2_ret");
	ASSERT_EQ(bss->usdt3_arg_rets[2], 0, "usdt3_arg3_ret");
	ASSERT_EQ(bss->usdt3_args[0], 1, "usdt3_arg1");
	ASSERT_EQ(bss->usdt3_args[1], 42, "usdt3_arg2");
	ASSERT_EQ(bss->usdt3_args[2], (uintptr_t)&bla, "usdt3_arg3");
	ASSERT_EQ(bss->usdt3_arg_sizes[0], 4, "usdt3_arg1_size");
	ASSERT_EQ(bss->usdt3_arg_sizes[1], 8, "usdt3_arg2_size");
	ASSERT_EQ(bss->usdt3_arg_sizes[2], 8, "usdt3_arg3_size");

	/* auto-attached usdt12 gets default zero cookie value */
	ASSERT_EQ(bss->usdt12_cookie, 0, "usdt12_cookie");
	ASSERT_EQ(bss->usdt12_arg_cnt, 12, "usdt12_arg_cnt");

	ASSERT_EQ(bss->usdt12_args[0], 1, "usdt12_arg1");
	ASSERT_EQ(bss->usdt12_args[1], 1 + 1, "usdt12_arg2");
	ASSERT_EQ(bss->usdt12_args[2], 42, "usdt12_arg3");
	ASSERT_EQ(bss->usdt12_args[3], 42 + 1, "usdt12_arg4");
	ASSERT_EQ(bss->usdt12_args[4], 5, "usdt12_arg5");
	ASSERT_EQ(bss->usdt12_args[5], 42 / 7, "usdt12_arg6");
	ASSERT_EQ(bss->usdt12_args[6], bla, "usdt12_arg7");
	ASSERT_EQ(bss->usdt12_args[7], (uintptr_t)&bla, "usdt12_arg8");
	ASSERT_EQ(bss->usdt12_args[8], -9, "usdt12_arg9");
	ASSERT_EQ(bss->usdt12_args[9], nums[1], "usdt12_arg10");
	ASSERT_EQ(bss->usdt12_args[10], nums[idx], "usdt12_arg11");
	ASSERT_EQ(bss->usdt12_args[11], t1.y, "usdt12_arg12");

	int usdt12_expected_arg_sizes[12] = { 4, 4, 8, 8, 4, 8, 8, 8, 4, 2, 2, 1 };

	for (i = 0; i < 12; i++)
		ASSERT_EQ(bss->usdt12_arg_sizes[i], usdt12_expected_arg_sizes[i], "usdt12_arg_size");

	/* trigger_func() is marked __always_inline, so USDT invocations will be
	 * inlined in two different places, meaning that each USDT will have
	 * at least 2 different places to be attached to. This verifies that
	 * bpf_program__attach_usdt() handles this properly and attaches to
	 * all possible places of USDT invocation.
	 */
	called += TRIGGER(2);

	ASSERT_EQ(bss->usdt0_called, called, "usdt0_called");
	ASSERT_EQ(bss->usdt3_called, called, "usdt3_called");
	ASSERT_EQ(bss->usdt12_called, called, "usdt12_called");

	/* only check values that depend on trigger_func()'s input value */
	ASSERT_EQ(bss->usdt3_args[0], 2, "usdt3_arg1");

	ASSERT_EQ(bss->usdt12_args[0], 2, "usdt12_arg1");
	ASSERT_EQ(bss->usdt12_args[1], 2 + 1, "usdt12_arg2");
	ASSERT_EQ(bss->usdt12_args[3], 42 + 2, "usdt12_arg4");
	ASSERT_EQ(bss->usdt12_args[9], nums[2], "usdt12_arg10");

	/* detach and re-attach usdt3 */
	bpf_link__destroy(skel->links.usdt3);

	opts.usdt_cookie = 0xBADC00C51E;
	skel->links.usdt3 = bpf_program__attach_usdt(skel->progs.usdt3, -1 /* any pid */,
						     "/proc/self/exe", "test", "usdt3", &opts);
	if (!ASSERT_OK_PTR(skel->links.usdt3, "usdt3_reattach"))
		goto cleanup;

	called += TRIGGER(3);

	ASSERT_EQ(bss->usdt3_called, called, "usdt3_called");
	/* this time usdt3 has custom cookie */
	ASSERT_EQ(bss->usdt3_cookie, 0xBADC00C51E, "usdt3_cookie");
	ASSERT_EQ(bss->usdt3_arg_cnt, 3, "usdt3_arg_cnt");

	ASSERT_EQ(bss->usdt3_arg_rets[0], 0, "usdt3_arg1_ret");
	ASSERT_EQ(bss->usdt3_arg_rets[1], 0, "usdt3_arg2_ret");
	ASSERT_EQ(bss->usdt3_arg_rets[2], 0, "usdt3_arg3_ret");
	ASSERT_EQ(bss->usdt3_args[0], 3, "usdt3_arg1");
	ASSERT_EQ(bss->usdt3_args[1], 42, "usdt3_arg2");
	ASSERT_EQ(bss->usdt3_args[2], (uintptr_t)&bla, "usdt3_arg3");

#if defined(__x86_64__) || defined(__i386__)
	trigger_sib_spec();
	ASSERT_EQ(bss->usdt_sib_called, 1, "usdt_sib_called");
	ASSERT_EQ(bss->usdt_sib_cookie, expected_cookie, "usdt_sib_cookie");
	ASSERT_EQ(bss->usdt_sib_arg_cnt, 1, "usdt_sib_arg_cnt");
	ASSERT_EQ(bss->usdt_sib_arg, nums[0], "usdt_sib_arg");
	ASSERT_EQ(bss->usdt_sib_arg_ret, 0, "usdt_sib_arg_ret");
	ASSERT_EQ(bss->usdt_sib_arg_size, sizeof(nums[0]), "usdt_sib_arg_size");
#endif

cleanup:
	test_usdt__destroy(skel);
#undef TRIGGER
}

unsigned short test_usdt_100_semaphore SEC(".probes");
unsigned short test_usdt_300_semaphore SEC(".probes");
unsigned short test_usdt_400_semaphore SEC(".probes");

#define R10(F, X)  F(X+0); F(X+1);F(X+2); F(X+3); F(X+4); \
		   F(X+5); F(X+6); F(X+7); F(X+8); F(X+9);
#define R100(F, X) R10(F,X+ 0);R10(F,X+10);R10(F,X+20);R10(F,X+30);R10(F,X+40); \
		   R10(F,X+50);R10(F,X+60);R10(F,X+70);R10(F,X+80);R10(F,X+90);

/* carefully control that we get exactly 100 inlines by preventing inlining */
static void __always_inline f100(int x)
{
	STAP_PROBE1(test, usdt_100, x);
}

__weak void trigger_100_usdts(void)
{
	R100(f100, 0);
}

/* we shouldn't be able to attach to test:usdt2_300 USDT as we don't have as
 * many slots for specs. It's important that each STAP_PROBE2() invocation
 * (after untolling) gets different arg spec due to compiler inlining i as
 * a constant
 */
static void __always_inline f300(int x)
{
	STAP_PROBE1(test, usdt_300, x);
}

__weak void trigger_300_usdts(void)
{
	R100(f300, 0);
	R100(f300, 100);
	R100(f300, 200);
}

static void __always_inline f400(int x __attribute__((unused)))
{
	STAP_PROBE1(test, usdt_400, 400);
}

/* this time we have 400 different USDT call sites, but they have uniform
 * argument location, so libbpf's spec string deduplication logic should keep
 * spec count use very small and so we should be able to attach to all 400
 * call sites
 */
__weak void trigger_400_usdts(void)
{
	R100(f400, 0);
	R100(f400, 100);
	R100(f400, 200);
	R100(f400, 300);
}

static void subtest_multispec_usdt(void)
{
	LIBBPF_OPTS(bpf_usdt_opts, opts);
	struct test_usdt *skel;
	struct test_usdt__bss *bss;
	int err, i;

	skel = test_usdt__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	bss = skel->bss;
	bss->my_pid = getpid();

	err = test_usdt__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	/* usdt_100 is auto-attached and there are 100 inlined call sites,
	 * let's validate that all of them are properly attached to and
	 * handled from BPF side
	 */
	trigger_100_usdts();

	ASSERT_EQ(bss->usdt_100_called, 100, "usdt_100_called");
	ASSERT_EQ(bss->usdt_100_sum, 99 * 100 / 2, "usdt_100_sum");

	/* Stress test free spec ID tracking. By default libbpf allows up to
	 * 256 specs to be used, so if we don't return free spec IDs back
	 * after few detachments and re-attachments we should run out of
	 * available spec IDs.
	 */
	for (i = 0; i < 2; i++) {
		bpf_link__destroy(skel->links.usdt_100);

		skel->links.usdt_100 = bpf_program__attach_usdt(skel->progs.usdt_100, -1,
							        "/proc/self/exe",
								"test", "usdt_100", NULL);
		if (!ASSERT_OK_PTR(skel->links.usdt_100, "usdt_100_reattach"))
			goto cleanup;

		bss->usdt_100_sum = 0;
		trigger_100_usdts();

		ASSERT_EQ(bss->usdt_100_called, (i + 1) * 100 + 100, "usdt_100_called");
		ASSERT_EQ(bss->usdt_100_sum, 99 * 100 / 2, "usdt_100_sum");
	}

	/* Now let's step it up and try to attach USDT that requires more than
	 * 256 attach points with different specs for each.
	 * Note that we need trigger_300_usdts() only to actually have 300
	 * USDT call sites, we are not going to actually trace them.
	 */
	trigger_300_usdts();

	bpf_link__destroy(skel->links.usdt_100);

	bss->usdt_100_called = 0;
	bss->usdt_100_sum = 0;

	/* If built with arm64/clang, there will be much less number of specs
	 * for usdt_300 call sites.
	 */
#if !defined(__aarch64__) || !defined(__clang__)
	/* we'll reuse usdt_100 BPF program for usdt_300 test */
	skel->links.usdt_100 = bpf_program__attach_usdt(skel->progs.usdt_100, -1, "/proc/self/exe",
							"test", "usdt_300", NULL);
	err = -errno;
	if (!ASSERT_ERR_PTR(skel->links.usdt_100, "usdt_300_bad_attach"))
		goto cleanup;
	ASSERT_EQ(err, -E2BIG, "usdt_300_attach_err");

	/* let's check that there are no "dangling" BPF programs attached due
	 * to partial success of the above test:usdt_300 attachment
	 */
	f300(777); /* this is 301st instance of usdt_300 */

	ASSERT_EQ(bss->usdt_100_called, 0, "usdt_301_called");
	ASSERT_EQ(bss->usdt_100_sum, 0, "usdt_301_sum");
#endif

	/* This time we have USDT with 400 inlined invocations, but arg specs
	 * should be the same across all sites, so libbpf will only need to
	 * use one spec and thus we'll be able to attach 400 uprobes
	 * successfully.
	 *
	 * Again, we are reusing usdt_100 BPF program.
	 */
	skel->links.usdt_100 = bpf_program__attach_usdt(skel->progs.usdt_100, -1,
							"/proc/self/exe",
							"test", "usdt_400", NULL);
	if (!ASSERT_OK_PTR(skel->links.usdt_100, "usdt_400_attach"))
		goto cleanup;

	trigger_400_usdts();

	ASSERT_EQ(bss->usdt_100_called, 400, "usdt_400_called");
	ASSERT_EQ(bss->usdt_100_sum, 400 * 400, "usdt_400_sum");

cleanup:
	test_usdt__destroy(skel);
}

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

static void subtest_urandom_usdt(bool auto_attach)
{
	struct test_urandom_usdt *skel;
	struct test_urandom_usdt__bss *bss;
	struct bpf_link *l;
	FILE *urand_pipe = NULL;
	int err, urand_pid = 0;

	skel = test_urandom_usdt__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	urand_pipe = urand_spawn(&urand_pid);
	if (!ASSERT_OK_PTR(urand_pipe, "urand_spawn"))
		goto cleanup;

	bss = skel->bss;
	bss->urand_pid = urand_pid;

	if (auto_attach) {
		err = test_urandom_usdt__attach(skel);
		if (!ASSERT_OK(err, "skel_auto_attach"))
			goto cleanup;
	} else {
		l = bpf_program__attach_usdt(skel->progs.urand_read_without_sema,
					     urand_pid, "./urandom_read",
					     "urand", "read_without_sema", NULL);
		if (!ASSERT_OK_PTR(l, "urand_without_sema_attach"))
			goto cleanup;
		skel->links.urand_read_without_sema = l;

		l = bpf_program__attach_usdt(skel->progs.urand_read_with_sema,
					     urand_pid, "./urandom_read",
					     "urand", "read_with_sema", NULL);
		if (!ASSERT_OK_PTR(l, "urand_with_sema_attach"))
			goto cleanup;
		skel->links.urand_read_with_sema = l;

		l = bpf_program__attach_usdt(skel->progs.urandlib_read_without_sema,
					     urand_pid, "./liburandom_read.so",
					     "urandlib", "read_without_sema", NULL);
		if (!ASSERT_OK_PTR(l, "urandlib_without_sema_attach"))
			goto cleanup;
		skel->links.urandlib_read_without_sema = l;

		l = bpf_program__attach_usdt(skel->progs.urandlib_read_with_sema,
					     urand_pid, "./liburandom_read.so",
					     "urandlib", "read_with_sema", NULL);
		if (!ASSERT_OK_PTR(l, "urandlib_with_sema_attach"))
			goto cleanup;
		skel->links.urandlib_read_with_sema = l;

	}

	/* trigger urandom_read USDTs */
	ASSERT_OK(urand_trigger(&urand_pipe), "urand_exit_code");

	ASSERT_EQ(bss->urand_read_without_sema_call_cnt, 1, "urand_wo_sema_cnt");
	ASSERT_EQ(bss->urand_read_without_sema_buf_sz_sum, 256, "urand_wo_sema_sum");

	ASSERT_EQ(bss->urand_read_with_sema_call_cnt, 1, "urand_w_sema_cnt");
	ASSERT_EQ(bss->urand_read_with_sema_buf_sz_sum, 256, "urand_w_sema_sum");

	ASSERT_EQ(bss->urandlib_read_without_sema_call_cnt, 1, "urandlib_wo_sema_cnt");
	ASSERT_EQ(bss->urandlib_read_without_sema_buf_sz_sum, 256, "urandlib_wo_sema_sum");

	ASSERT_EQ(bss->urandlib_read_with_sema_call_cnt, 1, "urandlib_w_sema_cnt");
	ASSERT_EQ(bss->urandlib_read_with_sema_buf_sz_sum, 256, "urandlib_w_sema_sum");

cleanup:
	if (urand_pipe)
		pclose(urand_pipe);
	test_urandom_usdt__destroy(skel);
}

void test_usdt(void)
{
	if (test__start_subtest("basic"))
		subtest_basic_usdt(false);
#ifdef __x86_64__
	if (test__start_subtest("basic_optimized"))
		subtest_basic_usdt(true);
#endif
	if (test__start_subtest("multispec"))
		subtest_multispec_usdt();
	if (test__start_subtest("urand_auto_attach"))
		subtest_urandom_usdt(true /* auto_attach */);
	if (test__start_subtest("urand_pid_attach"))
		subtest_urandom_usdt(false /* auto_attach */);
}
