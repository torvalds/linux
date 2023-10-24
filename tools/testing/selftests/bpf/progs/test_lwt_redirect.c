// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <linux/ip.h>
#include "bpf_tracing_net.h"

/* We don't care about whether the packet can be received by network stack.
 * Just care if the packet is sent to the correct device at correct direction
 * and not panic the kernel.
 */
static int prepend_dummy_mac(struct __sk_buff *skb)
{
	char mac[] = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0xf,
		      0xe, 0xd, 0xc, 0xb, 0xa, 0x08, 0x00};

	if (bpf_skb_change_head(skb, ETH_HLEN, 0))
		return -1;

	if (bpf_skb_store_bytes(skb, 0, mac, sizeof(mac), 0))
		return -1;

	return 0;
}

/* Use the last byte of IP address to redirect the packet */
static int get_redirect_target(struct __sk_buff *skb)
{
	struct iphdr *iph = NULL;
	void *start = (void *)(long)skb->data;
	void *end = (void *)(long)skb->data_end;

	if (start + sizeof(*iph) > end)
		return -1;

	iph = (struct iphdr *)start;
	return bpf_ntohl(iph->daddr) & 0xff;
}

SEC("redir_ingress")
int test_lwt_redirect_in(struct __sk_buff *skb)
{
	int target = get_redirect_target(skb);

	if (target < 0)
		return BPF_OK;

	if (prepend_dummy_mac(skb))
		return BPF_DROP;

	return bpf_redirect(target, BPF_F_INGRESS);
}

SEC("redir_egress")
int test_lwt_redirect_out(struct __sk_buff *skb)
{
	int target = get_redirect_target(skb);

	if (target < 0)
		return BPF_OK;

	if (prepend_dummy_mac(skb))
		return BPF_DROP;

	return bpf_redirect(target, 0);
}

SEC("redir_egress_nomac")
int test_lwt_redirect_out_nomac(struct __sk_buff *skb)
{
	int target = get_redirect_target(skb);

	if (target < 0)
		return BPF_OK;

	return bpf_redirect(target, 0);
}

SEC("redir_ingress_nomac")
int test_lwt_redirect_in_nomac(struct __sk_buff *skb)
{
	int target = get_redirect_target(skb);

	if (target < 0)
		return BPF_OK;

	return bpf_redirect(target, BPF_F_INGRESS);
}

char _license[] SEC("license") = "GPL";
