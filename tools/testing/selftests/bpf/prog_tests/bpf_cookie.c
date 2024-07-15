// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <unistd.h>
#include <test_progs.h>
#include <network_helpers.h>
#include <bpf/btf.h>
#include "test_bpf_cookie.skel.h"
#include "kprobe_multi.skel.h"
#include "uprobe_multi.skel.h"

/* uprobe attach point */
static noinline void trigger_func(void)
{
	asm volatile ("");
}

static void kprobe_subtest(struct test_bpf_cookie *skel)
{
	DECLARE_LIBBPF_OPTS(bpf_kprobe_opts, opts);
	struct bpf_link *link1 = NULL, *link2 = NULL;
	struct bpf_link *retlink1 = NULL, *retlink2 = NULL;

	/* attach two kprobes */
	opts.bpf_cookie = 0x1;
	opts.retprobe = false;
	link1 = bpf_program__attach_kprobe_opts(skel->progs.handle_kprobe,
						 SYS_NANOSLEEP_KPROBE_NAME, &opts);
	if (!ASSERT_OK_PTR(link1, "link1"))
		goto cleanup;

	opts.bpf_cookie = 0x2;
	opts.retprobe = false;
	link2 = bpf_program__attach_kprobe_opts(skel->progs.handle_kprobe,
						 SYS_NANOSLEEP_KPROBE_NAME, &opts);
	if (!ASSERT_OK_PTR(link2, "link2"))
		goto cleanup;

	/* attach two kretprobes */
	opts.bpf_cookie = 0x10;
	opts.retprobe = true;
	retlink1 = bpf_program__attach_kprobe_opts(skel->progs.handle_kretprobe,
						    SYS_NANOSLEEP_KPROBE_NAME, &opts);
	if (!ASSERT_OK_PTR(retlink1, "retlink1"))
		goto cleanup;

	opts.bpf_cookie = 0x20;
	opts.retprobe = true;
	retlink2 = bpf_program__attach_kprobe_opts(skel->progs.handle_kretprobe,
						    SYS_NANOSLEEP_KPROBE_NAME, &opts);
	if (!ASSERT_OK_PTR(retlink2, "retlink2"))
		goto cleanup;

	/* trigger kprobe && kretprobe */
	usleep(1);

	ASSERT_EQ(skel->bss->kprobe_res, 0x1 | 0x2, "kprobe_res");
	ASSERT_EQ(skel->bss->kretprobe_res, 0x10 | 0x20, "kretprobe_res");

cleanup:
	bpf_link__destroy(link1);
	bpf_link__destroy(link2);
	bpf_link__destroy(retlink1);
	bpf_link__destroy(retlink2);
}

static void kprobe_multi_test_run(struct kprobe_multi *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	prog_fd = bpf_program__fd(skel->progs.trigger);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	ASSERT_EQ(skel->bss->kprobe_test1_result, 1, "kprobe_test1_result");
	ASSERT_EQ(skel->bss->kprobe_test2_result, 1, "kprobe_test2_result");
	ASSERT_EQ(skel->bss->kprobe_test3_result, 1, "kprobe_test3_result");
	ASSERT_EQ(skel->bss->kprobe_test4_result, 1, "kprobe_test4_result");
	ASSERT_EQ(skel->bss->kprobe_test5_result, 1, "kprobe_test5_result");
	ASSERT_EQ(skel->bss->kprobe_test6_result, 1, "kprobe_test6_result");
	ASSERT_EQ(skel->bss->kprobe_test7_result, 1, "kprobe_test7_result");
	ASSERT_EQ(skel->bss->kprobe_test8_result, 1, "kprobe_test8_result");

	ASSERT_EQ(skel->bss->kretprobe_test1_result, 1, "kretprobe_test1_result");
	ASSERT_EQ(skel->bss->kretprobe_test2_result, 1, "kretprobe_test2_result");
	ASSERT_EQ(skel->bss->kretprobe_test3_result, 1, "kretprobe_test3_result");
	ASSERT_EQ(skel->bss->kretprobe_test4_result, 1, "kretprobe_test4_result");
	ASSERT_EQ(skel->bss->kretprobe_test5_result, 1, "kretprobe_test5_result");
	ASSERT_EQ(skel->bss->kretprobe_test6_result, 1, "kretprobe_test6_result");
	ASSERT_EQ(skel->bss->kretprobe_test7_result, 1, "kretprobe_test7_result");
	ASSERT_EQ(skel->bss->kretprobe_test8_result, 1, "kretprobe_test8_result");
}

