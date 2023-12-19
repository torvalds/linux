// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "kprobe_multi.skel.h"
#include "trace_helpers.h"
#include "bpf/libbpf_internal.h"

static struct ksyms *ksyms;

static void kprobe_multi_testmod_check(struct kprobe_multi *skel)
{
	ASSERT_EQ(skel->bss->kprobe_testmod_test1_result, 1, "kprobe_test1_result");
	ASSERT_EQ(skel->bss->kprobe_testmod_test2_result, 1, "kprobe_test2_result");
	ASSERT_EQ(skel->bss->kprobe_testmod_test3_result, 1, "kprobe_test3_result");

	ASSERT_EQ(skel->bss->kretprobe_testmod_test1_result, 1, "kretprobe_test1_result");
	ASSERT_EQ(skel->bss->kretprobe_testmod_test2_result, 1, "kretprobe_test2_result");
	ASSERT_EQ(skel->bss->kretprobe_testmod_test3_result, 1, "kretprobe_test3_result");
}

static void test_testmod_attach_api(struct bpf_kprobe_multi_opts *opts)
{
	struct kprobe_multi *skel = NULL;

	skel = kprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_raw_skel_load"))
		return;

	skel->bss->pid = getpid();

	skel->links.test_kprobe_testmod = bpf_program__attach_kprobe_multi_opts(
						skel->progs.test_kprobe_testmod,
						NULL, opts);
	if (!skel->links.test_kprobe_testmod)
		goto cleanup;

	opts->retprobe = true;
	skel->links.test_kretprobe_testmod = bpf_program__attach_kprobe_multi_opts(
						skel->progs.test_kretprobe_testmod,
						NULL, opts);
	if (!skel->links.test_kretprobe_testmod)
		goto cleanup;

	ASSERT_OK(trigger_module_test_read(1), "trigger_read");
	kprobe_multi_testmod_check(skel);

cleanup:
	kprobe_multi__destroy(skel);
}

static void test_testmod_attach_api_addrs(void)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	unsigned long long addrs[3];

	addrs[0] = ksym_get_addr_local(ksyms, "bpf_testmod_fentry_test1");
	ASSERT_NEQ(addrs[0], 0, "ksym_get_addr_local");
	addrs[1] = ksym_get_addr_local(ksyms, "bpf_testmod_fentry_test2");
	ASSERT_NEQ(addrs[1], 0, "ksym_get_addr_local");
	addrs[2] = ksym_get_addr_local(ksyms, "bpf_testmod_fentry_test3");
	ASSERT_NEQ(addrs[2], 0, "ksym_get_addr_local");

	opts.addrs = (const unsigned long *) addrs;
	opts.cnt = ARRAY_SIZE(addrs);

	test_testmod_attach_api(&opts);
}

static void test_testmod_attach_api_syms(void)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	const char *syms[3] = {
		"bpf_testmod_fentry_test1",
		"bpf_testmod_fentry_test2",
		"bpf_testmod_fentry_test3",
	};

	opts.syms = syms;
	opts.cnt = ARRAY_SIZE(syms);
	test_testmod_attach_api(&opts);
}

void serial_test_kprobe_multi_testmod_test(void)
{
	ksyms = load_kallsyms_local();
	if (!ASSERT_OK_PTR(ksyms, "load_kallsyms_local"))
		return;

	if (test__start_subtest("testmod_attach_api_syms"))
		test_testmod_attach_api_syms();

	if (test__start_subtest("testmod_attach_api_addrs"))
		test_testmod_attach_api_addrs();

	free_kallsyms_local(ksyms);
}
