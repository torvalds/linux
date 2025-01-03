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
/* size of tmp_str_buf in bpf_verifier.
 * we need at least 306 bytes to fit full stack mask representation
 * (in the "-8,-16,...,-512" form)
 */
#define TMP_STR_BUF_LEN 320
/* Patch buffer size */
#define INSN_BUF_SIZE 32

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

#define ITER_PREFIX "bpf_iter_"

enum bpf_iter_state {
	BPF_ITER_STATE_INVALID, /* for non-first slot */
	BPF_ITER_STATE_ACTIVE,
	BPF_ITER_STATE_DRAINED,
};

struct bpf_reg_state {
	/* Ordering of fields matters.  See states_equal() */
	enum bpf_reg_type type;
	/*
	 * Fixed part of pointer offset, pointer types only.
	 * Or constant delta between "linked" scalars with the same ID.
	 */
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

		struct { /* for PTR_TO_MEM | PTR_TO_MEM_OR_NULL */
			u32 mem_size;
			u32 dynptr_id; /* for dynptr slices */
		};

		/* For dynptr stack slots */
		struct {
			enum bpf_dynptr_type type;
			/* A dynptr is 16 bytes so it takes up 2 stack slots.
			 * We need to track which slot is the first slot
			 * to protect against cases where the user may try to
			 * pass in an address starting at the second slot of the
			 * dynptr.
			 */
			bool first_slot;
		} dynptr;

		/* For bpf_iter stack slots */
		struct {
			/* BTF container and BTF type ID describing
			 * struct bpf_iter_<type> of an iterator state
			 */
			struct btf *btf;
			u32 btf_id;
			/* packing following two fields to fit iter state into 16 bytes */
			enum bpf_iter_state state:2;
			int depth:30;
		} iter;

		/* Max size from any of the above. */
		struct {
			unsigned long raw1;
			unsigned long raw2;
		} raw;

		u32 subprogno; /* for PTR_TO_FUNC */
	};
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
	/* For PTR_TO_PACKET, used to find other pointers with the same variable
	 * offset, so they can share range knowledge.
	 * For PTR_TO_MAP_VALUE_OR_NULL this is used to share which map value we
	 * came from, when one is tested for != NULL.
	 * For PTR_TO_MEM_OR_NULL this is used to identify memory allocation
	 * for the purpose of tracking that it's freed.
	 * For PTR_TO_SOCKET this is used to share which pointers retain the
	 * same reference to the socket, to determine proper reference freeing.
	 * For stack slots that are dynptrs, this is used to track references to
	 * the dynptr to determine proper reference freeing.
	 * Similarly to dynptrs, we use ID to track "belonging" of a reference
	 * to a specific instance of bpf_iter.
	 */
	/*
	 * Upper bit of ID is used to remember relationship between "linked"
	 * registers. Example:
	 * r1 = r2;    both will have r1->id == r2->id == N
	 * r1 += 10;   r1->id == N | BPF_ADD_CONST and r1->off == 10
	 */
#define BPF_ADD_CONST (1U << 31)
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
	/* A dynptr is stored in this stack slot. The type of dynptr
	 * is stored in bpf_stack_state->spilled_ptr.dynptr.type
	 */
	STACK_DYNPTR,
	STACK_ITER,
};

#define BPF_REG_SIZE 8	/* size of eBPF register in bytes */

#define BPF_REGMASK_ARGS ((1 << BPF_REG_1) | (1 << BPF_REG_2) | \
			  (1 << BPF_REG_3) | (1 << BPF_REG_4) | \
			  (1 << BPF_REG_5))

#define BPF_DYNPTR_SIZE		sizeof(struct bpf_dynptr_kern)
#define BPF_DYNPTR_NR_SLOTS		(BPF_DYNPTR_SIZE / BPF_REG_SIZE)

struct bpf_stack_state {
	struct bpf_reg_state spilled_ptr;
	u8 slot_type[BPF_REG_SIZE];
};

struct bpf_reference_state {
	/* Each reference object has a type. Ensure REF_TYPE_PTR is zero to
	 * default to pointer reference on zero initialization of a state.
	 */
	enum ref_state_type {
		REF_TYPE_PTR = 0,
		REF_TYPE_LOCK,
	} type;
	/* Track each reference created with a unique id, even if the same
	 * instruction creates the reference multiple times (eg, via CALL).
	 */
	int id;
	/* Instruction where the allocation of this reference occurred. This
	 * is used purely to inform the user of a reference leak.
	 */
	int insn_idx;
	/* Use to keep track of the source object of a lock, to ensure
	 * it matches on unlock.
	 */
	void *ptr;
};

