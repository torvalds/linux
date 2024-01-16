// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include "test_xdp_context_test_run.skel.h"

void test_xdp_context_error(int prog_fd, struct bpf_test_run_opts opts,
			    __u32 data_meta, __u32 data, __u32 data_end,
			    __u32 ingress_ifindex, __u32 rx_queue_index,
			    __u32 egress_ifindex)
{
	struct xdp_md ctx = {
		.data = data,
		.data_end = data_end,
		.data_meta = data_meta,
		.ingress_ifindex = ingress_ifindex,
		.rx_queue_index = rx_queue_index,
		.egress_ifindex = egress_ifindex,
	};
	int err;

	opts.ctx_in = &ctx;
	opts.ctx_size_in = sizeof(ctx);
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_EQ(errno, EINVAL, "errno-EINVAL");
	ASSERT_ERR(err, "bpf_prog_test_run");
}

void test_xdp_context_test_run(void)
{
	struct test_xdp_context_test_run *skel = NULL;
	char data[sizeof(pkt_v4) + sizeof(__u32)];
	char bad_ctx[sizeof(struct xdp_md) + 1];
	struct xdp_md ctx_in, ctx_out;
	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, opts,
			    .data_in = &data,
			    .data_size_in = sizeof(data),
			    .ctx_out = &ctx_out,
			    .ctx_size_out = sizeof(ctx_out),
			    .repeat = 1,
		);
	int err, prog_fd;

	skel = test_xdp_context_test_run__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		return;
	prog_fd = bpf_program__fd(skel->progs.xdp_context);

	/* Data past the end of the kernel's struct xdp_md must be 0 */
	bad_ctx[sizeof(bad_ctx) - 1] = 1;
	opts.ctx_in = bad_ctx;
	opts.ctx_size_in = sizeof(bad_ctx);
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_EQ(errno, E2BIG, "extradata-errno");
	ASSERT_ERR(err, "bpf_prog_test_run(extradata)");

	*(__u32 *)data = XDP_PASS;
	*(struct ipv4_packet *)(data + sizeof(__u32)) = pkt_v4;
	opts.ctx_in = &ctx_in;
	opts.ctx_size_in = sizeof(ctx_in);
	memset(&ctx_in, 0, sizeof(ctx_in));
	ctx_in.data_meta = 0;
	ctx_in.data = sizeof(__u32);
	ctx_in.data_end = ctx_in.data + sizeof(pkt_v4);
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(err, "bpf_prog_test_run(valid)");
	ASSERT_EQ(opts.retval, XDP_PASS, "valid-retval");
	ASSERT_EQ(opts.data_size_out, sizeof(pkt_v4), "valid-datasize");
	ASSERT_EQ(opts.ctx_size_out, opts.ctx_size_in, "valid-ctxsize");
	ASSERT_EQ(ctx_out.data_meta, 0, "valid-datameta");
	ASSERT_EQ(ctx_out.data, 0, "valid-data");
	ASSERT_EQ(ctx_out.data_end, sizeof(pkt_v4), "valid-dataend");

	/* Meta data's size must be a multiple of 4 */
	test_xdp_context_error(prog_fd, opts, 0, 1, sizeof(data), 0, 0, 0);

	/* data_meta must reference the start of data */
	test_xdp_context_error(prog_fd, opts, 4, sizeof(__u32), sizeof(data),
			       0, 0, 0);

	/* Meta data must be 32 bytes or smaller */
	test_xdp_context_error(prog_fd, opts, 0, 36, sizeof(data), 0, 0, 0);

	/* Total size of data must match data_end - data_meta */
	test_xdp_context_error(prog_fd, opts, 0, sizeof(__u32),
			       sizeof(data) - 1, 0, 0, 0);
	test_xdp_context_error(prog_fd, opts, 0, sizeof(__u32),
			       sizeof(data) + 1, 0, 0, 0);

	/* RX queue cannot be specified without specifying an ingress */
	test_xdp_context_error(prog_fd, opts, 0, sizeof(__u32), sizeof(data),
			       0, 1, 0);

	/* Interface 1 is always the loopback interface which always has only
	 * one RX queue (index 0). This makes index 1 an invalid rx queue index
	 * for interface 1.
	 */
	test_xdp_context_error(prog_fd, opts, 0, sizeof(__u32), sizeof(data),
			       1, 1, 0);

	/* The egress cannot be specified */
	test_xdp_context_error(prog_fd, opts, 0, sizeof(__u32), sizeof(data),
			       0, 0, 1);

	test_xdp_context_test_run__destroy(skel);
}
