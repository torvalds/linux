/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 */
#ifndef _LINUX_BPF_VERIFIER_H
#define _LINUX_BPF_VERIFIER_H 1

#include <linux/bpf.h> /* for enum bpf_reg_type */
#include <linux/btf.h> /* for struct btf and btf_id() */
#include <linux/filter.h> /* for MAX_BPF_STACK */
#include <linux/tnum.h>
#include <linux/cnum.h>
#include <linux/math64.h> /* for div_s64_rem() */

/*
 * Mathematical modulo, the residue of 'v' modulo 'step', normalized to [0, step).
 * This differs from C's '%' operator, which truncates the quotient toward zero
 * and so returns a remainder with the sign of the dividend (e.g. -1 % 3 == -1, not 2).
 */
static inline u16 imod(s64 v, u16 step)
{
	s32 rem;

	div_s64_rem(v, step, &rem);
	return rem < 0 ? rem + step : rem;
}

/* Maximum variable offset umax_value permitted when resolving memory accesses.
 * In practice this is far bigger than any realistic pointer offset; this limit
 * ensures that umax_value + (int)off + (int)size cannot overflow a u64.
 */
#define BPF_MAX_VAR_OFF	(1 << 29)
/* Maximum variable size permitted for ARG_MEM_SIZE[_OR_ZERO].  This ensures
 * that converting umax_value to int cannot overflow.
 */
#define BPF_MAX_VAR_SIZ	(1 << 29)
/*
 * size of tmp_str_buf in bpf_verifier.
 * we need at least 1399 bytes to fit full stack mask representation
 * (in the "-8,-16,...,-2048" form)
 */
#define TMP_STR_BUF_LEN 1408
/* Patch buffer size */
#define INSN_BUF_SIZE 32

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
	 * Constant delta between "linked" scalars with the same ID.
	 */
	s32 delta;
	union {
		/* valid when type == PTR_TO_PACKET */
		int range;
		/*
		 * Valid when type == PTR_TO_STACK. Inside the callee two registers
		 * can be both PTR_TO_STACK like R1=fp-8 and R2=fp-8, but one of them
		 * points to this function stack while another to the caller's stack.
		 * To differentiate them 'frameno' is used which is an index in
		 * bpf_verifier_state->frame[] array pointing to bpf_func_state.
		 */
		u8 frameno;

		/*
		 * For CONST_PTR_TO_MAP, PTR_TO_MAP_KEY, PTR_TO_MAP_VALUE and
		 * PTR_TO_INSN.
		 */
		struct bpf_map *map_ptr;

		/* for PTR_TO_BTF_ID */
		struct {
			struct btf *btf;
			u32 btf_id;
		};

		struct { /* for PTR_TO_MEM | PTR_TO_MEM_OR_NULL */
			u32 mem_size;
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

		/* For irq stack slots */
		struct {
			enum {
				IRQ_NATIVE_KFUNC,
				IRQ_LOCK_KFUNC,
			} kfunc_class;
		} irq;

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
	struct cnum64 r64; /* 64-bit range as circular number */
	struct cnum32 r32; /* 32-bit range as circular number */
	/*
	 * The value described by this register, interpreted as s64, lies on
	 * a line described by a linear equation base + step * k.
	 * Invariant: base < step.
	 */
	u16 base;
	u16 step;
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
	 * r1 += 10;   r1->id == N | BPF_ADD_CONST and r1->delta == 10
	 * r3 = r2;    both will have r3->id == r2->id == N
	 * w3 += 10;   r3->id == N | BPF_ADD_CONST32 and r3->delta == 10
	 */
#define BPF_ADD_CONST64 (1U << 31)
#define BPF_ADD_CONST32 (1U << 30)
#define BPF_ADD_CONST (BPF_ADD_CONST64 | BPF_ADD_CONST32)
	u32 id;
	/*
	 * Tracks the parent object this register was derived from.
	 * Used for cascading invalidation: when the parent object is
	 * released or invalidated, all registers with matching parent_id
	 * are also invalidated. For example, a slice from bpf_dynptr_data()
	 * gets parent_id set to the dynptr's id.
	 */
	u32 parent_id;
	/*
	 * Distinguishes inner-map lookups and their keys and values. Zero for
	 * other registers. Kept outside the metadata union for ID remapping
	 * during state comparisons.
	 */
	u32 map_uid:31;
	/* if (!precise && SCALAR_VALUE) min/max/tnum don't affect safety */
	u32 precise:1;
};

static inline s64 reg_smin(const struct bpf_reg_state *reg)
{
	return cnum64_smin(reg->r64);
}

static inline s64 reg_smax(const struct bpf_reg_state *reg)
{
	return cnum64_smax(reg->r64);
}

static inline u64 reg_umin(const struct bpf_reg_state *reg)
{
	return cnum64_umin(reg->r64);
}

static inline u64 reg_umax(const struct bpf_reg_state *reg)
{
	return cnum64_umax(reg->r64);
}

static inline s32 reg_s32_min(const struct bpf_reg_state *reg)
{
	return cnum32_smin(reg->r32);
}

static inline s32 reg_s32_max(const struct bpf_reg_state *reg)
{
	return cnum32_smax(reg->r32);
}

static inline u32 reg_u32_min(const struct bpf_reg_state *reg)
{
	return cnum32_umin(reg->r32);
}

static inline u32 reg_u32_max(const struct bpf_reg_state *reg)
{
	return cnum32_umax(reg->r32);
}

static inline void reg_set_srange32(struct bpf_reg_state *reg, s32 smin, s32 smax)
{
	reg->r32 = cnum32_from_srange(smin, smax);
}

static inline void reg_set_urange32(struct bpf_reg_state *reg, u32 umin, u32 umax)
{
	reg->r32 = cnum32_from_urange(umin, umax);
}

static inline void reg_set_srange64(struct bpf_reg_state *reg, s64 smin, s64 smax)
{
	reg->r64 = cnum64_from_srange(smin, smax);
}

static inline void reg_set_urange64(struct bpf_reg_state *reg, u64 umin, u64 umax)
{
	reg->r64 = cnum64_from_urange(umin, umax);
}

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
	STACK_IRQ_FLAG,
	STACK_POISON,
};

#define BPF_REG_SIZE 8	/* size of eBPF register in bytes */

/*
 * Largest number of BPF_REG_SIZE stack slots a single frame can have, sized
 * for the largest stack budget any JIT supports. A frame may use any part of
 * its program's budget; check_max_stack_depth() enforces the budget on the
 * combined depth of frames sharing the kernel stack and on each frame using
 * a private stack.
 */
#define MAX_BPF_STACK_SLOTS	(MAX_BPF_STACK_JIT / BPF_REG_SIZE)

/* 4-byte stack slot granularity for liveness analysis */
#define BPF_HALF_REG_SIZE	4
#define STACK_SLOT_SZ		4

#define BPF_REGMASK_ARGS ((1 << BPF_REG_1) | (1 << BPF_REG_2) | \
			  (1 << BPF_REG_3) | (1 << BPF_REG_4) | \
			  (1 << BPF_REG_5))

#define BPF_MAIN_FUNC (-1)

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
		REF_TYPE_PTR		= (1 << 1),
		REF_TYPE_IRQ		= (1 << 2),
		REF_TYPE_LOCK		= (1 << 3),
		REF_TYPE_RES_LOCK 	= (1 << 4),
		REF_TYPE_RES_LOCK_IRQ	= (1 << 5),
		REF_TYPE_LOCK_MASK	= REF_TYPE_LOCK | REF_TYPE_RES_LOCK | REF_TYPE_RES_LOCK_IRQ,
	} type;
	/* Track each reference created with a unique id, even if the same
	 * instruction creates the reference multiple times (eg, via CALL).
	 */
	int id;
	/* Instruction where the allocation of this reference occurred. This
	 * is used purely to inform the user of a reference leak.
	 */
	int insn_idx;
	union {
		/* For REF_TYPE_PTR */
		int parent_id;
		/* Use to keep track of the source object of a lock, to ensure
		 * it matches on unlock.
		 */
		void *ptr;
	};
};

