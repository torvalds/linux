/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2019 Facebook */

#ifndef __RELO_CORE_H
#define __RELO_CORE_H

#include <linux/bpf.h>

struct bpf_core_cand {
	const struct btf *btf;
	__u32 id;
};

/* dynamically sized list of type IDs and its associated struct btf */
struct bpf_core_cand_list {
	struct bpf_core_cand *cands;
	int len;
};

#define BPF_CORE_SPEC_MAX_LEN 64

/* represents BPF CO-RE field or array element accessor */
struct bpf_core_accessor {
	__u32 type_id;		/* struct/union type or array element type */
	__u32 idx;		/* field index or array index */
	const char *name;	/* field name or NULL for array accessor */
};

struct bpf_core_spec {
	const struct btf *btf;
	/* high-level spec: named fields and array indices only */
	struct bpf_core_accessor spec[BPF_CORE_SPEC_MAX_LEN];
	/* original unresolved (no skip_mods_or_typedefs) root type ID */
	__u32 root_type_id;
	/* CO-RE relocation kind */
	enum bpf_core_relo_kind relo_kind;
	/* high-level spec length */
	int len;
	/* raw, low-level spec: 1-to-1 with accessor spec string */
	int raw_spec[BPF_CORE_SPEC_MAX_LEN];
	/* raw spec length */
	int raw_len;
	/* field bit offset represented by spec */
	__u32 bit_offset;
};

struct bpf_core_relo_res {
	/* expected value in the instruction, unless validate == false */
	__u64 orig_val;
	/* new value that needs to be patched up to */
	__u64 new_val;
	/* relocation unsuccessful, poison instruction, but don't fail load */
	bool poison;
	/* some relocations can't be validated against orig_val */
	bool validate;
	/* for field byte offset relocations or the forms:
	 *     *(T *)(rX + <off>) = rY
	 *     rX = *(T *)(rY + <off>),
	 * we remember original and resolved field size to adjust direct
	 * memory loads of pointers and integers; this is necessary for 32-bit
	 * host kernel architectures, but also allows to automatically
	 * relocate fields that were resized from, e.g., u32 to u64, etc.
	 */
	bool fail_memsz_adjust;
	__u32 orig_sz;
	__u32 orig_type_id;
	__u32 new_sz;
	__u32 new_type_id;
};

int __bpf_core_types_are_compat(const struct btf *local_btf, __u32 local_id,
				const struct btf *targ_btf, __u32 targ_id, int level);
int bpf_core_types_are_compat(const struct btf *local_btf, __u32 local_id,
			      const struct btf *targ_btf, __u32 targ_id);
int __bpf_core_types_match(const struct btf *local_btf, __u32 local_id, const struct btf *targ_btf,
			   __u32 targ_id, bool behind_ptr, int level);
int bpf_core_types_match(const struct btf *local_btf, __u32 local_id, const struct btf *targ_btf,
			 __u32 targ_id);

size_t bpf_core_essential_name_len(const char *name);

int bpf_core_calc_relo_insn(const char *prog_name,
			    const struct bpf_core_relo *relo, int relo_idx,
			    const struct btf *local_btf,
			    struct bpf_core_cand_list *cands,
			    struct bpf_core_spec *specs_scratch,
			    struct bpf_core_relo_res *targ_res);

int bpf_core_patch_insn(const char *prog_name, struct bpf_insn *insn,
			int insn_idx, const struct bpf_core_relo *relo,
			int relo_idx, const struct bpf_core_relo_res *res);

int bpf_core_parse_spec(const char *prog_name, const struct btf *btf,
		        const struct bpf_core_relo *relo,
		        struct bpf_core_spec *spec);

int bpf_core_format_spec(char *buf, size_t buf_sz, const struct bpf_core_spec *spec);

#endif