static void kprobe_multi_link_api_subtest(void)
{
	int prog_fd, link1_fd = -1, link2_fd = -1;
	struct kprobe_multi *skel = NULL;
	LIBBPF_OPTS(bpf_link_create_opts, opts);
	unsigned long long addrs[8];
	__u64 cookies[8];

	if (!ASSERT_OK(load_kallsyms(), "load_kallsyms"))
		goto cleanup;

	skel = kprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_raw_skel_load"))
		goto cleanup;

	skel->bss->pid = getpid();
	skel->bss->test_cookie = true;

#define GET_ADDR(__sym, __addr) ({				\
	__addr = ksym_get_addr(__sym);				\
	if (!ASSERT_NEQ(__addr, 0, "ksym_get_addr " #__sym))	\
		goto cleanup;					\
})

	GET_ADDR("bpf_fentry_test1", addrs[0]);
	GET_ADDR("bpf_fentry_test3", addrs[1]);
	GET_ADDR("bpf_fentry_test4", addrs[2]);
	GET_ADDR("bpf_fentry_test5", addrs[3]);
	GET_ADDR("bpf_fentry_test6", addrs[4]);
	GET_ADDR("bpf_fentry_test7", addrs[5]);
	GET_ADDR("bpf_fentry_test2", addrs[6]);
	GET_ADDR("bpf_fentry_test8", addrs[7]);

#undef GET_ADDR

	cookies[0] = 1; /* bpf_fentry_test1 */
	cookies[1] = 2; /* bpf_fentry_test3 */
	cookies[2] = 3; /* bpf_fentry_test4 */
	cookies[3] = 4; /* bpf_fentry_test5 */
	cookies[4] = 5; /* bpf_fentry_test6 */
	cookies[5] = 6; /* bpf_fentry_test7 */
	cookies[6] = 7; /* bpf_fentry_test2 */
	cookies[7] = 8; /* bpf_fentry_test8 */

	opts.kprobe_multi.addrs = (const unsigned long *) &addrs;
	opts.kprobe_multi.cnt = ARRAY_SIZE(addrs);
	opts.kprobe_multi.cookies = (const __u64 *) &cookies;
	prog_fd = bpf_program__fd(skel->progs.test_kprobe);

	link1_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_KPROBE_MULTI, &opts);
	if (!ASSERT_GE(link1_fd, 0, "link1_fd"))
		goto cleanup;

	cookies[0] = 8; /* bpf_fentry_test1 */
	cookies[1] = 7; /* bpf_fentry_test3 */
	cookies[2] = 6; /* bpf_fentry_test4 */
	cookies[3] = 5; /* bpf_fentry_test5 */
	cookies[4] = 4; /* bpf_fentry_test6 */
	cookies[5] = 3; /* bpf_fentry_test7 */
	cookies[6] = 2; /* bpf_fentry_test2 */
	cookies[7] = 1; /* bpf_fentry_test8 */

	opts.kprobe_multi.flags = BPF_F_KPROBE_MULTI_RETURN;
	prog_fd = bpf_program__fd(skel->progs.test_kretprobe);

	link2_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_KPROBE_MULTI, &opts);
	if (!ASSERT_GE(link2_fd, 0, "link2_fd"))
		goto cleanup;

	kprobe_multi_test_run(skel);

cleanup:
	close(link1_fd);
	close(link2_fd);
	kprobe_multi__destroy(skel);
}

