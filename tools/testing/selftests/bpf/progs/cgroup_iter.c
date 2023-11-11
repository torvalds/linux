// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Google */

#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";
int terminate_early = 0;
u64 terminal_cgroup = 0;

static inline u64 cgroup_id(struct cgroup *cgrp)
{
	return cgrp->kn->id;
}

SEC("iter/cgroup")
int cgroup_id_printer(struct bpf_iter__cgroup *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct cgroup *cgrp = ctx->cgroup;

	/* epilogue */
	if (cgrp == NULL) {
		BPF_SEQ_PRINTF(seq, "epilogue\n");
		return 0;
	}

	/* prologue */
	if (ctx->meta->seq_num == 0)
		BPF_SEQ_PRINTF(seq, "prologue\n");

	BPF_SEQ_PRINTF(seq, "%8llu\n", cgroup_id(cgrp));

	if (terminal_cgroup == cgroup_id(cgrp))
		return 1;

	return terminate_early ? 1 : 0;
}
