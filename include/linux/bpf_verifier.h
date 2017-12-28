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
#include <linux/tnum.h>

/* Maximum variable offset umax_value permitted when resolving memory accesses.
 * In practice this is far bigger than any realistic pointer offset; this limit
 * ensures that umax_value + (int)off + (int)size cannot overflow a u64.
 */
#define BPF_MAX_VAR_OFF	(1 << 29)
/* Maximum variable size permitted for ARG_CONST_SIZE[_OR_ZERO].  This ensures
 * that converting umax_value to int cannot overflow.
 */
#define BPF_MAX_VAR_SIZ	(1 << 29)

/* Liveness marks, used for registers and spilled-regs (in stack slots).
 * Read marks propagate upwards until they find a write mark; they record that
 * "one of this state's descendants read this reg" (and therefore the reg is
 * relevant for states_equal() checks).
 * Write marks collect downwards and do not propagate; they record that "the
 * straight-line code that reached this state (from its parent) wrote this reg"
 * (and therefore that reads propagated from this state or its descendants
 * should not propagate to its parent).
 * A state with a write mark can receive read marks; it just won't propagate
 * them to its parent, since the write mark is a property, not of the state,
 * but of the link between it and its parent.  See mark_reg_read() and
 * mark_stack_slot_read() in kernel/bpf/verifier.c.
 */
enum bpf_reg_liveness {
	REG_LIVE_NONE = 0, /* reg hasn't been read or written this branch */
	REG_LIVE_READ, /* reg was read, so we're sensitive to initial value */
	REG_LIVE_WRITTEN, /* reg was written first, screening off later reads */
};

struct bpf_reg_state {
	enum bpf_reg_type type;
	union {
		/* valid when type == PTR_TO_PACKET */
		u16 range;

		/* valid when type == CONST_PTR_TO_MAP | PTR_TO_MAP_VALUE |
		 *   PTR_TO_MAP_VALUE_OR_NULL
		 */
		struct bpf_map *map_ptr;
	};
	/* Fixed part of pointer offset, pointer types only */
	s32 off;
	/* For PTR_TO_PACKET, used to find other pointers with the same variable
	 * offset, so they can share range knowledge.
	 * For PTR_TO_MAP_VALUE_OR_NULL this is used to share which map value we
	 * came from, when one is tested for != NULL.
	 */
	u32 id;
	/* Ordering of fields matters.  See states_equal() */
	/* For scalar types (SCALAR_VALUE), this represents our knowledge of
	 * the actual value.
	 * For pointer types, this represents the variable part of the offset
	 * from the pointed-to object, and is shared with all bpf_reg_states
	 * with the same id as us.
	 */
	struct tnum var_off;
	/* Used to determine if any memory access using this register will
	 * result in a bad access.
	 * These refer to the same value as var_off, not necessarily the actual
	 * contents of the register.
	 */
	s64 smin_value; /* minimum possible (s64)value */
	s64 smax_value; /* maximum possible (s64)value */
	u64 umin_value; /* minimum possible (u64)value */
	u64 umax_value; /* maximum possible (u64)value */
	/* Inside the callee two registers can be both PTR_TO_STACK like
	 * R1=fp-8 and R2=fp-8, but one of them points to this function stack
	 * while another to the caller's stack. To differentiate them 'frameno'
	 * is used which is an index in bpf_verifier_state->frame[] array
	 * pointing to bpf_func_state.
	 * This field must be second to last, for states_equal() reasons.
	 */
	u32 frameno;
	/* This field must be last, for states_equal() reasons. */
	enum bpf_reg_liveness live;
};

enum bpf_stack_slot_type {
	STACK_INVALID,    /* nothing was stored in this stack slot */
	STACK_SPILL,      /* register spilled into stack */
	STACK_MISC,	  /* BPF program wrote some data into this slot */
	STACK_ZERO,	  /* BPF program wrote constant zero */
};

#define BPF_REG_SIZE 8	/* size of eBPF register in bytes */

struct bpf_stack_state {
	struct bpf_reg_state spilled_ptr;
	u8 slot_type[BPF_REG_SIZE];
};

