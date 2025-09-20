// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018 Facebook */

#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/bpf.h>
#include <linux/types.h>
#include <linux/if_ether.h>

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include "test_select_reuseport_common.h"

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} outer_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, NR_RESULTS);
	__type(key, __u32);
	__type(value, __u32);
} result_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, int);
} tmp_index_ovr_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} linum_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct data_check);
} data_check_map SEC(".maps");

#define GOTO_DONE(_result) ({			\
	result = (_result);			\
	linum = __LINE__;			\
	goto done;				\
})

SEC("sk_reuseport")
int _select_by_skb_data(struct sk_reuseport_md *reuse_md)
{
	__u32 linum, index = 0, flags = 0, index_zero = 0;
	__u32 *result_cnt;
	struct data_check data_check = {};
	struct cmd *cmd, cmd_copy;
	void *data, *data_end;
	void *reuseport_array;
	enum result result;
	int *index_ovr;
	int err;

	data = reuse_md->data;
	data_end = reuse_md->data_end;
	data_check.len = reuse_md->len;
	data_check.eth_protocol = reuse_md->eth_protocol;
	data_check.ip_protocol = reuse_md->ip_protocol;
	data_check.hash = reuse_md->hash;
	data_check.bind_inany = reuse_md->bind_inany;
	if (data_check.eth_protocol == bpf_htons(ETH_P_IP)) {
		if (bpf_skb_load_bytes_relative(reuse_md,
						offsetof(struct iphdr, saddr),
						data_check.skb_addrs, 8,
						BPF_HDR_START_NET))
			GOTO_DONE(DROP_MISC);
	} else {
		if (bpf_skb_load_bytes_relative(reuse_md,
						offsetof(struct ipv6hdr, saddr),
						data_check.skb_addrs, 32,
						BPF_HDR_START_NET))
			GOTO_DONE(DROP_MISC);
	}

	/*
	 * The ip_protocol could be a compile time decision
	 * if the bpf_prog.o is dedicated to either TCP or
	 * UDP.
	 *
	 * Otherwise, reuse_md->ip_protocol or
	 * the protocol field in the iphdr can be used.
	 */
	if (data_check.ip_protocol == IPPROTO_TCP) {
		struct tcphdr *th = data;

		if (th + 1 > data_end)
			GOTO_DONE(DROP_MISC);

		data_check.skb_ports[0] = th->source;
		data_check.skb_ports[1] = th->dest;

		if (th->fin)
			/* The connection is being torn down at the end of a
			 * test. It can't contain a cmd, so return early.
			 */
			return SK_PASS;

		if ((th->doff << 2) + sizeof(*cmd) > data_check.len)
			GOTO_DONE(DROP_ERR_SKB_DATA);
		if (bpf_skb_load_bytes(reuse_md, th->doff << 2, &cmd_copy,
				       sizeof(cmd_copy)))
			GOTO_DONE(DROP_MISC);
		cmd = &cmd_copy;
	} else if (data_check.ip_protocol == IPPROTO_UDP) {
		struct udphdr *uh = data;

		if (uh + 1 > data_end)
			GOTO_DONE(DROP_MISC);

		data_check.skb_ports[0] = uh->source;
		data_check.skb_ports[1] = uh->dest;

		if (sizeof(struct udphdr) + sizeof(*cmd) > data_check.len)
			GOTO_DONE(DROP_ERR_SKB_DATA);
		if (data + sizeof(struct udphdr) + sizeof(*cmd) > data_end) {
			if (bpf_skb_load_bytes(reuse_md, sizeof(struct udphdr),
					       &cmd_copy, sizeof(cmd_copy)))
				GOTO_DONE(DROP_MISC);
			cmd = &cmd_copy;
		} else {
			cmd = data + sizeof(struct udphdr);
		}
	} else {
		GOTO_DONE(DROP_MISC);
	}

	reuseport_array = bpf_map_lookup_elem(&outer_map, &index_zero);
	if (!reuseport_array)
		GOTO_DONE(DROP_ERR_INNER_MAP);

	index = cmd->reuseport_index;
	index_ovr = bpf_map_lookup_elem(&tmp_index_ovr_map, &index_zero);
	if (!index_ovr)
		GOTO_DONE(DROP_MISC);

	if (*index_ovr != -1) {
		index = *index_ovr;
		*index_ovr = -1;
	}
	err = bpf_sk_select_reuseport(reuse_md, reuseport_array, &index,
				      flags);
	if (!err)
		GOTO_DONE(PASS);

	if (cmd->pass_on_failure)
		GOTO_DONE(PASS_ERR_SK_SELECT_REUSEPORT);
	else
		GOTO_DONE(DROP_ERR_SK_SELECT_REUSEPORT);

done:
	result_cnt = bpf_map_lookup_elem(&result_map, &result);
	if (!result_cnt)
		return SK_DROP;

	bpf_map_update_elem(&linum_map, &index_zero, &linum, BPF_ANY);
	bpf_map_update_elem(&data_check_map, &index_zero, &data_check, BPF_ANY);

	(*result_cnt)++;
	return result < PASS ? SK_DROP : SK_PASS;
}

char _license[] SEC("license") = "GPL";
