// SPDX-License-Identifier: GPL-2.0

#include <unistd.h>
#include <test_progs.h>
#include "uprobe_multi.skel.h"
#include "uprobe_multi_bench.skel.h"
#include "uprobe_multi_usdt.skel.h"
#include "bpf/libbpf_internal.h"
#include "testing_helpers.h"

static char test_data[] = "test_data";

noinline void uprobe_multi_func_1(void)
{
	asm volatile ("");
}

noinline void uprobe_multi_func_2(void)
{
	asm volatile ("");
}

noinline void uprobe_multi_func_3(void)
{
	asm volatile ("");
}

struct child {
	int go[2];
	int pid;
};

static void release_child(struct child *child)
{
	int child_status;

	if (!child)
		return;
	close(child->go[1]);
	close(child->go[0]);
	if (child->pid > 0)
		waitpid(child->pid, &child_status, 0);
}

static void kick_child(struct child *child)
{
	char c = 1;

	if (child) {
		write(child->go[1], &c, 1);
		release_child(child);
	}
	fflush(NULL);
}

static struct child *spawn_child(void)
{
	static struct child child;
	int err;
	int c;

	/* pipe to notify child to execute the trigger functions */
	if (pipe(child.go))
		return NULL;

	child.pid = fork();
	if (child.pid < 0) {
		release_child(&child);
		errno = EINVAL;
		return NULL;
	}

	/* child */
	if (child.pid == 0) {
		close(child.go[1]);

		/* wait for parent's kick */
		err = read(child.go[0], &c, 1);
		if (err != 1)
			exit(err);

		uprobe_multi_func_1();
		uprobe_multi_func_2();
		uprobe_multi_func_3();

		exit(errno);
	}

	return &child;
}

static void uprobe_multi_test_run(struct uprobe_multi *skel, struct child *child)
{
	skel->bss->uprobe_multi_func_1_addr = (__u64) uprobe_multi_func_1;
	skel->bss->uprobe_multi_func_2_addr = (__u64) uprobe_multi_func_2;
	skel->bss->uprobe_multi_func_3_addr = (__u64) uprobe_multi_func_3;

	skel->bss->user_ptr = test_data;

	/*
	 * Disable pid check in bpf program if we are pid filter test,
	 * because the probe should be executed only by child->pid
	 * passed at the probe attach.
	 */
	skel->bss->pid = child ? 0 : getpid();

	if (child)
		kick_child(child);

	/* trigger all probes */
	uprobe_multi_func_1();
	uprobe_multi_func_2();
	uprobe_multi_func_3();

	/*
	 * There are 2 entry and 2 exit probe called for each uprobe_multi_func_[123]
	 * function and each slepable probe (6) increments uprobe_multi_sleep_result.
	 */
	ASSERT_EQ(skel->bss->uprobe_multi_func_1_result, 2, "uprobe_multi_func_1_result");
	ASSERT_EQ(skel->bss->uprobe_multi_func_2_result, 2, "uprobe_multi_func_2_result");
	ASSERT_EQ(skel->bss->uprobe_multi_func_3_result, 2, "uprobe_multi_func_3_result");

	ASSERT_EQ(skel->bss->uretprobe_multi_func_1_result, 2, "uretprobe_multi_func_1_result");
	ASSERT_EQ(skel->bss->uretprobe_multi_func_2_result, 2, "uretprobe_multi_func_2_result");
	ASSERT_EQ(skel->bss->uretprobe_multi_func_3_result, 2, "uretprobe_multi_func_3_result");

	ASSERT_EQ(skel->bss->uprobe_multi_sleep_result, 6, "uprobe_multi_sleep_result");

	if (child)
		ASSERT_EQ(skel->bss->child_pid, child->pid, "uprobe_multi_child_pid");
}

static void test_skel_api(void)
{
	struct uprobe_multi *skel = NULL;
	int err;

	skel = uprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_multi__open_and_load"))
		goto cleanup;

	err = uprobe_multi__attach(skel);
	if (!ASSERT_OK(err, "uprobe_multi__attach"))
		goto cleanup;

	uprobe_multi_test_run(skel, NULL);

cleanup:
	uprobe_multi__destroy(skel);
}

static void
__test_attach_api(const char *binary, const char *pattern, struct bpf_uprobe_multi_opts *opts,
		  struct child *child)
{
	pid_t pid = child ? child->pid : -1;
	struct uprobe_multi *skel = NULL;

	skel = uprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_multi__open_and_load"))
		goto cleanup;

