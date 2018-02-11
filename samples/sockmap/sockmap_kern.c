/* Copyright (c) 2017 Covalent IO, Inc. http://covalent.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/ip.h>
#include "../../tools/testing/selftests/bpf/bpf_helpers.h"
#include "../../tools/testing/selftests/bpf/bpf_endian.h"

/* Sockmap sample program connects a client and a backend together
 * using cgroups.
 *
 *    client:X <---> frontend:80 client:X <---> backend:80
 *
 * For simplicity we hard code values here and bind 1:1. The hard
 * coded values are part of the setup in sockmap.sh script that
 * is associated with this BPF program.
 *
 * The bpf_printk is verbose and prints information as connections
 * are established and verdicts are decided.
 */

#define bpf_printk(fmt, ...)					\
({								\
	       char ____fmt[] = fmt;				\
	       bpf_trace_printk(____fmt, sizeof(____fmt),	\
				##__VA_ARGS__);			\
})

struct bpf_map_def SEC("maps") sock_map = {
	.type = BPF_MAP_TYPE_SOCKMAP,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 20,
};

SEC("sk_skb1")
int bpf_prog1(struct __sk_buff *skb)
{
	return skb->len;
}

SEC("sk_skb2")
int bpf_prog2(struct __sk_buff *skb)
{
	__u32 lport = skb->local_port;
	__u32 rport = skb->remote_port;
	int ret = 0;

	if (lport == 10000)
		ret = 10;
	else
		ret = 1;

	bpf_printk("sockmap: %d -> %d @ %d\n", lport, bpf_ntohl(rport), ret);
	return bpf_sk_redirect_map(skb, &sock_map, ret, 0);
}

SEC("sockops")
int bpf_sockmap(struct bpf_sock_ops *skops)
{
	__u32 lport, rport;
	int op, err = 0, index, key, ret;


	op = (int) skops->op;

	switch (op) {
	case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
		lport = skops->local_port;
		rport = skops->remote_port;

		if (lport == 10000) {
			ret = 1;
			err = bpf_sock_map_update(skops, &sock_map, &ret,
						  BPF_NOEXIST);
			bpf_printk("passive(%i -> %i) map ctx update err: %d\n",
				   lport, bpf_ntohl(rport), err);
		}
		break;
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
		lport = skops->local_port;
		rport = skops->remote_port;

		if (bpf_ntohl(rport) == 10001) {
			ret = 10;
			err = bpf_sock_map_update(skops, &sock_map, &ret,
						  BPF_NOEXIST);
			bpf_printk("active(%i -> %i) map ctx update err: %d\n",
				   lport, bpf_ntohl(rport), err);
		}
		break;
	default:
		break;
	}

	return 0;
}
char _license[] SEC("license") = "GPL";
