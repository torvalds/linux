// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Jesper Dangaard Brouer */

#include <linux/if_link.h> /* before test_progs.h, avoid bpf_util.h redefines */
#include <test_progs.h>
#include "test_check_mtu.skel.h"
#include "network_helpers.h"

#include <stdlib.h>
#include <inttypes.h>

#define IFINDEX_LO 1

static __u32 duration; /* Hint: needed for CHECK macro */

static int read_mtu_device_lo(void)
{
	const char *filename = "/sys/class/net/lo/mtu";
	char buf[11] = {};
	int value, n, fd;

	fd = open(filename, 0, O_RDONLY);
	if (fd == -1)
		return -1;

	n = read(fd, buf, sizeof(buf));
	close(fd);

	if (n == -1)
		return -2;

	value = strtoimax(buf, NULL, 10);
	if (errno == ERANGE)
		return -3;

	return value;
}

static void test_check_mtu_xdp_attach(void)
{
	struct bpf_link_info link_info;
	__u32 link_info_len = sizeof(link_info);
	struct test_check_mtu *skel;
	struct bpf_program *prog;
	struct bpf_link *link;
	int err = 0;
	int fd;

	skel = test_check_mtu__open_and_load();
	if (CHECK(!skel, "open and load skel", "failed"))
		return; /* Exit if e.g. helper unknown to kernel */

	prog = skel->progs.xdp_use_helper_basic;

	link = bpf_program__attach_xdp(prog, IFINDEX_LO);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto out;
	skel->links.xdp_use_helper_basic = link;

	memset(&link_info, 0, sizeof(link_info));
	fd = bpf_link__fd(link);
	err = bpf_obj_get_info_by_fd(fd, &link_info, &link_info_len);
	if (CHECK(err, "link_info", "failed: %d\n", err))
		goto out;

	CHECK(link_info.type != BPF_LINK_TYPE_XDP, "link_type",
	      "got %u != exp %u\n", link_info.type, BPF_LINK_TYPE_XDP);
	CHECK(link_info.xdp.ifindex != IFINDEX_LO, "link_ifindex",
	      "got %u != exp %u\n", link_info.xdp.ifindex, IFINDEX_LO);

	err = bpf_link__detach(link);
	CHECK(err, "link_detach", "failed %d\n", err);

out:
	test_check_mtu__destroy(skel);
}

static void test_check_mtu_run_xdp(struct test_check_mtu *skel,
				   struct bpf_program *prog,
				   __u32 mtu_expect)
{
	int retval_expect = XDP_PASS;
	__u32 mtu_result = 0;
	char buf[256] = {};
	int err, prog_fd = bpf_program__fd(prog);
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.repeat = 1,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.data_out = buf,
		.data_size_out = sizeof(buf),
	);

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, retval_expect, "retval");

	/* Extract MTU that BPF-prog got */
	mtu_result = skel->bss->global_bpf_mtu_xdp;
	ASSERT_EQ(mtu_result, mtu_expect, "MTU-compare-user");
}


static void test_check_mtu_xdp(__u32 mtu, __u32 ifindex)
{
	struct test_check_mtu *skel;
	int err;

	skel = test_check_mtu__open();
	if (CHECK(!skel, "skel_open", "failed"))
		return;

	/* Update "constants" in BPF-prog *BEFORE* libbpf load */
	skel->rodata->GLOBAL_USER_MTU = mtu;
	skel->rodata->GLOBAL_USER_IFINDEX = ifindex;

	err = test_check_mtu__load(skel);
	if (CHECK(err, "skel_load", "failed: %d\n", err))
		goto cleanup;

	test_check_mtu_run_xdp(skel, skel->progs.xdp_use_helper, mtu);
	test_check_mtu_run_xdp(skel, skel->progs.xdp_exceed_mtu, mtu);
	test_check_mtu_run_xdp(skel, skel->progs.xdp_minus_delta, mtu);
	test_check_mtu_run_xdp(skel, skel->progs.xdp_input_len, mtu);
	test_check_mtu_run_xdp(skel, skel->progs.xdp_input_len_exceed, mtu);

cleanup:
	test_check_mtu__destroy(skel);
}

static void test_check_mtu_run_tc(struct test_check_mtu *skel,
				  struct bpf_program *prog,
				  __u32 mtu_expect)
{
	int retval_expect = BPF_OK;
	__u32 mtu_result = 0;
	char buf[256] = {};
	int err, prog_fd = bpf_program__fd(prog);
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.data_out = buf,
		.data_size_out = sizeof(buf),
		.repeat = 1,
	);

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, retval_expect, "retval");

	/* Extract MTU that BPF-prog got */
	mtu_result = skel->bss->global_bpf_mtu_tc;
	ASSERT_EQ(mtu_result, mtu_expect, "MTU-compare-user");
}


static void test_check_mtu_tc(__u32 mtu, __u32 ifindex)
{
	struct test_check_mtu *skel;
	int err;

	skel = test_check_mtu__open();
	if (CHECK(!skel, "skel_open", "failed"))
		return;

	/* Update "constants" in BPF-prog *BEFORE* libbpf load */
	skel->rodata->GLOBAL_USER_MTU = mtu;
	skel->rodata->GLOBAL_USER_IFINDEX = ifindex;

	err = test_check_mtu__load(skel);
	if (CHECK(err, "skel_load", "failed: %d\n", err))
		goto cleanup;

	test_check_mtu_run_tc(skel, skel->progs.tc_use_helper, mtu);
	test_check_mtu_run_tc(skel, skel->progs.tc_exceed_mtu, mtu);
	test_check_mtu_run_tc(skel, skel->progs.tc_exceed_mtu_da, mtu);
	test_check_mtu_run_tc(skel, skel->progs.tc_minus_delta, mtu);
	test_check_mtu_run_tc(skel, skel->progs.tc_input_len, mtu);
	test_check_mtu_run_tc(skel, skel->progs.tc_input_len_exceed, mtu);
cleanup:
	test_check_mtu__destroy(skel);
}

void serial_test_check_mtu(void)
{
	__u32 mtu_lo;

	if (test__start_subtest("bpf_check_mtu XDP-attach"))
		test_check_mtu_xdp_attach();

	mtu_lo = read_mtu_device_lo();
	if (CHECK(mtu_lo < 0, "reading MTU value", "failed (err:%d)", mtu_lo))
		return;

	if (test__start_subtest("bpf_check_mtu XDP-run"))
		test_check_mtu_xdp(mtu_lo, 0);

	if (test__start_subtest("bpf_check_mtu XDP-run ifindex-lookup"))
		test_check_mtu_xdp(mtu_lo, IFINDEX_LO);

	if (test__start_subtest("bpf_check_mtu TC-run"))
		test_check_mtu_tc(mtu_lo, 0);

	if (test__start_subtest("bpf_check_mtu TC-run ifindex-lookup"))
		test_check_mtu_tc(mtu_lo, IFINDEX_LO);
}