	opts->retprobe = false;
	skel->links.uprobe = bpf_program__attach_uprobe_multi(skel->progs.uprobe, pid,
							      binary, pattern, opts);
	if (!ASSERT_OK_PTR(skel->links.uprobe, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	opts->retprobe = true;
	skel->links.uretprobe = bpf_program__attach_uprobe_multi(skel->progs.uretprobe, pid,
								 binary, pattern, opts);
	if (!ASSERT_OK_PTR(skel->links.uretprobe, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	opts->retprobe = false;
	skel->links.uprobe_sleep = bpf_program__attach_uprobe_multi(skel->progs.uprobe_sleep, pid,
								    binary, pattern, opts);
	if (!ASSERT_OK_PTR(skel->links.uprobe_sleep, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	opts->retprobe = true;
	skel->links.uretprobe_sleep = bpf_program__attach_uprobe_multi(skel->progs.uretprobe_sleep,
								       pid, binary, pattern, opts);
	if (!ASSERT_OK_PTR(skel->links.uretprobe_sleep, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	opts->retprobe = false;
	skel->links.uprobe_extra = bpf_program__attach_uprobe_multi(skel->progs.uprobe_extra, -1,
								    binary, pattern, opts);
	if (!ASSERT_OK_PTR(skel->links.uprobe_extra, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	uprobe_multi_test_run(skel, child);

cleanup:
	uprobe_multi__destroy(skel);
}

static void
test_attach_api(const char *binary, const char *pattern, struct bpf_uprobe_multi_opts *opts)
{
	struct child *child;

	/* no pid filter */
	__test_attach_api(binary, pattern, opts, NULL);

	/* pid filter */
	child = spawn_child();
	if (!ASSERT_OK_PTR(child, "spawn_child"))
		return;

	__test_attach_api(binary, pattern, opts, child);
}

static void test_attach_api_pattern(void)
{
	LIBBPF_OPTS(bpf_uprobe_multi_opts, opts);

	test_attach_api("/proc/self/exe", "uprobe_multi_func_*", &opts);
	test_attach_api("/proc/self/exe", "uprobe_multi_func_?", &opts);
}

static void test_attach_api_syms(void)
{
	LIBBPF_OPTS(bpf_uprobe_multi_opts, opts);
	const char *syms[3] = {
		"uprobe_multi_func_1",
		"uprobe_multi_func_2",
		"uprobe_multi_func_3",
	};

	opts.syms = syms;
	opts.cnt = ARRAY_SIZE(syms);
	test_attach_api("/proc/self/exe", NULL, &opts);
}

static void test_attach_api_fails(void)
{
	LIBBPF_OPTS(bpf_link_create_opts, opts);
	const char *path = "/proc/self/exe";
	struct uprobe_multi *skel = NULL;
	int prog_fd, link_fd = -1;
	unsigned long offset = 0;

	skel = uprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_multi__open_and_load"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.uprobe_extra);

	/* abnormal cnt */
	opts.uprobe_multi.path = path;
	opts.uprobe_multi.offsets = &offset;
	opts.uprobe_multi.cnt = INT_MAX;
	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	if (!ASSERT_EQ(link_fd, -E2BIG, "big cnt"))
		goto cleanup;

	/* cnt is 0 */
	LIBBPF_OPTS_RESET(opts,
		.uprobe_multi.path = path,
		.uprobe_multi.offsets = (unsigned long *) &offset,
	);

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	if (!ASSERT_EQ(link_fd, -EINVAL, "cnt_is_zero"))
		goto cleanup;

	/* negative offset */
	offset = -1;
	opts.uprobe_multi.path = path;
	opts.uprobe_multi.offsets = (unsigned long *) &offset;
	opts.uprobe_multi.cnt = 1;

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	if (!ASSERT_EQ(link_fd, -EINVAL, "offset_is_negative"))
		goto cleanup;

	/* offsets is NULL */
	LIBBPF_OPTS_RESET(opts,
		.uprobe_multi.path = path,
		.uprobe_multi.cnt = 1,
	);

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	if (!ASSERT_EQ(link_fd, -EINVAL, "offsets_is_null"))
		goto cleanup;

	/* wrong offsets pointer */
	LIBBPF_OPTS_RESET(opts,
		.uprobe_multi.path = path,
		.uprobe_multi.offsets = (unsigned long *) 1,
		.uprobe_multi.cnt = 1,
	);

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	if (!ASSERT_EQ(link_fd, -EFAULT, "offsets_is_wrong"))
		goto cleanup;

	/* path is NULL */
	offset = 1;
	LIBBPF_OPTS_RESET(opts,
		.uprobe_multi.offsets = (unsigned long *) &offset,
		.uprobe_multi.cnt = 1,
	);

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	if (!ASSERT_EQ(link_fd, -EINVAL, "path_is_null"))
		goto cleanup;

	/* wrong path pointer  */
	LIBBPF_OPTS_RESET(opts,
		.uprobe_multi.path = (const char *) 1,
		.uprobe_multi.offsets = (unsigned long *) &offset,
		.uprobe_multi.cnt = 1,
	);

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	if (!ASSERT_EQ(link_fd, -EFAULT, "path_is_wrong"))
		goto cleanup;

	/* wrong path type */
	LIBBPF_OPTS_RESET(opts,
		.uprobe_multi.path = "/",
		.uprobe_multi.offsets = (unsigned long *) &offset,
		.uprobe_multi.cnt = 1,
	);

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	if (!ASSERT_EQ(link_fd, -EBADF, "path_is_wrong_type"))
		goto cleanup;

	/* wrong cookies pointer */
	LIBBPF_OPTS_RESET(opts,
		.uprobe_multi.path = path,
		.uprobe_multi.offsets = (unsigned long *) &offset,
		.uprobe_multi.cookies = (__u64 *) 1ULL,
		.uprobe_multi.cnt = 1,
	);

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	if (!ASSERT_EQ(link_fd, -EFAULT, "cookies_is_wrong"))
		goto cleanup;

	/* wrong ref_ctr_offsets pointer */
	LIBBPF_OPTS_RESET(opts,
		.uprobe_multi.path = path,
		.uprobe_multi.offsets = (unsigned long *) &offset,
		.uprobe_multi.cookies = (__u64 *) &offset,
		.uprobe_multi.ref_ctr_offsets = (unsigned long *) 1,
		.uprobe_multi.cnt = 1,
	);

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	if (!ASSERT_EQ(link_fd, -EFAULT, "ref_ctr_offsets_is_wrong"))
		goto cleanup;

	/* wrong flags */
	LIBBPF_OPTS_RESET(opts,
		.uprobe_multi.flags = 1 << 31,
	);

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	if (!ASSERT_EQ(link_fd, -EINVAL, "wrong_flags"))
		goto cleanup;

	/* wrong pid */
	LIBBPF_OPTS_RESET(opts,
		.uprobe_multi.path = path,
		.uprobe_multi.offsets = (unsigned long *) &offset,
		.uprobe_multi.cnt = 1,
		.uprobe_multi.pid = -2,
	);

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_ERR(link_fd, "link_fd"))
		goto cleanup;
	ASSERT_EQ(link_fd, -EINVAL, "pid_is_wrong");

cleanup:
	if (link_fd >= 0)
		close(link_fd);
	uprobe_multi__destroy(skel);
}

static void __test_link_api(struct child *child)
{
	int prog_fd, link1_fd = -1, link2_fd = -1, link3_fd = -1, link4_fd = -1;
	LIBBPF_OPTS(bpf_link_create_opts, opts);
	const char *path = "/proc/self/exe";
	struct uprobe_multi *skel = NULL;
	unsigned long *offsets = NULL;
	const char *syms[3] = {
		"uprobe_multi_func_1",
		"uprobe_multi_func_2",
		"uprobe_multi_func_3",
	};
	int link_extra_fd = -1;
	int err;

	err = elf_resolve_syms_offsets(path, 3, syms, (unsigned long **) &offsets, STT_FUNC);
	if (!ASSERT_OK(err, "elf_resolve_syms_offsets"))
		return;

	opts.uprobe_multi.path = path;
	opts.uprobe_multi.offsets = offsets;
	opts.uprobe_multi.cnt = ARRAY_SIZE(syms);
	opts.uprobe_multi.pid = child ? child->pid : 0;

	skel = uprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_multi__open_and_load"))
		goto cleanup;

	opts.kprobe_multi.flags = 0;
	prog_fd = bpf_program__fd(skel->progs.uprobe);
	link1_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_GE(link1_fd, 0, "link1_fd"))
		goto cleanup;

	opts.kprobe_multi.flags = BPF_F_UPROBE_MULTI_RETURN;
	prog_fd = bpf_program__fd(skel->progs.uretprobe);
	link2_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_GE(link2_fd, 0, "link2_fd"))
		goto cleanup;

	opts.kprobe_multi.flags = 0;
	prog_fd = bpf_program__fd(skel->progs.uprobe_sleep);
	link3_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_GE(link3_fd, 0, "link3_fd"))
		goto cleanup;

	opts.kprobe_multi.flags = BPF_F_UPROBE_MULTI_RETURN;
	prog_fd = bpf_program__fd(skel->progs.uretprobe_sleep);
	link4_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_GE(link4_fd, 0, "link4_fd"))
		goto cleanup;

	opts.kprobe_multi.flags = 0;
	opts.uprobe_multi.pid = 0;
	prog_fd = bpf_program__fd(skel->progs.uprobe_extra);
	link_extra_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &opts);
	if (!ASSERT_GE(link_extra_fd, 0, "link_extra_fd"))
		goto cleanup;

	uprobe_multi_test_run(skel, child);

cleanup:
	if (link1_fd >= 0)
		close(link1_fd);
	if (link2_fd >= 0)
		close(link2_fd);
	if (link3_fd >= 0)
		close(link3_fd);
	if (link4_fd >= 0)
		close(link4_fd);
	if (link_extra_fd >= 0)
		close(link_extra_fd);

	uprobe_multi__destroy(skel);
	free(offsets);
}

static void test_link_api(void)
{
	struct child *child;

	/* no pid filter */
	__test_link_api(NULL);

	/* pid filter */
	child = spawn_child();
	if (!ASSERT_OK_PTR(child, "spawn_child"))
		return;

	__test_link_api(child);
}

static void test_bench_attach_uprobe(void)
{
	long attach_start_ns = 0, attach_end_ns = 0;
	struct uprobe_multi_bench *skel = NULL;
	long detach_start_ns, detach_end_ns;
	double attach_delta, detach_delta;
	int err;

	skel = uprobe_multi_bench__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_multi_bench__open_and_load"))
		goto cleanup;

	attach_start_ns = get_time_ns();

	err = uprobe_multi_bench__attach(skel);
	if (!ASSERT_OK(err, "uprobe_multi_bench__attach"))
		goto cleanup;

	attach_end_ns = get_time_ns();

	system("./uprobe_multi bench");

	ASSERT_EQ(skel->bss->count, 50000, "uprobes_count");

cleanup:
	detach_start_ns = get_time_ns();
	uprobe_multi_bench__destroy(skel);
	detach_end_ns = get_time_ns();

	attach_delta = (attach_end_ns - attach_start_ns) / 1000000000.0;
	detach_delta = (detach_end_ns - detach_start_ns) / 1000000000.0;

	printf("%s: attached in %7.3lfs\n", __func__, attach_delta);
	printf("%s: detached in %7.3lfs\n", __func__, detach_delta);
}

static void test_bench_attach_usdt(void)
{
	long attach_start_ns = 0, attach_end_ns = 0;
	struct uprobe_multi_usdt *skel = NULL;
	long detach_start_ns, detach_end_ns;
	double attach_delta, detach_delta;

	skel = uprobe_multi_usdt__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_multi__open"))
		goto cleanup;

	attach_start_ns = get_time_ns();

	skel->links.usdt0 = bpf_program__attach_usdt(skel->progs.usdt0, -1, "./uprobe_multi",
						     "test", "usdt", NULL);
	if (!ASSERT_OK_PTR(skel->links.usdt0, "bpf_program__attach_usdt"))
		goto cleanup;

	attach_end_ns = get_time_ns();

	system("./uprobe_multi usdt");

	ASSERT_EQ(skel->bss->count, 50000, "usdt_count");

cleanup:
	detach_start_ns = get_time_ns();
	uprobe_multi_usdt__destroy(skel);
	detach_end_ns = get_time_ns();

	attach_delta = (attach_end_ns - attach_start_ns) / 1000000000.0;
	detach_delta = (detach_end_ns - detach_start_ns) / 1000000000.0;

	printf("%s: attached in %7.3lfs\n", __func__, attach_delta);
	printf("%s: detached in %7.3lfs\n", __func__, detach_delta);
}

void test_uprobe_multi_test(void)
{
	if (test__start_subtest("skel_api"))
		test_skel_api();
	if (test__start_subtest("attach_api_pattern"))
		test_attach_api_pattern();
	if (test__start_subtest("attach_api_syms"))
		test_attach_api_syms();
	if (test__start_subtest("link_api"))
		test_link_api();
	if (test__start_subtest("bench_uprobe"))
		test_bench_attach_uprobe();
	if (test__start_subtest("bench_usdt"))
		test_bench_attach_usdt();
	if (test__start_subtest("attach_api_fails"))
		test_attach_api_fails();
}
