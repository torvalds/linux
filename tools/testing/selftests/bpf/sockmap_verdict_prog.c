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

struct bpf_map_def SEC("maps") sock_map = {
	.type = BPF_MAP_TYPE_SOCKMAP,
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
	char *d = data;

	if (data + 8 > data_end)
		return SK_DROP;

	d[0] = 0xd;
	d[1] = 0xe;
	d[2] = 0xa;
	d[3] = 0xd;
	d[4] = 0xb;
	d[5] = 0xe;
	d[6] = 0xe;
	d[7] = 0xf;

	bpf_printk("data[0] = (%u): local_port %i remote %i\n",
		   d[0], lport, bpf_ntohl(rport));
	return bpf_sk_redirect_map(&sock_map, 5, 0);
}

char _license[] SEC("license") = "GPL";
