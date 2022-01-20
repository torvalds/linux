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

static void on_sample(void *ctx, int cpu, void *data, __u32 size)
{
	struct meta *meta = (struct meta *)data;
	struct ipv4_packet *trace_pkt_v4 = data + sizeof(*meta);

	ASSERT_GE(size, sizeof(pkt_v4) + sizeof(*meta), "check_size");
	ASSERT_EQ(meta->ifindex, if_nametoindex("lo"), "check_meta_ifindex");
	ASSERT_EQ(meta->pkt_len, sizeof(pkt_v4), "check_meta_pkt_len");
	ASSERT_EQ(memcmp(trace_pkt_v4, &pkt_v4, sizeof(pkt_v4)), 0,
		  "check_packet_content");

	*(bool *)ctx = true;
}

void test_xdp_bpf2bpf(void)
{
	__u32 duration = 0, retval, size;
	char buf[128];
	int err, pkt_fd, map_fd;
	bool passed = false;
	struct iphdr iph;
	struct iptnl_info value4 = {.family = AF_INET};
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
	pb = perf_buffer__new(bpf_map__fd(ftrace_skel->maps.perf_buf_map), 1,
			      on_sample, NULL, &passed, NULL);
	if (!ASSERT_OK_PTR(pb, "perf_buf__new"))
		goto out;

	/* Run test program */
	err = bpf_prog_test_run(pkt_fd, 1, &pkt_v4, sizeof(pkt_v4),
				buf, &size, &retval, &duration);
	memcpy(&iph, buf + sizeof(struct ethhdr), sizeof(iph));

	ASSERT_OK(err, "ipv4");
	ASSERT_EQ(retval, XDP_TX, "ipv4 retval");
	ASSERT_EQ(size, 74, "ipv4 size");
	ASSERT_EQ(iph.protocol, IPPROTO_IPIP, "ipv4 proto");

	/* Make sure bpf_xdp_output() was triggered and it sent the expected
	 * data to the perf ring buffer.
	 */
	err = perf_buffer__poll(pb, 100);

	ASSERT_GE(err, 0, "perf_buffer__poll");
	ASSERT_TRUE(passed, "test passed");
	/* Verify test results */
	ASSERT_EQ(ftrace_skel->bss->test_result_fentry, if_nametoindex("lo"),
		  "fentry result");
	ASSERT_EQ(ftrace_skel->bss->test_result_fexit, XDP_TX, "fexit result");
out:
	perf_buffer__free(pb);
	test_xdp__destroy(pkt_skel);
	test_xdp_bpf2bpf__destroy(ftrace_skel);
}
