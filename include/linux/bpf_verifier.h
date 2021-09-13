/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 */
#ifndef _LINUX_BPF_VERIFIER_H
#define _LINUX_BPF_VERIFIER_H 1

#include <linux/bpf.h> /* for enum bpf_reg_type */
#include <linux/btf.h> /* for struct btf and btf_id() */
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
	REG_LIVE_READ32 = 0x1, /* reg was read, so we're sensitive to initial value */
	REG_LIVE_READ64 = 0x2, /* likewise, but full 64-bit content matters */
	REG_LIVE_READ = REG_LIVE_READ32 | REG_LIVE_READ64,
	REG_LIVE_WRITTEN = 0x4, /* reg was written first, screening off later reads */
	REG_LIVE_DONE = 0x8, /* liveness won't be updating this register anymore */
};

struct bpf_reg_state {
	/* Ordering of fields matters.  See states_equal() */
	enum bpf_reg_type type;
	/* Fixed part of pointer offset, pointer types only */
	s32 off;
	union {
		/* valid when type == PTR_TO_PACKET */
		int range;

		/* valid when type == CONST_PTR_TO_MAP | PTR_TO_MAP_VALUE |
		 *   PTR_TO_MAP_VALUE_OR_NULL
		 */
		struct {
			struct bpf_map *map_ptr;
			/* To distinguish map lookups from outer map
			 * the map_uid is non-zero for registers
			 * pointing to inner maps.
			 */
			u32 map_uid;
		};

		/* for PTR_TO_BTF_ID */
		struct {
			struct btf *btf;
			u32 btf_id;
		};

		u32 mem_size; /* for PTR_TO_MEM | PTR_TO_MEM_OR_NULL */

		/* Max size from any of the above. */
		struct {
			unsigned long raw1;
			unsigned long raw2;
		} raw;

