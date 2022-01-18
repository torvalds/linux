// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include "test_bpf_nf.skel.h"

enum {
	TEST_XDP,
	TEST_TC_BPF,
};

void test_bpf_nf_ct(int mode)
{
	struct test_bpf_nf *skel;
	int prog_fd, err, retval;

	skel = test_bpf_nf__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_bpf_nf__open_and_load"))
		return;

	if (mode == TEST_XDP)
		prog_fd = bpf_program__fd(skel->progs.nf_xdp_ct_test);
	else
		prog_fd = bpf_program__fd(skel->progs.nf_skb_ct_test);

	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4), NULL, NULL,
				(__u32 *)&retval, NULL);
	if (!ASSERT_OK(err, "bpf_prog_test_run"))
		goto end;

	ASSERT_EQ(skel->bss->test_einval_bpf_tuple, -EINVAL, "Test EINVAL for NULL bpf_tuple");
	ASSERT_EQ(skel->bss->test_einval_reserved, -EINVAL, "Test EINVAL for reserved not set to 0");
	ASSERT_EQ(skel->bss->test_einval_netns_id, -EINVAL, "Test EINVAL for netns_id < -1");
	ASSERT_EQ(skel->bss->test_einval_len_opts, -EINVAL, "Test EINVAL for len__opts != NF_BPF_CT_OPTS_SZ");
	ASSERT_EQ(skel->bss->test_eproto_l4proto, -EPROTO, "Test EPROTO for l4proto != TCP or UDP");
	ASSERT_EQ(skel->bss->test_enonet_netns_id, -ENONET, "Test ENONET for bad but valid netns_id");
	ASSERT_EQ(skel->bss->test_enoent_lookup, -ENOENT, "Test ENOENT for failed lookup");
	ASSERT_EQ(skel->bss->test_eafnosupport, -EAFNOSUPPORT, "Test EAFNOSUPPORT for invalid len__tuple");
end:
	test_bpf_nf__destroy(skel);
}

void test_bpf_nf(void)
{
	if (test__start_subtest("xdp-ct"))
		test_bpf_nf_ct(TEST_XDP);
	if (test__start_subtest("tc-bpf-ct"))
		test_bpf_nf_ct(TEST_TC_BPF);
}
