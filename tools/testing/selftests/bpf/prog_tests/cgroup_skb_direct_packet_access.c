// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "cgroup_skb_direct_packet_access.skel.h"

void test_cgroup_skb_prog_run_direct_packet_access(void)
{
	int err;
	struct cgroup_skb_direct_packet_access *skel;
	char test_skb[64] = {};

	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = test_skb,
		.data_size_in = sizeof(test_skb),
	);

	skel = cgroup_skb_direct_packet_access__open_and_load();
	if (!ASSERT_OK_PTR(skel, "cgroup_skb_direct_packet_access__open_and_load"))
		return;

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.direct_packet_access), &topts);
	ASSERT_OK(err, "bpf_prog_test_run_opts err");
	ASSERT_EQ(topts.retval, 1, "retval");

	ASSERT_NEQ(skel->bss->data_end, 0, "data_end");

	cgroup_skb_direct_packet_access__destroy(skel);
}
