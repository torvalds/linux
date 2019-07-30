/* Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * BPF program to set initial receive window to 40 packets and send
 * and receive buffers to 1.5MB. This would usually be done after
 * doing appropriate checks that indicate the hosts are far enough
 * away (i.e. large RTT).
 *
 * Use "bpftool cgroup attach $cg sock_ops $prog" to load this BPF program.
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
int bpf_bufs(struct bpf_sock_ops *skops)
{
	int bufsize = 1500000;
	int rwnd_init = 40;
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
	bpf_printk("Returning %d\n", rv);
#endif

	/* Usually there would be a check to insure the hosts are far
	 * from each other so it makes sense to increase buffer sizes
	 */
	switch (op) {
	case BPF_SOCK_OPS_RWND_INIT:
		rv = rwnd_init;
		break;
	case BPF_SOCK_OPS_TCP_CONNECT_CB:
		/* Set sndbuf and rcvbuf of active connections */
		rv = bpf_setsockopt(skops, SOL_SOCKET, SO_SNDBUF, &bufsize,
				    sizeof(bufsize));
		rv += bpf_setsockopt(skops, SOL_SOCKET, SO_RCVBUF,
				     &bufsize, sizeof(bufsize));
		break;
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
		/* Nothing to do */
		break;
	case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
		/* Set sndbuf and rcvbuf of passive connections */
		rv = bpf_setsockopt(skops, SOL_SOCKET, SO_SNDBUF, &bufsize,
				    sizeof(bufsize));
		rv += bpf_setsockopt(skops, SOL_SOCKET, SO_RCVBUF,
				     &bufsize, sizeof(bufsize));
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
