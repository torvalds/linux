// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
int verdict_max_size = 10000;
struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 20);
	__type(key, int);
	__type(value, int);
} sock_map SEC(".maps");

SEC("sk_skb/stream_verdict")
int prog_skb_verdict(struct __sk_buff *skb)
{
	__u32 one = 1;

	if (skb->len > verdict_max_size)
		return SK_PASS;

	return bpf_sk_redirect_map(skb, &sock_map, one, 0);
}

SEC("sk_skb/stream_verdict")
int prog_skb_verdict_pass(struct __sk_buff *skb)
{
	return SK_PASS;
}

SEC("sk_skb/stream_parser")
int prog_skb_parser(struct __sk_buff *skb)
{
	return skb->len;
}

SEC("sk_skb/stream_parser")
int prog_skb_parser_partial(struct __sk_buff *skb)
{
	/* agreement with the test program on a 4-byte size header
	 * and 6-byte body.
	 */
	if (skb->len < 4) {
		/* need more header to determine full length */
		return 0;
	}
	/* return full length decoded from header.
	 * the return value may be larger than skb->len which
	 * means framework must wait body coming.
	 */
	return 10;
}

char _license[] SEC("license") = "GPL";
