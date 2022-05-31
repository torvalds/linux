// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>
#include <network_helpers.h>
#include "skb_pkt_end.skel.h"

static int sanity_run(struct bpf_program *prog)
{
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	prog_fd = bpf_program__fd(prog);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, "test_run"))
		return -1;
	if (!ASSERT_EQ(topts.retval, 123, "test_run retval"))
		return -1;
	return 0;
}

void test_test_skb_pkt_end(void)
{
	struct skb_pkt_end *skb_pkt_end_skel = NULL;
	__u32 duration = 0;
	int err;

	skb_pkt_end_skel = skb_pkt_end__open_and_load();
	if (CHECK(!skb_pkt_end_skel, "skb_pkt_end_skel_load", "skb_pkt_end skeleton failed\n"))
		goto cleanup;

	err = skb_pkt_end__attach(skb_pkt_end_skel);
	if (CHECK(err, "skb_pkt_end_attach", "skb_pkt_end attach failed: %d\n", err))
		goto cleanup;

	if (sanity_run(skb_pkt_end_skel->progs.main_prog))
		goto cleanup;

cleanup:
	skb_pkt_end__destroy(skb_pkt_end_skel);
}
