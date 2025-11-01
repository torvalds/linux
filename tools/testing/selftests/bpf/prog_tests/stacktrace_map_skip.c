// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "stacktrace_map_skip.skel.h"

#define TEST_STACK_DEPTH  2

void test_stacktrace_map_skip(void)
{
	struct stacktrace_map_skip *skel;
	int stackid_hmap_fd, stackmap_fd, stack_amap_fd;
	int err, stack_trace_len;

	skel = stacktrace_map_skip__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	/* find map fds */
	stackid_hmap_fd = bpf_map__fd(skel->maps.stackid_hmap);
	if (!ASSERT_GE(stackid_hmap_fd, 0, "stackid_hmap fd"))
		goto out;

	stackmap_fd = bpf_map__fd(skel->maps.stackmap);
	if (!ASSERT_GE(stackmap_fd, 0, "stackmap fd"))
		goto out;

	stack_amap_fd = bpf_map__fd(skel->maps.stack_amap);
	if (!ASSERT_GE(stack_amap_fd, 0, "stack_amap fd"))
		goto out;

	skel->bss->pid = getpid();

	err = stacktrace_map_skip__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	/* give some time for bpf program run */
	sleep(1);

	/* disable stack trace collection */
	skel->bss->control = 1;

	/* for every element in stackid_hmap, we can find a corresponding one
	 * in stackmap, and vice versa.
	 */
	err = compare_map_keys(stackid_hmap_fd, stackmap_fd);
	if (!ASSERT_OK(err, "compare_map_keys stackid_hmap vs. stackmap"))
		goto out;

	err = compare_map_keys(stackmap_fd, stackid_hmap_fd);
	if (!ASSERT_OK(err, "compare_map_keys stackmap vs. stackid_hmap"))
		goto out;

	stack_trace_len = TEST_STACK_DEPTH * sizeof(__u64);
	err = compare_stack_ips(stackmap_fd, stack_amap_fd, stack_trace_len);
	if (!ASSERT_OK(err, "compare_stack_ips stackmap vs. stack_amap"))
		goto out;

	if (!ASSERT_EQ(skel->bss->failed, 0, "skip_failed"))
		goto out;

out:
	stacktrace_map_skip__destroy(skel);
}
