// SPDX-License-Identifier: GPL-2.0
#define BPF_NO_PRESERVE_ACCESS_INDEX
#include <vmlinux.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>

#define INLINE __always_inline

#define skb_shorter(skb, len) ((void *)(long)(skb)->data + (len) > (void *)(long)skb->data_end)

#define ETH_IPV4_TCP_SIZE (14 + sizeof(struct iphdr) + sizeof(struct tcphdr))

static INLINE struct iphdr *get_iphdr(struct __sk_buff *skb)
{
	struct iphdr *ip = NULL;
	struct ethhdr *eth;

	if (skb_shorter(skb, ETH_IPV4_TCP_SIZE))
		goto out;

	eth = (void *)(long)skb->data;
	ip = (void *)(eth + 1);

out:
	return ip;
}

SEC("classifier/cls")
int main_prog(struct __sk_buff *skb)
{
	struct iphdr *ip = NULL;
	struct tcphdr *tcp;
	__u8 proto = 0;

	if (!(ip = get_iphdr(skb)))
		goto out;

	proto = ip->protocol;

	if (proto != IPPROTO_TCP)
		goto out;

	tcp = (void*)(ip + 1);
	if (tcp->dest != 0)
		goto out;
	if (!tcp)
		goto out;

	return tcp->urg_ptr;
out:
	return -1;
}
char _license[] SEC("license") = "GPL";