struct bpf_retval_range {
	s32 minval;
	s32 maxval;
	bool return_32bit;
};

/* SCEV/widening register space: r0..r10 plus every stack slot of a frame. */
#define BPF_SCEV_REGS_NUM (MAX_BPF_REG + MAX_BPF_STACK_SLOTS)

struct bpf_loop_iters {
	u32 min_header_count;   /* min number of times header is executed */
	u32 max_header_count;   /* max number of times header is executed */
	unsigned long precise_regs[BITS_TO_LONGS(BPF_SCEV_REGS_NUM)];
};

struct loop_stack_entry {
	struct bpf_verifier_state *entry_state;
	u32 min_header_count;
	u32 max_header_count;
	u32 loop_id:31;
	u32 terminates:1;
};

#define LOOP_STACK_SIZE 16

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
	/*
	 * Unique diagnostic identity for this function invocation. Frame depth is
	 * reused after returns, while this ID is preserved across state clones.
	 */
	u32 diag_frame_id;
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
	bool no_stack_arg_load;
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
	/* Instructions processed in this frame and callees on the current path. */
	u32 insns_subtotal;
	/*
	 * Control-flow loop nesting at the current insn within this frame's
	 * subprogram (loops never cross subprogram boundaries).
	 */
	u32 loop_stack_cnt;

	/* The following fields should be last. See copy_func_state() */
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

	u16 out_stack_arg_cnt; /* Number of outgoing on-stack argument slots */
	struct bpf_reg_state *stack_arg_regs; /* Outgoing on-stack arguments */
	struct loop_stack_entry *loop_stack;
};

#define MAX_CALL_FRAMES 16

/* instruction history flags, used in bpf_jmp_history_entry.flags field.
 * Frame number and SPI are stored in dedicated fields of bpf_jmp_history_entry.
 */
enum {
	INSN_F_STACK_ACCESS = BIT(0),

	INSN_F_DST_REG_STACK = BIT(1), /* dst_reg is PTR_TO_STACK */
	INSN_F_SRC_REG_STACK = BIT(2), /* src_reg is PTR_TO_STACK */

	INSN_F_STACK_ARG_ACCESS = BIT(3),
};

/* Registers linked to one jump condition that a history entry can record */
#define BPF_LINKED_REGS_MAX	5

struct bpf_jmp_history_entry {
	/* insn idx can't be bigger than 1 million */
	u32 idx : 20;
	u32 frame : 4;	/* stack access frame number */
	/* special INSN_F_xxx flags */
	u32 flags : 4;
	u32 : 4;
	u32 prev_idx : 20;
	u32 spi : 12;	/* stack slot index */
	/*
	 * Scalar registers and spilled scalars linked to the condition of
	 * this jump, which need precision tracking together when the jump is
	 * backtracked. Each is packed as 4 bits of frame number, one bit
	 * telling a register from a stack slot and 11 bits of register or
	 * slot index, see linked_regs_pack().
	 */
	u16 linked_regs[BPF_LINKED_REGS_MAX];
	u8 linked_regs_cnt;
};

static_assert(MAX_CALL_FRAMES <= (1 << 4));
static_assert(MAX_BPF_STACK_SLOTS <= (1 << 12));

/* Maximum number of bpf_reg_state objects that can exist at once */
#define MAX_STACK_ARG_SLOTS (MAX_BPF_FUNC_ARGS - MAX_BPF_FUNC_REG_ARGS)
#define BPF_ID_MAP_SIZE ((MAX_BPF_REG + MAX_BPF_STACK_SLOTS + MAX_STACK_ARG_SLOTS) * \
			 MAX_CALL_FRAMES)

struct bpf_verifier_state {
	/* call stack tracking */
	struct bpf_func_state *frame[MAX_CALL_FRAMES];
	struct bpf_verifier_state *parent;
	/* Acquired reference states */
	struct bpf_reference_state *refs;
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

	u32 acquired_refs;
	u32 active_locks;
	u32 active_preempt_locks;
	u32 active_irq_id;
	u32 active_lock_id;
	void *active_lock_ptr;
	u32 active_rcu_locks;

	bool speculative;
	bool in_sleepable;

	/*
	 * First and last insn idx of this verifier state.
	 * last_insn_idx is -1 if no instructions have been executed yet.
	 */
	u32 first_insn_idx;
	u32 last_insn_idx;
	/* if this state is a backedge state then equal_state
	 * records cached state to which this state is equal.
	 */
	struct bpf_verifier_state *equal_state;
	/* jmp history recorded from first to last.
	 * backtracking is using it to go from last to first.
	 * For most states jmp_history_cnt is [0-3].
	 * For loops can go up to ~40.
	 */
	struct bpf_jmp_history_entry *jmp_history;
	u32 jmp_history_cnt;
	u32 dfs_depth;
	u32 callback_unroll_depth;
	u32 may_goto_depth;
};

/* Number of BPF_REG_SIZE stack slots tracked for the frame so far. */
static inline u32 bpf_stack_nr_slots(const struct bpf_func_state *frame)
{
	return frame->allocated_stack / BPF_REG_SIZE;
}

/*
 * Stack slot @spi of @frame, covering bytes [fp - (spi + 1) * 8, fp - spi * 8).
 * The caller must ensure spi < bpf_stack_nr_slots(frame), see bpf_grow_stack_state().
 */
static inline struct bpf_stack_state *bpf_stack_slot(const struct bpf_func_state *frame, u32 spi)
{
	return &frame->stack[spi];
}

static inline struct bpf_reg_state *
bpf_get_spilled_reg(int slot, struct bpf_func_state *frame, u32 mask)
{
	struct bpf_stack_state *ss;

	if (slot >= bpf_stack_nr_slots(frame))
		return NULL;
	ss = bpf_stack_slot(frame, slot);
	if ((1 << ss->slot_type[BPF_REG_SIZE - 1]) & mask)
		return &ss->spilled_ptr;
	return NULL;
}

static inline struct bpf_reg_state *
bpf_get_spilled_stack_arg(int slot, struct bpf_func_state *frame)
{
	if (slot < frame->out_stack_arg_cnt &&
	    frame->stack_arg_regs[slot].type != NOT_INIT)
		return &frame->stack_arg_regs[slot];
	return NULL;
}

/* Iterate over 'frame', setting 'reg' to either NULL or a spilled register. */
#define bpf_for_each_spilled_reg(iter, frame, reg, mask)			\
	for (iter = 0, reg = bpf_get_spilled_reg(iter, frame, mask);		\
	     iter < bpf_stack_nr_slots(frame);				\
	     iter++, reg = bpf_get_spilled_reg(iter, frame, mask))

/* Iterate over 'frame', setting 'reg' to either NULL or a spilled stack arg. */
#define bpf_for_each_spilled_stack_arg(iter, frame, reg)               \
	for (iter = 0, reg = bpf_get_spilled_stack_arg(iter, frame);   \
	     iter < frame->out_stack_arg_cnt;                          \
	     iter++, reg = bpf_get_spilled_stack_arg(iter, frame))