static void kprobe_multi_attach_api_subtest(void)
{
	struct bpf_link *link1 = NULL, *link2 = NULL;
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct kprobe_multi *skel = NULL;
	const char *syms[8] = {
		"bpf_fentry_test1",
		"bpf_fentry_test3",
		"bpf_fentry_test4",
		"bpf_fentry_test5",
		"bpf_fentry_test6",
		"bpf_fentry_test7",
		"bpf_fentry_test2",
		"bpf_fentry_test8",
	};
	__u64 cookies[8];

	skel = kprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_raw_skel_load"))
		goto cleanup;

	skel->bss->pid = getpid();
	skel->bss->test_cookie = true;

	cookies[0] = 1; /* bpf_fentry_test1 */
	cookies[1] = 2; /* bpf_fentry_test3 */
	cookies[2] = 3; /* bpf_fentry_test4 */
	cookies[3] = 4; /* bpf_fentry_test5 */
	cookies[4] = 5; /* bpf_fentry_test6 */
	cookies[5] = 6; /* bpf_fentry_test7 */
	cookies[6] = 7; /* bpf_fentry_test2 */
	cookies[7] = 8; /* bpf_fentry_test8 */

	opts.syms = syms;
	opts.cnt = ARRAY_SIZE(syms);
	opts.cookies = cookies;

	link1 = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe,
						      NULL, &opts);
	if (!ASSERT_OK_PTR(link1, "bpf_program__attach_kprobe_multi_opts"))
		goto cleanup;

	cookies[0] = 8; /* bpf_fentry_test1 */
	cookies[1] = 7; /* bpf_fentry_test3 */
	cookies[2] = 6; /* bpf_fentry_test4 */
	cookies[3] = 5; /* bpf_fentry_test5 */
	cookies[4] = 4; /* bpf_fentry_test6 */
	cookies[5] = 3; /* bpf_fentry_test7 */
	cookies[6] = 2; /* bpf_fentry_test2 */
	cookies[7] = 1; /* bpf_fentry_test8 */

	opts.retprobe = true;

	link2 = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kretprobe,
						      NULL, &opts);
	if (!ASSERT_OK_PTR(link2, "bpf_program__attach_kprobe_multi_opts"))
		goto cleanup;

	kprobe_multi_test_run(skel);

cleanup:
	bpf_link__destroy(link2);
	bpf_link__destroy(link1);
	kprobe_multi__destroy(skel);
}

/* defined in prog_tests/uprobe_multi_test.c */
void uprobe_multi_func_1(void);
void uprobe_multi_func_2(void);
void uprobe_multi_func_3(void);

static void uprobe_multi_test_run(struct uprobe_multi *skel)
{
	skel->bss->uprobe_multi_func_1_addr = (__u64) uprobe_multi_func_1;
	skel->bss->uprobe_multi_func_2_addr = (__u64) uprobe_multi_func_2;
	skel->bss->uprobe_multi_func_3_addr = (__u64) uprobe_multi_func_3;

	skel->bss->pid = getpid();
	skel->bss->test_cookie = true;

	uprobe_multi_func_1();
	uprobe_multi_func_2();
	uprobe_multi_func_3();

	ASSERT_EQ(skel->bss->uprobe_multi_func_1_result, 1, "uprobe_multi_func_1_result");
	ASSERT_EQ(skel->bss->uprobe_multi_func_2_result, 1, "uprobe_multi_func_2_result");
	ASSERT_EQ(skel->bss->uprobe_multi_func_3_result, 1, "uprobe_multi_func_3_result");

	ASSERT_EQ(skel->bss->uretprobe_multi_func_1_result, 1, "uretprobe_multi_func_1_result");
	ASSERT_EQ(skel->bss->uretprobe_multi_func_2_result, 1, "uretprobe_multi_func_2_result");
	ASSERT_EQ(skel->bss->uretprobe_multi_func_3_result, 1, "uretprobe_multi_func_3_result");
}

