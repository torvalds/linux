/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Facebook */
#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";
int count = 0;

SEC("iter/task")
int dump_task(struct bpf_iter__task *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	char c;

	if (count < 4) {
		c = START_CHAR + count;
		bpf_seq_write(seq, &c, sizeof(c));
		count++;
	}

	return 0;
}