#define bpf_for_each_reg_in_vstate_mask(__vst, __state, __reg, __stack, __mask, __expr)   \
	({                                                               \
		struct bpf_verifier_state *___vstate = __vst;            \
		int ___i, ___j;                                          \
		for (___i = 0; ___i <= ___vstate->curframe; ___i++) {    \
			struct bpf_reg_state *___regs;                   \
			__state = ___vstate->frame[___i];                \
			___regs = __state->regs;                         \
			__stack = NULL;                                  \
			for (___j = 0; ___j < MAX_BPF_REG; ___j++) {     \
				__reg = &___regs[___j];                  \
				(void)(__expr);                          \
			}                                                \
			bpf_for_each_spilled_reg(___j, __state, __reg, __mask) { \
				if (!__reg)                              \
					continue;                        \
				__stack = bpf_stack_slot(__state, ___j); \
				(void)(__expr);                          \
			}                                                \
			__stack = NULL;                                  \
			bpf_for_each_spilled_stack_arg(___j, __state, __reg) { \
				if (!__reg)                              \
					continue;                        \
				(void)(__expr);                          \
			}						 \
		}                                                        \
		(void)__stack;                                           \
	})

/* Invoke __expr over regsiters in __vst, setting __state and __reg */
#define bpf_for_each_reg_in_vstate(__vst, __state, __reg, __expr)		\
	({									\
		struct bpf_stack_state * ___stack;                        	\
		(void)___stack;							\
		bpf_for_each_reg_in_vstate_mask(__vst, __state, __reg, ___stack,\
						1 << STACK_SPILL, __expr);	\
	})

/* linked list of verifier states used to prune search */
struct bpf_verifier_state_list {
	struct bpf_verifier_state state;
	struct list_head node;
	u32 miss_cnt;
	u32 hit_cnt:31;
	u32 in_free_list:1;
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

/*
 * An array of BPF instructions.
 * Primary usage: return value of bpf_insn_successors.
 */
struct bpf_iarray {
	int cnt;
	u32 items[];
};

#define iarray_for_each(item, arr)						\
	for (int ___idx = 0;							\
	     ___idx < (arr)->cnt && ({ item = (arr)->items[___idx]; 1; });	\
	     ___idx++)

#define MAX_BACKEDGES 16
#define MAX_LOOP_EXITS 256

/* Number of exit conditions dominating a backedge. */
#define MAX_LATCHES 4

struct bpf_backedge {
	int from;
	/*
	 * Conditional jumps within the loop that dominate 'from' and have
	 * a successor outside the loop, nearest first. latches[0] is the
	 * latch; the remaining entries are additional exit conditions.
	 * Each usable condition gives an upper bound on the iteration count,
	 * so keeping only a subset is sound.
	 */
	int latches[MAX_LATCHES];
	u8 latches_cnt;
};

struct bpf_loop_exit {
	int from; /* instruction inside the loop */
	int to; /* instruction outside the loop */
};

struct bpf_loop {
	struct bpf_backedge backedges[MAX_BACKEDGES];
	/* edges exiting from this loop, includes edges from nested loops */
	struct bpf_loop_exit *exits;
	int backedges_cnt;
	int exits_cnt;
	bool irreducible;
	bool backedges_overflow;
	bool exits_overflow;
	/*
	 * Loop-entry registers used to compute stack addresses for loads,
	 * stores and calls requiring fixed stack offsets, in this and nested
	 * loops.
	 */
	unsigned long store_base_regs[BITS_TO_LONGS(BPF_SCEV_REGS_NUM)];
};

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
	/*
	 * Per-instruction summary of stack slots in the current frame
	 * that this instruction may write to.
	 */
	DECLARE_BITMAP(may_write_mask, MAX_BPF_STACK_SLOTS);
	DECLARE_BITMAP(live_stack_before, MAX_BPF_STACK_SLOTS);
	u16 stack_ptrs; /* bitmask of regs that may hold a frame pointer here (arg_track) */
	int ctx_field_size; /* the ctx field size for load insn, maybe 0 */
	u32 seen; /* this insn was processed by the verifier at env->pass_cnt */
	bool nospec; /* do not execute this instruction speculatively */
	bool nospec_result; /* result is unsafe under speculation, nospec must follow */
	bool zext_dst; /* this insn zero extends dst reg */
	bool needs_zext; /* alu op needs to clear upper bits */
	bool prevent_zext; /* alu op cannot be zext (already used with 64-bit scalars) */
	bool non_sleepable; /* helper/kfunc may be called from non-sleepable context */
	bool is_iter_next; /* bpf_iter_<type>_next() kfunc call */
	bool call_with_percpu_alloc_ptr; /* {this,per}_cpu_ptr() with prog percpu alloc */
	bool arena_scalar; /* ldx/stx/st/atomic through a number, it's an address in arena */
	bool bb_end; /* last instruction of a basic block */
	bool need_scev;
	u8 alu_state; /* used in combination with alu_limit */
	/* true if STX or LDX instruction is a part of a spill/fill
	 * pattern for a bpf_fastcall call.
	 */
	u8 fastcall_pattern:1;
	/* for CALL instructions, a number of spill/fill pairs in the
	 * bpf_fastcall pattern.
	 */
	u8 fastcall_spills_num:3;
	u8 arg_prog:4;

	/* below fields are initialized once */
	unsigned int orig_idx; /* original instruction index */
	u32 jmp_point:1;
	u32 prune_point:1;
	/* ensure we check state equivalence and save state checkpoint and
	 * this instruction, regardless of any heuristics
	 */
	u32 force_checkpoint:1;
	/* true if instruction is a call to a helper function that
	 * accepts callback function as a parameter.
	 */
	u32 calls_callback:1;
	u32 indirect_target:1; /* if it is an indirect jump target */
	u32 non_stack_access:1; /* instruction can access non-stack memory */
	/* true if some jump or call instruction targets this instruction */
	u32 jump_target:1;
	/*
	 * CFG strongly connected component this instruction belongs to,
	 * zero if it is a singleton SCC.
	 */
	u32 scc;
	/* registers alive before this instruction. */
	u16 live_regs_before;
	/*
	 * Bitmask of R0-R9 that hold known values at this instruction.
	 * const_reg_mask: scalar constants that fit in 32 bits.
	 * const_reg_map_mask: map pointers, val is map_index into used_maps[].
	 * const_reg_subprog_mask: subprog pointers, val is subprog number.
	 * const_reg_vals[i] holds the 32-bit value for register i.
	 * Populated by compute_const_regs() pre-pass.
	 */
	u16 const_reg_mask;
	u16 const_reg_map_mask;
	u16 const_reg_subprog_mask;
	u32 const_reg_vals[10];
	/*
	 * Header index of the innermost loop containing this instruction, -1 if none.
	 * For a loop header, identifies the parent loop instead.
	 */
	s32 loop_header;
	/* additional information about the loop if this instruction is a loop header */
	struct bpf_loop *loop;
};

#define MAX_USED_MAPS 64 /* max number of maps accessed by one eBPF program */
#define MAX_USED_BTFS 64 /* max number of BTFs accessed by one BPF program */

/*
 * Longest line the verifier log can carry: a full stack mask of
 * MAX_BPF_STACK_SLOTS slots, see TMP_STR_BUF_LEN, plus its prefix.
 */
#define BPF_VERIFIER_TMP_LOG_SIZE	2048

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

struct bpf_log_attr {
	char __user *ubuf;
	u32 size;
	u32 level;
	u32 offsetof_true_size;
	bpfptr_t uattr;
};

int bpf_log_attr_init(struct bpf_log_attr *log, u64 log_buf, u32 log_size, u32 log_level,
		      u32 offsetof_log_true_size, bpfptr_t uattr, struct bpf_common_attr *common,
		      bpfptr_t uattr_common, u32 size_common);
struct bpf_verifier_log *bpf_log_attr_create_vlog(struct bpf_log_attr *attr_log,
						  struct bpf_common_attr *common, bpfptr_t uattr,
						  u32 size);
int bpf_log_attr_finalize(struct bpf_log_attr *attr, struct bpf_verifier_log *log);

#define BPF_MAX_SUBPROGS 256

