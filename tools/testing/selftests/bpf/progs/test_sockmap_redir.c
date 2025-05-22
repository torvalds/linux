// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC(".maps") struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} nop_map, sock_map;

SEC(".maps") struct {
	__uint(type, BPF_MAP_TYPE_SOCKHASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} nop_hash, sock_hash;

SEC(".maps") struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 2);
	__type(key, int);
	__type(value, unsigned int);
} verdict_map;

/* Set by user space */
int redirect_type;
int redirect_flags;

#define redirect_map(__data)                                                   \
	_Generic((__data),                                                     \
		 struct __sk_buff * : bpf_sk_redirect_map,                     \
		 struct sk_msg_md * : bpf_msg_redirect_map                     \
	)((__data), &sock_map, (__u32){0}, redirect_flags)

#define redirect_hash(__data)                                                  \
	_Generic((__data),                                                     \
		 struct __sk_buff * : bpf_sk_redirect_hash,                    \
		 struct sk_msg_md * : bpf_msg_redirect_hash                    \
	)((__data), &sock_hash, &(__u32){0}, redirect_flags)

#define DEFINE_PROG(__type, __param)                                           \
SEC("sk_" XSTR(__type))                                                        \
int prog_ ## __type ## _verdict(__param data)                                  \
{                                                                              \
	unsigned int *count;                                                   \
	int verdict;                                                           \
									       \
	if (redirect_type == BPF_MAP_TYPE_SOCKMAP)                             \
		verdict = redirect_map(data);                                  \
	else if (redirect_type == BPF_MAP_TYPE_SOCKHASH)                       \
		verdict = redirect_hash(data);                                 \
	else                                                                   \
		verdict = redirect_type - __MAX_BPF_MAP_TYPE;                  \
									       \
	count = bpf_map_lookup_elem(&verdict_map, &verdict);                   \
	if (count)                                                             \
		(*count)++;                                                    \
									       \
	return verdict;                                                        \
}

DEFINE_PROG(skb, struct __sk_buff *);
DEFINE_PROG(msg, struct sk_msg_md *);

char _license[] SEC("license") = "GPL";
