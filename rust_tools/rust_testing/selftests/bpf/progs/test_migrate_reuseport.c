// SPDX-License-Identifier: GPL-2.0
/*
 * Check if we can migrate child sockets.
 *
 *   1. If reuse_md->migrating_sk is NULL (SYN packet),
 *        return SK_PASS without selecting a listener.
 *   2. If reuse_md->migrating_sk is not NULL (socket migration),
 *        select a listener (reuseport_map[migrate_map[cookie]])
 *
 * Author: Kuniyuki Iwashima <kuniyu@amazon.co.jp>
 */

#include <stddef.h>
#include <string.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_REUSEPORT_SOCKARRAY);
	__uint(max_entries, 256);
	__type(key, int);
	__type(value, __u64);
} reuseport_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 256);
	__type(key, __u64);
	__type(value, int);
} migrate_map SEC(".maps");

int migrated_at_close = 0;
int migrated_at_close_fastopen = 0;
int migrated_at_send_synack = 0;
int migrated_at_recv_ack = 0;
__be16 server_port;

SEC("xdp")
int drop_ack(struct xdp_md *xdp)
{
	void *data_end = (void *)(long)xdp->data_end;
	void *data = (void *)(long)xdp->data;
	struct ethhdr *eth = data;
	struct tcphdr *tcp = NULL;

	if (eth + 1 > data_end)
		goto pass;

	switch (bpf_ntohs(eth->h_proto)) {
	case ETH_P_IP: {
		struct iphdr *ip = (struct iphdr *)(eth + 1);

		if (ip + 1 > data_end)
			goto pass;

		if (ip->protocol != IPPROTO_TCP)
			goto pass;

		tcp = (struct tcphdr *)((void *)ip + ip->ihl * 4);
		break;
	}
	case ETH_P_IPV6: {
		struct ipv6hdr *ipv6 = (struct ipv6hdr *)(eth + 1);

		if (ipv6 + 1 > data_end)
			goto pass;

		if (ipv6->nexthdr != IPPROTO_TCP)
			goto pass;

		tcp = (struct tcphdr *)(ipv6 + 1);
		break;
	}
	default:
		goto pass;
	}

	if (tcp + 1 > data_end)
		goto pass;

	if (tcp->dest != server_port)
		goto pass;

	if (!tcp->syn && tcp->ack)
		return XDP_DROP;

pass:
	return XDP_PASS;
}

SEC("sk_reuseport/migrate")
int migrate_reuseport(struct sk_reuseport_md *reuse_md)
{
	int *key, flags = 0, state, err;
	__u64 cookie;

	if (!reuse_md->migrating_sk)
		return SK_PASS;

	state = reuse_md->migrating_sk->state;
	cookie = bpf_get_socket_cookie(reuse_md->sk);

	key = bpf_map_lookup_elem(&migrate_map, &cookie);
	if (!key)
		return SK_DROP;

	err = bpf_sk_select_reuseport(reuse_md, &reuseport_map, key, flags);
	if (err)
		return SK_PASS;

	switch (state) {
	case BPF_TCP_ESTABLISHED:
		__sync_fetch_and_add(&migrated_at_close, 1);
		break;
	case BPF_TCP_SYN_RECV:
		__sync_fetch_and_add(&migrated_at_close_fastopen, 1);
		break;
	case BPF_TCP_NEW_SYN_RECV:
		if (!reuse_md->len)
			__sync_fetch_and_add(&migrated_at_send_synack, 1);
		else
			__sync_fetch_and_add(&migrated_at_recv_ack, 1);
		break;
	}

	return SK_PASS;
}

char _license[] SEC("license") = "GPL";