static void uprobe_multi_attach_api_subtest(void)
{
	struct bpf_link *link1 = NULL, *link2 = NULL;
	struct uprobe_multi *skel = NULL;
	LIBBPF_OPTS(bpf_uprobe_multi_opts, opts);
	const char *syms[3] = {
		"uprobe_multi_func_1",
		"uprobe_multi_func_2",
		"uprobe_multi_func_3",
	};
	__u64 cookies[3];

	cookies[0] = 3; /* uprobe_multi_func_1 */
	cookies[1] = 1; /* uprobe_multi_func_2 */
	cookies[2] = 2; /* uprobe_multi_func_3 */

	opts.syms = syms;
	opts.cnt = ARRAY_SIZE(syms);
	opts.cookies = &cookies[0];

	skel = uprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_multi"))
		goto cleanup;

	link1 = bpf_program__attach_uprobe_multi(skel->progs.uprobe, -1,
						 "/proc/self/exe", NULL, &opts);
	if (!ASSERT_OK_PTR(link1, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	cookies[0] = 2; /* uprobe_multi_func_1 */
	cookies[1] = 3; /* uprobe_multi_func_2 */
	cookies[2] = 1; /* uprobe_multi_func_3 */

	opts.retprobe = true;
	link2 = bpf_program__attach_uprobe_multi(skel->progs.uretprobe, -1,
						      "/proc/self/exe", NULL, &opts);
	if (!ASSERT_OK_PTR(link2, "bpf_program__attach_uprobe_multi_retprobe"))
		goto cleanup;

	uprobe_multi_test_run(skel);

cleanup:
	bpf_link__destroy(link2);
	bpf_link__destroy(link1);
	uprobe_multi__destroy(skel);
}

static void uprobe_subtest(struct test_bpf_cookie *skel)
{
	DECLARE_LIBBPF_OPTS(bpf_uprobe_opts, opts);
	struct bpf_link *link1 = NULL, *link2 = NULL;
	struct bpf_link *retlink1 = NULL, *retlink2 = NULL;
	ssize_t uprobe_offset;

	uprobe_offset = get_uprobe_offset(&trigger_func);
	if (!ASSERT_GE(uprobe_offset, 0, "uprobe_offset"))
		goto cleanup;

	/* attach two uprobes */
	opts.bpf_cookie = 0x100;
	opts.retprobe = false;
	link1 = bpf_program__attach_uprobe_opts(skel->progs.handle_uprobe, 0 /* self pid */,
						"/proc/self/exe", uprobe_offset, &opts);
	if (!ASSERT_OK_PTR(link1, "link1"))
		goto cleanup;

	opts.bpf_cookie = 0x200;
	opts.retprobe = false;
	link2 = bpf_program__attach_uprobe_opts(skel->progs.handle_uprobe, -1 /* any pid */,
						"/proc/self/exe", uprobe_offset, &opts);
	if (!ASSERT_OK_PTR(link2, "link2"))
		goto cleanup;

	/* attach two uretprobes */
	opts.bpf_cookie = 0x1000;
	opts.retprobe = true;
	retlink1 = bpf_program__attach_uprobe_opts(skel->progs.handle_uretprobe, -1 /* any pid */,
						   "/proc/self/exe", uprobe_offset, &opts);
	if (!ASSERT_OK_PTR(retlink1, "retlink1"))
		goto cleanup;

	opts.bpf_cookie = 0x2000;
	opts.retprobe = true;
	retlink2 = bpf_program__attach_uprobe_opts(skel->progs.handle_uretprobe, 0 /* self pid */,
						   "/proc/self/exe", uprobe_offset, &opts);
	if (!ASSERT_OK_PTR(retlink2, "retlink2"))
		goto cleanup;

	/* trigger uprobe && uretprobe */
	trigger_func();

	ASSERT_EQ(skel->bss->uprobe_res, 0x100 | 0x200, "uprobe_res");
	ASSERT_EQ(skel->bss->uretprobe_res, 0x1000 | 0x2000, "uretprobe_res");

cleanup:
	bpf_link__destroy(link1);
	bpf_link__destroy(link2);
	bpf_link__destroy(retlink1);
	bpf_link__destroy(retlink2);
}

static void tp_subtest(struct test_bpf_cookie *skel)
{
	DECLARE_LIBBPF_OPTS(bpf_tracepoint_opts, opts);
	struct bpf_link *link1 = NULL, *link2 = NULL, *link3 = NULL;

	/* attach first tp prog */
	opts.bpf_cookie = 0x10000;
	link1 = bpf_program__attach_tracepoint_opts(skel->progs.handle_tp1,
						    "syscalls", "sys_enter_nanosleep", &opts);
	if (!ASSERT_OK_PTR(link1, "link1"))
		goto cleanup;

	/* attach second tp prog */
	opts.bpf_cookie = 0x20000;
	link2 = bpf_program__attach_tracepoint_opts(skel->progs.handle_tp2,
						    "syscalls", "sys_enter_nanosleep", &opts);
	if (!ASSERT_OK_PTR(link2, "link2"))
		goto cleanup;

	/* trigger tracepoints */
	usleep(1);

	ASSERT_EQ(skel->bss->tp_res, 0x10000 | 0x20000, "tp_res1");

	/* now we detach first prog and will attach third one, which causes
	 * two internal calls to bpf_prog_array_copy(), shuffling
	 * bpf_prog_array_items around. We test here that we don't lose track
	 * of associated bpf_cookies.
	 */
	bpf_link__destroy(link1);
	link1 = NULL;
	kern_sync_rcu();
	skel->bss->tp_res = 0;

	/* attach third tp prog */
	opts.bpf_cookie = 0x40000;
	link3 = bpf_program__attach_tracepoint_opts(skel->progs.handle_tp3,
						    "syscalls", "sys_enter_nanosleep", &opts);
	if (!ASSERT_OK_PTR(link3, "link3"))
		goto cleanup;

	/* trigger tracepoints */
	usleep(1);

	ASSERT_EQ(skel->bss->tp_res, 0x20000 | 0x40000, "tp_res2");

cleanup:
	bpf_link__destroy(link1);
	bpf_link__destroy(link2);
	bpf_link__destroy(link3);
}

static void burn_cpu(void)
{
	volatile int j = 0;
	cpu_set_t cpu_set;
	int i, err;

	/* generate some branches on cpu 0 */
	CPU_ZERO(&cpu_set);
	CPU_SET(0, &cpu_set);
	err = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
	ASSERT_OK(err, "set_thread_affinity");

	/* spin the loop for a while (random high number) */
	for (i = 0; i < 1000000; ++i)
		++j;
}

static void pe_subtest(struct test_bpf_cookie *skel)
{
	DECLARE_LIBBPF_OPTS(bpf_perf_event_opts, opts);
	struct bpf_link *link = NULL;
	struct perf_event_attr attr;
	int pfd = -1;

	/* create perf event */
	memset(&attr, 0, sizeof(attr));
	attr.size = sizeof(attr);
	attr.type = PERF_TYPE_SOFTWARE;
	attr.config = PERF_COUNT_SW_CPU_CLOCK;
	attr.freq = 1;
	attr.sample_freq = 1000;
	pfd = syscall(__NR_perf_event_open, &attr, -1, 0, -1, PERF_FLAG_FD_CLOEXEC);
	if (!ASSERT_GE(pfd, 0, "perf_fd"))
		goto cleanup;

	opts.bpf_cookie = 0x100000;
	link = bpf_program__attach_perf_event_opts(skel->progs.handle_pe, pfd, &opts);
	if (!ASSERT_OK_PTR(link, "link1"))
		goto cleanup;

	burn_cpu(); /* trigger BPF prog */

	ASSERT_EQ(skel->bss->pe_res, 0x100000, "pe_res1");

	/* prevent bpf_link__destroy() closing pfd itself */
	bpf_link__disconnect(link);
	/* close BPF link's FD explicitly */
	close(bpf_link__fd(link));
	/* free up memory used by struct bpf_link */
	bpf_link__destroy(link);
	link = NULL;
	kern_sync_rcu();
	skel->bss->pe_res = 0;

	opts.bpf_cookie = 0x200000;
	link = bpf_program__attach_perf_event_opts(skel->progs.handle_pe, pfd, &opts);
	if (!ASSERT_OK_PTR(link, "link2"))
		goto cleanup;

	burn_cpu(); /* trigger BPF prog */

	ASSERT_EQ(skel->bss->pe_res, 0x200000, "pe_res2");

cleanup:
	close(pfd);
	bpf_link__destroy(link);
}

static void tracing_subtest(struct test_bpf_cookie *skel)
{
	__u64 cookie;
	int prog_fd;
	int fentry_fd = -1, fexit_fd = -1, fmod_ret_fd = -1;
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	LIBBPF_OPTS(bpf_link_create_opts, link_opts);

	skel->bss->fentry_res = 0;
	skel->bss->fexit_res = 0;

	cookie = 0x10000000000000L;
	prog_fd = bpf_program__fd(skel->progs.fentry_test1);
	link_opts.tracing.cookie = cookie;
	fentry_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_FENTRY, &link_opts);
	if (!ASSERT_GE(fentry_fd, 0, "fentry.link_create"))
		goto cleanup;

	cookie = 0x20000000000000L;
	prog_fd = bpf_program__fd(skel->progs.fexit_test1);
	link_opts.tracing.cookie = cookie;
	fexit_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_FEXIT, &link_opts);
	if (!ASSERT_GE(fexit_fd, 0, "fexit.link_create"))
		goto cleanup;

	cookie = 0x30000000000000L;
	prog_fd = bpf_program__fd(skel->progs.fmod_ret_test);
	link_opts.tracing.cookie = cookie;
	fmod_ret_fd = bpf_link_create(prog_fd, 0, BPF_MODIFY_RETURN, &link_opts);
	if (!ASSERT_GE(fmod_ret_fd, 0, "fmod_ret.link_create"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.fentry_test1);
	bpf_prog_test_run_opts(prog_fd, &opts);

	prog_fd = bpf_program__fd(skel->progs.fmod_ret_test);
	bpf_prog_test_run_opts(prog_fd, &opts);

	ASSERT_EQ(skel->bss->fentry_res, 0x10000000000000L, "fentry_res");
	ASSERT_EQ(skel->bss->fexit_res, 0x20000000000000L, "fexit_res");
	ASSERT_EQ(skel->bss->fmod_ret_res, 0x30000000000000L, "fmod_ret_res");

cleanup:
	if (fentry_fd >= 0)
		close(fentry_fd);
	if (fexit_fd >= 0)
		close(fexit_fd);
	if (fmod_ret_fd >= 0)
		close(fmod_ret_fd);
}

int stack_mprotect(void);

static void lsm_subtest(struct test_bpf_cookie *skel)
{
	__u64 cookie;
	int prog_fd;
	int lsm_fd = -1;
	LIBBPF_OPTS(bpf_link_create_opts, link_opts);
	int err;

	skel->bss->lsm_res = 0;

	cookie = 0x90000000000090L;
	prog_fd = bpf_program__fd(skel->progs.test_int_hook);
	link_opts.tracing.cookie = cookie;
	lsm_fd = bpf_link_create(prog_fd, 0, BPF_LSM_MAC, &link_opts);
	if (!ASSERT_GE(lsm_fd, 0, "lsm.link_create"))
		goto cleanup;

	err = stack_mprotect();
	if (!ASSERT_EQ(err, -1, "stack_mprotect") ||
	    !ASSERT_EQ(errno, EPERM, "stack_mprotect"))
		goto cleanup;

	usleep(1);

	ASSERT_EQ(skel->bss->lsm_res, 0x90000000000090L, "fentry_res");

cleanup:
	if (lsm_fd >= 0)
		close(lsm_fd);
}

static void tp_btf_subtest(struct test_bpf_cookie *skel)
{
	__u64 cookie;
	int prog_fd, link_fd = -1;
	struct bpf_link *link = NULL;
	LIBBPF_OPTS(bpf_link_create_opts, link_opts);
	LIBBPF_OPTS(bpf_raw_tp_opts, raw_tp_opts);
	LIBBPF_OPTS(bpf_trace_opts, trace_opts);

	/* There are three different ways to attach tp_btf (BTF-aware raw
	 * tracepoint) programs. Let's test all of them.
	 */
	prog_fd = bpf_program__fd(skel->progs.handle_tp_btf);

	/* low-level BPF_RAW_TRACEPOINT_OPEN command wrapper */
	skel->bss->tp_btf_res = 0;

	raw_tp_opts.cookie = cookie = 0x11000000000000L;
	link_fd = bpf_raw_tracepoint_open_opts(prog_fd, &raw_tp_opts);
	if (!ASSERT_GE(link_fd, 0, "bpf_raw_tracepoint_open_opts"))
		goto cleanup;

	usleep(1); /* trigger */
	close(link_fd); /* detach */
	link_fd = -1;

	ASSERT_EQ(skel->bss->tp_btf_res, cookie, "raw_tp_open_res");

	/* low-level generic bpf_link_create() API */
	skel->bss->tp_btf_res = 0;

	link_opts.tracing.cookie = cookie = 0x22000000000000L;
	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_RAW_TP, &link_opts);
	if (!ASSERT_GE(link_fd, 0, "bpf_link_create"))
		goto cleanup;

	usleep(1); /* trigger */
	close(link_fd); /* detach */
	link_fd = -1;

	ASSERT_EQ(skel->bss->tp_btf_res, cookie, "link_create_res");

	/* high-level bpf_link-based bpf_program__attach_trace_opts() API */
	skel->bss->tp_btf_res = 0;

	trace_opts.cookie = cookie = 0x33000000000000L;
	link = bpf_program__attach_trace_opts(skel->progs.handle_tp_btf, &trace_opts);
	if (!ASSERT_OK_PTR(link, "attach_trace_opts"))
		goto cleanup;

	usleep(1); /* trigger */
	bpf_link__destroy(link); /* detach */
	link = NULL;

	ASSERT_EQ(skel->bss->tp_btf_res, cookie, "attach_trace_opts_res");

cleanup:
	if (link_fd >= 0)
		close(link_fd);
	bpf_link__destroy(link);
}

