/* Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * Sample BPF program to set send and receive buffers to 150KB, sndcwnd clamp
 * to 100 packets and SYN and SYN_ACK RTOs to 10ms when both hosts are within
 * the same datacenter. For his example, we assume they are within the same
 * datacenter when the first 5.5 bytes of their IPv6 addresses are the same.
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
int bpf_clamp(struct bpf_sock_ops *skops)
{
	int bufsize = 150000;
	int to_init = 10;
	int clamp = 100;
	int rv = 0;
	int op;

	/* For testing purposes, only execute rest of BPF program
	 * if neither port numberis 55601
	 */
	if (bpf_ntohl(skops->remote_port) != 55601 && skops->local_port != 55601) {
		skops->reply = -1;
		return 0;
	}

	op = (int) skops->op;

#ifdef DEBUG
	bpf_printk("BPF command: %d\n", op);
#endif

	/* Check that both hosts are within same datacenter. For this example
	 * it is the case when the first 5.5 bytes of their IPv6 addresses are
	 * the same.
	 */
	if (skops->family == AF_INET6 &&
	    skops->local_ip6[0] == skops->remote_ip6[0] &&
	    (bpf_ntohl(skops->local_ip6[1]) & 0xfff00000) ==
	    (bpf_ntohl(skops->remote_ip6[1]) & 0xfff00000)) {
		switch (op) {
		case BPF_SOCK_OPS_TIMEOUT_INIT:
			rv = to_init;
			break;
		case BPF_SOCK_OPS_TCP_CONNECT_CB:
			/* Set sndbuf and rcvbuf of active connections */
			rv = bpf_setsockopt(skops, SOL_SOCKET, SO_SNDBUF,
					    &bufsize, sizeof(bufsize));
			rv += bpf_setsockopt(skops, SOL_SOCKET,
					     SO_RCVBUF, &bufsize,
					     sizeof(bufsize));
			break;
		case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
			rv = bpf_setsockopt(skops, SOL_TCP,
					    TCP_BPF_SNDCWND_CLAMP,
					    &clamp, sizeof(clamp));
			break;
		case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
			/* Set sndbuf and rcvbuf of passive connections */
			rv = bpf_setsockopt(skops, SOL_TCP,
					    TCP_BPF_SNDCWND_CLAMP,
					    &clamp, sizeof(clamp));
			rv += bpf_setsockopt(skops, SOL_SOCKET,
					     SO_SNDBUF, &bufsize,
					     sizeof(bufsize));
			rv += bpf_setsockopt(skops, SOL_SOCKET,
					     SO_RCVBUF, &bufsize,
					     sizeof(bufsize));
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
