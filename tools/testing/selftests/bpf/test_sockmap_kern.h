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
#include "bpf_helpers.h"
#include "bpf_endian.h"

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
	.type = TEST_MAP_TYPE,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 20,
};

struct bpf_map_def SEC("maps") sock_map_txmsg = {
	.type = TEST_MAP_TYPE,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 20,
};

struct bpf_map_def SEC("maps") sock_map_redir = {
	.type = TEST_MAP_TYPE,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 20,
};

struct bpf_map_def SEC("maps") sock_apply_bytes = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 1
};

struct bpf_map_def SEC("maps") sock_cork_bytes = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 1
};

struct bpf_map_def SEC("maps") sock_bytes = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 4
};

struct bpf_map_def SEC("maps") sock_redir_flags = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 1
};

struct bpf_map_def SEC("maps") sock_skb_opts = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 1
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
	int len, *f, ret, zero = 0;
	__u64 flags = 0;

	if (lport == 10000)
		ret = 10;
	else
		ret = 1;

	len = (__u32)skb->data_end - (__u32)skb->data;
	f = bpf_map_lookup_elem(&sock_skb_opts, &zero);
	if (f && *f) {
		ret = 3;
		flags = *f;
	}

	bpf_printk("sk_skb2: redirect(%iB) flags=%i\n",
		   len, flags);
#ifdef SOCKMAP
	return bpf_sk_redirect_map(skb, &sock_map, ret, flags);
#else
	return bpf_sk_redirect_hash(skb, &sock_map, &ret, flags);
#endif

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
#ifdef SOCKMAP
			err = bpf_sock_map_update(skops, &sock_map, &ret,
						  BPF_NOEXIST);
#else
			err = bpf_sock_hash_update(skops, &sock_map, &ret,
						   BPF_NOEXIST);
#endif
			bpf_printk("passive(%i -> %i) map ctx update err: %d\n",
				   lport, bpf_ntohl(rport), err);
		}
		break;
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
		lport = skops->local_port;
		rport = skops->remote_port;

		if (bpf_ntohl(rport) == 10001) {
			ret = 10;
#ifdef SOCKMAP
			err = bpf_sock_map_update(skops, &sock_map, &ret,
						  BPF_NOEXIST);
#else
			err = bpf_sock_hash_update(skops, &sock_map, &ret,
						   BPF_NOEXIST);
#endif
			bpf_printk("active(%i -> %i) map ctx update err: %d\n",
				   lport, bpf_ntohl(rport), err);
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
	int *bytes, zero = 0, one = 1, two = 2, three = 3;
	int *start, *end, *start_push, *end_push;

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
	if (start_push && end_push)
		bpf_msg_push_data(msg, *start_push, *end_push, 0);
	return SK_PASS;
}

SEC("sk_msg2")
int bpf_prog5(struct sk_msg_md *msg)
{
	int zero = 0, one = 1, two = 2, three = 3;
	int *start, *end, *start_push, *end_push;
	int *bytes, len1, len2 = 0, len3;
	int err1 = -1, err2 = -1;

	bytes = bpf_map_lookup_elem(&sock_apply_bytes, &zero);
	if (bytes)
		err1 = bpf_msg_apply_bytes(msg, *bytes);
	bytes = bpf_map_lookup_elem(&sock_cork_bytes, &zero);
	if (bytes)
		err2 = bpf_msg_cork_bytes(msg, *bytes);
	len1 = (__u64)msg->data_end - (__u64)msg->data;
	start = bpf_map_lookup_elem(&sock_bytes, &zero);
	end = bpf_map_lookup_elem(&sock_bytes, &one);
	if (start && end) {
		int err;

		bpf_printk("sk_msg2: pull(%i:%i)\n",
			   start ? *start : 0, end ? *end : 0);
		err = bpf_msg_pull_data(msg, *start, *end, 0);
		if (err)
			bpf_printk("sk_msg2: pull_data err %i\n",
				   err);
		len2 = (__u64)msg->data_end - (__u64)msg->data;
		bpf_printk("sk_msg2: length update %i->%i\n",
			   len1, len2);
	}

	start_push = bpf_map_lookup_elem(&sock_bytes, &two);
	end_push = bpf_map_lookup_elem(&sock_bytes, &three);
	if (start_push && end_push) {
		int err;

		bpf_printk("sk_msg2: push(%i:%i)\n",
			   start_push ? *start_push : 0,
			   end_push ? *end_push : 0);
		err = bpf_msg_push_data(msg, *start_push, *end_push, 0);
		if (err)
			bpf_printk("sk_msg2: push_data err %i\n", err);
		len3 = (__u64)msg->data_end - (__u64)msg->data;
		bpf_printk("sk_msg2: length push_update %i->%i\n",
			   len2 ? len2 : len1, len3);
	}

	bpf_printk("sk_msg2: data length %i err1 %i err2 %i\n",
		   len1, err1, err2);
	return SK_PASS;
}

