#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

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

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 20);
	__type(key, int);
	__type(value, int);
} sock_map_msg SEC(".maps");

SEC("sk_skb/stream_verdict")
int prog_skb_verdict(struct __sk_buff *skb)
{
	return SK_PASS;
}

int clone_called;

SEC("sk_skb/stream_verdict")
int prog_skb_verdict_clone(struct __sk_buff *skb)
{
	clone_called = 1;
	return SK_PASS;
}

SEC("sk_skb/stream_parser")
int prog_skb_parser(struct __sk_buff *skb)
{
	return SK_PASS;
}

SEC("sk_skb/stream_verdict")
int prog_skb_verdict_ingress(struct __sk_buff *skb)
{
	int one = 1;

	return bpf_sk_redirect_map(skb, &sock_map_rx, one, BPF_F_INGRESS);
}

SEC("sk_skb/stream_parser")
int prog_skb_verdict_ingress_strp(struct __sk_buff *skb)
{
	return skb->len;
}

char _license[] SEC("license") = "GPL";
