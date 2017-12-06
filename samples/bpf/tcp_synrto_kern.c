/* Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * BPF program to set SYN and SYN-ACK RTOs to 10ms when using IPv6 addresses
 * and the first 5.5 bytes of the IPv6 addresses are the same (in this example
 * that means both hosts are in the same datacenter).
 *
 * Use load_sock_ops to load this BPF program.
 */

#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/ip.h>
#include <linux/socket.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"

#define DEBUG 1

#define bpf_printk(fmt, ...)					\
({								\
	       char ____fmt[] = fmt;				\
	       bpf_trace_printk(____fmt, sizeof(____fmt),	\
				##__VA_ARGS__);			\
})

SEC("sockops")
int bpf_synrto(struct bpf_sock_ops *skops)
{
	int rv = -1;
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

	/* Check for TIMEOUT_INIT operation and IPv6 addresses */
	if (op == BPF_SOCK_OPS_TIMEOUT_INIT &&
		skops->family == AF_INET6) {

		/* If the first 5.5 bytes of the IPv6 address are the same
		 * then both hosts are in the same datacenter
		 * so use an RTO of 10ms
		 */
		if (skops->local_ip6[0] == skops->remote_ip6[0] &&
		    (bpf_ntohl(skops->local_ip6[1]) & 0xfff00000) ==
		    (bpf_ntohl(skops->remote_ip6[1]) & 0xfff00000))
			rv = 10;
	}
#ifdef DEBUG
	bpf_printk("Returning %d\n", rv);
#endif
	skops->reply = rv;
	return 1;
}
char _license[] SEC("license") = "GPL";
