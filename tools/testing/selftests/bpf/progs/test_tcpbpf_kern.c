// SPDX-License-Identifier: GPL-2.0
#include <stddef.h>
#include <string.h>
#include <netinet/in.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/tcp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include "bpf_tcp_helpers.h"
#include "test_tcpbpf.h"

struct tcpbpf_globals global = {};

/**
 * SOL_TCP is defined in <netinet/tcp.h> while
 * TCP_SAVED_SYN is defined in already included <linux/tcp.h>
 */
#ifndef SOL_TCP
#define SOL_TCP 6
#endif

static __always_inline int get_tp_window_clamp(struct bpf_sock_ops *skops)
{
	struct bpf_sock *sk;
	struct tcp_sock *tp;

	sk = skops->sk;
	if (!sk)
		return -1;
	tp = bpf_skc_to_tcp_sock(sk);
	if (!tp)
		return -1;
	return tp->window_clamp;
}

SEC("sockops")
int bpf_testcb(struct bpf_sock_ops *skops)
{
	char header[sizeof(struct ipv6hdr) + sizeof(struct tcphdr)];
	struct bpf_sock_ops *reuse = skops;
	struct tcphdr *thdr;
	int window_clamp = 9216;
	int save_syn = 1;
	int rv = -1;
	int v = 0;
	int op;

	/* Test reading fields in bpf_sock_ops using single register */
	asm volatile (
		"%[reuse] = *(u32 *)(%[reuse] +96)"
		: [reuse] "+r"(reuse)
		:);

	asm volatile (
		"%[op] = *(u32 *)(%[skops] +96)"
		: [op] "=r"(op)
		: [skops] "r"(skops)
		:);

	asm volatile (
		"r9 = %[skops];\n"
		"r8 = *(u32 *)(r9 +164);\n"
		"*(u32 *)(r9 +164) = r8;\n"
		:: [skops] "r"(skops)
		: "r9", "r8");

	asm volatile (
		"r1 = %[skops];\n"
		"r1 = *(u64 *)(r1 +184);\n"
		"if r1 == 0 goto +1;\n"
		"r1 = *(u32 *)(r1 +4);\n"
		:: [skops] "r"(skops):"r1");

	asm volatile (
		"r9 = %[skops];\n"
		"r9 = *(u64 *)(r9 +184);\n"
		"if r9 == 0 goto +1;\n"
		"r9 = *(u32 *)(r9 +4);\n"
		:: [skops] "r"(skops):"r9");

	asm volatile (
		"r1 = %[skops];\n"
		"r2 = *(u64 *)(r1 +184);\n"
		"if r2 == 0 goto +1;\n"
		"r2 = *(u32 *)(r2 +4);\n"
		:: [skops] "r"(skops):"r1", "r2");

	op = (int) skops->op;

	global.event_map |= (1 << op);

	switch (op) {
	case BPF_SOCK_OPS_TCP_CONNECT_CB:
		rv = bpf_setsockopt(skops, SOL_TCP, TCP_WINDOW_CLAMP,
				    &window_clamp, sizeof(window_clamp));
		global.window_clamp_client = get_tp_window_clamp(skops);
		break;
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
		/* Test failure to set largest cb flag (assumes not defined) */
		global.bad_cb_test_rv = bpf_sock_ops_cb_flags_set(skops, 0x80);
		/* Set callback */
		global.good_cb_test_rv = bpf_sock_ops_cb_flags_set(skops,
						 BPF_SOCK_OPS_STATE_CB_FLAG);
		break;
	case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
		skops->sk_txhash = 0x12345f;
		v = 0xff;
		rv = bpf_setsockopt(skops, SOL_IPV6, IPV6_TCLASS, &v,
				    sizeof(v));
		if (skops->family == AF_INET6) {
			v = bpf_getsockopt(skops, IPPROTO_TCP, TCP_SAVED_SYN,
					   header, (sizeof(struct ipv6hdr) +
						    sizeof(struct tcphdr)));
			if (!v) {
				int offset = sizeof(struct ipv6hdr);

				thdr = (struct tcphdr *)(header + offset);
				v = thdr->syn;

				global.tcp_saved_syn = v;
			}
		}
		rv = bpf_setsockopt(skops, SOL_TCP, TCP_WINDOW_CLAMP,
				    &window_clamp, sizeof(window_clamp));

		global.window_clamp_server = get_tp_window_clamp(skops);
		break;
	case BPF_SOCK_OPS_RTO_CB:
		break;
	case BPF_SOCK_OPS_RETRANS_CB:
		break;
	case BPF_SOCK_OPS_STATE_CB:
		if (skops->args[1] == BPF_TCP_CLOSE) {
			if (skops->args[0] == BPF_TCP_LISTEN) {
				global.num_listen++;
			} else {
				global.total_retrans = skops->total_retrans;
				global.data_segs_in = skops->data_segs_in;
				global.data_segs_out = skops->data_segs_out;
				global.bytes_received = skops->bytes_received;
				global.bytes_acked = skops->bytes_acked;
			}
			global.num_close_events++;
		}
		break;
	case BPF_SOCK_OPS_TCP_LISTEN_CB:
		bpf_sock_ops_cb_flags_set(skops, BPF_SOCK_OPS_STATE_CB_FLAG);
		v = bpf_setsockopt(skops, IPPROTO_TCP, TCP_SAVE_SYN,
				   &save_syn, sizeof(save_syn));
		/* Update global map w/ result of setsock opt */
		global.tcp_save_syn = v;
		break;
	default:
		rv = -1;
	}
	skops->reply = rv;
	return 1;
}
char _license[] SEC("license") = "GPL";
