// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <linux/bpf.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

/* This function extracts the last byte of the daddr, and uses it
 * as output dev index.
 */
SEC("lwt_xmit")
int test_lwt_reroute(struct __sk_buff *skb)
{
	struct iphdr *iph = NULL;
	void *start = (void *)(long)skb->data;
	void *end = (void *)(long)skb->data_end;

	/* set mark at most once */
	if (skb->mark != 0)
		return BPF_OK;

	if (start + sizeof(*iph) > end)
		return BPF_DROP;

	iph = (struct iphdr *)start;
	skb->mark = bpf_ntohl(iph->daddr) & 0xff;

	/* do not reroute x.x.x.0 packets */
	if (skb->mark == 0)
		return BPF_OK;

	return BPF_LWT_REROUTE;
}

char _license[] SEC("license") = "GPL";
