// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>

#ifdef __x86_64__

#include <unistd.h>
#include <asm/ptrace.h>
#include <linux/compiler.h>
#include <linux/stringify.h>
#include <linux/kernel.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <asm/prctl.h>
#include "uprobe_syscall.skel.h"
#include "uprobe_syscall_executed.skel.h"
#include "bpf/libbpf_internal.h"

#define USDT_NOP .byte 0x0f, 0x1f, 0x44, 0x00, 0x00
#include "usdt.h"

#pragma GCC diagnostic ignored "-Wattributes"

__attribute__((aligned(16)))
__nocf_check __weak __naked unsigned long uprobe_regs_trigger(void)
{
	asm volatile (
		".byte 0x0f, 0x1f, 0x44, 0x00, 0x00\n" /* nop5 */
		"movq $0xdeadbeef, %rax\n"
		"ret\n"
	);
}

__naked void uprobe_regs(struct pt_regs *before, struct pt_regs *after)
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
		"pushq %rax\n"
		"pushf\n"
		"pop %rax\n"
		"movq %rax, 144(%rdi)\n" /* eflags   */
		"pop %rax\n"
		"movq %rsp, 152(%rdi)\n" /* rsp      */
		"movq   $0, 160(%rdi)\n" /* ss       */

		/* save 2nd argument */
		"pushq %rsi\n"
		"call uprobe_regs_trigger\n"

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

