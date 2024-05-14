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

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 20);
	__type(key, int);
	__type(value, int);
} sock_map_break SEC(".maps");

SEC("sk_skb2")
int bpf_prog2(struct __sk_buff *skb)
{
	void *data_end = (void *)(long) skb->data_end;
	void *data = (void *)(long) skb->data;
	__u32 lport = skb->local_port;
	__u32 rport = skb->remote_port;
	__u8 *d = data;
	__u8 sk, map;

	if (data + 8 > data_end)
		return SK_DROP;

	map = d[0];
	sk = d[1];

	d[0] = 0xd;
	d[1] = 0xe;
	d[2] = 0xa;
	d[3] = 0xd;
	d[4] = 0xb;
	d[5] = 0xe;
	d[6] = 0xe;
	d[7] = 0xf;

	if (!map)
		return bpf_sk_redirect_map(skb, &sock_map_rx, sk, 0);
	return bpf_sk_redirect_map(skb, &sock_map_tx, sk, 0);
}

char _license[] SEC("license") = "GPL";