struct bpf_retval_range {
	s32 minval;
	s32 maxval;
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
	struct bpf_retval_range callback_ret_range;
	bool in_callback_fn;
	bool in_async_callback_fn;
	bool in_exception_callback_fn;
	/* For callback calling functions that limit number of possible
	 * callback executions (e.g. bpf_loop) keeps track of current
	 * simulated iteration number.
	 * Value in frame N refers to number of times callback with frame
	 * N+1 was simulated, e.g. for the following call:
	 *
	 *   bpf_loop(..., fn, ...); | suppose current frame is N
	 *                           | fn would be simulated in frame N+1
	 *                           | number of simulations is tracked in frame N
	 */
	u32 callback_depth;

	/* The following fields should be last. See copy_func_state() */
	int acquired_refs;
	int active_locks;
	struct bpf_reference_state *refs;
	/* The state of the stack. Each element of the array describes BPF_REG_SIZE
	 * (i.e. 8) bytes worth of stack memory.
	 * stack[0] represents bytes [*(r10-8)..*(r10-1)]
	 * stack[1] represents bytes [*(r10-16)..*(r10-9)]
	 * ...
	 * stack[allocated_stack/8 - 1] represents [*(r10-allocated_stack)..*(r10-allocated_stack+7)]
	 */
	struct bpf_stack_state *stack;
	/* Size of the current stack, in bytes. The stack state is tracked below, in
	 * `stack`. allocated_stack is always a multiple of BPF_REG_SIZE.
	 */
	int allocated_stack;
};

#define MAX_CALL_FRAMES 8

/* instruction history flags, used in bpf_insn_hist_entry.flags field */
enum {
	/* instruction references stack slot through PTR_TO_STACK register;
	 * we also store stack's frame number in lower 3 bits (MAX_CALL_FRAMES is 8)
	 * and accessed stack slot's index in next 6 bits (MAX_BPF_STACK is 512,
	 * 8 bytes per slot, so slot index (spi) is [0, 63])
	 */
	INSN_F_FRAMENO_MASK = 0x7, /* 3 bits */

	INSN_F_SPI_MASK = 0x3f, /* 6 bits */
	INSN_F_SPI_SHIFT = 3, /* shifted 3 bits to the left */

	INSN_F_STACK_ACCESS = BIT(9), /* we need 10 bits total */
};

static_assert(INSN_F_FRAMENO_MASK + 1 >= MAX_CALL_FRAMES);
static_assert(INSN_F_SPI_MASK + 1 >= MAX_BPF_STACK / 8);

struct bpf_insn_hist_entry {
	u32 idx;
	/* insn idx can't be bigger than 1 million */
	u32 prev_idx : 22;
	/* special flags, e.g., whether insn is doing register stack spill/load */
	u32 flags : 10;
	/* additional registers that need precision tracking when this
	 * jump is backtracked, vector of six 10-bit records
	 */
	u64 linked_regs;
};

/* Maximum number of register states that can exist at once */
#define BPF_ID_MAP_SIZE ((MAX_BPF_REG + MAX_BPF_STACK / BPF_REG_SIZE) * MAX_CALL_FRAMES)
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
	 * equivalency, so two states being 'states_equal' does not mean
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

	bool speculative;
	bool active_rcu_lock;
	u32 active_preempt_lock;
	/* If this state was ever pointed-to by other state's loop_entry field
	 * this flag would be set to true. Used to avoid freeing such states
	 * while they are still in use.
	 */
	bool used_as_loop_entry;
	bool in_sleepable;

	/* first and last insn idx of this verifier state */
	u32 first_insn_idx;
	u32 last_insn_idx;
	/* If this state is a part of states loop this field points to some
	 * parent of this state such that:
	 * - it is also a member of the same states loop;
	 * - DFS states traversal starting from initial state visits loop_entry
	 *   state before this state.
	 * Used to compute topmost loop entry for state loops.
	 * State loops might appear because of open coded iterators logic.
	 * See get_loop_entry() for more information.
	 */
	struct bpf_verifier_state *loop_entry;
	/* Sub-range of env->insn_hist[] corresponding to this state's
	 * instruction history.
	 * Backtracking is using it to go from last to first.
	 * For most states instruction history is short, 0-3 instructions.
	 * For loops can go up to ~40.
	 */
	u32 insn_hist_start;
	u32 insn_hist_end;
	u32 dfs_depth;
	u32 callback_unroll_depth;
	u32 may_goto_depth;
};