		u32 subprogno; /* for PTR_TO_FUNC */
	};
	/* For PTR_TO_PACKET, used to find other pointers with the same variable
	 * offset, so they can share range knowledge.
	 * For PTR_TO_MAP_VALUE_OR_NULL this is used to share which map value we
	 * came from, when one is tested for != NULL.
	 * For PTR_TO_MEM_OR_NULL this is used to identify memory allocation
	 * for the purpose of tracking that it's freed.
	 * For PTR_TO_SOCKET this is used to share which pointers retain the
	 * same reference to the socket, to determine proper reference freeing.
	 */
	u32 id;
	/* PTR_TO_SOCKET and PTR_TO_TCP_SOCK could be a ptr returned
	 * from a pointer-cast helper, bpf_sk_fullsock() and
	 * bpf_tcp_sock().
	 *
	 * Consider the following where "sk" is a reference counted
	 * pointer returned from "sk = bpf_sk_lookup_tcp();":
	 *
	 * 1: sk = bpf_sk_lookup_tcp();
	 * 2: if (!sk) { return 0; }
	 * 3: fullsock = bpf_sk_fullsock(sk);
	 * 4: if (!fullsock) { bpf_sk_release(sk); return 0; }
	 * 5: tp = bpf_tcp_sock(fullsock);
	 * 6: if (!tp) { bpf_sk_release(sk); return 0; }
	 * 7: bpf_sk_release(sk);
	 * 8: snd_cwnd = tp->snd_cwnd;  // verifier will complain
	 *
	 * After bpf_sk_release(sk) at line 7, both "fullsock" ptr and
	 * "tp" ptr should be invalidated also.  In order to do that,
	 * the reg holding "fullsock" and "sk" need to remember
	 * the original refcounted ptr id (i.e. sk_reg->id) in ref_obj_id
	 * such that the verifier can reset all regs which have
	 * ref_obj_id matching the sk_reg->id.
	 *
	 * sk_reg->ref_obj_id is set to sk_reg->id at line 1.
	 * sk_reg->id will stay as NULL-marking purpose only.
	 * After NULL-marking is done, sk_reg->id can be reset to 0.
	 *
	 * After "fullsock = bpf_sk_fullsock(sk);" at line 3,
	 * fullsock_reg->ref_obj_id is set to sk_reg->ref_obj_id.
	 *
	 * After "tp = bpf_tcp_sock(fullsock);" at line 5,
	 * tp_reg->ref_obj_id is set to fullsock_reg->ref_obj_id
	 * which is the same as sk_reg->ref_obj_id.
	 *
	 * From the verifier perspective, if sk, fullsock and tp
	 * are not NULL, they are the same ptr with different
	 * reg->type.  In particular, bpf_sk_release(tp) is also
	 * allowed and has the same effect as bpf_sk_release(sk).
	 */
	u32 ref_obj_id;
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
	s32 s32_min_value; /* minimum possible (s32)value */
	s32 s32_max_value; /* maximum possible (s32)value */
	u32 u32_min_value; /* minimum possible (u32)value */
	u32 u32_max_value; /* maximum possible (u32)value */
	/* parentage chain for liveness checking */
	struct bpf_reg_state *parent;
	/* Inside the callee two registers can be both PTR_TO_STACK like
	 * R1=fp-8 and R2=fp-8, but one of them points to this function stack
	 * while another to the caller's stack. To differentiate them 'frameno'
	 * is used which is an index in bpf_verifier_state->frame[] array
	 * pointing to bpf_func_state.
	 */
	u32 frameno;
	/* Tracks subreg definition. The stored value is the insn_idx of the
	 * writing insn. This is safe because subreg_def is used before any insn
	 * patching which only happens after main verification finished.
	 */
	s32 subreg_def;
	enum bpf_reg_liveness live;
	/* if (!precise && SCALAR_VALUE) min/max/tnum don't affect safety */
	bool precise;
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

struct bpf_reference_state {
	/* Track each reference created with a unique id, even if the same
	 * instruction creates the reference multiple times (eg, via CALL).
	 */
	int id;
	/* Instruction where the allocation of this reference occurred. This
	 * is used purely to inform the user of a reference leak.
	 */
	int insn_idx;
};

/* state of the program:
 * type of all registers and stack info
 */
struct bpf_func_state {
	struct bpf_reg_state regs[MAX_BPF_REG];
	/* index of call instruction that called into this func */
	int callsite;
	/* stack frame number of this function state from pov of
	 * enclosing bpf_verifier_state.
	 * 0 = main function, 1 = first callee.
	 */
	u32 frameno;
	/* subprog number == index within subprog_info
	 * zero == main subprog
	 */
	u32 subprogno;
	/* Every bpf_timer_start will increment async_entry_cnt.
	 * It's used to distinguish:
	 * void foo(void) { for(;;); }
	 * void foo(void) { bpf_timer_set_callback(,foo); }
	 */
	u32 async_entry_cnt;
	bool in_callback_fn;
	bool in_async_callback_fn;

