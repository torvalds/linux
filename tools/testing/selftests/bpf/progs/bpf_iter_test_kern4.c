// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

__u32 map1_id = 0, map2_id = 0;
__u32 map1_accessed = 0, map2_accessed = 0;
__u64 map1_seqnum = 0, map2_seqnum1 = 0, map2_seqnum2 = 0;

volatile const __u32 print_len;
volatile const __u32 ret1;

SEC("iter/bpf_map")
int dump_bpf_map(struct bpf_iter__bpf_map *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct bpf_map *map = ctx->map;
	__u64 seq_num;
	int i, ret = 0;

	if (map == (void *)0)
		return 0;

	/* only dump map1_id and map2_id */
	if (map->id != map1_id && map->id != map2_id)
		return 0;

	seq_num = ctx->meta->seq_num;
	if (map->id == map1_id) {
		map1_seqnum = seq_num;
		map1_accessed++;
	}

	if (map->id == map2_id) {
		if (map2_accessed == 0) {
			map2_seqnum1 = seq_num;
			if (ret1)
				ret = 1;
		} else {
			map2_seqnum2 = seq_num;
		}
		map2_accessed++;
	}

	/* fill seq_file buffer */
	for (i = 0; i < (int)print_len; i++)
		bpf_seq_write(seq, &seq_num, sizeof(seq_num));

	return ret;
}
