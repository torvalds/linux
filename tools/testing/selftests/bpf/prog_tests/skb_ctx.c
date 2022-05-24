// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

void test_skb_ctx(void)
{
	struct __sk_buff skb = {
		.cb[0] = 1,
		.cb[1] = 2,
		.cb[2] = 3,
		.cb[3] = 4,
		.cb[4] = 5,
		.priority = 6,
		.ingress_ifindex = 11,
		.ifindex = 1,
		.tstamp = 7,
		.wire_len = 100,
		.gso_segs = 8,
		.mark = 9,
		.gso_size = 10,
		.hwtstamp = 11,
	};
	LIBBPF_OPTS(bpf_test_run_opts, tattr,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.ctx_in = &skb,
		.ctx_size_in = sizeof(skb),
		.ctx_out = &skb,
		.ctx_size_out = sizeof(skb),
	);
	struct bpf_object *obj;
	int err, prog_fd, i;

	err = bpf_prog_test_load("./test_skb_ctx.o", BPF_PROG_TYPE_SCHED_CLS,
				 &obj, &prog_fd);
	if (!ASSERT_OK(err, "load"))
		return;

	/* ctx_in != NULL, ctx_size_in == 0 */

	tattr.ctx_size_in = 0;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	ASSERT_NEQ(err, 0, "ctx_size_in");
	tattr.ctx_size_in = sizeof(skb);

	/* ctx_out != NULL, ctx_size_out == 0 */

	tattr.ctx_size_out = 0;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	ASSERT_NEQ(err, 0, "ctx_size_out");
	tattr.ctx_size_out = sizeof(skb);

	/* non-zero [len, tc_index] fields should be rejected*/

	skb.len = 1;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	ASSERT_NEQ(err, 0, "len");
	skb.len = 0;

	skb.tc_index = 1;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	ASSERT_NEQ(err, 0, "tc_index");
	skb.tc_index = 0;

	/* non-zero [hash, sk] fields should be rejected */

	skb.hash = 1;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	ASSERT_NEQ(err, 0, "hash");
	skb.hash = 0;

	skb.sk = (struct bpf_sock *)1;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	ASSERT_NEQ(err, 0, "sk");
	skb.sk = 0;

	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	ASSERT_OK(err, "test_run");
	ASSERT_OK(tattr.retval, "test_run retval");
	ASSERT_EQ(tattr.ctx_size_out, sizeof(skb), "ctx_size_out");

	for (i = 0; i < 5; i++)
		ASSERT_EQ(skb.cb[i], i + 2, "ctx_out_cb");
	ASSERT_EQ(skb.priority, 7, "ctx_out_priority");
	ASSERT_EQ(skb.ifindex, 1, "ctx_out_ifindex");
	ASSERT_EQ(skb.ingress_ifindex, 11, "ctx_out_ingress_ifindex");
	ASSERT_EQ(skb.tstamp, 8, "ctx_out_tstamp");
	ASSERT_EQ(skb.mark, 10, "ctx_out_mark");

	bpf_object__close(obj);
}