	/* The following fields should be last. See copy_func_state() */
	int acquired_refs;
	struct bpf_reference_state *refs;
	int allocated_stack;
	struct bpf_stack_state *stack;
};

struct bpf_idx_pair {
	u32 prev_idx;
	u32 idx;
};

struct bpf_id_pair {
	u32 old;
	u32 cur;
};

/* Maximum number of register states that can exist at once */
#define BPF_ID_MAP_SIZE (MAX_BPF_REG + MAX_BPF_STACK / BPF_REG_SIZE)
#define MAX_CALL_FRAMES 8
struct bpf_verifier_state {
	/* call stack tracking */
	struct bpf_func_state *frame[MAX_CALL_FRAMES];
	struct bpf_verifier_state *parent;
	/*
	 * 'branches' field is the number of branches left to explore:
	 * 0 - all possible paths from this state reached bpf_exit or
	 * were safely pruned
	 * 1 - at least one path is being explored.
	 * This state hasn't reached bpf_exit
	 * 2 - at least two paths are being explored.
	 * This state is an immediate parent of two children.
	 * One is fallthrough branch with branches==1 and another
	 * state is pushed into stack (to be explored later) also with
	 * branches==1. The parent of this state has branches==1.
	 * The verifier state tree connected via 'parent' pointer looks like:
	 * 1
	 * 1
	 * 2 -> 1 (first 'if' pushed into stack)
	 * 1
	 * 2 -> 1 (second 'if' pushed into stack)
	 * 1
	 * 1
	 * 1 bpf_exit.
	 *
	 * Once do_check() reaches bpf_exit, it calls update_branch_counts()
	 * and the verifier state tree will look:
	 * 1
	 * 1
	 * 2 -> 1 (first 'if' pushed into stack)
	 * 1
	 * 1 -> 1 (second 'if' pushed into stack)
	 * 0
	 * 0
	 * 0 bpf_exit.
	 * After pop_stack() the do_check() will resume at second 'if'.
	 *
	 * If is_state_visited() sees a state with branches > 0 it means
	 * there is a loop. If such state is exactly equal to the current state
	 * it's an infinite loop. Note states_equal() checks for states
	 * equvalency, so two states being 'states_equal' does not mean
	 * infinite loop. The exact comparison is provided by
	 * states_maybe_looping() function. It's a stronger pre-check and
	 * much faster than states_equal().
	 *
	 * This algorithm may not find all possible infinite loops or
	 * loop iteration count may be too high.
	 * In such cases BPF_COMPLEXITY_LIMIT_INSNS limit kicks in.
	 */
	u32 branches;
	u32 insn_idx;
	u32 curframe;
	u32 active_spin_lock;
	bool speculative;

	/* first and last insn idx of this verifier state */
	u32 first_insn_idx;
	u32 last_insn_idx;
	/* jmp history recorded from first to last.
	 * backtracking is using it to go from last to first.
	 * For most states jmp_history_cnt is [0-3].
	 * For loops can go up to ~40.
	 */
	struct bpf_idx_pair *jmp_history;
	u32 jmp_history_cnt;
};

#define bpf_get_spilled_reg(slot, frame)				\
	(((slot < frame->allocated_stack / BPF_REG_SIZE) &&		\
	  (frame->stack[slot].slot_type[0] == STACK_SPILL))		\
	 ? &frame->stack[slot].spilled_ptr : NULL)

/* Iterate over 'frame', setting 'reg' to either NULL or a spilled register. */
#define bpf_for_each_spilled_reg(iter, frame, reg)			\
	for (iter = 0, reg = bpf_get_spilled_reg(iter, frame);		\
	     iter < frame->allocated_stack / BPF_REG_SIZE;		\
	     iter++, reg = bpf_get_spilled_reg(iter, frame))

/* linked list of verifier states used to prune search */
struct bpf_verifier_state_list {
	struct bpf_verifier_state state;
	struct bpf_verifier_state_list *next;
	int miss_cnt, hit_cnt;
};

/* Possible states for alu_state member. */
#define BPF_ALU_SANITIZE_SRC		(1U << 0)
#define BPF_ALU_SANITIZE_DST		(1U << 1)
#define BPF_ALU_NEG_VALUE		(1U << 2)
#define BPF_ALU_NON_POINTER		(1U << 3)
#define BPF_ALU_IMMEDIATE		(1U << 4)
#define BPF_ALU_SANITIZE		(BPF_ALU_SANITIZE_SRC | \
					 BPF_ALU_SANITIZE_DST)