#define bpf_get_spilled_reg(slot, frame, mask)				\
	(((slot < frame->allocated_stack / BPF_REG_SIZE) &&		\
	  ((1 << frame->stack[slot].slot_type[BPF_REG_SIZE - 1]) & (mask))) \
	 ? &frame->stack[slot].spilled_ptr : NULL)

/* Iterate over 'frame', setting 'reg' to either NULL or a spilled register. */
#define bpf_for_each_spilled_reg(iter, frame, reg, mask)			\
	for (iter = 0, reg = bpf_get_spilled_reg(iter, frame, mask);		\
	     iter < frame->allocated_stack / BPF_REG_SIZE;		\
	     iter++, reg = bpf_get_spilled_reg(iter, frame, mask))

#define bpf_for_each_reg_in_vstate_mask(__vst, __state, __reg, __mask, __expr)   \
	({                                                               \
		struct bpf_verifier_state *___vstate = __vst;            \
		int ___i, ___j;                                          \
		for (___i = 0; ___i <= ___vstate->curframe; ___i++) {    \
			struct bpf_reg_state *___regs;                   \
			__state = ___vstate->frame[___i];                \
			___regs = __state->regs;                         \
			for (___j = 0; ___j < MAX_BPF_REG; ___j++) {     \
				__reg = &___regs[___j];                  \
				(void)(__expr);                          \
			}                                                \
			bpf_for_each_spilled_reg(___j, __state, __reg, __mask) { \
				if (!__reg)                              \
					continue;                        \
				(void)(__expr);                          \
			}                                                \
		}                                                        \
	})

/* Invoke __expr over regsiters in __vst, setting __state and __reg */
#define bpf_for_each_reg_in_vstate(__vst, __state, __reg, __expr) \
	bpf_for_each_reg_in_vstate_mask(__vst, __state, __reg, 1 << STACK_SPILL, __expr)

/* linked list of verifier states used to prune search */
struct bpf_verifier_state_list {
	struct bpf_verifier_state state;
	struct bpf_verifier_state_list *next;
	int miss_cnt, hit_cnt;
};

struct bpf_loop_inline_state {
	unsigned int initialized:1; /* set to true upon first entry */
	unsigned int fit_for_inline:1; /* true if callback function is the same
					* at each call and flags are always zero
					*/
	u32 callback_subprogno; /* valid when fit_for_inline is true */
};

/* pointer and state for maps */
struct bpf_map_ptr_state {
	struct bpf_map *map_ptr;
	bool poison;
	bool unpriv;
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
		struct bpf_map_ptr_state map_ptr_state;
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
		/* if instruction is a call to bpf_loop this field tracks
		 * the state of the relevant registers to make decision about inlining
		 */
		struct bpf_loop_inline_state loop_inline_state;
	};
	union {
		/* remember the size of type passed to bpf_obj_new to rewrite R1 */
		u64 obj_new_size;
		/* remember the offset of node field within type to rewrite */
		u64 insert_off;
	};
	struct btf_struct_meta *kptr_struct_meta;
	u64 map_key_state; /* constant (32 bit) key tracking for maps */
	int ctx_field_size; /* the ctx field size for load insn, maybe 0 */
	u32 seen; /* this insn was processed by the verifier at env->pass_cnt */
	bool sanitize_stack_spill; /* subject to Spectre v4 sanitation */
	bool zext_dst; /* this insn zero extends dst reg */
	bool needs_zext; /* alu op needs to clear upper bits */
	bool storage_get_func_atomic; /* bpf_*_storage_get() with atomic memory alloc */
	bool is_iter_next; /* bpf_iter_<type>_next() kfunc call */
	bool call_with_percpu_alloc_ptr; /* {this,per}_cpu_ptr() with prog percpu alloc */
	u8 alu_state; /* used in combination with alu_limit */
	/* true if STX or LDX instruction is a part of a spill/fill
	 * pattern for a bpf_fastcall call.
	 */
	u8 fastcall_pattern:1;
	/* for CALL instructions, a number of spill/fill pairs in the
	 * bpf_fastcall pattern.
	 */
	u8 fastcall_spills_num:3;

	/* below fields are initialized once */
	unsigned int orig_idx; /* original instruction index */
	bool jmp_point;
	bool prune_point;
	/* ensure we check state equivalence and save state checkpoint and
	 * this instruction, regardless of any heuristics
	 */
	bool force_checkpoint;
	/* true if instruction is a call to a helper function that
	 * accepts callback function as a parameter.
	 */
	bool calls_callback;
};

