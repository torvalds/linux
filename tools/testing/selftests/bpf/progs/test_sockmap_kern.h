/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017-2018 Covalent IO, Inc. http://covalent.io */
#include <stddef.h>
#include <string.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/pkt_cls.h>
#include <sys/socket.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include "bpf_misc.h"

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

struct {
	__uint(type, TEST_MAP_TYPE);
	__uint(max_entries, 20);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} sock_map SEC(".maps");

struct {
	__uint(type, TEST_MAP_TYPE);
	__uint(max_entries, 20);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} sock_map_txmsg SEC(".maps");

struct {
	__uint(type, TEST_MAP_TYPE);
	__uint(max_entries, 20);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} sock_map_redir SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} sock_apply_bytes SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} sock_cork_bytes SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 6);
	__type(key, int);
	__type(value, int);
} sock_bytes SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} sock_redir_flags SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 3);
	__type(key, int);
	__type(value, int);
} sock_skb_opts SEC(".maps");

struct {
	__uint(type, TEST_MAP_TYPE);
	__uint(max_entries, 20);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} tls_sock_map SEC(".maps");

SEC("sk_skb1")
int bpf_prog1(struct __sk_buff *skb)
{
	int *f, two = 2;

	f = bpf_map_lookup_elem(&sock_skb_opts, &two);
	if (f && *f) {
		return *f;
	}
	return skb->len;
}

SEC("sk_skb2")
int bpf_prog2(struct __sk_buff *skb)
{
	__u32 lport = skb->local_port;
	__u32 rport = skb->remote_port;
	int len, *f, ret, zero = 0;
	__u64 flags = 0;

	__sink(rport);
	if (lport == 10000)
		ret = 10;
	else
		ret = 1;

	len = (__u32)skb->data_end - (__u32)skb->data;
	__sink(len);

	f = bpf_map_lookup_elem(&sock_skb_opts, &zero);
	if (f && *f) {
		ret = 3;
		flags = *f;
	}

#ifdef SOCKMAP
	return bpf_sk_redirect_map(skb, &sock_map, ret, flags);
#else
	return bpf_sk_redirect_hash(skb, &sock_map, &ret, flags);
#endif

}

static inline void bpf_write_pass(struct __sk_buff *skb, int offset)
{
	int err = bpf_skb_pull_data(skb, 6 + offset);
	void *data_end;
	char *c;

	if (err)
		return;

	c = (char *)(long)skb->data;
	data_end = (void *)(long)skb->data_end;

	if (c + 5 + offset < data_end)
		memcpy(c + offset, "PASS", 4);
}

SEC("sk_skb3")
int bpf_prog3(struct __sk_buff *skb)
{
	int err, *f, ret = SK_PASS;
	const int one = 1;

	f = bpf_map_lookup_elem(&sock_skb_opts, &one);
	if (f && *f) {
		__u64 flags = 0;

		ret = 0;
		flags = *f;

		err = bpf_skb_adjust_room(skb, -13, 0, 0);
		if (err)
			return SK_DROP;
		err = bpf_skb_adjust_room(skb, 4, 0, 0);
		if (err)
			return SK_DROP;
		bpf_write_pass(skb, 0);
#ifdef SOCKMAP
		return bpf_sk_redirect_map(skb, &tls_sock_map, ret, flags);
#else
		return bpf_sk_redirect_hash(skb, &tls_sock_map, &ret, flags);
#endif
	}
	f = bpf_map_lookup_elem(&sock_skb_opts, &one);
	if (f && *f)
		ret = SK_DROP;
	err = bpf_skb_adjust_room(skb, 4, 0, 0);
	if (err)
		return SK_DROP;
	bpf_write_pass(skb, 13);
	return ret;
}

SEC("sockops")
int bpf_sockmap(struct bpf_sock_ops *skops)
{
	__u32 lport, rport;
	int op, ret;

	op = (int) skops->op;

	switch (op) {
	case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
		lport = skops->local_port;
		rport = skops->remote_port;

		if (lport == 10000) {
			ret = 1;
#ifdef SOCKMAP
			bpf_sock_map_update(skops, &sock_map, &ret,
						  BPF_NOEXIST);
#else
			bpf_sock_hash_update(skops, &sock_map, &ret,
						   BPF_NOEXIST);
#endif
		}
		break;
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
		lport = skops->local_port;
		rport = skops->remote_port;

		if (bpf_ntohl(rport) == 10001) {
			ret = 10;
#ifdef SOCKMAP
			bpf_sock_map_update(skops, &sock_map, &ret,
						  BPF_NOEXIST);
#else
			bpf_sock_hash_update(skops, &sock_map, &ret,
						   BPF_NOEXIST);
#endif
		}
		break;
	default:
		break;
	}

	return 0;
}

