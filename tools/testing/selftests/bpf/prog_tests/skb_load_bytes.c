// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include "skb_load_bytes.skel.h"

void test_skb_load_bytes(void)
{
	struct skb_load_bytes *skel;
	int err, prog_fd, test_result;
	struct __sk_buff skb = { 0 };

	LIBBPF_OPTS(bpf_test_run_opts, tattr,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.ctx_in = &skb,
		.ctx_size_in = sizeof(skb),
	);

	skel = skb_load_bytes__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	prog_fd = bpf_program__fd(skel->progs.skb_process);
	if (!ASSERT_GE(prog_fd, 0, "prog_fd"))
		goto out;

	skel->bss->load_offset = (uint32_t)(-1);
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	if (!ASSERT_OK(err, "bpf_prog_test_run_opts"))
		goto out;
	test_result = skel->bss->test_result;
	if (!ASSERT_EQ(test_result, -EFAULT, "offset -1"))
		goto out;

	skel->bss->load_offset = (uint32_t)10;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	if (!ASSERT_OK(err, "bpf_prog_test_run_opts"))
		goto out;
	test_result = skel->bss->test_result;
	if (!ASSERT_EQ(test_result, 0, "offset 10"))
		goto out;

out:
	skb_load_bytes__destroy(skel);
}