struct bpf_insn_aux_data {
	union {
		enum bpf_reg_type ptr_type;	/* pointer type for load/store insns */
		unsigned long map_ptr_state;	/* pointer/poison value for maps */
		s32 call_imm;			/* saved imm field of call insn */
		u32 alu_limit;			/* limit for add/sub register with pointer */
		struct {
			u32 map_index;		/* index into used_maps[] */
			u32 map_off;		/* offset from value base address */
		};
		struct {
			enum bpf_reg_type reg_type;	/* type of pseudo_btf_id */
			union {
				struct {
					struct btf *btf;
					u32 btf_id;	/* btf_id for struct typed var */
				};
				u32 mem_size;	/* mem_size for non-struct typed var */
			};
		} btf_var;
	};
	u64 map_key_state; /* constant (32 bit) key tracking for maps */
	int ctx_field_size; /* the ctx field size for load insn, maybe 0 */
	u32 seen; /* this insn was processed by the verifier at env->pass_cnt */
	bool sanitize_stack_spill; /* subject to Spectre v4 sanitation */
	bool zext_dst; /* this insn zero extends dst reg */
	u8 alu_state; /* used in combination with alu_limit */

	/* below fields are initialized once */
	unsigned int orig_idx; /* original instruction index */
	bool prune_point;
};

#define MAX_USED_MAPS 64 /* max number of maps accessed by one eBPF program */
#define MAX_USED_BTFS 64 /* max number of BTFs accessed by one BPF program */

#define BPF_VERIFIER_TMP_LOG_SIZE	1024

struct bpf_verifier_log {
	u32 level;
	char kbuf[BPF_VERIFIER_TMP_LOG_SIZE];
	char __user *ubuf;
	u32 len_used;
	u32 len_total;
};

static inline bool bpf_verifier_log_full(const struct bpf_verifier_log *log)
{
	return log->len_used >= log->len_total - 1;
}

#define BPF_LOG_LEVEL1	1
#define BPF_LOG_LEVEL2	2
#define BPF_LOG_STATS	4
#define BPF_LOG_LEVEL	(BPF_LOG_LEVEL1 | BPF_LOG_LEVEL2)
#define BPF_LOG_MASK	(BPF_LOG_LEVEL | BPF_LOG_STATS)
#define BPF_LOG_KERNEL	(BPF_LOG_MASK + 1) /* kernel internal flag */

static inline bool bpf_verifier_log_needed(const struct bpf_verifier_log *log)
{
	return log &&
		((log->level && log->ubuf && !bpf_verifier_log_full(log)) ||
		 log->level == BPF_LOG_KERNEL);
}

#define BPF_MAX_SUBPROGS 256

struct bpf_subprog_info {
	/* 'start' has to be the first field otherwise find_subprog() won't work */
	u32 start; /* insn idx of function entry point */
	u32 linfo_idx; /* The idx to the main_prog->aux->linfo */
	u16 stack_depth; /* max. stack depth used by this function */
	bool has_tail_call;
	bool tail_call_reachable;
	bool has_ld_abs;
	bool is_async_cb;
};

/* single container for all structs
 * one verifier_env per bpf_check() call
 */