static void raw_tp_subtest(struct test_bpf_cookie *skel)
{
	__u64 cookie;
	int prog_fd, link_fd = -1;
	struct bpf_link *link = NULL;
	LIBBPF_OPTS(bpf_raw_tp_opts, raw_tp_opts);
	LIBBPF_OPTS(bpf_raw_tracepoint_opts, opts);

	/* There are two different ways to attach raw_tp programs */
	prog_fd = bpf_program__fd(skel->progs.handle_raw_tp);

	/* low-level BPF_RAW_TRACEPOINT_OPEN command wrapper */
	skel->bss->raw_tp_res = 0;

	raw_tp_opts.tp_name = "sys_enter";
	raw_tp_opts.cookie = cookie = 0x55000000000000L;
	link_fd = bpf_raw_tracepoint_open_opts(prog_fd, &raw_tp_opts);
	if (!ASSERT_GE(link_fd, 0, "bpf_raw_tracepoint_open_opts"))
		goto cleanup;

	usleep(1); /* trigger */
	close(link_fd); /* detach */
	link_fd = -1;

	ASSERT_EQ(skel->bss->raw_tp_res, cookie, "raw_tp_open_res");

	/* high-level bpf_link-based bpf_program__attach_raw_tracepoint_opts() API */
	skel->bss->raw_tp_res = 0;

	opts.cookie = cookie = 0x66000000000000L;
	link = bpf_program__attach_raw_tracepoint_opts(skel->progs.handle_raw_tp,
						       "sys_enter", &opts);
	if (!ASSERT_OK_PTR(link, "attach_raw_tp_opts"))
		goto cleanup;

	usleep(1); /* trigger */
	bpf_link__destroy(link); /* detach */
	link = NULL;

	ASSERT_EQ(skel->bss->raw_tp_res, cookie, "attach_raw_tp_opts_res");

cleanup:
	if (link_fd >= 0)
		close(link_fd);
	bpf_link__destroy(link);
}

void test_bpf_cookie(void)
{
	struct test_bpf_cookie *skel;

	skel = test_bpf_cookie__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->bss->my_tid = syscall(SYS_gettid);

	if (test__start_subtest("kprobe"))
		kprobe_subtest(skel);
	if (test__start_subtest("multi_kprobe_link_api"))
		kprobe_multi_link_api_subtest();
	if (test__start_subtest("multi_kprobe_attach_api"))
		kprobe_multi_attach_api_subtest();
	if (test__start_subtest("uprobe"))
		uprobe_subtest(skel);
	if (test__start_subtest("multi_uprobe_attach_api"))
		uprobe_multi_attach_api_subtest();
	if (test__start_subtest("tracepoint"))
		tp_subtest(skel);
	if (test__start_subtest("perf_event"))
		pe_subtest(skel);
	if (test__start_subtest("trampoline"))
		tracing_subtest(skel);
	if (test__start_subtest("lsm"))
		lsm_subtest(skel);
	if (test__start_subtest("tp_btf"))
		tp_btf_subtest(skel);
	if (test__start_subtest("raw_tp"))
		raw_tp_subtest(skel);
	test_bpf_cookie__destroy(skel);
}