static void test_uprobe_regs_equal(bool retprobe)
{
	LIBBPF_OPTS(bpf_uprobe_opts, opts,
		.retprobe = retprobe,
	);
	struct uprobe_syscall *skel = NULL;
	struct pt_regs before = {}, after = {};
	unsigned long *pb = (unsigned long *) &before;
	unsigned long *pa = (unsigned long *) &after;
	unsigned long *pp;
	unsigned long offset;
	unsigned int i, cnt;

	offset = get_uprobe_offset(&uprobe_regs_trigger);
	if (!ASSERT_GE(offset, 0, "get_uprobe_offset"))
		return;

	skel = uprobe_syscall__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_syscall__open_and_load"))
		goto cleanup;

	skel->links.probe = bpf_program__attach_uprobe_opts(skel->progs.probe,
				0, "/proc/self/exe", offset, &opts);
	if (!ASSERT_OK_PTR(skel->links.probe, "bpf_program__attach_uprobe_opts"))
		goto cleanup;

	/* make sure uprobe gets optimized */
	if (!retprobe)
		uprobe_regs_trigger();

	uprobe_regs(&before, &after);

	pp = (unsigned long *) &skel->bss->regs;
	cnt = sizeof(before)/sizeof(*pb);

	for (i = 0; i < cnt; i++) {
		unsigned int offset = i * sizeof(unsigned long);

		/*
		 * Check register before and after uprobe_regs_trigger call
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
		 * uprobe_regs_trigger call (with rax exception, check below).
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
		/*
		 * uprobe does not see return value in rax, it needs to see the
		 * original (before) rax value
		 */
		case offsetof(struct pt_regs, rax):
			if (!retprobe) {
				ASSERT_EQ(pp[i], pb[i], "uprobe rax prog-before value check");
				break;
			}
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

static void test_regs_change(void)
{
	struct pt_regs before = {}, after = {};
	unsigned long *pb = (unsigned long *) &before;
	unsigned long *pa = (unsigned long *) &after;
	unsigned long cnt = sizeof(before)/sizeof(*pb);
	unsigned int i, err, offset;

	offset = get_uprobe_offset(uprobe_regs_trigger);

	err = write_bpf_testmod_uprobe(offset);
	if (!ASSERT_OK(err, "register_uprobe"))
		return;

	/* make sure uprobe gets optimized */
	uprobe_regs_trigger();

	uprobe_regs(&before, &after);

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
	struct bpf_link *link;

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

	skel->bss->pid = pid;

	link = bpf_program__attach_uprobe_multi(skel->progs.test_uretprobe_multi,
						pid, "/proc/self/exe",
						"uretprobe_syscall_call", &opts);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_uprobe_multi"))
		goto cleanup;
	skel->links.test_uretprobe_multi = link;

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

#define TRAMP "[uprobes-trampoline]"

__attribute__((aligned(16)))
__nocf_check __weak __naked void uprobe_test(void)
{
	asm volatile ("					\n"
		".byte 0x0f, 0x1f, 0x44, 0x00, 0x00	\n"
		"ret					\n"
	);
}

__attribute__((aligned(16)))
__nocf_check __weak void usdt_test(void)
{
	USDT(optimized_uprobe, usdt);
}

static int find_uprobes_trampoline(void *tramp_addr)
{
	void *start, *end;
	char line[128];
	int ret = -1;
	FILE *maps;

	maps = fopen("/proc/self/maps", "r");
	if (!maps) {
		fprintf(stderr, "cannot open maps\n");
		return -1;
	}

	while (fgets(line, sizeof(line), maps)) {
		int m = -1;

		/* We care only about private r-x mappings. */
		if (sscanf(line, "%p-%p r-xp %*x %*x:%*x %*u %n", &start, &end, &m) != 2)
			continue;
		if (m < 0)
			continue;
		if (!strncmp(&line[m], TRAMP, sizeof(TRAMP)-1) && (start == tramp_addr)) {
			ret = 0;
			break;
		}
	}

	fclose(maps);
	return ret;
}

static unsigned char nop5[5] = { 0x0f, 0x1f, 0x44, 0x00, 0x00 };

static void *find_nop5(void *fn)
{
	int i;

	for (i = 0; i < 10; i++) {
		if (!memcmp(nop5, fn + i, 5))
			return fn + i;
	}
	return NULL;
}

typedef void (__attribute__((nocf_check)) *trigger_t)(void);

static void *check_attach(struct uprobe_syscall_executed *skel, trigger_t trigger,
			  void *addr, int executed)
{
	struct __arch_relative_insn {
		__u8 op;
		__s32 raddr;
	} __packed *call;
	void *tramp = NULL;

	/* Uprobe gets optimized after first trigger, so let's press twice. */
	trigger();
	trigger();

	/* Make sure bpf program got executed.. */
	ASSERT_EQ(skel->bss->executed, executed, "executed");

	/* .. and check the trampoline is as expected. */
	call = (struct __arch_relative_insn *) addr;
	tramp = (void *) (call + 1) + call->raddr;
	ASSERT_EQ(call->op, 0xe8, "call");
	ASSERT_OK(find_uprobes_trampoline(tramp), "uprobes_trampoline");

	return tramp;
}

static void check_detach(void *addr, void *tramp)
{
	/* [uprobes_trampoline] stays after detach */
	ASSERT_OK(find_uprobes_trampoline(tramp), "uprobes_trampoline");
	ASSERT_OK(memcmp(addr, nop5, 5), "nop5");
}

static void check(struct uprobe_syscall_executed *skel, struct bpf_link *link,
		  trigger_t trigger, void *addr, int executed)
{
	void *tramp;

	tramp = check_attach(skel, trigger, addr, executed);
	bpf_link__destroy(link);
	check_detach(addr, tramp);
}

static void test_uprobe_legacy(void)
{
	struct uprobe_syscall_executed *skel = NULL;
	LIBBPF_OPTS(bpf_uprobe_opts, opts,
		.retprobe = true,
	);
	struct bpf_link *link;
	unsigned long offset;

	offset = get_uprobe_offset(&uprobe_test);
	if (!ASSERT_GE(offset, 0, "get_uprobe_offset"))
		goto cleanup;

	/* uprobe */
	skel = uprobe_syscall_executed__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_syscall_executed__open_and_load"))
		return;

	skel->bss->pid = getpid();

	link = bpf_program__attach_uprobe_opts(skel->progs.test_uprobe,
				0, "/proc/self/exe", offset, NULL);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_uprobe_opts"))
		goto cleanup;

	check(skel, link, uprobe_test, uprobe_test, 2);

	/* uretprobe */
	skel->bss->executed = 0;

	link = bpf_program__attach_uprobe_opts(skel->progs.test_uretprobe,
				0, "/proc/self/exe", offset, &opts);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_uprobe_opts"))
		goto cleanup;

	check(skel, link, uprobe_test, uprobe_test, 2);

cleanup:
	uprobe_syscall_executed__destroy(skel);
}

static void test_uprobe_multi(void)
{
	struct uprobe_syscall_executed *skel = NULL;
	LIBBPF_OPTS(bpf_uprobe_multi_opts, opts);
	struct bpf_link *link;
	unsigned long offset;

	offset = get_uprobe_offset(&uprobe_test);
	if (!ASSERT_GE(offset, 0, "get_uprobe_offset"))
		goto cleanup;

	opts.offsets = &offset;
	opts.cnt = 1;

	skel = uprobe_syscall_executed__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_syscall_executed__open_and_load"))
		return;

	skel->bss->pid = getpid();

	/* uprobe.multi */
	link = bpf_program__attach_uprobe_multi(skel->progs.test_uprobe_multi,
				0, "/proc/self/exe", NULL, &opts);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	check(skel, link, uprobe_test, uprobe_test, 2);

	/* uretprobe.multi */
	skel->bss->executed = 0;
	opts.retprobe = true;
	link = bpf_program__attach_uprobe_multi(skel->progs.test_uretprobe_multi,
				0, "/proc/self/exe", NULL, &opts);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	check(skel, link, uprobe_test, uprobe_test, 2);

cleanup:
	uprobe_syscall_executed__destroy(skel);
}

static void test_uprobe_session(void)
{
	struct uprobe_syscall_executed *skel = NULL;
	LIBBPF_OPTS(bpf_uprobe_multi_opts, opts,
		.session = true,
	);
	struct bpf_link *link;
	unsigned long offset;

	offset = get_uprobe_offset(&uprobe_test);
	if (!ASSERT_GE(offset, 0, "get_uprobe_offset"))
		goto cleanup;

	opts.offsets = &offset;
	opts.cnt = 1;

	skel = uprobe_syscall_executed__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_syscall_executed__open_and_load"))
		return;

	skel->bss->pid = getpid();

	link = bpf_program__attach_uprobe_multi(skel->progs.test_uprobe_session,
				0, "/proc/self/exe", NULL, &opts);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	check(skel, link, uprobe_test, uprobe_test, 4);

cleanup:
	uprobe_syscall_executed__destroy(skel);
}

static void test_uprobe_usdt(void)
{
	struct uprobe_syscall_executed *skel;
	struct bpf_link *link;
	void *addr;

	errno = 0;
	addr = find_nop5(usdt_test);
	if (!ASSERT_OK_PTR(addr, "find_nop5"))
		return;

	skel = uprobe_syscall_executed__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_syscall_executed__open_and_load"))
		return;

	skel->bss->pid = getpid();

	link = bpf_program__attach_usdt(skel->progs.test_usdt,
				-1 /* all PIDs */, "/proc/self/exe",
				"optimized_uprobe", "usdt", NULL);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_usdt"))
		goto cleanup;

	check(skel, link, usdt_test, addr, 2);

cleanup:
	uprobe_syscall_executed__destroy(skel);
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

	/* Run all the tests with shadow stack in place. */

	test_uprobe_regs_equal(false);
	test_uprobe_regs_equal(true);
	test_uretprobe_syscall_call();

	test_uprobe_legacy();
	test_uprobe_multi();
	test_uprobe_session();
	test_uprobe_usdt();

	test_regs_change();

	ARCH_PRCTL(ARCH_SHSTK_DISABLE, ARCH_SHSTK_SHSTK);
}

static volatile bool race_stop;

static USDT_DEFINE_SEMA(race);

static void *worker_trigger(void *arg)
{
	unsigned long rounds = 0;

	while (!race_stop) {
		uprobe_test();
		rounds++;
	}

	printf("tid %ld trigger rounds: %lu\n", sys_gettid(), rounds);
	return NULL;
}

static void *worker_attach(void *arg)
{
	LIBBPF_OPTS(bpf_uprobe_opts, opts);
	struct uprobe_syscall_executed *skel;
	unsigned long rounds = 0, offset;
	const char *sema[2] = {
		__stringify(USDT_SEMA(race)),
		NULL,
	};
	unsigned long *ref;
	int err;

	offset = get_uprobe_offset(&uprobe_test);
	if (!ASSERT_GE(offset, 0, "get_uprobe_offset"))
		return NULL;

	err = elf_resolve_syms_offsets("/proc/self/exe", 1, (const char **) &sema, &ref, STT_OBJECT);
	if (!ASSERT_OK(err, "elf_resolve_syms_offsets_sema"))
		return NULL;

	opts.ref_ctr_offset = *ref;

	skel = uprobe_syscall_executed__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_syscall_executed__open_and_load"))
		return NULL;

	skel->bss->pid = getpid();

	while (!race_stop) {
		skel->links.test_uprobe = bpf_program__attach_uprobe_opts(skel->progs.test_uprobe,
					0, "/proc/self/exe", offset, &opts);
		if (!ASSERT_OK_PTR(skel->links.test_uprobe, "bpf_program__attach_uprobe_opts"))
			break;

		bpf_link__destroy(skel->links.test_uprobe);
		skel->links.test_uprobe = NULL;
		rounds++;
	}

	printf("tid %ld attach rounds: %lu hits: %d\n", sys_gettid(), rounds, skel->bss->executed);
	uprobe_syscall_executed__destroy(skel);
	free(ref);
	return NULL;
}

static useconds_t race_msec(void)
{
	char *env;

	env = getenv("BPF_SELFTESTS_UPROBE_SYSCALL_RACE_MSEC");
	if (env)
		return atoi(env);

	/* default duration is 500ms */
	return 500;
}

static void test_uprobe_race(void)
{
	int err, i, nr_threads;
	pthread_t *threads;

	nr_threads = libbpf_num_possible_cpus();
	if (!ASSERT_GT(nr_threads, 0, "libbpf_num_possible_cpus"))
		return;
	nr_threads = max(2, nr_threads);

	threads = alloca(sizeof(*threads) * nr_threads);
	if (!ASSERT_OK_PTR(threads, "malloc"))
		return;

	for (i = 0; i < nr_threads; i++) {
		err = pthread_create(&threads[i], NULL, i % 2 ? worker_trigger : worker_attach,
				     NULL);
		if (!ASSERT_OK(err, "pthread_create"))
			goto cleanup;
	}

	usleep(race_msec() * 1000);

cleanup:
	race_stop = true;
	for (nr_threads = i, i = 0; i < nr_threads; i++)
		pthread_join(threads[i], NULL);

	ASSERT_FALSE(USDT_SEMA_IS_ACTIVE(race), "race_semaphore");
}

#ifndef __NR_uprobe
#define __NR_uprobe 336
#endif

static void test_uprobe_error(void)
{
	long err = syscall(__NR_uprobe);

	ASSERT_EQ(err, -1, "error");
	ASSERT_EQ(errno, ENXIO, "errno");
}

static void __test_uprobe_syscall(void)
{
	if (test__start_subtest("uretprobe_regs_equal"))
		test_uprobe_regs_equal(true);
	if (test__start_subtest("uretprobe_syscall_call"))
		test_uretprobe_syscall_call();
	if (test__start_subtest("uretprobe_shadow_stack"))
		test_uretprobe_shadow_stack();
	if (test__start_subtest("uprobe_legacy"))
		test_uprobe_legacy();
	if (test__start_subtest("uprobe_multi"))
		test_uprobe_multi();
	if (test__start_subtest("uprobe_session"))
		test_uprobe_session();
	if (test__start_subtest("uprobe_usdt"))
		test_uprobe_usdt();
	if (test__start_subtest("uprobe_race"))
		test_uprobe_race();
	if (test__start_subtest("uprobe_error"))
		test_uprobe_error();
	if (test__start_subtest("uprobe_regs_equal"))
		test_uprobe_regs_equal(false);
	if (test__start_subtest("regs_change"))
		test_regs_change();
}
#else
static void __test_uprobe_syscall(void)
{
	test__skip();
}
#endif

void test_uprobe_syscall(void)
{
	__test_uprobe_syscall();
}