struct bpf_verifier_env {
	u32 insn_idx;
	u32 prev_insn_idx;
	struct bpf_prog *prog;		/* eBPF program being verified */
	const struct bpf_verifier_ops *ops;
	struct bpf_verifier_stack_elem *head; /* stack of verifier states to be processed */
	int stack_size;			/* number of states to be processed */
	bool strict_alignment;		/* perform strict pointer alignment checks */
	bool test_state_freq;		/* test verifier with different pruning frequency */
	struct bpf_verifier_state *cur_state; /* current verifier state */
	struct bpf_verifier_state_list **explored_states; /* search pruning optimization */
	struct bpf_verifier_state_list *free_list;
	struct bpf_map *used_maps[MAX_USED_MAPS]; /* array of map's used by eBPF program */
	struct btf_mod_pair used_btfs[MAX_USED_BTFS]; /* array of BTF's used by BPF program */
	u32 used_map_cnt;		/* number of used maps */
	u32 used_btf_cnt;		/* number of used BTF objects */
	u32 id_gen;			/* used to generate unique reg IDs */
	bool explore_alu_limits;
	bool allow_ptr_leaks;
	bool allow_uninit_stack;
	bool allow_ptr_to_map_access;
	bool bpf_capable;
	bool bypass_spec_v1;
	bool bypass_spec_v4;
	bool seen_direct_write;
	struct bpf_insn_aux_data *insn_aux_data; /* array of per-insn state */
	const struct bpf_line_info *prev_linfo;
	struct bpf_verifier_log log;
	struct bpf_subprog_info subprog_info[BPF_MAX_SUBPROGS + 1];
	struct bpf_id_pair idmap_scratch[BPF_ID_MAP_SIZE];
	struct {
		int *insn_state;
		int *insn_stack;
		int cur_stack;
	} cfg;
	u32 pass_cnt; /* number of times do_check() was called */
	u32 subprog_cnt;
	/* number of instructions analyzed by the verifier */
	u32 prev_insn_processed, insn_processed;
	/* number of jmps, calls, exits analyzed so far */
	u32 prev_jmps_processed, jmps_processed;
	/* total verification time */
	u64 verification_time;
	/* maximum number of verifier states kept in 'branching' instructions */
	u32 max_states_per_insn;
	/* total number of allocated verifier states */
	u32 total_states;
	/* some states are freed during program analysis.
	 * this is peak number of states. this number dominates kernel
	 * memory consumption during verification
	 */
	u32 peak_states;
	/* longest register parentage chain walked for liveness marking */
	u32 longest_mark_read_walk;
	bpfptr_t fd_array;
};

__printf(2, 0) void bpf_verifier_vlog(struct bpf_verifier_log *log,
				      const char *fmt, va_list args);
__printf(2, 3) void bpf_verifier_log_write(struct bpf_verifier_env *env,
					   const char *fmt, ...);
__printf(2, 3) void bpf_log(struct bpf_verifier_log *log,
			    const char *fmt, ...);

static inline struct bpf_func_state *cur_func(struct bpf_verifier_env *env)
{
	struct bpf_verifier_state *cur = env->cur_state;

	return cur->frame[cur->curframe];
}

static inline struct bpf_reg_state *cur_regs(struct bpf_verifier_env *env)
{
	return cur_func(env)->regs;
}

int bpf_prog_offload_verifier_prep(struct bpf_prog *prog);
int bpf_prog_offload_verify_insn(struct bpf_verifier_env *env,
				 int insn_idx, int prev_insn_idx);
int bpf_prog_offload_finalize(struct bpf_verifier_env *env);
void
bpf_prog_offload_replace_insn(struct bpf_verifier_env *env, u32 off,
			      struct bpf_insn *insn);
void
bpf_prog_offload_remove_insns(struct bpf_verifier_env *env, u32 off, u32 cnt);

int check_ctx_reg(struct bpf_verifier_env *env,
		  const struct bpf_reg_state *reg, int regno);
int check_mem_reg(struct bpf_verifier_env *env, struct bpf_reg_state *reg,
		   u32 regno, u32 mem_size);

/* this lives here instead of in bpf.h because it needs to dereference tgt_prog */
static inline u64 bpf_trampoline_compute_key(const struct bpf_prog *tgt_prog,
					     struct btf *btf, u32 btf_id)
{
	if (tgt_prog)
		return ((u64)tgt_prog->aux->id << 32) | btf_id;
	else
		return ((u64)btf_obj_id(btf) << 32) | 0x80000000 | btf_id;
}

/* unpack the IDs from the key as constructed above */
static inline void bpf_trampoline_unpack_key(u64 key, u32 *obj_id, u32 *btf_id)
{
	if (obj_id)
		*obj_id = key >> 32;
	if (btf_id)
		*btf_id = key & 0x7FFFFFFF;
}

int bpf_check_attach_target(struct bpf_verifier_log *log,
			    const struct bpf_prog *prog,
			    const struct bpf_prog *tgt_prog,
			    u32 btf_id,
			    struct bpf_attach_target_info *tgt_info);

#endif /* _LINUX_BPF_VERIFIER_H */
