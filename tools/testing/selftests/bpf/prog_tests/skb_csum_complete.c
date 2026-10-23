// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include "skb_csum_complete.skel.h"

static void run(struct bpf_program *prog, int expected_err)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
		.flags = BPF_F_TEST_SKB_CHECKSUM_COMPLETE,
	);
	int err;

	err = bpf_prog_test_run_opts(bpf_program__fd(prog), &topts);
	if (!ASSERT_EQ(err, expected_err, "test_run"))
		return;
	if (!expected_err)
		ASSERT_EQ(topts.retval, 0, "retval");
}

void test_skb_csum_complete(void)
{
	struct skb_csum_complete *skel;

	skel = skb_csum_complete__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	if (test__start_subtest("change_tail_trim"))
		run(skel->progs.change_tail_trim, 0);
	if (test__start_subtest("change_tail_grow_write"))
		run(skel->progs.change_tail_grow_write, 0);
	if (test__start_subtest("store_no_recompute"))
		run(skel->progs.store_no_recompute, -EBADMSG);

	skb_csum_complete__destroy(skel);
}