SEC("sk_msg1")
int bpf_prog4(struct sk_msg_md *msg)
{
	int *bytes, zero = 0, one = 1, two = 2, three = 3, four = 4, five = 5;
	int *start, *end, *start_push, *end_push, *start_pop, *pop, err = 0;

	bytes = bpf_map_lookup_elem(&sock_apply_bytes, &zero);
	if (bytes)
		bpf_msg_apply_bytes(msg, *bytes);
	bytes = bpf_map_lookup_elem(&sock_cork_bytes, &zero);
	if (bytes)
		bpf_msg_cork_bytes(msg, *bytes);
	start = bpf_map_lookup_elem(&sock_bytes, &zero);
	end = bpf_map_lookup_elem(&sock_bytes, &one);
	if (start && end)
		bpf_msg_pull_data(msg, *start, *end, 0);
	start_push = bpf_map_lookup_elem(&sock_bytes, &two);
	end_push = bpf_map_lookup_elem(&sock_bytes, &three);
	if (start_push && end_push) {
		err = bpf_msg_push_data(msg, *start_push, *end_push, 0);
		if (err)
			return SK_DROP;
	}
	start_pop = bpf_map_lookup_elem(&sock_bytes, &four);
	pop = bpf_map_lookup_elem(&sock_bytes, &five);
	if (start_pop && pop)
		bpf_msg_pop_data(msg, *start_pop, *pop, 0);
	return SK_PASS;
}

SEC("sk_msg2")
int bpf_prog6(struct sk_msg_md *msg)
{
	int zero = 0, one = 1, two = 2, three = 3, four = 4, five = 5, key = 0;
	int *bytes, *start, *end, *start_push, *end_push, *start_pop, *pop, *f;
	int err = 0;
	__u64 flags = 0;

	bytes = bpf_map_lookup_elem(&sock_apply_bytes, &zero);
	if (bytes)
		bpf_msg_apply_bytes(msg, *bytes);
	bytes = bpf_map_lookup_elem(&sock_cork_bytes, &zero);
	if (bytes)
		bpf_msg_cork_bytes(msg, *bytes);

	start = bpf_map_lookup_elem(&sock_bytes, &zero);
	end = bpf_map_lookup_elem(&sock_bytes, &one);
	if (start && end)
		bpf_msg_pull_data(msg, *start, *end, 0);

	start_push = bpf_map_lookup_elem(&sock_bytes, &two);
	end_push = bpf_map_lookup_elem(&sock_bytes, &three);
	if (start_push && end_push) {
		err = bpf_msg_push_data(msg, *start_push, *end_push, 0);
		if (err)
			return SK_DROP;
	}

	start_pop = bpf_map_lookup_elem(&sock_bytes, &four);
	pop = bpf_map_lookup_elem(&sock_bytes, &five);
	if (start_pop && pop)
		bpf_msg_pop_data(msg, *start_pop, *pop, 0);

	f = bpf_map_lookup_elem(&sock_redir_flags, &zero);
	if (f && *f) {
		key = 2;
		flags = *f;
	}
#ifdef SOCKMAP
	return bpf_msg_redirect_map(msg, &sock_map_redir, key, flags);
#else
	return bpf_msg_redirect_hash(msg, &sock_map_redir, &key, flags);
#endif
}

SEC("sk_msg3")
int bpf_prog8(struct sk_msg_md *msg)
{
	void *data_end = (void *)(long) msg->data_end;
	void *data = (void *)(long) msg->data;
	int ret = 0, *bytes, zero = 0;

	bytes = bpf_map_lookup_elem(&sock_apply_bytes, &zero);
	if (bytes) {
		ret = bpf_msg_apply_bytes(msg, *bytes);
		if (ret)
			return SK_DROP;
	} else {
		return SK_DROP;
	}

	__sink(data_end);
	__sink(data);

	return SK_PASS;
}
SEC("sk_msg4")
int bpf_prog9(struct sk_msg_md *msg)
{
	void *data_end = (void *)(long) msg->data_end;
	void *data = (void *)(long) msg->data;
	int ret = 0, *bytes, zero = 0;

	bytes = bpf_map_lookup_elem(&sock_cork_bytes, &zero);
	if (bytes) {
		if (((__u64)data_end - (__u64)data) >= *bytes)
			return SK_PASS;
		ret = bpf_msg_cork_bytes(msg, *bytes);
		if (ret)
			return SK_DROP;
	}
	return SK_PASS;
}

SEC("sk_msg5")
int bpf_prog10(struct sk_msg_md *msg)
{
	int *bytes, *start, *end, *start_push, *end_push, *start_pop, *pop;
	int zero = 0, one = 1, two = 2, three = 3, four = 4, five = 5, err = 0;

	bytes = bpf_map_lookup_elem(&sock_apply_bytes, &zero);
	if (bytes)
		bpf_msg_apply_bytes(msg, *bytes);
	bytes = bpf_map_lookup_elem(&sock_cork_bytes, &zero);
	if (bytes)
		bpf_msg_cork_bytes(msg, *bytes);
	start = bpf_map_lookup_elem(&sock_bytes, &zero);
	end = bpf_map_lookup_elem(&sock_bytes, &one);
	if (start && end)
		bpf_msg_pull_data(msg, *start, *end, 0);
	start_push = bpf_map_lookup_elem(&sock_bytes, &two);
	end_push = bpf_map_lookup_elem(&sock_bytes, &three);
	if (start_push && end_push) {
		err = bpf_msg_push_data(msg, *start_push, *end_push, 0);
		if (err)
			return SK_PASS;
	}
	start_pop = bpf_map_lookup_elem(&sock_bytes, &four);
	pop = bpf_map_lookup_elem(&sock_bytes, &five);
	if (start_pop && pop)
		bpf_msg_pop_data(msg, *start_pop, *pop, 0);
	return SK_DROP;
}

char _license[] SEC("license") = "GPL";
