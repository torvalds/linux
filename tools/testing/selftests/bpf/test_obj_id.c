/* Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <stddef.h>
#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include "bpf_helpers.h"

/* It is a dumb bpf program such that it must have no
 * issue to be loaded since testing the verifier is
 * not the focus here.
 */

int _version SEC("version") = 1;

struct bpf_map_def SEC("maps") test_map_id = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(__u64),
	.max_entries = 1,
};

SEC("test_obj_id_dummy")
int test_obj_id(struct __sk_buff *skb)
{
	__u32 key = 0;
	__u64 *value;

	value = bpf_map_lookup_elem(&test_map_id, &key);

	return TC_ACT_OK;
}
