/* Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * BPF program to set congestion control to dctcp when both hosts are
 * in the same datacenter (as deteremined by IPv6 prefix).
 *
 * Use "bpftool cgroup attach $cg sock_ops $prog" to load this BPF program.
 */

#include <uapi/linux/bpf.h>
#include <uapi/linux/tcp.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/ip.h>
#include <linux/socket.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define DEBUG 1

SEC("sockops")
int bpf_cong(struct bpf_sock_ops *skops)
{
	char cong[] = "dctcp";
	int rv = 0;
	int op;

	/* For testing purposes, only execute rest of BPF program
	 * if neither port numberis 55601
	 */
	if (bpf_ntohl(skops->remote_port) != 55601 &&
	    skops->local_port != 55601) {
		skops->reply = -1;
		return 1;
	}

	op = (int) skops->op;

#ifdef DEBUG
	bpf_printk("BPF command: %d\n", op);
#endif

	/* Check if both hosts are in the same datacenter. For this
	 * example they are if the 1st 5.5 bytes in the IPv6 address
	 * are the same.
	 */
	if (skops->family == AF_INET6 &&
	    skops->local_ip6[0] == skops->remote_ip6[0] &&
	    (bpf_ntohl(skops->local_ip6[1]) & 0xfff00000) ==
	    (bpf_ntohl(skops->remote_ip6[1]) & 0xfff00000)) {
		switch (op) {
		case BPF_SOCK_OPS_NEEDS_ECN:
			rv = 1;
			break;
		case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
			rv = bpf_setsockopt(skops, SOL_TCP, TCP_CONGESTION,
					    cong, sizeof(cong));
			break;
		case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
			rv = bpf_setsockopt(skops, SOL_TCP, TCP_CONGESTION,
					    cong, sizeof(cong));
			break;
		default:
			rv = -1;
		}
	} else {
		rv = -1;
	}
#ifdef DEBUG
	bpf_printk("Returning %d\n", rv);
#endif
	skops->reply = rv;
	return 1;
}
char _license[] SEC("license") = "GPL";
