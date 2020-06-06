// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Facebook
 *
 * BPF program to automatically reflect TOS option from received syn packet
 *
 * Use "bpftool cgroup attach $cg sock_ops $prog" to load this BPF program.
 */

#include <uapi/linux/bpf.h>
#include <uapi/linux/tcp.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/ipv6.h>
#include <uapi/linux/in.h>
#include <linux/socket.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define DEBUG 1

SEC("sockops")
int bpf_basertt(struct bpf_sock_ops *skops)
{
	char header[sizeof(struct ipv6hdr)];
	struct ipv6hdr *hdr6;
	struct iphdr *hdr;
	int hdr_size = 0;
	int save_syn = 1;
	int tos = 0;
	int rv = 0;
	int op;

	op = (int) skops->op;

#ifdef DEBUG
	bpf_printk("BPF command: %d\n", op);
#endif
	switch (op) {
	case BPF_SOCK_OPS_TCP_LISTEN_CB:
		rv = bpf_setsockopt(skops, SOL_TCP, TCP_SAVE_SYN,
				   &save_syn, sizeof(save_syn));
		break;
	case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
		if (skops->family == AF_INET)
			hdr_size = sizeof(struct iphdr);
		else
			hdr_size = sizeof(struct ipv6hdr);
		rv = bpf_getsockopt(skops, SOL_TCP, TCP_SAVED_SYN,
				    header, hdr_size);
		if (!rv) {
			if (skops->family == AF_INET) {
				hdr = (struct iphdr *) header;
				tos = hdr->tos;
				if (tos != 0)
					bpf_setsockopt(skops, SOL_IP, IP_TOS,
						       &tos, sizeof(tos));
			} else {
				hdr6 = (struct ipv6hdr *) header;
				tos = ((hdr6->priority) << 4 |
				       (hdr6->flow_lbl[0]) >>  4);
				if (tos)
					bpf_setsockopt(skops, SOL_IPV6,
						       IPV6_TCLASS,
						       &tos, sizeof(tos));
			}
			rv = 0;
		}
		break;
	default:
		rv = -1;
	}
#ifdef DEBUG
	bpf_printk("Returning %d\n", rv);
#endif
	skops->reply = rv;
	return 1;
}
char _license[] SEC("license") = "GPL";
