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

static void uprobe_multi_test_run(struct uprobe_multi *skel)
{
	skel->bss->uprobe_multi_func_1_addr = (__u64) uprobe_multi_func_1;
	skel->bss->uprobe_multi_func_2_addr = (__u64) uprobe_multi_func_2;
	skel->bss->uprobe_multi_func_3_addr = (__u64) uprobe_multi_func_3;

	skel->bss->user_ptr = test_data;
	skel->bss->pid = getpid();

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

	uprobe_multi_test_run(skel);

cleanup:
	uprobe_multi__destroy(skel);
}

static void
test_attach_api(const char *binary, const char *pattern, struct bpf_uprobe_multi_opts *opts)
{
	struct uprobe_multi *skel = NULL;

	skel = uprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_multi__open_and_load"))
		goto cleanup;

	opts->retprobe = false;
	skel->links.uprobe = bpf_program__attach_uprobe_multi(skel->progs.uprobe, -1,
							      binary, pattern, opts);
	if (!ASSERT_OK_PTR(skel->links.uprobe, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	opts->retprobe = true;
	skel->links.uretprobe = bpf_program__attach_uprobe_multi(skel->progs.uretprobe, -1,
								 binary, pattern, opts);
	if (!ASSERT_OK_PTR(skel->links.uretprobe, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	opts->retprobe = false;
	skel->links.uprobe_sleep = bpf_program__attach_uprobe_multi(skel->progs.uprobe_sleep, -1,
								    binary, pattern, opts);
	if (!ASSERT_OK_PTR(skel->links.uprobe_sleep, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	opts->retprobe = true;
	skel->links.uretprobe_sleep = bpf_program__attach_uprobe_multi(skel->progs.uretprobe_sleep,
								       -1, binary, pattern, opts);
	if (!ASSERT_OK_PTR(skel->links.uretprobe_sleep, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	uprobe_multi_test_run(skel);

cleanup:
	uprobe_multi__destroy(skel);
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

static void test_link_api(void)
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
	int err;

	err = elf_resolve_syms_offsets(path, 3, syms, (unsigned long **) &offsets);
	if (!ASSERT_OK(err, "elf_resolve_syms_offsets"))
		return;

	opts.uprobe_multi.path = path;
	opts.uprobe_multi.offsets = offsets;
	opts.uprobe_multi.cnt = ARRAY_SIZE(syms);

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

	uprobe_multi_test_run(skel);

cleanup:
	if (link1_fd >= 0)
		close(link1_fd);
	if (link2_fd >= 0)
		close(link2_fd);
	if (link3_fd >= 0)
		close(link3_fd);
	if (link4_fd >= 0)
		close(link4_fd);

	uprobe_multi__destroy(skel);
	free(offsets);
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
}
