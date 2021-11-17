// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "test_enable_stats.skel.h"

void test_enable_stats(void)
{
	struct test_enable_stats *skel;
	int stats_fd, err, prog_fd;
	struct bpf_prog_info info;
	__u32 info_len = sizeof(info);
	int duration = 0;

	skel = test_enable_stats__open_and_load();
	if (CHECK(!skel, "skel_open_and_load", "skeleton open/load failed\n"))
		return;

	stats_fd = bpf_enable_stats(BPF_STATS_RUN_TIME);
	if (CHECK(stats_fd < 0, "get_stats_fd", "failed %d\n", errno)) {
		test_enable_stats__destroy(skel);
		return;
	}

	err = test_enable_stats__attach(skel);
	if (CHECK(err, "attach_raw_tp", "err %d\n", err))
		goto cleanup;

	test_enable_stats__detach(skel);

	prog_fd = bpf_program__fd(skel->progs.test_enable_stats);
	memset(&info, 0, info_len);
	err = bpf_obj_get_info_by_fd(prog_fd, &info, &info_len);
	if (CHECK(err, "get_prog_info",
		  "failed to get bpf_prog_info for fd %d\n", prog_fd))
		goto cleanup;
	if (CHECK(info.run_time_ns == 0, "check_stats_enabled",
		  "failed to enable run_time_ns stats\n"))
		goto cleanup;

	CHECK(info.run_cnt != skel->bss->count, "check_run_cnt_valid",
	      "invalid run_cnt stats\n");

cleanup:
	test_enable_stats__destroy(skel);
	close(stats_fd);
}
