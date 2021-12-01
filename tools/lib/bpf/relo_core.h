/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2019 Facebook */

#ifndef __RELO_CORE_H
#define __RELO_CORE_H

#include <linux/bpf.h>

struct bpf_core_cand {
	const struct btf *btf;
	const struct btf_type *t;
	const char *name;
	__u32 id;
};

/* dynamically sized list of type IDs and its associated struct btf */
struct bpf_core_cand_list {
	struct bpf_core_cand *cands;
	int len;
};

int bpf_core_apply_relo_insn(const char *prog_name,
			     struct bpf_insn *insn, int insn_idx,
			     const struct bpf_core_relo *relo, int relo_idx,
			     const struct btf *local_btf,
			     struct bpf_core_cand_list *cands);
int bpf_core_types_are_compat(const struct btf *local_btf, __u32 local_id,
			      const struct btf *targ_btf, __u32 targ_id);

size_t bpf_core_essential_name_len(const char *name);
#endif
