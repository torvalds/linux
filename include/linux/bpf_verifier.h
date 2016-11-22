/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#ifndef _LINUX_BPF_VERIFIER_H
#define _LINUX_BPF_VERIFIER_H 1

#include <linux/bpf.h> /* for enum bpf_reg_type */
#include <linux/filter.h> /* for MAX_BPF_STACK */

 /* Just some arbitrary values so we can safely do math without overflowing and
  * are obviously wrong for any sort of memory access.
  */
#define BPF_REGISTER_MAX_RANGE (1024 * 1024 * 1024)
#define BPF_REGISTER_MIN_RANGE -1

struct bpf_reg_state {
	enum bpf_reg_type type;
	/*
	 * Used to determine if any memory access using this register will
	 * result in a bad access.
	 */
	s64 min_value;
	u64 max_value;
	u32 id;
	union {
		/* valid when type == CONST_IMM | PTR_TO_STACK | UNKNOWN_VALUE */
		s64 imm;

		/* valid when type == PTR_TO_PACKET* */
		struct {
			u16 off;
			u16 range;
		};

		/* valid when type == CONST_PTR_TO_MAP | PTR_TO_MAP_VALUE |
		 *   PTR_TO_MAP_VALUE_OR_NULL
		 */
		struct bpf_map *map_ptr;
	};
};

enum bpf_stack_slot_type {
	STACK_INVALID,    /* nothing was stored in this stack slot */
	STACK_SPILL,      /* register spilled into stack */
	STACK_MISC	  /* BPF program wrote some data into this slot */
};

#define BPF_REG_SIZE 8	/* size of eBPF register in bytes */

/* state of the program:
 * type of all registers and stack info
 */
struct bpf_verifier_state {
	struct bpf_reg_state regs[MAX_BPF_REG];
	u8 stack_slot_type[MAX_BPF_STACK];
	struct bpf_reg_state spilled_regs[MAX_BPF_STACK / BPF_REG_SIZE];
};

/* linked list of verifier states used to prune search */
struct bpf_verifier_state_list {
	struct bpf_verifier_state state;
	struct bpf_verifier_state_list *next;
};

struct bpf_insn_aux_data {
	enum bpf_reg_type ptr_type;	/* pointer type for load/store insns */
};

#define MAX_USED_MAPS 64 /* max number of maps accessed by one eBPF program */

struct bpf_verifier_env;
struct bpf_ext_analyzer_ops {
	int (*insn_hook)(struct bpf_verifier_env *env,
			 int insn_idx, int prev_insn_idx);
};

/* single container for all structs
 * one verifier_env per bpf_check() call
 */
struct bpf_verifier_env {
	struct bpf_prog *prog;		/* eBPF program being verified */
	struct bpf_verifier_stack_elem *head; /* stack of verifier states to be processed */
	int stack_size;			/* number of states to be processed */
	struct bpf_verifier_state cur_state; /* current verifier state */
	struct bpf_verifier_state_list **explored_states; /* search pruning optimization */
	const struct bpf_ext_analyzer_ops *analyzer_ops; /* external analyzer ops */
	void *analyzer_priv; /* pointer to external analyzer's private data */
	struct bpf_map *used_maps[MAX_USED_MAPS]; /* array of map's used by eBPF program */
	u32 used_map_cnt;		/* number of used maps */
	u32 id_gen;			/* used to generate unique reg IDs */
	bool allow_ptr_leaks;
	bool seen_direct_write;
	bool varlen_map_value_access;
	struct bpf_insn_aux_data *insn_aux_data; /* array of per-insn state */
};

int bpf_analyzer(struct bpf_prog *prog, const struct bpf_ext_analyzer_ops *ops,
		 void *priv);

#endif /* _LINUX_BPF_VERIFIER_H */
