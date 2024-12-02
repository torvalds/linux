// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

char _license[] SEC("license") = "GPL";

int ifindex;
int ret;

SEC("lwt_xmit")
int redirect_ingress(struct __sk_buff *skb)
{
	ret = bpf_clone_redirect(skb, ifindex, BPF_F_INGRESS);
	return 0;
}

SEC("lwt_xmit")
int redirect_egress(struct __sk_buff *skb)
{
	ret = bpf_clone_redirect(skb, ifindex, 0);
	return 0;
}

SEC("tc")
int tc_redirect_ingress(struct __sk_buff *skb)
{
	ret = bpf_clone_redirect(skb, ifindex, BPF_F_INGRESS);
	return 0;
}

SEC("tc")
int tc_redirect_egress(struct __sk_buff *skb)
{
	ret = bpf_clone_redirect(skb, ifindex, 0);
	return 0;
}
