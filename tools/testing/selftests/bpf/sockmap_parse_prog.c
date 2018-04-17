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

SEC("sk_skb1")
int bpf_prog1(struct __sk_buff *skb)
{
	void *data_end = (void *)(long) skb->data_end;
	void *data = (void *)(long) skb->data;
	__u32 lport = skb->local_port;
	__u32 rport = skb->remote_port;
	__u8 *d = data;

	if (data + 10 > data_end)
		return skb->len;

	/* This write/read is a bit pointless but tests the verifier and
	 * strparser handler for read/write pkt data and access into sk
	 * fields.
	 */
	d[7] = 1;
	return skb->len;
}

char _license[] SEC("license") = "GPL";
