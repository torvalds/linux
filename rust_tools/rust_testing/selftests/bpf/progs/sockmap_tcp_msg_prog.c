#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

SEC("sk_msg1")
int bpf_prog1(struct sk_msg_md *msg)
{
	return SK_PASS;
}

char _license[] SEC("license") = "GPL";
