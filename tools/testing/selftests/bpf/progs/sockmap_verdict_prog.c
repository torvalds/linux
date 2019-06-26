#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "bpf_util.h"
#include "bpf_endian.h"

int _version SEC("version") = 1;

#define bpf_printk(fmt, ...)					\
({								\
	       char ____fmt[] = fmt;				\
	       bpf_trace_printk(____fmt, sizeof(____fmt),	\
				##__VA_ARGS__);			\
})

struct bpf_map_def SEC("maps") sock_map_rx = {
	.type = BPF_MAP_TYPE_SOCKMAP,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 20,
};

struct bpf_map_def SEC("maps") sock_map_tx = {
	.type = BPF_MAP_TYPE_SOCKMAP,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 20,
};

struct bpf_map_def SEC("maps") sock_map_msg = {
	.type = BPF_MAP_TYPE_SOCKMAP,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 20,
};

struct bpf_map_def SEC("maps") sock_map_break = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 20,
};

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