/*
 * A pointer to a static subprog in the value of a frozen read-only array map:
 * a 64-bit value that is the offset in bytes of the first instruction of
 * the subprog in the program.
 */
struct bpf_func_ptr {
	struct bpf_map *map;
	u32 map_off;		/* offset of the pointer in the value of the map */
	u32 orig_off;		/* what the map has: the first instruction of the subprog */
	u32 xlated_off;		/* the same after instructions were patched and removed */
};

/* the subprog that a bpf_func_ptr pointed to was removed as dead code */
#define BPF_FUNC_PTR_DELETED ((u32)-1)

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
	const char *name; /* name extracted from BTF */
	u32 start; /* insn idx of function entry point */
	u32 linfo_idx; /* The idx to the main_prog->aux->linfo */
	u32 postorder_start; /* The idx to the env->cfg.insn_postorder */
	/*
	 * Index of one of the BPF_EXIT instructions in this subprogram, or
	 * U32_MAX when it has none.
	 */
	u32 exit_idx;
	struct bpf_iarray *jt; /* jump table shared by all gotox of this subprogram */
	u16 stack_depth; /* max. stack depth used by this function */
	u16 stack_extra;
	u32 insns_total;
	u32 insns_self;
	/* offsets in range [stack_depth .. fastcall_stack_off)
	 * are used for bpf_fastcall spills and fills.
	 */
	s16 fastcall_stack_off;
	bool has_tail_call: 1;
	bool might_throw: 1;
	bool tail_call_reachable: 1;
	bool has_ld_abs: 1;
	bool is_cb: 1;
	bool is_async_cb: 1;
	bool is_exception_cb: 1;
	bool args_cached: 1;
	/* true if the return value is passed in the R0:R2 register pair */
	bool ret_reg_pair: 1;
	/* true if bpf_fastcall stack region is used by functions that can't be inlined */
	bool keep_fastcall_stack: 1;
	bool changes_pkt_data: 1;
	bool might_sleep: 1;
	u8 arg_slot_cnt:4;

	enum priv_stack_mode priv_stack_mode;
	struct bpf_subprog_arg_info args[MAX_BPF_FUNC_ARGS];
	u16 stack_arg_cnt; /* incoming + max outgoing */
	u16 max_out_stack_arg_cnt;
};

static inline u16 bpf_in_stack_arg_cnt(const struct bpf_subprog_info *sub)
{
	if (sub->arg_slot_cnt > MAX_BPF_FUNC_REG_ARGS)
		return sub->arg_slot_cnt - MAX_BPF_FUNC_REG_ARGS;
	return 0;
}

struct bpf_diag;
struct bpf_verifier_env;

struct backtrack_state {
	struct bpf_verifier_env *env;
	u32 frame;
	u32 reg_masks[MAX_CALL_FRAMES];
	unsigned long stack_masks[MAX_CALL_FRAMES][BITS_TO_LONGS(MAX_BPF_STACK_SLOTS)];
	u8 stack_arg_masks[MAX_CALL_FRAMES];
};

struct bpf_id_pair {
	u32 old;
	u32 cur;
};

struct bpf_idmap {
	u32 tmp_id_gen;
	u32 cnt;
	struct bpf_id_pair map[BPF_ID_MAP_SIZE];
};

struct bpf_idset {
	u32 num_ids;
	struct {
		u32 id;
		u32 cnt;
	} entries[BPF_ID_MAP_SIZE];
};

/* see verifier.c:compute_scc_callchain() */
struct bpf_scc_callchain {
	/* call sites from bpf_verifier_state->frame[*]->callsite leading to this SCC */
	u32 callsites[MAX_CALL_FRAMES - 1];
	/* last frame in a chain is identified by SCC id */
	u32 scc;
};

/* verifier state waiting for propagate_backedges() */
struct bpf_scc_backedge {
	struct bpf_scc_backedge *next;
	struct bpf_verifier_state state;
};

struct bpf_scc_visit {
	struct bpf_scc_callchain callchain;
	/* first state in current verification path that entered SCC
	 * identified by the callchain
	 */
	struct bpf_verifier_state *entry_state;
	struct bpf_scc_backedge *backedges; /* list of backedges */
	u32 num_backedges;
};

/* An array of bpf_scc_visit structs sharing tht same bpf_scc_callchain->scc
 * but having different bpf_scc_callchain->callsites.
 */
struct bpf_scc_info {
	u32 num_visits;
	struct bpf_scc_visit visits[];
};

struct bpf_liveness;
struct scev;

struct bpf_fd_array {
	union {
		struct bpf_map *map;
		struct btf *btf;
		unsigned long val;
	};
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
	bool arena_scalar;		/* load and store through a number is arena access */
	struct bpf_verifier_state *cur_state; /* current verifier state */
	/* Search pruning optimization, array of list_heads for
	 * lists of struct bpf_verifier_state_list.
	 */
	struct list_head *explored_states;
	struct list_head free_list;	/* list of struct bpf_verifier_state_list */
	struct bpf_map *used_maps[MAX_USED_MAPS]; /* array of map's used by eBPF program */
	struct btf_mod_pair used_btfs[MAX_USED_BTFS]; /* array of BTF's used by BPF program */
	struct bpf_map *insn_array_maps[MAX_USED_MAPS]; /* array of INSN_ARRAY map's to be relocated */
	u32 used_map_cnt;		/* number of used maps */
	u32 used_btf_cnt;		/* number of used BTF objects */
	u32 insn_array_map_cnt;		/* number of used maps of type BPF_MAP_TYPE_INSN_ARRAY */
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
	bool signature;
	u32 insn_aux_data_len;
	struct bpf_insn_aux_data *insn_aux_data; /* array of per-insn state */
	const struct bpf_line_info *prev_linfo;
	struct bpf_verifier_log log;
	struct bpf_diag *diag;
	struct bpf_func_proto bpf_subprog_scratch;
	struct bpf_subprog_info subprog_info[BPF_MAX_SUBPROGS + 2]; /* max + 2 for the fake and exception subprogs */
	/* subprog indices sorted in topological order: leaves first, callers last */
	int subprog_topo_order[BPF_MAX_SUBPROGS + 2];
	/*
	 * Pointers to static subprogs found in frozen read-only maps of the
	 * program, see resolve_func_ptrs(). Sorted by map and map_off.
	 */
	struct bpf_func_ptr *func_ptrs;
	u32 func_ptr_cnt;
	bool has_callx;
	/*
	 * Call graph edges created by callx instructions. A bitmap of
	 * subprog_cnt * subprog_cnt bits, where bit (caller * subprog_cnt + callee)
	 * is set when the main verification pass sees 'caller' calling 'callee'
	 * via callx. Allocated when the first such edge is recorded.
	 */
	unsigned long *callx_edges;
	union {
		struct bpf_idmap idmap_scratch;
		struct bpf_idset idset_scratch;
	};
	struct {
		int *insn_state;
		int *insn_stack;
		/*
		 * vector of instruction indexes sorted in post-order, grouped by subprogram,
		 * see bpf_subprog_info->postorder_start.
		 */
		int *insn_postorder;
		int *postorder_nums;
		int cur_stack;
		/* current position in the insn_postorder vector */
		int cur_postorder;
		u32 gotox_edges;
		bool subprog_jts_ready;
	} cfg;
	struct backtrack_state bt;
	struct bpf_jmp_history_entry *cur_hist_ent;
	/* Per-callsite snapshot of the parent's tracked spill slots for cross-frame fills. */
	struct spill_snapshot **callsite_at_stack;
	u32 pass_cnt; /* number of times do_check() was called */
	u32 subprog_cnt;
	/* number of instructions analyzed by the verifier */
	u32 prev_insn_processed, insn_processed;
	/* number of jmps, calls, exits analyzed so far */
	u32 prev_jmps_processed, jmps_processed;
	/* maximum combined stack depth */
	u32 max_stack_depth;
	/* stack budget of the program, see bpf_prog_stack_limit() */
	u32 stack_limit;
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
	u32 free_list_size;
	u32 explored_states_size;
	u32 num_backedges;
	/*
	 * The program's fd_array comes in two shapes, told apart by whether
	 * the caller passed fd_array_cnt. They are mutually exclusive:
	 *  - continuous (fd_array_cnt given): ->fd_array holds every entry
	 *    resolved to its object up front, indexed by fd_array position,
	 *    with ->fd_array_cnt slots; ->fd_array_raw is unused.
	 *  - sparse (no fd_array_cnt): ->fd_array is NULL, and entries are
	 *    read from ->fd_array_raw (the caller's fd_array) and resolved
	 *    on the spot at each reference.
	 */
	struct bpf_fd_array *fd_array;
	u32 fd_array_cnt;
	bpfptr_t fd_array_raw;

