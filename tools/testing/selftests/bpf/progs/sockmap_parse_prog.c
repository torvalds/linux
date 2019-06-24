#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"

int _version SEC("version") = 1;

SEC("sk_skb1")
int bpf_prog1(struct __sk_buff *skb)
{
	void *data_end = (void *)(long) skb->data_end;
	void *data = (void *)(long) skb->data;
	__u32 lport = skb->local_port;
	__u32 rport = skb->remote_port;
	__u8 *d = data;
	__u32 len = (__u32) data_end - (__u32) data;
	int err;

	if (data + 10 > data_end) {
		err = bpf_skb_pull_data(skb, 10);
		if (err)
			return SK_DROP;

		data_end = (void *)(long)skb->data_end;
		data = (void *)(long)skb->data;
		if (data + 10 > data_end)
			return SK_DROP;
	}

	/* This write/read is a bit pointless but tests the verifier and
	 * strparser handler for read/write pkt data and access into sk
	 * fields.
	 */
	d = data;
	d[7] = 1;
	return skb->len;
}

char _license[] SEC("license") = "GPL";
