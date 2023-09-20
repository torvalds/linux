// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "missed_kprobe.skel.h"

/*
 * Putting kprobe on bpf_fentry_test1 that calls bpf_kfunc_common_test
 * kfunc, which has also kprobe on. The latter won't get triggered due
 * to kprobe recursion check and kprobe missed counter is incremented.
 */
static void test_missed_perf_kprobe(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct bpf_link_info info = {};
	struct missed_kprobe *skel;
	__u32 len = sizeof(info);
	int err, prog_fd;

	skel = missed_kprobe__open_and_load();
	if (!ASSERT_OK_PTR(skel, "missed_kprobe__open_and_load"))
		goto cleanup;

	err = missed_kprobe__attach(skel);
	if (!ASSERT_OK(err, "missed_kprobe__attach"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.trigger);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	err = bpf_link_get_info_by_fd(bpf_link__fd(skel->links.test2), &info, &len);
	if (!ASSERT_OK(err, "bpf_link_get_info_by_fd"))
		goto cleanup;

	ASSERT_EQ(info.type, BPF_LINK_TYPE_PERF_EVENT, "info.type");
	ASSERT_EQ(info.perf_event.type, BPF_PERF_EVENT_KPROBE, "info.perf_event.type");
	ASSERT_EQ(info.perf_event.kprobe.missed, 1, "info.perf_event.kprobe.missed");

cleanup:
	missed_kprobe__destroy(skel);
}

void test_missed(void)
{
	if (test__start_subtest("perf_kprobe"))
		test_missed_perf_kprobe();
}
