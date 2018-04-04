/* Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * BPF program to set base_rtt to 80us when host is running TCP-NV and
 * both hosts are in the same datacenter (as determined by IPv6 prefix).
 *
 * Use load_sock_ops to load this BPF program.
 */

#include <uapi/linux/bpf.h>
#include <uapi/linux/tcp.h>
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
int bpf_basertt(struct bpf_sock_ops *skops)
{
	char cong[20];
	char nv[] = "nv";
	int rv = 0, n;
	int op;

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
		case BPF_SOCK_OPS_BASE_RTT:
			n = bpf_getsockopt(skops, SOL_TCP, TCP_CONGESTION,
					   cong, sizeof(cong));
			if (!n && !__builtin_memcmp(cong, nv, sizeof(nv)+1)) {
				/* Set base_rtt to 80us */
				rv = 80;
			} else if (n) {
				rv = n;
			} else {
				rv = -1;
			}
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
