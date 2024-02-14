/* Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * BPF program to set initial receive window to 40 packets when using IPv6
 * and the first 5.5 bytes of the IPv6 addresses are not the same (in this
 * example that means both hosts are not the same datacenter).
 *
 * Use "bpftool cgroup attach $cg sock_ops $prog" to load this BPF program.
 */

#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/ip.h>
#include <linux/socket.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define DEBUG 1

SEC("sockops")
int bpf_rwnd(struct bpf_sock_ops *skops)
{
	int rv = -1;
	int op;

	/* For testing purposes, only execute rest of BPF program
	 * if neither port numberis 55601
	 */
	if (bpf_ntohl(skops->remote_port) !=
	    55601 && skops->local_port != 55601) {
		skops->reply = -1;
		return 1;
	}

	op = (int) skops->op;

#ifdef DEBUG
	bpf_printk("BPF command: %d\n", op);
#endif

	/* Check for RWND_INIT operation and IPv6 addresses */
	if (op == BPF_SOCK_OPS_RWND_INIT &&
		skops->family == AF_INET6) {

		/* If the first 5.5 bytes of the IPv6 address are not the same
		 * then both hosts are not in the same datacenter
		 * so use a larger initial advertized window (40 packets)
		 */
		if (skops->local_ip6[0] != skops->remote_ip6[0] ||
		    (bpf_ntohl(skops->local_ip6[1]) & 0xfffff000) !=
		    (bpf_ntohl(skops->remote_ip6[1]) & 0xfffff000))
			rv = 40;
	}
#ifdef DEBUG
	bpf_printk("Returning %d\n", rv);
#endif
	skops->reply = rv;
	return 1;
}
char _license[] SEC("license") = "GPL";