/* state of the program:
 * type of all registers and stack info
 */
struct bpf_func_state {
	struct bpf_reg_state regs[MAX_BPF_REG];
	struct bpf_verifier_state *parent;
	/* index of call instruction that called into this func */
	int callsite;
	/* stack frame number of this function state from pov of
	 * enclosing bpf_verifier_state.
	 * 0 = main function, 1 = first callee.
	 */
	u32 frameno;
	/* subprog number == index within subprog_stack_depth
	 * zero == main subprog
	 */
	u32 subprogno;

	/* should be second to last. See copy_func_state() */
	int allocated_stack;
	struct bpf_stack_state *stack;
};

#define MAX_CALL_FRAMES 8
struct bpf_verifier_state {
	/* call stack tracking */
	struct bpf_func_state *frame[MAX_CALL_FRAMES];
	struct bpf_verifier_state *parent;
	u32 curframe;
};

/* linked list of verifier states used to prune search */
struct bpf_verifier_state_list {
	struct bpf_verifier_state state;
	struct bpf_verifier_state_list *next;
};

struct bpf_insn_aux_data {
	union {
		enum bpf_reg_type ptr_type;	/* pointer type for load/store insns */
		struct bpf_map *map_ptr;	/* pointer for call insn into lookup_elem */
		s32 call_imm;			/* saved imm field of call insn */
	};
	int ctx_field_size; /* the ctx field size for load insn, maybe 0 */
	bool seen; /* this insn was processed by the verifier */
};

#define MAX_USED_MAPS 64 /* max number of maps accessed by one eBPF program */

#define BPF_VERIFIER_TMP_LOG_SIZE	1024

struct bpf_verifer_log {
	u32 level;
	char kbuf[BPF_VERIFIER_TMP_LOG_SIZE];
	char __user *ubuf;
	u32 len_used;
	u32 len_total;
};

static inline bool bpf_verifier_log_full(const struct bpf_verifer_log *log)
{
	return log->len_used >= log->len_total - 1;
}

struct bpf_verifier_env;
struct bpf_ext_analyzer_ops {
	int (*insn_hook)(struct bpf_verifier_env *env,
			 int insn_idx, int prev_insn_idx);
};

#define BPF_MAX_SUBPROGS 256

/* single container for all structs
 * one verifier_env per bpf_check() call
 */
struct bpf_verifier_env {
	struct bpf_prog *prog;		/* eBPF program being verified */
	const struct bpf_verifier_ops *ops;
	struct bpf_verifier_stack_elem *head; /* stack of verifier states to be processed */
	int stack_size;			/* number of states to be processed */
	bool strict_alignment;		/* perform strict pointer alignment checks */
	struct bpf_verifier_state *cur_state; /* current verifier state */
	struct bpf_verifier_state_list **explored_states; /* search pruning optimization */
	const struct bpf_ext_analyzer_ops *dev_ops; /* device analyzer ops */
	struct bpf_map *used_maps[MAX_USED_MAPS]; /* array of map's used by eBPF program */
	u32 used_map_cnt;		/* number of used maps */
	u32 id_gen;			/* used to generate unique reg IDs */
	bool allow_ptr_leaks;
	bool seen_direct_write;
	struct bpf_insn_aux_data *insn_aux_data; /* array of per-insn state */
	struct bpf_verifer_log log;
	u32 subprog_starts[BPF_MAX_SUBPROGS];
	/* computes the stack depth of each bpf function */
	u16 subprog_stack_depth[BPF_MAX_SUBPROGS + 1];
	u32 subprog_cnt;
};

static inline struct bpf_reg_state *cur_regs(struct bpf_verifier_env *env)
{
	struct bpf_verifier_state *cur = env->cur_state;

	return cur->frame[cur->curframe]->regs;
}

#if defined(CONFIG_NET) && defined(CONFIG_BPF_SYSCALL)
int bpf_prog_offload_verifier_prep(struct bpf_verifier_env *env);
#else
static inline int bpf_prog_offload_verifier_prep(struct bpf_verifier_env *env)
{
	return -EOPNOTSUPP;
}
#endif

#endif /* _LINUX_BPF_VERIFIER_H */