	/* bit mask to keep track of whether a register has been accessed
	 * since the last time the function state was printed
	 */
	u32 scratched_regs;
	/* Same as scratched_regs but for stack slots */
	DECLARE_BITMAP(scratched_stack_slots, MAX_BPF_STACK_SLOTS);
	u64 prev_log_pos, prev_insn_print_pos;
	/* buffer used to temporary hold constants as scalar registers */
	struct bpf_reg_state fake_reg[1];
	/* buffers used to save updated reg states while simulating branches */
	struct bpf_reg_state true_reg1, true_reg2, false_reg1, false_reg2;
	/* buffer used to generate temporary string representations,
	 * e.g., in reg_type_str() to generate reg_type string
	 */
	char tmp_str_buf[TMP_STR_BUF_LEN];
	char tmp_arg_name[32];
	struct bpf_insn insn_buf[INSN_BUF_SIZE];
	struct bpf_insn epilogue_buf[INSN_BUF_SIZE];
	struct bpf_scc_callchain callchain_buf;
	struct bpf_liveness *liveness;
	/* array of pointers to bpf_scc_info indexed by SCC id */
	struct bpf_scc_info **scc_info;
	u32 scc_cnt;
	struct bpf_iarray *succ;
	struct bpf_iarray *gotox_tmp_buf;
	int *idoms;
	struct scev *scev;
};

static inline struct bpf_func_info_aux *subprog_aux(struct bpf_verifier_env *env, int subprog)
{
	return &env->prog->aux->func_info_aux[subprog];
}

static inline struct bpf_subprog_info *subprog_info(struct bpf_verifier_env *env, int subprog)
{
	return &env->subprog_info[subprog];
}

static inline bool bpf_ret_reg_pair(struct bpf_verifier_env *env, int subprog)
{
	return subprog_info(env, subprog)->ret_reg_pair;
}

struct bpf_call_summary {
	u8 arg_slot_cnt;
	bool is_void;
	bool fastcall;
};

static inline bool bpf_helper_call(const struct bpf_insn *insn)
{
	return insn->code == (BPF_JMP | BPF_CALL) &&
	       insn->src_reg == 0;
}

static inline bool bpf_pseudo_call(const struct bpf_insn *insn)
{
	return insn->code == (BPF_JMP | BPF_CALL) &&
	       insn->src_reg == BPF_PSEUDO_CALL;
}

static inline bool bpf_pseudo_kfunc_call(const struct bpf_insn *insn)
{
	return insn->code == (BPF_JMP | BPF_CALL) &&
	       insn->src_reg == BPF_PSEUDO_KFUNC_CALL;
}

