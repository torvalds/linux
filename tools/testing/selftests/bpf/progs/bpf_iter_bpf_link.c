// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Red Hat, Inc. */
#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

SEC("iter/bpf_link")
int dump_bpf_link(struct bpf_iter__bpf_link *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct bpf_link *link = ctx->link;
	int link_id;

	if (!link)
		return 0;

	link_id = link->id;
	bpf_seq_write(seq, &link_id, sizeof(link_id));
	return 0;
}
