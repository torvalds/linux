// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

#include "test_pkt_access.skel.h"

static const __u32 duration;

static void check_run_cnt(int prog_fd, __u64 run_cnt)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	int err;

	err = bpf_obj_get_info_by_fd(prog_fd, &info, &info_len);
	if (CHECK(err, "get_prog_info", "failed to get bpf_prog_info for fd %d\n", prog_fd))
		return;

	CHECK(run_cnt != info.run_cnt, "run_cnt",
	      "incorrect number of repetitions, want %llu have %llu\n", run_cnt, info.run_cnt);
}

void test_prog_run_xattr(void)
{
	struct test_pkt_access *skel;
	int err, stats_fd = -1;
	char buf[10] = {};
	__u64 run_cnt = 0;

	struct bpf_prog_test_run_attr tattr = {
		.repeat = 1,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.data_out = buf,
		.data_size_out = 5,
	};

	stats_fd = bpf_enable_stats(BPF_STATS_RUN_TIME);
	if (CHECK_ATTR(stats_fd < 0, "enable_stats", "failed %d\n", errno))
		return;

	skel = test_pkt_access__open_and_load();
	if (CHECK_ATTR(!skel, "open_and_load", "failed\n"))
		goto cleanup;

	tattr.prog_fd = bpf_program__fd(skel->progs.test_pkt_access);

	err = bpf_prog_test_run_xattr(&tattr);
	CHECK_ATTR(err >= 0 || errno != ENOSPC || tattr.retval, "run",
	      "err %d errno %d retval %d\n", err, errno, tattr.retval);

	CHECK_ATTR(tattr.data_size_out != sizeof(pkt_v4), "data_size_out",
	      "incorrect output size, want %zu have %u\n",
	      sizeof(pkt_v4), tattr.data_size_out);

	CHECK_ATTR(buf[5] != 0, "overflow",
	      "BPF_PROG_TEST_RUN ignored size hint\n");

	run_cnt += tattr.repeat;
	check_run_cnt(tattr.prog_fd, run_cnt);

	tattr.data_out = NULL;
	tattr.data_size_out = 0;
	tattr.repeat = 2;
	errno = 0;

	err = bpf_prog_test_run_xattr(&tattr);
	CHECK_ATTR(err || errno || tattr.retval, "run_no_output",
	      "err %d errno %d retval %d\n", err, errno, tattr.retval);

	tattr.data_size_out = 1;
	err = bpf_prog_test_run_xattr(&tattr);
	CHECK_ATTR(err != -EINVAL, "run_wrong_size_out", "err %d\n", err);

	run_cnt += tattr.repeat;
	check_run_cnt(tattr.prog_fd, run_cnt);

cleanup:
	if (skel)
		test_pkt_access__destroy(skel);
	if (stats_fd >= 0)
		close(stats_fd);
}
