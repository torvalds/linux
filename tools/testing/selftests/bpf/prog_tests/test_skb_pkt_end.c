// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>
#include <network_helpers.h>
#include "skb_pkt_end.skel.h"

static int sanity_run(struct bpf_program *prog)
{
	__u32 duration, retval;
	int err, prog_fd;

	prog_fd = bpf_program__fd(prog);
	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
				NULL, NULL, &retval, &duration);
	if (CHECK(err || retval != 123, "test_run",
		  "err %d errno %d retval %d duration %d\n",
		  err, errno, retval, duration))
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
