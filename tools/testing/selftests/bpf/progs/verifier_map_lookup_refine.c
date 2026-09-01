// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_kfuncs.h"

char _license[] SEC("license") = "GPL";

struct inner_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} inner_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1);
	__type(key, int);
	__array(values, struct inner_map);
} outer_map SEC(".maps") = {
	.values = { [0] = &inner_map },
};

SEC("?tc")
__failure __msg("type=map_ptr_or_null expected=fp")
int mapofmaps_value_as_kfunc_mem_buf(struct __sk_buff *skb)
{
	struct bpf_dynptr dptr;
	__u32 key = 0;
	void *inner;
	char *p;

	inner = bpf_map_lookup_elem(&outer_map, &key);
	/* intentionally NOT NULL-checked: type is map_ptr_or_null */

	bpf_dynptr_from_skb(skb, 0, &dptr);
	/* arg3 is mem+size */
	p = bpf_dynptr_slice(&dptr, 0, inner, 4);
	if (p)
		return p[0];
	return 0;
}

SEC("?tc")
__failure __msg("type=map_ptr_or_null expected=fp")
int mapofmaps_value_as_helper_mem_buf(struct __sk_buff *skb)
{
	__u32 key = 0;
	void *inner;

	inner = bpf_map_lookup_elem(&outer_map, &key);
	/* intentionally NOT NULL-checked: type is map_ptr_or_null */

	/* arg1 is mem+size */
	return bpf_csum_diff(inner, 4, NULL, 0, 0) + skb->len;
}

SEC("?tc")
__failure __msg("type=map_ptr_or_null expected=fp")
int mapofmaps_value_as_helper_fixed_mem(struct __sk_buff *skb)
{
	char th[sizeof(struct tcphdr)] = {};
	__u32 key = 0;
	void *inner;

	inner = bpf_map_lookup_elem(&outer_map, &key);
	/* intentionally NOT NULL-checked: type is map_ptr_or_null */

	/* arg1 is fixed-sized mem */
	return bpf_tcp_raw_check_syncookie_ipv4(inner, (void *)th);
}
