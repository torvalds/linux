// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

static void on_sample(void *ctx, int cpu, void *data, __u32 size)
{
	int ifindex = *(int *)data, duration = 0;
	struct ipv6_packet *pkt_v6 = data + 4;

	if (ifindex != 1)
		/* spurious kfree_skb not on loopback device */
		return;
	if (CHECK(size != 76, "check_size", "size %u != 76\n", size))
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
	struct bpf_prog_load_attr attr = {
		.file = "./kfree_skb.o",
	};

	struct bpf_object *obj, *obj2 = NULL;
	struct perf_buffer_opts pb_opts = {};
	struct perf_buffer *pb = NULL;
	struct bpf_link *link = NULL;
	struct bpf_map *perf_buf_map;
	struct bpf_program *prog;
	__u32 duration, retval;
	int err, pkt_fd, kfree_skb_fd;
	bool passed = false;

	err = bpf_prog_load("./test_pkt_access.o", BPF_PROG_TYPE_SCHED_CLS, &obj, &pkt_fd);
	if (CHECK(err, "prog_load sched cls", "err %d errno %d\n", err, errno))
		return;

	err = bpf_prog_load_xattr(&attr, &obj2, &kfree_skb_fd);
	if (CHECK(err, "prog_load raw tp", "err %d errno %d\n", err, errno))
		goto close_prog;

	prog = bpf_object__find_program_by_title(obj2, "tp_btf/kfree_skb");
	if (CHECK(!prog, "find_prog", "prog kfree_skb not found\n"))
		goto close_prog;
	link = bpf_program__attach_raw_tracepoint(prog, NULL);
	if (CHECK(IS_ERR(link), "attach_raw_tp", "err %ld\n", PTR_ERR(link)))
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

	err = bpf_prog_test_run(pkt_fd, 1, &pkt_v6, sizeof(pkt_v6),
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "ipv6",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	/* read perf buffer */
	err = perf_buffer__poll(pb, 100);
	if (CHECK(err < 0, "perf_buffer__poll", "err %d\n", err))
		goto close_prog;
	/* make sure kfree_skb program was triggered
	 * and it sent expected skb into ring buffer
	 */
	CHECK_FAIL(!passed);
close_prog:
	perf_buffer__free(pb);
	if (!IS_ERR_OR_NULL(link))
		bpf_link__destroy(link);
	bpf_object__close(obj);
	bpf_object__close(obj2);
}