/* callx: indirect call of a bpf subprog whose address is in insn->dst_reg */
static inline bool bpf_is_callx(const struct bpf_insn *insn)
{
	return insn->code == (BPF_JMP | BPF_CALL | BPF_X);
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
void bpf_verbose_snum(struct bpf_verifier_env *env, s64 num);

#define verifier_bug_if(cond, env, fmt, args...)						\
	({											\
		bool __cond = (cond);								\
		if (unlikely(__cond))								\
			verifier_bug(env, fmt " (" #cond ")", ##args);				\
		(__cond);									\
	})
#define verifier_bug(env, fmt, args...)								\
	({											\
		BPF_WARN_ONCE(1, "verifier bug: " fmt "\n", ##args);				\
		bpf_log(&env->log, "verifier bug: " fmt "\n", ##args);				\
	})

static inline void mark_prune_point(struct bpf_verifier_env *env, int idx)
{
	env->insn_aux_data[idx].prune_point = true;
}

static inline bool bpf_is_prune_point(struct bpf_verifier_env *env, int insn_idx)
{
	return env->insn_aux_data[insn_idx].prune_point;
}

static inline void mark_force_checkpoint(struct bpf_verifier_env *env, int idx)
{
	env->insn_aux_data[idx].force_checkpoint = true;
}

static inline bool bpf_is_force_checkpoint(struct bpf_verifier_env *env, int insn_idx)
{
	return env->insn_aux_data[insn_idx].force_checkpoint;
}

static inline void mark_calls_callback(struct bpf_verifier_env *env, int idx)
{
	env->insn_aux_data[idx].calls_callback = true;
}

static inline bool bpf_calls_callback(struct bpf_verifier_env *env, int insn_idx)
{
	return env->insn_aux_data[insn_idx].calls_callback;
}

static inline void mark_jmp_point(struct bpf_verifier_env *env, int idx)
{
	env->insn_aux_data[idx].jmp_point = true;
}

static inline void mark_jump_target(struct bpf_verifier_env *env, int idx)
{
	env->insn_aux_data[idx].jump_target = true;
}

static inline bool bpf_is_jump_target(struct bpf_verifier_env *env, int insn_idx)
{
	return env->insn_aux_data[insn_idx].jump_target;
}

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

int bpf_prepare_btf_info(struct bpf_verifier_env *env,
			 const union bpf_attr *attr, bpfptr_t uattr);
int bpf_check_core_relo(struct bpf_verifier_env *env,
			const union bpf_attr *attr, bpfptr_t uattr);
int bpf_check_btf_info(struct bpf_verifier_env *env,
		       const union bpf_attr *attr, bpfptr_t uattr);

int bpf_check_attach_target(struct bpf_verifier_log *log,
			    const struct bpf_prog *prog,
			    const struct bpf_prog *tgt_prog,
			    u32 btf_id,
			    struct bpf_attach_target_info *tgt_info);
void bpf_free_kfunc_btf_tab(struct bpf_kfunc_btf_tab *tab);

int mark_chain_precision(struct bpf_verifier_env *env, int regno);

int bpf_is_state_visited(struct bpf_verifier_env *env, int insn_idx);
int bpf_split_cur_state(struct bpf_verifier_env *env);
int bpf_update_branch_counts(struct bpf_verifier_env *env, struct bpf_verifier_state *st);

void bpf_clear_jmp_history(struct bpf_verifier_state *state);
int bpf_copy_verifier_state(struct bpf_verifier_state *dst_state,
			    const struct bpf_verifier_state *src);
struct list_head *bpf_explored_state(struct bpf_verifier_env *env, int idx);
void bpf_free_verifier_state(struct bpf_verifier_state *state, bool free_self);
void bpf_free_backedges(struct bpf_scc_visit *visit);
int bpf_push_jmp_history(struct bpf_verifier_env *env, struct bpf_verifier_state *cur,
			 int insn_flags, int spi, int frame, const u16 *linked_regs,
			 u8 linked_regs_cnt);
void bpf_bt_sync_linked_regs(struct backtrack_state *bt, struct bpf_jmp_history_entry *hist);
void bpf_mark_reg_not_init(const struct bpf_verifier_env *env,
			   struct bpf_reg_state *reg);
void bpf_mark_reg_known_scalar(struct bpf_reg_state *reg, u64 imm);
void bpf_mark_reg_unknown_imprecise(struct bpf_reg_state *reg);
int bpf_grow_stack_state(struct bpf_verifier_env *env, struct bpf_func_state *state, int size);
void bpf_mark_all_scalars_precise(struct bpf_verifier_env *env,
				  struct bpf_verifier_state *st);
void bpf_clear_singular_ids(struct bpf_verifier_env *env, struct bpf_verifier_state *st);
int bpf_mark_chain_precision(struct bpf_verifier_env *env,
			     struct bpf_verifier_state *starting_state,
			     int regno, bool *changed);

static inline int bpf_get_spi(s32 off)
{
	return (-off - 1) / BPF_REG_SIZE;
}

/*
 * Stack a program may use in total: combined over the frames of a call
 * chain on the kernel stack, or per frame on a private stack. Any single
 * frame may reach that deep. Only a JIT that lays out such frames may go
 * beyond MAX_BPF_STACK, the interpreter's frame size, and only one whose
 * tail calls let the target set up its own frame: without subprogram
 * tail calls, do_misc_fixups() gives every program with tail calls a
 * MAX_BPF_STACK frame, which a deeper frame would overrun.
 */
static inline u32 bpf_prog_stack_limit(const struct bpf_prog *prog)
{
	/* an offloaded program never runs on the host JIT, whatever it supports */
	if (prog->jit_requested && !bpf_prog_is_offloaded(prog->aux) &&
	    bpf_jit_supports_large_stack() && bpf_jit_supports_subprog_tailcalls())
		return MAX_BPF_STACK_JIT;
	return MAX_BPF_STACK;
}

/*
 * Return the function state a stack pointer register refers to. frameno
 * shares storage with other pointer metadata, so return NULL for any
 * other register type instead of indexing frame[] with aliased bytes.
 */
static inline struct bpf_func_state *bpf_func(struct bpf_verifier_env *env,
					      const struct bpf_reg_state *reg)
{
	struct bpf_verifier_state *cur = env->cur_state;

	if (reg->type != PTR_TO_STACK)
		return NULL;
	return cur->frame[reg->frameno];
}

/* Return IP for a given frame in a call stack */
static inline u32 bpf_frame_insn_idx(struct bpf_verifier_state *st, u32 frame)
{
	return frame == st->curframe
	       ? st->insn_idx
	       : st->frame[frame + 1]->callsite;
}

static inline bool bpf_is_jmp_point(struct bpf_verifier_env *env, int insn_idx)
{
	return env->insn_aux_data[insn_idx].jmp_point;
}

static inline bool bpf_is_spilled_reg(const struct bpf_stack_state *stack)
{
	return stack->slot_type[BPF_REG_SIZE - 1] == STACK_SPILL;
}

static inline int bpf_spill_size(const struct bpf_stack_state *stack)
{
	int i, spill_size = 0;

	for (i = BPF_REG_SIZE - 1; i >= 0 && stack->slot_type[i] == STACK_SPILL; i--)
		spill_size++;
	return spill_size;
}

static inline bool bpf_is_spilled_scalar_reg(const struct bpf_stack_state *stack)
{
	return bpf_is_spilled_reg(stack) && stack->spilled_ptr.type == SCALAR_VALUE;
}

static inline bool bpf_register_is_null(struct bpf_reg_state *reg)
{
	return reg->type == SCALAR_VALUE && tnum_equals_const(reg->var_off, 0);
}

static inline void bpf_bt_set_frame_reg(struct backtrack_state *bt, u32 frame, u32 reg)
{
	bt->reg_masks[frame] |= 1 << reg;
}

static inline void bpf_bt_set_frame_slot(struct backtrack_state *bt, u32 frame, u32 slot)
{
	__set_bit(slot, bt->stack_masks[frame]);
}

static inline void bt_set_frame_stack_arg_slot(struct backtrack_state *bt, u32 frame, u32 slot)
{
	bt->stack_arg_masks[frame] |= 1 << slot;
}

static inline bool bt_is_frame_reg_set(struct backtrack_state *bt, u32 frame, u32 reg)
{
	return bt->reg_masks[frame] & (1 << reg);
}

static inline bool bt_is_frame_slot_set(struct backtrack_state *bt, u32 frame, u32 slot)
{
	return test_bit(slot, bt->stack_masks[frame]);
}

bool bpf_map_is_rdonly(const struct bpf_map *map);
struct bpf_func_ptr *bpf_map_func_ptrs(struct bpf_verifier_env *env,
				       const struct bpf_map *map, u32 *cnt);
struct bpf_func_ptr *bpf_map_range_func_ptrs(struct bpf_verifier_env *env,
					     const struct bpf_map *map,
					     u64 off, u64 size, u32 *cnt);
void bpf_adjust_func_ptrs(struct bpf_verifier_env *env, u32 off, u32 len);
void bpf_adjust_func_ptrs_after_remove(struct bpf_verifier_env *env, u32 off, u32 len);
int bpf_map_direct_read(struct bpf_map *map, int off, int size, u64 *val,
			bool is_ldsx);

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

static inline bool bpf_is_ptr_to_mem_or_btf_id(enum bpf_reg_type type)
{
	switch (base_type(type)) {
	case PTR_TO_MEM:
	case PTR_TO_BTF_ID:
		return true;
	default:
		return false;
	}
}

static inline bool bpf_may_fault_on_deref(enum bpf_reg_type type)
{
	/*
	 * The pointer types which must not be dereferenced without fault
	 * protection, that is, the ones bpf_convert_ctx_accesses() has to
	 * turn a BPF_LDX into a BPF_PROBE_MEM one for.
	 */
	return type == PTR_TO_BTF_ID || (type_flag(type) & PTR_UNTRUSTED);
}

static inline bool bpf_prog_has_arena_ctx_arg(const struct bpf_prog *prog)
{
	int i;

	for (i = 0; i < prog->aux->ctx_arg_info_size; i++)
		if (base_type(prog->aux->ctx_arg_info[i].reg_type) == PTR_TO_ARENA)
			return true;
	return false;
}

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
	case BPF_PROG_TYPE_SYSCALL:
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
	return base_type(type) == PTR_TO_BTF_ID &&
	       type_flag(type) & MEM_ALLOC &&
	       !(type_flag(type) & PTR_UNTRUSTED);
}

static inline bool type_is_non_owning_ref(u32 type)
{
	return type_is_ptr_alloc_obj(type) && type_flag(type) & NON_OWN_REF;
}

static inline bool type_is_map_ptr(enum bpf_reg_type type)
{
	switch (base_type(type)) {
	case CONST_PTR_TO_MAP:
	case PTR_TO_MAP_KEY:
	case PTR_TO_MAP_VALUE:
		return true;
	default:
		return false;
	}
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
	__set_bit(spi, env->scratched_stack_slots);
}

static inline bool reg_scratched(const struct bpf_verifier_env *env, u32 regno)
{
	return (env->scratched_regs >> regno) & 1;
}

static inline bool stack_slot_scratched(const struct bpf_verifier_env *env, u32 spi)
{
	return test_bit(spi, env->scratched_stack_slots);
}

static inline bool verifier_state_scratched(const struct bpf_verifier_env *env)
{
	return env->scratched_regs ||
	       !bitmap_empty(env->scratched_stack_slots, MAX_BPF_STACK_SLOTS);
}

static inline void mark_verifier_state_clean(struct bpf_verifier_env *env)
{
	env->scratched_regs = 0U;
	bitmap_zero(env->scratched_stack_slots, MAX_BPF_STACK_SLOTS);
}

/* Used for printing the entire verifier state. */
static inline void mark_verifier_state_scratched(struct bpf_verifier_env *env)
{
	env->scratched_regs = ~0U;
	bitmap_fill(env->scratched_stack_slots, MAX_BPF_STACK_SLOTS);
}

static inline bool bpf_stack_narrow_access_ok(int off, int fill_size, int spill_size)
{
#ifdef __BIG_ENDIAN
	off -= spill_size - fill_size;
#endif

	return !(off % BPF_REG_SIZE);
}

static inline bool insn_is_gotox(struct bpf_insn *insn)
{
	return BPF_CLASS(insn->code) == BPF_JMP &&
	       BPF_OP(insn->code) == BPF_JA &&
	       BPF_SRC(insn->code) == BPF_X;
}

const char *reg_type_str(struct bpf_verifier_env *env, enum bpf_reg_type type);
const char *dynptr_type_str(enum bpf_dynptr_type type);
const char *iter_type_str(const struct btf *btf, u32 btf_id);
const char *iter_state_str(enum bpf_iter_state state);

void print_verifier_state(struct bpf_verifier_env *env, const struct bpf_verifier_state *vstate,
			  u32 frameno, bool print_all);
void print_insn_state(struct bpf_verifier_env *env, const struct bpf_verifier_state *vstate,
		      u32 frameno);
u32 bpf_vlog_alignment(u32 pos);
const char *bpf_disasm_kfunc_name(void *data, const struct bpf_insn *insn);

struct bpf_subprog_info *bpf_find_containing_subprog(struct bpf_verifier_env *env, int off);
const char *bpf_subprog_name(const struct bpf_verifier_env *env, int subprog);
int bpf_jmp_offset(struct bpf_insn *insn);
struct bpf_iarray *bpf_insn_successors(struct bpf_verifier_env *env, u32 idx);
void bpf_fmt_stack_mask(char *buf, ssize_t buf_sz, const unsigned long *stack_mask);
bool bpf_subprog_is_global(const struct bpf_verifier_env *env, int subprog);

/* Kinds of member a by-value struct or union may be composed of. */
enum btf_member_kind {
	BTF_MEMBER_SCALAR	= BIT(0), /* an int or an enum */
	BTF_MEMBER_ARENA_PTR	= BIT(1), /* a pointer carrying the "arena" type tag */
};

bool btf_struct_is_composed_of(struct bpf_verifier_env *env, const struct btf *btf,
			       const struct btf_type *t, u32 member_kinds);
u32 btf_func_arg_align(const struct btf *btf, const struct btf_type *t);

int bpf_find_subprog(struct bpf_verifier_env *env, int off);
bool bpf_is_throw_kfunc(struct bpf_insn *insn);
int bpf_compute_const_regs(struct bpf_verifier_env *env);
int bpf_prune_dead_branches(struct bpf_verifier_env *env);
int bpf_check_cfg(struct bpf_verifier_env *env);
void bpf_free_subprog_jts(struct bpf_verifier_env *env);
int bpf_compute_postorder(struct bpf_verifier_env *env);
int bpf_compute_scc(struct bpf_verifier_env *env);

struct bpf_map_desc {
	struct bpf_map *ptr;
	int uid;
};

/* The last initialized dynptr; Populated by process_dynptr_func() */
struct bpf_dynptr_desc {
	enum bpf_dynptr_type type;
	u32 id;
	u32 parent_id;
};

/*
 * The last seen rereferenced object; Updated by update_ref_obj() when a register refers to a
 * referenced object. Used when the helper or kfunc is casting a referenced object, returning
 * allocated memory derived from referenced object or creating a dynptr with a referenced
 * object as parent.
 */
struct ref_obj_desc {
	u32 id;
	u32 parent_id;
	u8 cnt;
};

/*
 * Generic MEM_UNINIT arguments, indexed by ABI slot. var_size_mask excludes
 * variable-sized buffers from raw mode without losing the output annotation.
 * size records constant ranges to mark initialized after checking all arguments,
 * only when the caller is allowed to read uninitialized stack memory.
 */
struct arg_raw_mem_desc {
	u16 mask;
	u16 var_size_mask;
	int size[MAX_BPF_FUNC_ARGS];
};

/* Size of PTR_TO_MEM returned, taken from a constant allocation-size argument */
struct ret_mem_desc {
	u32 size;
	bool found;
};

/* A constant scalar argument; Populated by process_const_arg() */
struct arg_constant_desc {
	u64 value;
	bool found;
};

struct bpf_call_arg_meta {
	/* Common */
	struct btf *btf;
	u32 func_id;
	const struct bpf_func_proto *fn;
	const struct btf_type *func_proto;
	u8 release_regno;
	u32 ret_btf_id;
	u32 subprogno;
	struct bpf_map_desc map;
	struct bpf_dynptr_desc dynptr;
	struct ref_obj_desc ref_obj;
	struct ret_mem_desc ret_mem;
	struct arg_raw_mem_desc arg_raw_mem;

	/* Only set by subprog */
	bool subprog_may_change_pkt;

	/* Only set by kfunc */
	bool r0_rdonly;
	u32 kfunc_flags;
	const char *func_name;
	struct arg_constant_desc arg_constant;

	/* arg_{btf,btf_id,owning_ref} are used by kfunc-specific handling,
	 * generally to pass info about user-defined local kptr types to later
	 * verification logic
	 *   bpf_obj_drop/bpf_percpu_obj_drop
	 *     Record the local kptr type to be drop'd
	 *   bpf_refcount_acquire (via ARG_PTR_TO_REFCOUNTED_KPTR arg type)
	 *     Record the local kptr type to be refcount_incr'd and use
	 *     arg_owning_ref to determine whether refcount_acquire should be
	 *     fallible
	 */
	struct btf *arg_btf;
	u32 arg_btf_id;
	bool arg_owning_ref;

	struct {
		struct btf_field *field;
	} arg_list_head;
	struct {
		struct btf_field *field;
	} arg_rbtree_root;
	struct {
		u8 spi;
		u8 frameno;
	} iter;

	/* Only set by helper */
	u64 msize_max_value;
	s64 const_map_key;
	struct btf *ret_btf;
	struct btf_field *kptr_field;
};

int bpf_get_helper_proto(struct bpf_verifier_env *env, int func_id,
			 const struct bpf_func_proto **ptr);
int bpf_fetch_kfunc_arg_meta(struct bpf_verifier_env *env, s32 func_id,
			     s16 offset, struct bpf_call_arg_meta *meta);
bool bpf_is_async_callback_calling_insn(struct bpf_insn *insn);
bool bpf_is_sync_callback_calling_insn(struct bpf_insn *insn);
static inline bool bpf_is_iter_next_kfunc(struct bpf_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_ITER_NEXT;
}

static inline bool bpf_is_kfunc_sleepable(struct bpf_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_SLEEPABLE;
}
bool bpf_is_kfunc_pkt_changing(struct bpf_call_arg_meta *meta);
struct bpf_iarray *bpf_iarray_realloc(struct bpf_iarray *old, size_t n_elem);
int bpf_copy_insn_array_uniq(struct bpf_map *map, u32 start, u32 end, u32 *off);
bool bpf_insn_is_cond_jump(u8 code);
bool bpf_is_may_goto_insn(struct bpf_insn *insn);

void bpf_verbose_insn(struct bpf_verifier_env *env, struct bpf_insn *insn);
bool bpf_get_call_summary(struct bpf_verifier_env *env, struct bpf_insn *call,
			  struct bpf_call_summary *cs);
/* Stack effects used for state pruning; must_write implies may_write. */
struct arg_access_info {
	u32 size;		/* Maximum extent; U32_MAX if unknown. */
	u8 may_read:1;		/* Incoming contents or initialization may be needed. */
	u8 may_write:1;
	u8 must_write:1;	/* Prior verifier state is destroyed throughout size. */
};

struct arg_access_info
bpf_helper_stack_access_bytes(struct bpf_verifier_env *env,
			      struct bpf_insn *insn, int arg, int insn_idx);
struct arg_access_info
bpf_kfunc_stack_access_bytes(struct bpf_verifier_env *env,
			     struct bpf_insn *insn, int arg, int insn_idx);
struct arg_access_info
bpf_global_subprog_stack_access_bytes(struct bpf_verifier_env *env,
				      struct bpf_insn *insn, int arg, int insn_idx);
int bpf_compute_subprog_arg_access(struct bpf_verifier_env *env);
bool bpf_same_memory_origin(const struct bpf_reg_state *reg_a,
			    const struct bpf_reg_state *reg_b);
int bpf_set_reg_range(struct bpf_verifier_env *env, struct bpf_reg_state *reg,
		      struct cnum64 range, u16 base, u16 step);
bool bpf_reg_union_compatible(const struct bpf_reg_state *a, const struct bpf_reg_state *b);
int bpf_reg_union(struct bpf_verifier_env *env, struct bpf_reg_state *acc,
		  const struct bpf_reg_state *src);

int bpf_stack_liveness_init(struct bpf_verifier_env *env);
void bpf_stack_liveness_free(struct bpf_verifier_env *env);
int bpf_live_stack_query_init(struct bpf_verifier_env *env, struct bpf_verifier_state *st);
const unsigned long *bpf_may_write_mask(struct bpf_verifier_env *env, u32 insn_idx);
bool bpf_needs_fixed_stack_off(struct bpf_verifier_env *env, int insn_idx);
bool bpf_stack_slot_alive(struct bpf_verifier_env *env, u32 frameno, u32 spi);
int bpf_compute_live_registers(struct bpf_verifier_env *env);

#define BPF_MAP_KEY_POISON	(1ULL << 63)
#define BPF_MAP_KEY_SEEN	(1ULL << 62)

static inline bool bpf_map_ptr_poisoned(const struct bpf_insn_aux_data *aux)
{
	return aux->map_ptr_state.poison;
}

static inline bool bpf_map_ptr_unpriv(const struct bpf_insn_aux_data *aux)
{
	return aux->map_ptr_state.unpriv;
}

static inline bool bpf_map_key_poisoned(const struct bpf_insn_aux_data *aux)
{
	return aux->map_key_state & BPF_MAP_KEY_POISON;
}

static inline bool bpf_map_key_unseen(const struct bpf_insn_aux_data *aux)
{
	return !(aux->map_key_state & BPF_MAP_KEY_SEEN);
}

static inline u64 bpf_map_key_immediate(const struct bpf_insn_aux_data *aux)
{
	return aux->map_key_state & ~(BPF_MAP_KEY_SEEN | BPF_MAP_KEY_POISON);
}

static inline bool bpf_is_mem_insn(struct bpf_insn *insn)
{
	if (BPF_CLASS(insn->code) != BPF_ST &&
	    BPF_CLASS(insn->code) != BPF_STX &&
	    BPF_CLASS(insn->code) != BPF_LDX)
		return false;

	if (insn->code == (BPF_ST | BPF_NOSPEC))
		return false;

	return (BPF_MODE(insn->code) == BPF_MEM ||
		BPF_MODE(insn->code) == BPF_MEMSX ||
		BPF_MODE(insn->code) == BPF_ATOMIC);
}

#define MAX_PACKET_OFF 0xffff
#define CALLER_SAVED_REGS 6

enum bpf_reg_arg_type {
	SRC_OP,		/* register is used as source operand */
	DST_OP,		/* register is used as destination operand */
	DST_OP_NO_MARK	/* same as above, check only, don't mark */
};

#define MAX_KFUNC_DESCS 256
/* Each kfunc can have its canonical and one specialized call target. */
#define MAX_KFUNC_CALL_DESCS (MAX_KFUNC_DESCS * 2)
static_assert(MAX_KFUNC_CALL_DESCS <= S16_MAX + 1);

struct bpf_kfunc_desc {
	struct btf_func_model func_model;
	struct bpf_func_proto proto;
	u32 func_id;
	u16 offset;
	unsigned long addr;
};

struct bpf_kfunc_desc_tab {
	u32 nr_descs;
	u32 nr_base_descs;
	/* Sorted by func_id (BTF ID) and offset (fd_array offset) during
	 * verification. JITs use the descriptor index stored in the finalized
	 * call's off field. The first nr_base_descs entries are the canonical
	 * descriptors used for verifier lookups. Call specialization may append
	 * immutable descriptors for additional targets.
	 *
	 * Grown one entry at a time by bpf_add_kfunc_call() and during
	 * call specialization.
	 */
	struct bpf_kfunc_desc descs[];
};

/* Functions exported from verifier.c, used by fixups.c */
void bpf_mark_subprog_exc_cb(struct bpf_verifier_env *env, int subprog);
bool bpf_allow_tail_call_in_subprogs(struct bpf_verifier_env *env);
bool bpf_verifier_inlines_helper_call(struct bpf_verifier_env *env, s32 imm);
int bpf_add_kfunc_call(struct bpf_verifier_env *env, u32 func_id, u16 offset);
int bpf_fixup_kfunc_call(struct bpf_verifier_env *env, struct bpf_insn *insn,
			 struct bpf_insn *insn_buf, int insn_idx, int *cnt);

/* Functions exported from verifier.c, used by trampoline.c */
int bpf_check_attach_btf_id_multi(struct btf *btf, struct bpf_prog *prog, u32 btf_id,
				  struct bpf_attach_target_info *tgt_info);

/* Functions in fixups.c, called from bpf_check() */
void bpf_clear_insn_aux_data(struct bpf_verifier_env *env, int start, int len);
int bpf_remove_fastcall_spills_fills(struct bpf_verifier_env *env);
int bpf_optimize_bpf_loop(struct bpf_verifier_env *env);
void bpf_opt_hard_wire_dead_code_branches(struct bpf_verifier_env *env);
int bpf_opt_remove_dead_code(struct bpf_verifier_env *env);
int bpf_opt_remove_nops(struct bpf_verifier_env *env);
int bpf_opt_subreg_zext_lo32_rnd_hi32(struct bpf_verifier_env *env, const union bpf_attr *attr);
int bpf_convert_ctx_accesses(struct bpf_verifier_env *env);
int bpf_jit_subprogs(struct bpf_verifier_env *env);
int bpf_fixup_call_args(struct bpf_verifier_env *env);
int bpf_do_misc_fixups(struct bpf_verifier_env *env);
int bpf_insn_def32(struct bpf_prog *prog, struct bpf_insn *insn);

int bpf_flip_opcode(u32 opcode);
u8 bpf_rev_opcode(u8 opcode);

int bpf_compute_idoms(struct bpf_verifier_env *env);
int bpf_compute_loops(struct bpf_verifier_env *env);
int bpf_loop_at_index(struct bpf_verifier_env *env, u32 idx);
bool bpf_is_nested_loop(struct bpf_verifier_env *env, int inner_header, int outer_header);

int bpf_init_scev(struct bpf_verifier_env *env);
void bpf_free_scev(struct bpf_verifier_env *env);
int bpf_compute_scev(struct bpf_verifier_env *env);

int bpf_compute_loop_iters(struct bpf_verifier_env *env, struct bpf_verifier_state *st,
			   struct bpf_loop_iters *iters);
int bpf_widen_scev_regs(struct bpf_verifier_env *env, struct bpf_verifier_state *st,
			struct bpf_verifier_state *loop_entry, struct bpf_loop_iters *iters);
int bpf_clamp_scev_regs(struct bpf_verifier_env *env, struct bpf_func_state *st, u32 insn_idx,
			struct bpf_verifier_state *entry_state, u32 max_header_count);

#endif /* _LINUX_BPF_VERIFIER_H */
