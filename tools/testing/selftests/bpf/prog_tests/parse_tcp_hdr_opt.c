// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <test_progs.h>
#include <network_helpers.h>
#include "test_parse_tcp_hdr_opt.skel.h"
#include "test_parse_tcp_hdr_opt_dynptr.skel.h"
#include "test_tcp_hdr_options.h"

struct test_pkt {
	struct ipv6_packet pk6_v6;
	u8 options[16];
} __packed;

struct test_pkt pkt = {
	.pk6_v6.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
	.pk6_v6.iph.nexthdr = IPPROTO_TCP,
	.pk6_v6.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
	.pk6_v6.tcp.urg_ptr = 123,
	.pk6_v6.tcp.doff = 9, /* 16 bytes of options */

	.options = {
		TCPOPT_MSS, 4, 0x05, 0xB4, TCPOPT_NOP, TCPOPT_NOP,
		0, 6, 0xBB, 0xBB, 0xBB, 0xBB, TCPOPT_EOL
	},
};

static void test_parse_opt(void)
{
	struct test_parse_tcp_hdr_opt *skel;
	struct bpf_program *prog;
	char buf[128];
	int err;

	LIBBPF_OPTS(bpf_test_run_opts, topts,
		    .data_in = &pkt,
		    .data_size_in = sizeof(pkt),
		    .data_out = buf,
		    .data_size_out = sizeof(buf),
		    .repeat = 3,
	);

	skel = test_parse_tcp_hdr_opt__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	pkt.options[6] = skel->rodata->tcp_hdr_opt_kind_tpr;
	prog = skel->progs.xdp_ingress_v6;

	err = bpf_prog_test_run_opts(bpf_program__fd(prog), &topts);
	ASSERT_OK(err, "ipv6 test_run");
	ASSERT_EQ(topts.retval, XDP_PASS, "ipv6 test_run retval");
	ASSERT_EQ(skel->bss->server_id, 0xBBBBBBBB, "server id");

	test_parse_tcp_hdr_opt__destroy(skel);
}

static void test_parse_opt_dynptr(void)
{
	struct test_parse_tcp_hdr_opt_dynptr *skel;
	struct bpf_program *prog;
	char buf[128];
	int err;

	LIBBPF_OPTS(bpf_test_run_opts, topts,
		    .data_in = &pkt,
		    .data_size_in = sizeof(pkt),
		    .data_out = buf,
		    .data_size_out = sizeof(buf),
		    .repeat = 3,
	);

	skel = test_parse_tcp_hdr_opt_dynptr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	pkt.options[6] = skel->rodata->tcp_hdr_opt_kind_tpr;
	prog = skel->progs.xdp_ingress_v6;

	err = bpf_prog_test_run_opts(bpf_program__fd(prog), &topts);
	ASSERT_OK(err, "ipv6 test_run");
	ASSERT_EQ(topts.retval, XDP_PASS, "ipv6 test_run retval");
	ASSERT_EQ(skel->bss->server_id, 0xBBBBBBBB, "server id");

	test_parse_tcp_hdr_opt_dynptr__destroy(skel);
}

void test_parse_tcp_hdr_opt(void)
{
	if (test__start_subtest("parse_tcp_hdr_opt"))
		test_parse_opt();
	if (test__start_subtest("parse_tcp_hdr_opt_dynptr"))
		test_parse_opt_dynptr();
}