#define MAX_USED_MAPS 64 /* max number of maps accessed by one eBPF program */
#define MAX_USED_BTFS 64 /* max number of BTFs accessed by one BPF program */

#define BPF_VERIFIER_TMP_LOG_SIZE	1024

struct bpf_verifier_log {
	/* Logical start and end positions of a "log window" of the verifier log.
	 * start_pos == 0 means we haven't truncated anything.
	 * Once truncation starts to happen, start_pos + len_total == end_pos,
	 * except during log reset situations, in which (end_pos - start_pos)
	 * might get smaller than len_total (see bpf_vlog_reset()).
	 * Generally, (end_pos - start_pos) gives number of useful data in
	 * user log buffer.
	 */
	u64 start_pos;
	u64 end_pos;
	char __user *ubuf;
	u32 level;
	u32 len_total;
	u32 len_max;
	char kbuf[BPF_VERIFIER_TMP_LOG_SIZE];
};

#define BPF_LOG_LEVEL1	1
#define BPF_LOG_LEVEL2	2
#define BPF_LOG_STATS	4
#define BPF_LOG_FIXED	8
#define BPF_LOG_LEVEL	(BPF_LOG_LEVEL1 | BPF_LOG_LEVEL2)
#define BPF_LOG_MASK	(BPF_LOG_LEVEL | BPF_LOG_STATS | BPF_LOG_FIXED)
#define BPF_LOG_KERNEL	(BPF_LOG_MASK + 1) /* kernel internal flag */
#define BPF_LOG_MIN_ALIGNMENT 8U
#define BPF_LOG_ALIGNMENT 40U

static inline bool bpf_verifier_log_needed(const struct bpf_verifier_log *log)
{
	return log && log->level;
}

#define BPF_MAX_SUBPROGS 256

struct bpf_subprog_arg_info {
	enum bpf_arg_type arg_type;
	union {
		u32 mem_size;
		u32 btf_id;
	};
};

enum priv_stack_mode {
	PRIV_STACK_UNKNOWN,
	NO_PRIV_STACK,
	PRIV_STACK_ADAPTIVE,
};

struct bpf_subprog_info {
	/* 'start' has to be the first field otherwise find_subprog() won't work */
	u32 start; /* insn idx of function entry point */
	u32 linfo_idx; /* The idx to the main_prog->aux->linfo */
	u16 stack_depth; /* max. stack depth used by this function */
	u16 stack_extra;
	/* offsets in range [stack_depth .. fastcall_stack_off)
	 * are used for bpf_fastcall spills and fills.
	 */
	s16 fastcall_stack_off;
	bool has_tail_call: 1;
	bool tail_call_reachable: 1;
	bool has_ld_abs: 1;
	bool is_cb: 1;
	bool is_async_cb: 1;
	bool is_exception_cb: 1;
	bool args_cached: 1;
	/* true if bpf_fastcall stack region is used by functions that can't be inlined */
	bool keep_fastcall_stack: 1;

	enum priv_stack_mode priv_stack_mode;
	u8 arg_cnt;
	struct bpf_subprog_arg_info args[MAX_BPF_FUNC_REG_ARGS];
};

struct bpf_verifier_env;

struct backtrack_state {
	struct bpf_verifier_env *env;
	u32 frame;
	u32 reg_masks[MAX_CALL_FRAMES];
	u64 stack_masks[MAX_CALL_FRAMES];
};

struct bpf_id_pair {
	u32 old;
	u32 cur;
};

struct bpf_idmap {
	u32 tmp_id_gen;
	struct bpf_id_pair map[BPF_ID_MAP_SIZE];
};

struct bpf_idset {
	u32 count;
	u32 ids[BPF_ID_MAP_SIZE];
};

/* single container for all structs
 * one verifier_env per bpf_check() call
 */
