// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

struct meta {
	int ifindex;
	__u32 cb32_0;
	__u8 cb8_0;
};

static union {
	__u32 cb32[5];
	__u8 cb8[20];
} cb = {
	.cb32[0] = 0x81828384,
};

static void on_sample(void *ctx, int cpu, void *data, __u32 size)
{
	struct meta *meta = (struct meta *)data;
	struct ipv6_packet *pkt_v6 = data + sizeof(*meta);
	int duration = 0;

	if (CHECK(size != 72 + sizeof(*meta), "check_size", "size %u != %zu\n",
		  size, 72 + sizeof(*meta)))
		return;
	if (CHECK(meta->ifindex != 1, "check_meta_ifindex",
		  "meta->ifindex = %d\n", meta->ifindex))
		/* spurious kfree_skb not on loopback device */
		return;
	if (CHECK(meta->cb8_0 != cb.cb8[0], "check_cb8_0", "cb8_0 %x != %x\n",
		  meta->cb8_0, cb.cb8[0]))
		return;
	if (CHECK(meta->cb32_0 != cb.cb32[0], "check_cb32_0",
		  "cb32_0 %x != %x\n",
		  meta->cb32_0, cb.cb32[0]))
		return;
	if (CHECK(pkt_v6->eth.h_proto != 0xdd86, "check_eth",
		  "h_proto %x\n", pkt_v6->eth.h_proto))
		return;
	if (CHECK(pkt_v6->iph.nexthdr != 6, "check_ip",
		  "iph.nexthdr %x\n", pkt_v6->iph.nexthdr))
		return;
	if (CHECK(pkt_v6->tcp.doff != 5, "check_tcp",
		  "tcp.doff %x\n", pkt_v6->tcp.doff))
		return;

	*(bool *)ctx = true;
}

void test_kfree_skb(void)
{
	struct __sk_buff skb = {};
	struct bpf_prog_test_run_attr tattr = {
		.data_in = &pkt_v6,
		.data_size_in = sizeof(pkt_v6),
		.ctx_in = &skb,
		.ctx_size_in = sizeof(skb),
	};
	struct bpf_prog_load_attr attr = {
		.file = "./kfree_skb.o",
	};

	struct bpf_link *link = NULL, *link_fentry = NULL, *link_fexit = NULL;
	struct bpf_map *perf_buf_map, *global_data;
	struct bpf_program *prog, *fentry, *fexit;
	struct bpf_object *obj, *obj2 = NULL;
	struct perf_buffer_opts pb_opts = {};
	struct perf_buffer *pb = NULL;
	int err, kfree_skb_fd;
	bool passed = false;
	__u32 duration = 0;
	const int zero = 0;
	bool test_ok[2];

	err = bpf_prog_load("./test_pkt_access.o", BPF_PROG_TYPE_SCHED_CLS,
			    &obj, &tattr.prog_fd);
	if (CHECK(err, "prog_load sched cls", "err %d errno %d\n", err, errno))
		return;

	err = bpf_prog_load_xattr(&attr, &obj2, &kfree_skb_fd);
	if (CHECK(err, "prog_load raw tp", "err %d errno %d\n", err, errno))
		goto close_prog;

	prog = bpf_object__find_program_by_title(obj2, "tp_btf/kfree_skb");
	if (CHECK(!prog, "find_prog", "prog kfree_skb not found\n"))
		goto close_prog;
	fentry = bpf_object__find_program_by_title(obj2, "fentry/eth_type_trans");
	if (CHECK(!fentry, "find_prog", "prog eth_type_trans not found\n"))
		goto close_prog;
	fexit = bpf_object__find_program_by_title(obj2, "fexit/eth_type_trans");
	if (CHECK(!fexit, "find_prog", "prog eth_type_trans not found\n"))
		goto close_prog;

	global_data = bpf_object__find_map_by_name(obj2, "kfree_sk.bss");
	if (CHECK(!global_data, "find global data", "not found\n"))
		goto close_prog;

	link = bpf_program__attach_raw_tracepoint(prog, NULL);
	if (CHECK(IS_ERR(link), "attach_raw_tp", "err %ld\n", PTR_ERR(link)))
		goto close_prog;
	link_fentry = bpf_program__attach_trace(fentry);
	if (CHECK(IS_ERR(link_fentry), "attach fentry", "err %ld\n",
		  PTR_ERR(link_fentry)))
		goto close_prog;
	link_fexit = bpf_program__attach_trace(fexit);
	if (CHECK(IS_ERR(link_fexit), "attach fexit", "err %ld\n",
		  PTR_ERR(link_fexit)))
		goto close_prog;

	perf_buf_map = bpf_object__find_map_by_name(obj2, "perf_buf_map");
	if (CHECK(!perf_buf_map, "find_perf_buf_map", "not found\n"))
		goto close_prog;

	/* set up perf buffer */
	pb_opts.sample_cb = on_sample;
	pb_opts.ctx = &passed;
	pb = perf_buffer__new(bpf_map__fd(perf_buf_map), 1, &pb_opts);
	if (CHECK(IS_ERR(pb), "perf_buf__new", "err %ld\n", PTR_ERR(pb)))
		goto close_prog;

	memcpy(skb.cb, &cb, sizeof(cb));
	err = bpf_prog_test_run_xattr(&tattr);
	duration = tattr.duration;
	CHECK(err || tattr.retval, "ipv6",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, tattr.retval, duration);

	/* read perf buffer */
	err = perf_buffer__poll(pb, 100);
	if (CHECK(err < 0, "perf_buffer__poll", "err %d\n", err))
		goto close_prog;

	/* make sure kfree_skb program was triggered
	 * and it sent expected skb into ring buffer
	 */
	CHECK_FAIL(!passed);

	err = bpf_map_lookup_elem(bpf_map__fd(global_data), &zero, test_ok);
	if (CHECK(err, "get_result",
		  "failed to get output data: %d\n", err))
		goto close_prog;

	CHECK_FAIL(!test_ok[0] || !test_ok[1]);
close_prog:
	perf_buffer__free(pb);
	if (!IS_ERR_OR_NULL(link))
		bpf_link__destroy(link);
	if (!IS_ERR_OR_NULL(link_fentry))
		bpf_link__destroy(link_fentry);
	if (!IS_ERR_OR_NULL(link_fexit))
		bpf_link__destroy(link_fexit);
	bpf_object__close(obj);
	bpf_object__close(obj2);
}