SEC("sk_msg3")
int bpf_prog6(struct sk_msg_md *msg)
{
	int *bytes, *start, *end, *start_push, *end_push, *f;
	int zero = 0, one = 1, two = 2, three = 3, key = 0;
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
	if (start_push && end_push)
		bpf_msg_push_data(msg, *start_push, *end_push, 0);

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

SEC("sk_msg4")
int bpf_prog7(struct sk_msg_md *msg)
{
	int zero = 0, one = 1, two = 2, three = 3, len1, len2 = 0, len3;
	int *bytes, *start, *end, *start_push, *end_push, *f;
	int err1 = 0, err2 = 0, key = 0;
	__u64 flags = 0;

		int err;
	bytes = bpf_map_lookup_elem(&sock_apply_bytes, &zero);
	if (bytes)
		err1 = bpf_msg_apply_bytes(msg, *bytes);
	bytes = bpf_map_lookup_elem(&sock_cork_bytes, &zero);
	if (bytes)
		err2 = bpf_msg_cork_bytes(msg, *bytes);
	len1 = (__u64)msg->data_end - (__u64)msg->data;

	start = bpf_map_lookup_elem(&sock_bytes, &zero);
	end = bpf_map_lookup_elem(&sock_bytes, &one);
	if (start && end) {
		bpf_printk("sk_msg2: pull(%i:%i)\n",
			   start ? *start : 0, end ? *end : 0);
		err = bpf_msg_pull_data(msg, *start, *end, 0);
		if (err)
			bpf_printk("sk_msg2: pull_data err %i\n",
				   err);
		len2 = (__u64)msg->data_end - (__u64)msg->data;
		bpf_printk("sk_msg2: length update %i->%i\n",
			   len1, len2);
	}

	start_push = bpf_map_lookup_elem(&sock_bytes, &two);
	end_push = bpf_map_lookup_elem(&sock_bytes, &three);
	if (start_push && end_push) {
		bpf_printk("sk_msg4: push(%i:%i)\n",
			   start_push ? *start_push : 0,
			   end_push ? *end_push : 0);
		err = bpf_msg_push_data(msg, *start_push, *end_push, 0);
		if (err)
			bpf_printk("sk_msg4: push_data err %i\n",
				   err);
		len3 = (__u64)msg->data_end - (__u64)msg->data;
		bpf_printk("sk_msg4: length push_update %i->%i\n",
			   len2 ? len2 : len1, len3);
	}

	f = bpf_map_lookup_elem(&sock_redir_flags, &zero);
	if (f && *f) {
		key = 2;
		flags = *f;
	}
	bpf_printk("sk_msg3: redirect(%iB) flags=%i err=%i\n",
		   len1, flags, err1 ? err1 : err2);
#ifdef SOCKMAP
	err = bpf_msg_redirect_map(msg, &sock_map_redir, key, flags);
#else
	err = bpf_msg_redirect_hash(msg, &sock_map_redir, &key, flags);
#endif
	bpf_printk("sk_msg3: err %i\n", err);
	return err;
}

SEC("sk_msg5")
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
	return SK_PASS;
}
SEC("sk_msg6")
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

SEC("sk_msg7")
int bpf_prog10(struct sk_msg_md *msg)
{
	int *bytes, *start, *end, *start_push, *end_push;
	int zero = 0, one = 1, two = 2, three = 3;

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
	if (start_push && end_push)
		bpf_msg_push_data(msg, *start_push, *end_push, 0);

	return SK_DROP;
}

int _version SEC("version") = 1;
char _license[] SEC("license") = "GPL";