struct bpf_verifier_env {
	u32 insn_idx;
	u32 prev_insn_idx;
	struct bpf_prog *prog;		/* eBPF program being verified */
	const struct bpf_verifier_ops *ops;
	struct module *attach_btf_mod;	/* The owner module of prog->aux->attach_btf */
	struct bpf_verifier_stack_elem *head; /* stack of verifier states to be processed */
	int stack_size;			/* number of states to be processed */
	bool strict_alignment;		/* perform strict pointer alignment checks */
	bool test_state_freq;		/* test verifier with different pruning frequency */
	bool test_reg_invariants;	/* fail verification on register invariants violations */
	struct bpf_verifier_state *cur_state; /* current verifier state */
	struct bpf_verifier_state_list **explored_states; /* search pruning optimization */
	struct bpf_verifier_state_list *free_list;
	struct bpf_map *used_maps[MAX_USED_MAPS]; /* array of map's used by eBPF program */
	struct btf_mod_pair used_btfs[MAX_USED_BTFS]; /* array of BTF's used by BPF program */
	u32 used_map_cnt;		/* number of used maps */
	u32 used_btf_cnt;		/* number of used BTF objects */
	u32 id_gen;			/* used to generate unique reg IDs */
	u32 hidden_subprog_cnt;		/* number of hidden subprogs */
	int exception_callback_subprog;
	bool explore_alu_limits;
	bool allow_ptr_leaks;
	/* Allow access to uninitialized stack memory. Writes with fixed offset are
	 * always allowed, so this refers to reads (with fixed or variable offset),
	 * to writes with variable offset and to indirect (helper) accesses.
	 */
	bool allow_uninit_stack;
	bool bpf_capable;
	bool bypass_spec_v1;
	bool bypass_spec_v4;
	bool seen_direct_write;
	bool seen_exception;
	struct bpf_insn_aux_data *insn_aux_data; /* array of per-insn state */
	const struct bpf_line_info *prev_linfo;
	struct bpf_verifier_log log;
	struct bpf_subprog_info subprog_info[BPF_MAX_SUBPROGS + 2]; /* max + 2 for the fake and exception subprogs */
	union {
		struct bpf_idmap idmap_scratch;
		struct bpf_idset idset_scratch;
	};
	struct {
		int *insn_state;
		int *insn_stack;
		int cur_stack;
	} cfg;
	struct backtrack_state bt;
	struct bpf_insn_hist_entry *insn_hist;
	struct bpf_insn_hist_entry *cur_hist_ent;
	u32 insn_hist_cap;
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

	/* bit mask to keep track of whether a register has been accessed
	 * since the last time the function state was printed
	 */
	u32 scratched_regs;
	/* Same as scratched_regs but for stack slots */
	u64 scratched_stack_slots;
	u64 prev_log_pos, prev_insn_print_pos;
	/* buffer used to temporary hold constants as scalar registers */
	struct bpf_reg_state fake_reg[2];
	/* buffer used to generate temporary string representations,
	 * e.g., in reg_type_str() to generate reg_type string
	 */
	char tmp_str_buf[TMP_STR_BUF_LEN];
	struct bpf_insn insn_buf[INSN_BUF_SIZE];
	struct bpf_insn epilogue_buf[INSN_BUF_SIZE];
};

static inline struct bpf_func_info_aux *subprog_aux(struct bpf_verifier_env *env, int subprog)
{
	return &env->prog->aux->func_info_aux[subprog];
}

static inline struct bpf_subprog_info *subprog_info(struct bpf_verifier_env *env, int subprog)
{
	return &env->subprog_info[subprog];
}

__printf(2, 0) void bpf_verifier_vlog(struct bpf_verifier_log *log,
				      const char *fmt, va_list args);
__printf(2, 3) void bpf_verifier_log_write(struct bpf_verifier_env *env,
					   const char *fmt, ...);
__printf(2, 3) void bpf_log(struct bpf_verifier_log *log,
			    const char *fmt, ...);
int bpf_vlog_init(struct bpf_verifier_log *log, u32 log_level,
		  char __user *log_buf, u32 log_size);
void bpf_vlog_reset(struct bpf_verifier_log *log, u64 new_pos);
int bpf_vlog_finalize(struct bpf_verifier_log *log, u32 *log_size_actual);

__printf(3, 4) void verbose_linfo(struct bpf_verifier_env *env,
				  u32 insn_off,
				  const char *prefix_fmt, ...);

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
void bpf_free_kfunc_btf_tab(struct bpf_kfunc_btf_tab *tab);

int mark_chain_precision(struct bpf_verifier_env *env, int regno);

#define BPF_BASE_TYPE_MASK	GENMASK(BPF_BASE_TYPE_BITS - 1, 0)

