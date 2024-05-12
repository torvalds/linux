// SPDX-License-Identifier: GPL-2.0

/* This parsing logic is taken from the open source library katran, a layer 4
 * load balancer.
 *
 * This code logic using dynptrs can be found in test_parse_tcp_hdr_opt_dynptr.c
 *
 * https://github.com/facebookincubator/katran/blob/main/katran/lib/bpf/pckt_parsing.h
 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/tcp.h>
#include <stdbool.h>
#include <linux/ipv6.h>
#include <linux/if_ether.h>
#include "test_tcp_hdr_options.h"

char _license[] SEC("license") = "GPL";

/* Kind number used for experiments */
const __u32 tcp_hdr_opt_kind_tpr = 0xFD;
/* Length of the tcp header option */
const __u32 tcp_hdr_opt_len_tpr = 6;
/* maximum number of header options to check to lookup server_id */
const __u32 tcp_hdr_opt_max_opt_checks = 15;

__u32 server_id;

struct hdr_opt_state {
	__u32 server_id;
	__u8 byte_offset;
	__u8 hdr_bytes_remaining;
};

static int parse_hdr_opt(const struct xdp_md *xdp, struct hdr_opt_state *state)
{
	const void *data = (void *)(long)xdp->data;
	const void *data_end = (void *)(long)xdp->data_end;
	__u8 *tcp_opt, kind, hdr_len;

	tcp_opt = (__u8 *)(data + state->byte_offset);
	if (tcp_opt + 1 > data_end)
		return -1;

	kind = tcp_opt[0];

	if (kind == TCPOPT_EOL)
		return -1;

	if (kind == TCPOPT_NOP) {
		state->hdr_bytes_remaining--;
		state->byte_offset++;
		return 0;
	}

	if (state->hdr_bytes_remaining < 2 ||
	    tcp_opt + sizeof(__u8) + sizeof(__u8) > data_end)
		return -1;

	hdr_len = tcp_opt[1];
	if (hdr_len > state->hdr_bytes_remaining)
		return -1;

	if (kind == tcp_hdr_opt_kind_tpr) {
		if (hdr_len != tcp_hdr_opt_len_tpr)
			return -1;

		if (tcp_opt + tcp_hdr_opt_len_tpr > data_end)
			return -1;

		state->server_id = *(__u32 *)&tcp_opt[2];
		return 1;
	}

	state->hdr_bytes_remaining -= hdr_len;
	state->byte_offset += hdr_len;
	return 0;
}

SEC("xdp")
int xdp_ingress_v6(struct xdp_md *xdp)
{
	const void *data = (void *)(long)xdp->data;
	const void *data_end = (void *)(long)xdp->data_end;
	struct hdr_opt_state opt_state = {};
	__u8 tcp_hdr_opt_len = 0;
	struct tcphdr *tcp_hdr;
	__u64 tcp_offset = 0;
	int err;

	tcp_offset = sizeof(struct ethhdr) + sizeof(struct ipv6hdr);
	tcp_hdr = (struct tcphdr *)(data + tcp_offset);
	if (tcp_hdr + 1 > data_end)
		return XDP_DROP;

	tcp_hdr_opt_len = (tcp_hdr->doff * 4) - sizeof(struct tcphdr);
	if (tcp_hdr_opt_len < tcp_hdr_opt_len_tpr)
		return XDP_DROP;

	opt_state.hdr_bytes_remaining = tcp_hdr_opt_len;
	opt_state.byte_offset = sizeof(struct tcphdr) + tcp_offset;

	/* max number of bytes of options in tcp header is 40 bytes */
	for (int i = 0; i < tcp_hdr_opt_max_opt_checks; i++) {
		err = parse_hdr_opt(xdp, &opt_state);

		if (err || !opt_state.hdr_bytes_remaining)
			break;
	}

	if (!opt_state.server_id)
		return XDP_DROP;

	server_id = opt_state.server_id;

	return XDP_PASS;
}
