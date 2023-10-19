// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include <net/if.h>
#include "test_xdp.skel.h"
#include "test_xdp_bpf2bpf.skel.h"

struct meta {
	int ifindex;
	int pkt_len;
};

struct test_ctx_s {
	bool passed;
	int pkt_size;
};

struct test_ctx_s test_ctx;

static void on_sample(void *ctx, int cpu, void *data, __u32 size)
{
	struct meta *meta = (struct meta *)data;
	struct ipv4_packet *trace_pkt_v4 = data + sizeof(*meta);
	unsigned char *raw_pkt = data + sizeof(*meta);
	struct test_ctx_s *tst_ctx = ctx;

	ASSERT_GE(size, sizeof(pkt_v4) + sizeof(*meta), "check_size");
	ASSERT_EQ(meta->ifindex, if_nametoindex("lo"), "check_meta_ifindex");
	ASSERT_EQ(meta->pkt_len, tst_ctx->pkt_size, "check_meta_pkt_len");
	ASSERT_EQ(memcmp(trace_pkt_v4, &pkt_v4, sizeof(pkt_v4)), 0,
		  "check_packet_content");

	if (meta->pkt_len > sizeof(pkt_v4)) {
		for (int i = 0; i < meta->pkt_len - sizeof(pkt_v4); i++)
			ASSERT_EQ(raw_pkt[i + sizeof(pkt_v4)], (unsigned char)i,
				  "check_packet_content");
	}

	tst_ctx->passed = true;
}

#define BUF_SZ	9000

static void run_xdp_bpf2bpf_pkt_size(int pkt_fd, struct perf_buffer *pb,
				     struct test_xdp_bpf2bpf *ftrace_skel,
				     int pkt_size)
{
	__u8 *buf, *buf_in;
	int err;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	if (!ASSERT_LE(pkt_size, BUF_SZ, "pkt_size") ||
	    !ASSERT_GE(pkt_size, sizeof(pkt_v4), "pkt_size"))
		return;

	buf_in = malloc(BUF_SZ);
	if (!ASSERT_OK_PTR(buf_in, "buf_in malloc()"))
		return;

	buf = malloc(BUF_SZ);
	if (!ASSERT_OK_PTR(buf, "buf malloc()")) {
		free(buf_in);
		return;
	}

	test_ctx.passed = false;
	test_ctx.pkt_size = pkt_size;

	memcpy(buf_in, &pkt_v4, sizeof(pkt_v4));
	if (pkt_size > sizeof(pkt_v4)) {
		for (int i = 0; i < (pkt_size - sizeof(pkt_v4)); i++)
			buf_in[i + sizeof(pkt_v4)] = i;
	}

	/* Run test program */
	topts.data_in = buf_in;
	topts.data_size_in = pkt_size;
	topts.data_out = buf;
	topts.data_size_out = BUF_SZ;

	err = bpf_prog_test_run_opts(pkt_fd, &topts);

	ASSERT_OK(err, "ipv4");
	ASSERT_EQ(topts.retval, XDP_PASS, "ipv4 retval");
	ASSERT_EQ(topts.data_size_out, pkt_size, "ipv4 size");

	/* Make sure bpf_xdp_output() was triggered and it sent the expected
	 * data to the perf ring buffer.
	 */
	err = perf_buffer__poll(pb, 100);

	ASSERT_GE(err, 0, "perf_buffer__poll");
	ASSERT_TRUE(test_ctx.passed, "test passed");
	/* Verify test results */
	ASSERT_EQ(ftrace_skel->bss->test_result_fentry, if_nametoindex("lo"),
		  "fentry result");
	ASSERT_EQ(ftrace_skel->bss->test_result_fexit, XDP_PASS, "fexit result");

	free(buf);
	free(buf_in);
}

void test_xdp_bpf2bpf(void)
{
	int err, pkt_fd, map_fd;
	int pkt_sizes[] = {sizeof(pkt_v4), 1024, 4100, 8200};
	struct iptnl_info value4 = {.family = AF_INET6};
	struct test_xdp *pkt_skel = NULL;
	struct test_xdp_bpf2bpf *ftrace_skel = NULL;
	struct vip key4 = {.protocol = 6, .family = AF_INET};
	struct bpf_program *prog;
	struct perf_buffer *pb = NULL;

	/* Load XDP program to introspect */
	pkt_skel = test_xdp__open_and_load();
	if (!ASSERT_OK_PTR(pkt_skel, "test_xdp__open_and_load"))
		return;

	pkt_fd = bpf_program__fd(pkt_skel->progs._xdp_tx_iptunnel);

	map_fd = bpf_map__fd(pkt_skel->maps.vip2tnl);
	bpf_map_update_elem(map_fd, &key4, &value4, 0);

	/* Load trace program */
	ftrace_skel = test_xdp_bpf2bpf__open();
	if (!ASSERT_OK_PTR(ftrace_skel, "test_xdp_bpf2bpf__open"))
		goto out;

	/* Demonstrate the bpf_program__set_attach_target() API rather than
	 * the load with options, i.e. opts.attach_prog_fd.
	 */
	prog = ftrace_skel->progs.trace_on_entry;
	bpf_program__set_expected_attach_type(prog, BPF_TRACE_FENTRY);
	bpf_program__set_attach_target(prog, pkt_fd, "_xdp_tx_iptunnel");

	prog = ftrace_skel->progs.trace_on_exit;
	bpf_program__set_expected_attach_type(prog, BPF_TRACE_FEXIT);
	bpf_program__set_attach_target(prog, pkt_fd, "_xdp_tx_iptunnel");

	err = test_xdp_bpf2bpf__load(ftrace_skel);
	if (!ASSERT_OK(err, "test_xdp_bpf2bpf__load"))
		goto out;

	err = test_xdp_bpf2bpf__attach(ftrace_skel);
	if (!ASSERT_OK(err, "test_xdp_bpf2bpf__attach"))
		goto out;

	/* Set up perf buffer */
	pb = perf_buffer__new(bpf_map__fd(ftrace_skel->maps.perf_buf_map), 8,
			      on_sample, NULL, &test_ctx, NULL);
	if (!ASSERT_OK_PTR(pb, "perf_buf__new"))
		goto out;

	for (int i = 0; i < ARRAY_SIZE(pkt_sizes); i++)
		run_xdp_bpf2bpf_pkt_size(pkt_fd, pb, ftrace_skel,
					 pkt_sizes[i]);
out:
	perf_buffer__free(pb);
	test_xdp__destroy(pkt_skel);
	test_xdp_bpf2bpf__destroy(ftrace_skel);
}