/* extract base type from bpf_{arg, return, reg}_type. */
static inline u32 base_type(u32 type)
{
	return type & BPF_BASE_TYPE_MASK;
}

/* extract flags from an extended type. See bpf_type_flag in bpf.h. */
static inline u32 type_flag(u32 type)
{
	return type & ~BPF_BASE_TYPE_MASK;
}

/* only use after check_attach_btf_id() */
static inline enum bpf_prog_type resolve_prog_type(const struct bpf_prog *prog)
{
	return (prog->type == BPF_PROG_TYPE_EXT && prog->aux->saved_dst_prog_type) ?
		prog->aux->saved_dst_prog_type : prog->type;
}

static inline bool bpf_prog_check_recur(const struct bpf_prog *prog)
{
	switch (resolve_prog_type(prog)) {
	case BPF_PROG_TYPE_TRACING:
		return prog->expected_attach_type != BPF_TRACE_ITER;
	case BPF_PROG_TYPE_STRUCT_OPS:
		return prog->aux->jits_use_priv_stack;
	case BPF_PROG_TYPE_LSM:
		return false;
	default:
		return true;
	}
}

#define BPF_REG_TRUSTED_MODIFIERS (MEM_ALLOC | PTR_TRUSTED | NON_OWN_REF)

static inline bool bpf_type_has_unsafe_modifiers(u32 type)
{
	return type_flag(type) & ~BPF_REG_TRUSTED_MODIFIERS;
}

static inline bool type_is_ptr_alloc_obj(u32 type)
{
	return base_type(type) == PTR_TO_BTF_ID && type_flag(type) & MEM_ALLOC;
}

static inline bool type_is_non_owning_ref(u32 type)
{
	return type_is_ptr_alloc_obj(type) && type_flag(type) & NON_OWN_REF;
}

static inline bool type_is_pkt_pointer(enum bpf_reg_type type)
{
	type = base_type(type);
	return type == PTR_TO_PACKET ||
	       type == PTR_TO_PACKET_META;
}

static inline bool type_is_sk_pointer(enum bpf_reg_type type)
{
	return type == PTR_TO_SOCKET ||
		type == PTR_TO_SOCK_COMMON ||
		type == PTR_TO_TCP_SOCK ||
		type == PTR_TO_XDP_SOCK;
}

static inline bool type_may_be_null(u32 type)
{
	return type & PTR_MAYBE_NULL;
}

static inline void mark_reg_scratched(struct bpf_verifier_env *env, u32 regno)
{
	env->scratched_regs |= 1U << regno;
}

static inline void mark_stack_slot_scratched(struct bpf_verifier_env *env, u32 spi)
{
	env->scratched_stack_slots |= 1ULL << spi;
}

static inline bool reg_scratched(const struct bpf_verifier_env *env, u32 regno)
{
	return (env->scratched_regs >> regno) & 1;
}

static inline bool stack_slot_scratched(const struct bpf_verifier_env *env, u64 regno)
{
	return (env->scratched_stack_slots >> regno) & 1;
}

static inline bool verifier_state_scratched(const struct bpf_verifier_env *env)
{
	return env->scratched_regs || env->scratched_stack_slots;
}

static inline void mark_verifier_state_clean(struct bpf_verifier_env *env)
{
	env->scratched_regs = 0U;
	env->scratched_stack_slots = 0ULL;
}

/* Used for printing the entire verifier state. */
static inline void mark_verifier_state_scratched(struct bpf_verifier_env *env)
{
	env->scratched_regs = ~0U;
	env->scratched_stack_slots = ~0ULL;
}

static inline bool bpf_stack_narrow_access_ok(int off, int fill_size, int spill_size)
{
#ifdef __BIG_ENDIAN
	off -= spill_size - fill_size;
#endif

	return !(off % BPF_REG_SIZE);
}

const char *reg_type_str(struct bpf_verifier_env *env, enum bpf_reg_type type);
const char *dynptr_type_str(enum bpf_dynptr_type type);
const char *iter_type_str(const struct btf *btf, u32 btf_id);
const char *iter_state_str(enum bpf_iter_state state);

void print_verifier_state(struct bpf_verifier_env *env,
			  const struct bpf_func_state *state, bool print_all);
void print_insn_state(struct bpf_verifier_env *env, const struct bpf_func_state *state);

#endif /* _LINUX_BPF_VERIFIER_H */
