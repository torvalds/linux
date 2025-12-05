// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "stacktrace_ips.skel.h"

#ifdef __x86_64__
static int check_stacktrace_ips(int fd, __u32 key, int cnt, ...)
{
	__u64 ips[PERF_MAX_STACK_DEPTH];
	struct ksyms *ksyms = NULL;
	int i, err = 0;
	va_list args;

	/* sorted by addr */
	ksyms = load_kallsyms_local();
	if (!ASSERT_OK_PTR(ksyms, "load_kallsyms_local"))
		return -1;

	/* unlikely, but... */
	if (!ASSERT_LT(cnt, PERF_MAX_STACK_DEPTH, "check_max"))
		return -1;

	err = bpf_map_lookup_elem(fd, &key, ips);
	if (err)
		goto out;

	/*
	 * Compare all symbols provided via arguments with stacktrace ips,
	 * and their related symbol addresses.t
	 */
	va_start(args, cnt);

	for (i = 0; i < cnt; i++) {
		unsigned long val;
		struct ksym *ksym;

		val = va_arg(args, unsigned long);
		ksym = ksym_search_local(ksyms, ips[i]);
		if (!ASSERT_OK_PTR(ksym, "ksym_search_local"))
			break;
		ASSERT_EQ(ksym->addr, val, "stack_cmp");
	}

	va_end(args);

out:
	free_kallsyms_local(ksyms);
	return err;
}

static void test_stacktrace_ips_kprobe_multi(bool retprobe)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts,
		.retprobe = retprobe
	);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct stacktrace_ips *skel;

	skel = stacktrace_ips__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stacktrace_ips__open_and_load"))
		return;

	if (!skel->kconfig->CONFIG_UNWINDER_ORC) {
		test__skip();
		goto cleanup;
	}

	skel->links.kprobe_multi_test = bpf_program__attach_kprobe_multi_opts(
							skel->progs.kprobe_multi_test,
							"bpf_testmod_stacktrace_test", &opts);
	if (!ASSERT_OK_PTR(skel->links.kprobe_multi_test, "bpf_program__attach_kprobe_multi_opts"))
		goto cleanup;

	trigger_module_test_read(1);

	load_kallsyms();

	check_stacktrace_ips(bpf_map__fd(skel->maps.stackmap), skel->bss->stack_key, 4,
			     ksym_get_addr("bpf_testmod_stacktrace_test_3"),
			     ksym_get_addr("bpf_testmod_stacktrace_test_2"),
			     ksym_get_addr("bpf_testmod_stacktrace_test_1"),
			     ksym_get_addr("bpf_testmod_test_read"));

cleanup:
	stacktrace_ips__destroy(skel);
}

static void test_stacktrace_ips_raw_tp(void)
{
	__u32 info_len = sizeof(struct bpf_prog_info);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct bpf_prog_info info = {};
	struct stacktrace_ips *skel;
	__u64 bpf_prog_ksym = 0;
	int err;

	skel = stacktrace_ips__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stacktrace_ips__open_and_load"))
		return;

	if (!skel->kconfig->CONFIG_UNWINDER_ORC) {
		test__skip();
		goto cleanup;
	}

	skel->links.rawtp_test = bpf_program__attach_raw_tracepoint(
							skel->progs.rawtp_test,
							"bpf_testmod_test_read");
	if (!ASSERT_OK_PTR(skel->links.rawtp_test, "bpf_program__attach_raw_tracepoint"))
		goto cleanup;

	/* get bpf program address */
	info.jited_ksyms = ptr_to_u64(&bpf_prog_ksym);
	info.nr_jited_ksyms = 1;
	err = bpf_prog_get_info_by_fd(bpf_program__fd(skel->progs.rawtp_test),
				      &info, &info_len);
	if (!ASSERT_OK(err, "bpf_prog_get_info_by_fd"))
		goto cleanup;

	trigger_module_test_read(1);

	load_kallsyms();

	check_stacktrace_ips(bpf_map__fd(skel->maps.stackmap), skel->bss->stack_key, 2,
			     bpf_prog_ksym,
			     ksym_get_addr("bpf_trace_run2"));

cleanup:
	stacktrace_ips__destroy(skel);
}

static void __test_stacktrace_ips(void)
{
	if (test__start_subtest("kprobe_multi"))
		test_stacktrace_ips_kprobe_multi(false);
	if (test__start_subtest("kretprobe_multi"))
		test_stacktrace_ips_kprobe_multi(true);
	if (test__start_subtest("raw_tp"))
		test_stacktrace_ips_raw_tp();
}
#else
static void __test_stacktrace_ips(void)
{
	test__skip();
}
#endif

void test_stacktrace_ips(void)
{
	__test_stacktrace_ips();
}
