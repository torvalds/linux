// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

long process_byte = 0;
int  verdict_dir = 0;
int  dropped = 0;
int  pkt_size = 0;
struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 20);
	__type(key, int);
	__type(value, int);
} sock_map_rx SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 20);
	__type(key, int);
	__type(value, int);
} sock_map_tx SEC(".maps");

SEC("sk_skb/stream_parser")
int prog_skb_parser(struct __sk_buff *skb)
{
	return pkt_size;
}

SEC("sk_skb/stream_verdict")
int prog_skb_verdict(struct __sk_buff *skb)
{
	int one = 1;
	int ret =  bpf_sk_redirect_map(skb, &sock_map_rx, one, verdict_dir);

	if (ret == SK_DROP)
		dropped++;
	__sync_fetch_and_add(&process_byte, skb->len);
	return ret;
}

SEC("sk_skb/stream_verdict")
int prog_skb_pass(struct __sk_buff *skb)
{
	__sync_fetch_and_add(&process_byte, skb->len);
	return SK_PASS;
}

SEC("sk_msg")
int prog_skmsg_verdict(struct sk_msg_md *msg)
{
	int one = 1;

	__sync_fetch_and_add(&process_byte, msg->size);
	return bpf_msg_redirect_map(msg, &sock_map_tx, one, verdict_dir);
}

SEC("sk_msg")
int prog_skmsg_pass(struct sk_msg_md *msg)
{
	__sync_fetch_and_add(&process_byte, msg->size);
	return SK_PASS;
}

char _license[] SEC("license") = "GPL";
