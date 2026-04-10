// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf_verifier.h>
#include <linux/btf.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>
#include <linux/slab.h>

#define verbose(env, fmt, args...) bpf_verifier_log_write(env, fmt, ##args)

/*
 * This file implements live stack slots analysis. After accumulating
 * stack usage data, the analysis answers queries about whether a
 * particular stack slot may be read by an instruction or any of it's
 * successors.  This data is consumed by the verifier states caching
 * mechanism to decide which stack slots are important when looking for a
 * visited state corresponding to the current state.
 *
 * The analysis is call chain sensitive, meaning that data is collected
 * and queried for tuples (call chain, subprogram instruction index).
 * Such sensitivity allows identifying if some subprogram call always
 * leads to writes in the caller's stack.
 *
 * The basic idea is as follows:
 * - As the verifier accumulates a set of visited states, the analysis instance
 *   accumulates a conservative estimate of stack slots that can be read
 *   or must be written for each visited tuple (call chain, instruction index).
 * - If several states happen to visit the same instruction with the same
 *   call chain, stack usage information for the corresponding tuple is joined:
 *   - "may_read" set represents a union of all possibly read slots
 *     (any slot in "may_read" set might be read at or after the instruction);
 *   - "must_write" set represents an intersection of all possibly written slots
 *     (any slot in "must_write" set is guaranteed to be written by the instruction).
 * - The analysis is split into two phases:
 *   - read and write marks accumulation;
 *   - read and write marks propagation.
 * - The propagation phase is a textbook live variable data flow analysis:
 *
 *     state[cc, i].live_after = U [state[cc, s].live_before for s in bpf_insn_successors(i)]
 *     state[cc, i].live_before =
 *       (state[cc, i].live_after / state[cc, i].must_write) U state[i].may_read
 *
 *   Where:
 *   - `U`  stands for set union
 *   - `/`  stands for set difference;
 *   - `cc` stands for a call chain;
 *   - `i` and `s` are instruction indexes;
 *
 *   The above equations are computed for each call chain and instruction
 *   index until state stops changing.
 * - Additionally, in order to transfer "must_write" information from a
 *   subprogram to call instructions invoking this subprogram,
 *   the "must_write_acc" set is tracked for each (cc, i) tuple.
 *   A set of stack slots that are guaranteed to be written by this
 *   instruction or any of its successors (within the subprogram).
 *   The equation for "must_write_acc" propagation looks as follows:
 *
 *     state[cc, i].must_write_acc =
 *       ∩ [state[cc, s].must_write_acc for s in bpf_insn_successors(i)]
 *       U state[cc, i].must_write
 *
 *   (An intersection of all "must_write_acc" for instruction successors
 *    plus all "must_write" slots for the instruction itself).
 * - After the propagation phase completes for a subprogram, information from
 *   (cc, 0) tuple (subprogram entry) is transferred to the caller's call chain:
 *   - "must_write_acc" set is intersected with the call site's "must_write" set;
 *   - "may_read" set is added to the call site's "may_read" set.
 * - Any live stack queries must be taken after the propagation phase.
 * - Accumulation and propagation phases can be entered multiple times,
 *   at any point in time:
 *   - "may_read" set only grows;
 *   - "must_write" set only shrinks;
 *   - for each visited verifier state with zero branches, all relevant
 *     read and write marks are already recorded by the analysis instance.
 *
 * Technically, the analysis is facilitated by the following data structures:
 * - Call chain: for given verifier state, the call chain is a tuple of call
 *   instruction indexes leading to the current subprogram plus the subprogram
 *   entry point index.
 * - Function instance: for a given call chain, for each instruction in
 *   the current subprogram, a mapping between instruction index and a
 *   set of "may_read", "must_write" and other marks accumulated for this
 *   instruction.
 * - A hash table mapping call chains to function instances.
 */

struct callchain {
	u32 callsites[MAX_CALL_FRAMES];	/* instruction pointer for each frame */
	/* cached subprog_info[*].start for functions owning the frames:
	 * - sp_starts[curframe] used to get insn relative index within current function;
	 * - sp_starts[0..current-1] used for fast callchain_frame_up().
	 */
	u32 sp_starts[MAX_CALL_FRAMES];
	u32 curframe;			/* depth of callsites and sp_starts arrays */
};

struct per_frame_masks {
	spis_t may_read;	/* stack slots that may be read by this instruction */
	spis_t must_write;	/* stack slots written by this instruction */
	spis_t must_write_acc;	/* stack slots written by this instruction and its successors */
	spis_t live_before;	/* stack slots that may be read by this insn and its successors */
};

/*
 * A function instance created for a specific callchain.
 * Encapsulates read and write marks for each instruction in the function.
 * Marks are tracked for each frame in the callchain.
 */
struct func_instance {
	struct hlist_node hl_node;
	struct callchain callchain;
	u32 subprog;		/* subprog index */
	u32 insn_cnt;		/* cached number of insns in the function */
	bool updated;
	bool must_write_dropped;
	/* Per frame, per instruction masks, frames allocated lazily. */
	struct per_frame_masks *frames[MAX_CALL_FRAMES];
	/* For each instruction a flag telling if "must_write" had been initialized for it. */
	bool *must_write_set;
};

struct live_stack_query {
	struct func_instance *instances[MAX_CALL_FRAMES]; /* valid in range [0..curframe] */
	u32 curframe;
	u32 insn_idx;
};

struct bpf_liveness {
	DECLARE_HASHTABLE(func_instances, 8);		/* maps callchain to func_instance */
	struct live_stack_query live_stack_query;	/* cache to avoid repetitive ht lookups */
	/* Cached instance corresponding to env->cur_state, avoids per-instruction ht lookup */
	struct func_instance *cur_instance;
	/*
	 * Below fields are used to accumulate stack write marks for instruction at
	 * @write_insn_idx before submitting the marks to @cur_instance.
	 */
	spis_t write_masks_acc[MAX_CALL_FRAMES];
	u32 write_insn_idx;
	u32 subprog_calls;				/* analyze_subprog() invocations */
};

/* Compute callchain corresponding to state @st at depth @frameno */
static void compute_callchain(struct bpf_verifier_env *env, struct bpf_verifier_state *st,
			      struct callchain *callchain, u32 frameno)
{
	struct bpf_subprog_info *subprog_info = env->subprog_info;
	u32 i;

	memset(callchain, 0, sizeof(*callchain));
	for (i = 0; i <= frameno; i++) {
		callchain->sp_starts[i] = subprog_info[st->frame[i]->subprogno].start;
		if (i < st->curframe)
			callchain->callsites[i] = st->frame[i + 1]->callsite;
	}
	callchain->curframe = frameno;
	callchain->callsites[callchain->curframe] = callchain->sp_starts[callchain->curframe];
}

static u32 hash_callchain(struct callchain *callchain)
{
	return jhash2(callchain->callsites, callchain->curframe, 0);
}

static bool same_callsites(struct callchain *a, struct callchain *b)
{
	int i;

	if (a->curframe != b->curframe)
		return false;
	for (i = a->curframe; i >= 0; i--)
		if (a->callsites[i] != b->callsites[i])
			return false;
	return true;
}

/*
 * Find existing or allocate new function instance corresponding to @callchain.
 * Instances are accumulated in env->liveness->func_instances and persist
 * until the end of the verification process.
 */
static struct func_instance *__lookup_instance(struct bpf_verifier_env *env,
					       struct callchain *callchain)
{
	struct bpf_liveness *liveness = env->liveness;
	struct bpf_subprog_info *subprog;
	struct func_instance *result;
	u32 subprog_sz, size, key;

	key = hash_callchain(callchain);
	hash_for_each_possible(liveness->func_instances, result, hl_node, key)
		if (same_callsites(&result->callchain, callchain))
			return result;

	subprog = bpf_find_containing_subprog(env, callchain->sp_starts[callchain->curframe]);
	subprog_sz = (subprog + 1)->start - subprog->start;
	size = sizeof(struct func_instance);
	result = kvzalloc(size, GFP_KERNEL_ACCOUNT);
	if (!result)
		return ERR_PTR(-ENOMEM);
	result->must_write_set = kvzalloc_objs(*result->must_write_set,
					       subprog_sz, GFP_KERNEL_ACCOUNT);
	if (!result->must_write_set) {
		kvfree(result);
		return ERR_PTR(-ENOMEM);
	}
	memcpy(&result->callchain, callchain, sizeof(*callchain));
	result->subprog = subprog - env->subprog_info;
	result->insn_cnt = subprog_sz;
	hash_add(liveness->func_instances, &result->hl_node, key);
	return result;
}

static struct func_instance *call_instance(struct bpf_verifier_env *env,
					   struct func_instance *caller,
					   u32 callsite, int subprog)
{
	struct callchain cc;

	if (caller) {
		cc = caller->callchain;
		cc.callsites[cc.curframe] = callsite;
		cc.curframe++;
	} else {
		memset(&cc, 0, sizeof(cc));
	}
	cc.sp_starts[cc.curframe] = env->subprog_info[subprog].start;
	cc.callsites[cc.curframe] = cc.sp_starts[cc.curframe];
	return __lookup_instance(env, &cc);
}

static struct func_instance *lookup_instance(struct bpf_verifier_env *env,
					     struct bpf_verifier_state *st,
					     u32 frameno)
{
	struct callchain callchain;

	compute_callchain(env, st, &callchain, frameno);
	return __lookup_instance(env, &callchain);
}

int bpf_stack_liveness_init(struct bpf_verifier_env *env)
{
	env->liveness = kvzalloc_obj(*env->liveness, GFP_KERNEL_ACCOUNT);
	if (!env->liveness)
		return -ENOMEM;
	hash_init(env->liveness->func_instances);
	return 0;
}

void bpf_stack_liveness_free(struct bpf_verifier_env *env)
{
	struct func_instance *instance;
	struct hlist_node *tmp;
	int bkt, i;

	if (!env->liveness)
		return;
	hash_for_each_safe(env->liveness->func_instances, bkt, tmp, instance, hl_node) {
		for (i = 0; i <= instance->callchain.curframe; i++)
			kvfree(instance->frames[i]);
		kvfree(instance->must_write_set);
		kvfree(instance);
	}
	kvfree(env->liveness);
}

/*
 * Convert absolute instruction index @insn_idx to an index relative
 * to start of the function corresponding to @instance.
 */
static int relative_idx(struct func_instance *instance, u32 insn_idx)
{
	return insn_idx - instance->callchain.sp_starts[instance->callchain.curframe];
}

static struct per_frame_masks *get_frame_masks(struct func_instance *instance,
					       u32 frame, u32 insn_idx)
{
	if (!instance->frames[frame])
		return NULL;

	return &instance->frames[frame][relative_idx(instance, insn_idx)];
}

static struct per_frame_masks *alloc_frame_masks(struct func_instance *instance,
						 u32 frame, u32 insn_idx)
{
	struct per_frame_masks *arr;

	if (!instance->frames[frame]) {
		arr = kvzalloc_objs(*arr, instance->insn_cnt,
				    GFP_KERNEL_ACCOUNT);
		instance->frames[frame] = arr;
		if (!arr)
			return ERR_PTR(-ENOMEM);
	}
	return get_frame_masks(instance, frame, insn_idx);
}

void bpf_reset_live_stack_callchain(struct bpf_verifier_env *env)
{
	env->liveness->cur_instance = NULL;
}

/* If @env->liveness->cur_instance is null, set it to instance corresponding to @env->cur_state. */
static int ensure_cur_instance(struct bpf_verifier_env *env)
{
	struct bpf_liveness *liveness = env->liveness;
	struct func_instance *instance;

	if (liveness->cur_instance)
		return 0;

	instance = lookup_instance(env, env->cur_state, env->cur_state->curframe);
	if (IS_ERR(instance))
		return PTR_ERR(instance);

	liveness->cur_instance = instance;
	return 0;
}

/* Accumulate may_read masks for @frame at @insn_idx */
static int mark_stack_read(struct func_instance *instance, u32 frame, u32 insn_idx, spis_t mask)
{
	struct per_frame_masks *masks;
	spis_t new_may_read;

	masks = alloc_frame_masks(instance, frame, insn_idx);
	if (IS_ERR(masks))
		return PTR_ERR(masks);
	new_may_read = spis_or(masks->may_read, mask);
	if (!spis_equal(new_may_read, masks->may_read) &&
	    !spis_equal(spis_or(new_may_read, masks->live_before),
				masks->live_before))
		instance->updated = true;
	masks->may_read = spis_or(masks->may_read, mask);
	return 0;
}

int bpf_mark_stack_read(struct bpf_verifier_env *env, u32 frame, u32 insn_idx, spis_t mask)
{
	int err;

	err = ensure_cur_instance(env);
	err = err ?: mark_stack_read(env->liveness->cur_instance, frame, insn_idx, mask);
	return err;
}

static void reset_stack_write_marks(struct bpf_verifier_env *env, struct func_instance *instance)
{
	struct bpf_liveness *liveness = env->liveness;
	int i;

	for (i = 0; i <= instance->callchain.curframe; i++)
		liveness->write_masks_acc[i] = SPIS_ZERO;
}

int bpf_reset_stack_write_marks(struct bpf_verifier_env *env, u32 insn_idx)
{
	struct bpf_liveness *liveness = env->liveness;
	int err;

	err = ensure_cur_instance(env);
	if (err)
		return err;

	liveness->write_insn_idx = insn_idx;
	reset_stack_write_marks(env, liveness->cur_instance);
	return 0;
}

void bpf_mark_stack_write(struct bpf_verifier_env *env, u32 frame, spis_t mask)
{
	env->liveness->write_masks_acc[frame] = spis_or(env->liveness->write_masks_acc[frame], mask);
}

static int commit_stack_write_marks(struct bpf_verifier_env *env,
				    struct func_instance *instance,
				    u32 insn_idx)
{
	struct bpf_liveness *liveness = env->liveness;
	u32 idx, frame, curframe;
	struct per_frame_masks *masks;
	spis_t mask, old_must_write, dropped;

	if (!instance)
		return 0;

	curframe = instance->callchain.curframe;
	idx = relative_idx(instance, insn_idx);
	for (frame = 0; frame <= curframe; frame++) {
		mask = liveness->write_masks_acc[frame];
		/* avoid allocating frames for zero masks */
		if (spis_is_zero(mask) && !instance->must_write_set[idx])
			continue;
		masks = alloc_frame_masks(instance, frame, insn_idx);
		if (IS_ERR(masks))
			return PTR_ERR(masks);
		old_must_write = masks->must_write;
		/*
		 * If instruction at this callchain is seen for a first time, set must_write equal
		 * to @mask. Otherwise take intersection with the previous value.
		 */
		if (instance->must_write_set[idx])
			mask = spis_and(mask, old_must_write);
		if (!spis_equal(old_must_write, mask)) {
			masks->must_write = mask;
			instance->updated = true;
		}
		/* dropped = old_must_write & ~mask */
		dropped = spis_and(old_must_write, spis_not(mask));
		if (!spis_is_zero(dropped))
			instance->must_write_dropped = true;
	}
	instance->must_write_set[idx] = true;
	liveness->write_insn_idx = 0;
	return 0;
}

/*
 * Merge stack writes marks in @env->liveness->write_masks_acc
 * with information already in @env->liveness->cur_instance.
 */
int bpf_commit_stack_write_marks(struct bpf_verifier_env *env)
{
	return commit_stack_write_marks(env, env->liveness->cur_instance, env->liveness->write_insn_idx);
}

static char *fmt_callchain(struct bpf_verifier_env *env, struct callchain *callchain)
{
	char *buf_end = env->tmp_str_buf + sizeof(env->tmp_str_buf);
	char *buf = env->tmp_str_buf;
	int i;

	buf += snprintf(buf, buf_end - buf, "(");
	for (i = 0; i <= callchain->curframe; i++)
		buf += snprintf(buf, buf_end - buf, "%s%d", i ? "," : "", callchain->callsites[i]);
	snprintf(buf, buf_end - buf, ")");
	return env->tmp_str_buf;
}

/*
 * When both halves of an 8-byte SPI are set, print as "-8","-16",...
 * When only one half is set, print as "-4h","-8h",...
 */
static void bpf_fmt_spis_mask(char *buf, ssize_t buf_sz, spis_t spis)
{
	bool first = true;
	int spi, n;

	buf[0] = '\0';

	for (spi = 0; spi < STACK_SLOTS / 2 && buf_sz > 0; spi++) {
		bool lo = spis_test_bit(spis, spi * 2);
		bool hi = spis_test_bit(spis, spi * 2 + 1);

		if (!lo && !hi)
			continue;
		n = snprintf(buf, buf_sz, "%s%d%s",
			     first ? "" : ",",
			     -(spi + 1) * BPF_REG_SIZE + (lo && !hi ? BPF_HALF_REG_SIZE : 0),
			     lo && hi ? "" : "h");
		first = false;
		buf += n;
		buf_sz -= n;
	}
}

static void log_mask_change(struct bpf_verifier_env *env, struct callchain *callchain,
			    char *pfx, u32 frame, u32 insn_idx,
			    spis_t old, spis_t new)
{
	spis_t changed_bits, new_ones, new_zeros;

	changed_bits = spis_xor(old, new);
	new_ones = spis_and(new, changed_bits);
	new_zeros = spis_and(spis_not(new), changed_bits);

	if (spis_is_zero(changed_bits))
		return;
	bpf_log(&env->log, "%s frame %d insn %d ", fmt_callchain(env, callchain), frame, insn_idx);
	if (!spis_is_zero(new_ones)) {
		bpf_fmt_spis_mask(env->tmp_str_buf, sizeof(env->tmp_str_buf), new_ones);
		bpf_log(&env->log, "+%s %s ", pfx, env->tmp_str_buf);
	}
	if (!spis_is_zero(new_zeros)) {
		bpf_fmt_spis_mask(env->tmp_str_buf, sizeof(env->tmp_str_buf), new_zeros);
		bpf_log(&env->log, "-%s %s", pfx, env->tmp_str_buf);
	}
	bpf_log(&env->log, "\n");
}

int bpf_jmp_offset(struct bpf_insn *insn)
{
	u8 code = insn->code;

	if (code == (BPF_JMP32 | BPF_JA))
		return insn->imm;
	return insn->off;
}

__diag_push();
__diag_ignore_all("-Woverride-init", "Allow field initialization overrides for opcode_info_tbl");

/*
 * Returns an array of instructions succ, with succ->items[0], ...,
 * succ->items[n-1] with successor instructions, where n=succ->cnt
 */
inline struct bpf_iarray *
bpf_insn_successors(struct bpf_verifier_env *env, u32 idx)
{
	static const struct opcode_info {
		bool can_jump;
		bool can_fallthrough;
	} opcode_info_tbl[256] = {
		[0 ... 255] = {.can_jump = false, .can_fallthrough = true},
	#define _J(code, ...) \
		[BPF_JMP   | code] = __VA_ARGS__, \
		[BPF_JMP32 | code] = __VA_ARGS__

		_J(BPF_EXIT,  {.can_jump = false, .can_fallthrough = false}),
		_J(BPF_JA,    {.can_jump = true,  .can_fallthrough = false}),
		_J(BPF_JEQ,   {.can_jump = true,  .can_fallthrough = true}),
		_J(BPF_JNE,   {.can_jump = true,  .can_fallthrough = true}),
		_J(BPF_JLT,   {.can_jump = true,  .can_fallthrough = true}),
		_J(BPF_JLE,   {.can_jump = true,  .can_fallthrough = true}),
		_J(BPF_JGT,   {.can_jump = true,  .can_fallthrough = true}),
		_J(BPF_JGE,   {.can_jump = true,  .can_fallthrough = true}),
		_J(BPF_JSGT,  {.can_jump = true,  .can_fallthrough = true}),
		_J(BPF_JSGE,  {.can_jump = true,  .can_fallthrough = true}),
		_J(BPF_JSLT,  {.can_jump = true,  .can_fallthrough = true}),
		_J(BPF_JSLE,  {.can_jump = true,  .can_fallthrough = true}),
		_J(BPF_JCOND, {.can_jump = true,  .can_fallthrough = true}),
		_J(BPF_JSET,  {.can_jump = true,  .can_fallthrough = true}),
	#undef _J
	};
	struct bpf_prog *prog = env->prog;
	struct bpf_insn *insn = &prog->insnsi[idx];
	const struct opcode_info *opcode_info;
	struct bpf_iarray *succ, *jt;
	int insn_sz;

	jt = env->insn_aux_data[idx].jt;
	if (unlikely(jt))
		return jt;

	/* pre-allocated array of size up to 2; reset cnt, as it may have been used already */
	succ = env->succ;
	succ->cnt = 0;

	opcode_info = &opcode_info_tbl[BPF_CLASS(insn->code) | BPF_OP(insn->code)];
	insn_sz = bpf_is_ldimm64(insn) ? 2 : 1;
	if (opcode_info->can_fallthrough)
		succ->items[succ->cnt++] = idx + insn_sz;

	if (opcode_info->can_jump)
		succ->items[succ->cnt++] = idx + bpf_jmp_offset(insn) + 1;

	return succ;
}

__diag_pop();

static struct func_instance *get_outer_instance(struct bpf_verifier_env *env,
						struct func_instance *instance)
{
	struct callchain callchain = instance->callchain;

	/* Adjust @callchain to represent callchain one frame up */
	callchain.callsites[callchain.curframe] = 0;
	callchain.sp_starts[callchain.curframe] = 0;
	callchain.curframe--;
	callchain.callsites[callchain.curframe] = callchain.sp_starts[callchain.curframe];
	return __lookup_instance(env, &callchain);
}

static u32 callchain_subprog_start(struct callchain *callchain)
{
	return callchain->sp_starts[callchain->curframe];
}

/*
 * Transfer @may_read and @must_write_acc marks from the first instruction of @instance,
 * to the call instruction in function instance calling @instance.
 */
static int propagate_to_outer_instance(struct bpf_verifier_env *env,
				       struct func_instance *instance)
{
	struct callchain *callchain = &instance->callchain;
	u32 this_subprog_start, callsite, frame;
	struct func_instance *outer_instance;
	struct per_frame_masks *insn;
	int err;

	this_subprog_start = callchain_subprog_start(callchain);
	outer_instance = get_outer_instance(env, instance);
	if (IS_ERR(outer_instance))
		return PTR_ERR(outer_instance);
	callsite = callchain->callsites[callchain->curframe - 1];
	reset_stack_write_marks(env, outer_instance);
	for (frame = 0; frame < callchain->curframe; frame++) {
		insn = get_frame_masks(instance, frame, this_subprog_start);
		if (!insn)
			continue;
		bpf_mark_stack_write(env, frame, insn->must_write_acc);
		err = mark_stack_read(outer_instance, frame, callsite, insn->live_before);
		if (err)
			return err;
	}
	commit_stack_write_marks(env, outer_instance, callsite);
	return 0;
}

static inline bool update_insn(struct bpf_verifier_env *env,
			       struct func_instance *instance, u32 frame, u32 insn_idx)
{
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	spis_t new_before, new_after, must_write_acc;
	struct per_frame_masks *insn, *succ_insn;
	struct bpf_iarray *succ;
	u32 s;
	bool changed;

	succ = bpf_insn_successors(env, insn_idx);
	if (succ->cnt == 0)
		return false;

	changed = false;
	insn = get_frame_masks(instance, frame, insn_idx);
	new_before = SPIS_ZERO;
	new_after = SPIS_ZERO;
	/*
	 * New "must_write_acc" is an intersection of all "must_write_acc"
	 * of successors plus all "must_write" slots of instruction itself.
	 */
	must_write_acc = SPIS_ALL;
	for (s = 0; s < succ->cnt; ++s) {
		succ_insn = get_frame_masks(instance, frame, succ->items[s]);
		new_after = spis_or(new_after, succ_insn->live_before);
		must_write_acc = spis_and(must_write_acc, succ_insn->must_write_acc);
	}
	must_write_acc = spis_or(must_write_acc, insn->must_write);
	/*
	 * New "live_before" is a union of all "live_before" of successors
	 * minus slots written by instruction plus slots read by instruction.
	 * new_before = (new_after & ~insn->must_write) | insn->may_read
	 */
	new_before = spis_or(spis_and(new_after, spis_not(insn->must_write)),
			     insn->may_read);
	changed |= !spis_equal(new_before, insn->live_before);
	changed |= !spis_equal(must_write_acc, insn->must_write_acc);
	if (unlikely(env->log.level & BPF_LOG_LEVEL2) &&
	    (!spis_is_zero(insn->may_read) || !spis_is_zero(insn->must_write) ||
	     insn_idx == callchain_subprog_start(&instance->callchain) ||
	     aux[insn_idx].prune_point)) {
		log_mask_change(env, &instance->callchain, "live",
				frame, insn_idx, insn->live_before, new_before);
		log_mask_change(env, &instance->callchain, "written",
				frame, insn_idx, insn->must_write_acc, must_write_acc);
	}
	insn->live_before = new_before;
	insn->must_write_acc = must_write_acc;
	return changed;
}

/* Fixed-point computation of @live_before and @must_write_acc marks */
static int update_instance(struct bpf_verifier_env *env, struct func_instance *instance)
{
	u32 i, frame, po_start, po_end, cnt, this_subprog_start;
	struct callchain *callchain = &instance->callchain;
	int *insn_postorder = env->cfg.insn_postorder;
	struct bpf_subprog_info *subprog;
	struct per_frame_masks *insn;
	bool changed;
	int err;

	if (!instance->updated)
		return 0;

	this_subprog_start = callchain_subprog_start(callchain);
	/*
	 * If must_write marks were updated must_write_acc needs to be reset
	 * (to account for the case when new must_write sets became smaller).
	 */
	if (instance->must_write_dropped) {
		for (frame = 0; frame <= callchain->curframe; frame++) {
			if (!instance->frames[frame])
				continue;

			for (i = 0; i < instance->insn_cnt; i++) {
				insn = get_frame_masks(instance, frame, this_subprog_start + i);
				insn->must_write_acc = SPIS_ZERO;
			}
		}
	}

	subprog = bpf_find_containing_subprog(env, this_subprog_start);
	po_start = subprog->postorder_start;
	po_end = (subprog + 1)->postorder_start;
	cnt = 0;
	/* repeat until fixed point is reached */
	do {
		cnt++;
		changed = false;
		for (frame = 0; frame <= instance->callchain.curframe; frame++) {
			if (!instance->frames[frame])
				continue;

			for (i = po_start; i < po_end; i++)
				changed |= update_insn(env, instance, frame, insn_postorder[i]);
		}
	} while (changed);

	if (env->log.level & BPF_LOG_LEVEL2)
		bpf_log(&env->log, "%s live stack update done in %d iterations\n",
			fmt_callchain(env, callchain), cnt);

	/* transfer marks accumulated for outer frames to outer func instance (caller) */
	if (callchain->curframe > 0) {
		err = propagate_to_outer_instance(env, instance);
		if (err)
			return err;
	}

	instance->updated = false;
	instance->must_write_dropped = false;
	return 0;
}

/*
 * Prepare all callchains within @env->cur_state for querying.
 * This function should be called after each verifier.c:pop_stack()
 * and whenever verifier.c:do_check_insn() processes subprogram exit.
 * This would guarantee that visited verifier states with zero branches
 * have their bpf_mark_stack_{read,write}() effects propagated in
 * @env->liveness.
 */
int bpf_update_live_stack(struct bpf_verifier_env *env)
{
	struct func_instance *instance;
	int err, frame;

	bpf_reset_live_stack_callchain(env);
	for (frame = env->cur_state->curframe; frame >= 0; --frame) {
		instance = lookup_instance(env, env->cur_state, frame);
		if (IS_ERR(instance))
			return PTR_ERR(instance);

		err = update_instance(env, instance);
		if (err)
			return err;
	}
	return 0;
}

static bool is_live_before(struct func_instance *instance, u32 insn_idx, u32 frameno, u32 half_spi)
{
	struct per_frame_masks *masks;

	masks = get_frame_masks(instance, frameno, insn_idx);
	return masks && spis_test_bit(masks->live_before, half_spi);
}

int bpf_live_stack_query_init(struct bpf_verifier_env *env, struct bpf_verifier_state *st)
{
	struct live_stack_query *q = &env->liveness->live_stack_query;
	struct func_instance *instance;
	u32 frame;

	memset(q, 0, sizeof(*q));
	for (frame = 0; frame <= st->curframe; frame++) {
		instance = lookup_instance(env, st, frame);
		if (IS_ERR(instance))
			return PTR_ERR(instance);
		q->instances[frame] = instance;
	}
	q->curframe = st->curframe;
	q->insn_idx = st->insn_idx;
	return 0;
}

bool bpf_stack_slot_alive(struct bpf_verifier_env *env, u32 frameno, u32 half_spi)
{
	/*
	 * Slot is alive if it is read before q->st->insn_idx in current func instance,
	 * or if for some outer func instance:
	 * - alive before callsite if callsite calls callback, otherwise
	 * - alive after callsite
	 */
	struct live_stack_query *q = &env->liveness->live_stack_query;
	struct func_instance *instance, *curframe_instance;
	u32 i, callsite;
	bool alive;

	curframe_instance = q->instances[q->curframe];
	alive = is_live_before(curframe_instance, q->insn_idx, frameno, half_spi);
	if (alive)
		return true;

	for (i = frameno; i < q->curframe; i++) {
		callsite = curframe_instance->callchain.callsites[i];
		instance = q->instances[i];
		alive = bpf_calls_callback(env, callsite)
			? is_live_before(instance, callsite, frameno, half_spi)
			: is_live_before(instance, callsite + 1, frameno, half_spi);
		if (alive)
			return true;
	}

	return false;
}

/*
 * Per-register tracking state for compute_subprog_args().
 * Tracks which frame's FP a value is derived from
 * and the byte offset from that frame's FP.
 *
 * The .frame field forms a lattice with three levels of precision:
 *
 *   precise {frame=N, off=V}      -- known absolute frame index and byte offset
 *        |
 *   offset-imprecise {frame=N, off=OFF_IMPRECISE}
 *        |                        -- known frame identity, unknown offset
 *   fully-imprecise {frame=ARG_IMPRECISE, mask=bitmask}
 *                                 -- unknown frame identity; .mask is a
 *                                    bitmask of which frame indices might be
 *                                    involved
 *
 * At CFG merge points, arg_track_join() moves down the lattice:
 *   - same frame + same offset  -> precise
 *   - same frame + different offset -> offset-imprecise
 *   - different frames          -> fully-imprecise (bitmask OR)
 *
 * At memory access sites (LDX/STX/ST), offset-imprecise marks only
 * the known frame's access mask as SPIS_ALL, while fully-imprecise
 * iterates bits in the bitmask and routes each frame to its target.
 */
#define MAX_ARG_OFFSETS 4

struct arg_track {
	union {
		s16 off[MAX_ARG_OFFSETS]; /* byte offsets; off_cnt says how many */
		u16 mask;	/* arg bitmask when arg == ARG_IMPRECISE */
	};
	s8 frame;	/* absolute frame index, or enum arg_track_state */
	s8 off_cnt;	/* 0 = offset-imprecise, 1-4 = # of precise offsets */
};

enum arg_track_state {
	ARG_NONE	= -1,	/* not derived from any argument */
	ARG_UNVISITED	= -2,	/* not yet reached by dataflow */
	ARG_IMPRECISE	= -3,	/* lost identity; .mask is arg bitmask */
};

#define OFF_IMPRECISE	S16_MIN	/* arg identity known but offset unknown */

/* Track callee stack slots fp-8 through fp-512 (64 slots of 8 bytes each) */
#define MAX_ARG_SPILL_SLOTS 64

static bool arg_is_visited(const struct arg_track *at)
{
	return at->frame != ARG_UNVISITED;
}

static bool arg_is_fp(const struct arg_track *at)
{
	return at->frame >= 0 || at->frame == ARG_IMPRECISE;
}

/*
 * Clear all tracked callee stack slots overlapping the byte range
 * [off, off+sz-1] where off is a negative FP-relative offset.
 */
static void clear_overlapping_stack_slots(struct arg_track *at_stack, s16 off, u32 sz)
{
	struct arg_track none = { .frame = ARG_NONE };

	if (off == OFF_IMPRECISE) {
		for (int i = 0; i < MAX_ARG_SPILL_SLOTS; i++)
			at_stack[i] = none;
		return;
	}
	for (int i = 0; i < MAX_ARG_SPILL_SLOTS; i++) {
		int slot_start = -((i + 1) * 8);
		int slot_end = slot_start + 8;

		if (slot_start < off + (int)sz && slot_end > off)
			at_stack[i] = none;
	}
}

static void verbose_arg_track(struct bpf_verifier_env *env, struct arg_track *at)
{
	int i;

	switch (at->frame) {
	case ARG_NONE:      verbose(env, "_");                          break;
	case ARG_UNVISITED: verbose(env, "?");                          break;
	case ARG_IMPRECISE: verbose(env, "IMP%x", at->mask);            break;
	default:
		/* frame >= 0: absolute frame index */
		if (at->off_cnt == 0) {
			verbose(env, "fp%d ?", at->frame);
		} else {
			for (i = 0; i < at->off_cnt; i++) {
				if (i)
					verbose(env, "|");
				verbose(env, "fp%d%+d", at->frame, at->off[i]);
			}
		}
		break;
	}
}

static bool arg_track_eq(const struct arg_track *a, const struct arg_track *b)
{
	int i;

	if (a->frame != b->frame)
		return false;
	if (a->frame == ARG_IMPRECISE)
		return a->mask == b->mask;
	if (a->frame < 0)
		return true;
	if (a->off_cnt != b->off_cnt)
		return false;
	for (i = 0; i < a->off_cnt; i++)
		if (a->off[i] != b->off[i])
			return false;
	return true;
}

static struct arg_track arg_single(s8 arg, s16 off)
{
	struct arg_track at = {};

	at.frame = arg;
	at.off[0] = off;
	at.off_cnt = 1;
	return at;
}

/*
 * Merge two sorted offset arrays, deduplicate.
 * Returns off_cnt=0 if the result exceeds MAX_ARG_OFFSETS.
 * Both args must have the same frame and off_cnt > 0.
 */
static struct arg_track arg_merge_offsets(struct arg_track a, struct arg_track b)
{
	struct arg_track result = { .frame = a.frame };
	struct arg_track imp = { .frame = a.frame };
	int i = 0, j = 0, k = 0;

	while (i < a.off_cnt && j < b.off_cnt) {
		s16 v;

		if (a.off[i] <= b.off[j]) {
			v = a.off[i++];
			if (v == b.off[j])
				j++;
		} else {
			v = b.off[j++];
		}
		if (k > 0 && result.off[k - 1] == v)
			continue;
		if (k >= MAX_ARG_OFFSETS)
			return imp;
		result.off[k++] = v;
	}
	while (i < a.off_cnt) {
		if (k >= MAX_ARG_OFFSETS)
			return imp;
		result.off[k++] = a.off[i++];
	}
	while (j < b.off_cnt) {
		if (k >= MAX_ARG_OFFSETS)
			return imp;
		result.off[k++] = b.off[j++];
	}
	result.off_cnt = k;
	return result;
}

/*
 * Merge two arg_tracks into ARG_IMPRECISE, collecting the frame
 * bits from both operands. Precise frame indices (frame >= 0)
 * contribute a single bit; existing ARG_IMPRECISE values
 * contribute their full bitmask.
 */
static struct arg_track arg_join_imprecise(struct arg_track a, struct arg_track b)
{
	u32 m = 0;

	if (a.frame >= 0)
		m |= BIT(a.frame);
	else if (a.frame == ARG_IMPRECISE)
		m |= a.mask;

	if (b.frame >= 0)
		m |= BIT(b.frame);
	else if (b.frame == ARG_IMPRECISE)
		m |= b.mask;

	return (struct arg_track){ .mask = m, .frame = ARG_IMPRECISE };
}

/* Join two arg_track values at merge points */
static struct arg_track __arg_track_join(struct arg_track a, struct arg_track b)
{
	if (!arg_is_visited(&b))
		return a;
	if (!arg_is_visited(&a))
		return b;
	if (a.frame == b.frame && a.frame >= 0) {
		/* Both offset-imprecise: stay imprecise */
		if (a.off_cnt == 0 || b.off_cnt == 0)
			return (struct arg_track){ .frame = a.frame };
		/* Merge offset sets; falls back to off_cnt=0 if >4 */
		return arg_merge_offsets(a, b);
	}

	/*
	 * args are different, but one of them is known
	 * arg + none -> arg
	 * none + arg -> arg
	 *
	 * none + none -> none
	 */
	if (a.frame == ARG_NONE && b.frame == ARG_NONE)
		return a;
	if (a.frame >= 0 && b.frame == ARG_NONE) {
		/*
		 * When joining single fp-N add fake fp+0 to
		 * keep stack_use and prevent stack_def
		 */
		if (a.off_cnt == 1)
			return arg_merge_offsets(a, arg_single(a.frame, 0));
		return a;
	}
	if (b.frame >= 0 && a.frame == ARG_NONE) {
		if (b.off_cnt == 1)
			return arg_merge_offsets(b, arg_single(b.frame, 0));
		return b;
	}

	return arg_join_imprecise(a, b);
}

static bool arg_track_join(struct bpf_verifier_env *env, int idx, int target, int r,
			   struct arg_track *in, struct arg_track out)
{
	struct arg_track old = *in;
	struct arg_track new_val = __arg_track_join(old, out);

	if (arg_track_eq(&new_val, &old))
		return false;

	*in = new_val;
	if (!(env->log.level & BPF_LOG_LEVEL2) || !arg_is_visited(&old))
		return true;

	verbose(env, "arg JOIN insn %d -> %d ", idx, target);
	if (r >= 0)
		verbose(env, "r%d: ", r);
	else
		verbose(env, "fp%+d: ", r * 8);
	verbose_arg_track(env, &old);
	verbose(env, " + ");
	verbose_arg_track(env, &out);
	verbose(env, " => ");
	verbose_arg_track(env, &new_val);
	verbose(env, "\n");
	return true;
}

/*
 * Compute the result when an ALU op destroys offset precision.
 * If a single arg is identifiable, preserve it with OFF_IMPRECISE.
 * If two different args are involved or one is already ARG_IMPRECISE,
 * the result is fully ARG_IMPRECISE.
 */
static void arg_track_alu64(struct arg_track *dst, const struct arg_track *src)
{
	WARN_ON_ONCE(!arg_is_visited(dst));
	WARN_ON_ONCE(!arg_is_visited(src));

	if (dst->frame >= 0 && (src->frame == ARG_NONE || src->frame == dst->frame)) {
		/*
		 * rX += rY where rY is not arg derived
		 * rX += rX
		 */
		dst->off_cnt = 0;
		return;
	}
	if (src->frame >= 0 && dst->frame == ARG_NONE) {
		/*
		 * rX += rY where rX is not arg derived
		 * rY identity leaks into rX
		 */
		dst->off_cnt = 0;
		dst->frame = src->frame;
		return;
	}

	if (dst->frame == ARG_NONE && src->frame == ARG_NONE)
		return;

	*dst = arg_join_imprecise(*dst, *src);
}

static s16 arg_add(s16 off, s64 delta)
{
	s64 res;

	if (off == OFF_IMPRECISE)
		return OFF_IMPRECISE;
	res = (s64)off + delta;
	if (res < S16_MIN + 1 || res > S16_MAX)
		return OFF_IMPRECISE;
	return res;
}

static void arg_padd(struct arg_track *at, s64 delta)
{
	int i;

	if (at->off_cnt == 0)
		return;
	for (i = 0; i < at->off_cnt; i++) {
		s16 new_off = arg_add(at->off[i], delta);

		if (new_off == OFF_IMPRECISE) {
			at->off_cnt = 0;
			return;
		}
		at->off[i] = new_off;
	}
}

/*
 * Convert a byte offset from FP to a callee stack slot index.
 * Returns -1 if out of range or not 8-byte aligned.
 * Slot 0 = fp-8, slot 1 = fp-16, ..., slot 7 = fp-64, ....
 */
static int fp_off_to_slot(s16 off)
{
	if (off == OFF_IMPRECISE)
		return -1;
	if (off >= 0 || off < -(int)(MAX_ARG_SPILL_SLOTS * 8))
		return -1;
	if (off % 8)
		return -1;
	return (-off) / 8 - 1;
}

static struct arg_track fill_from_stack(struct bpf_insn *insn,
					struct arg_track *at_out, int reg,
					struct arg_track *at_stack_out,
					int depth)
{
	struct arg_track imp = {
		.mask = (1u << (depth + 1)) - 1,
		.frame = ARG_IMPRECISE
	};
	struct arg_track result = { .frame = ARG_NONE };
	int cnt, i;

	if (reg == BPF_REG_FP) {
		int slot = fp_off_to_slot(insn->off);

		return slot >= 0 ? at_stack_out[slot] : imp;
	}
	cnt = at_out[reg].off_cnt;
	if (cnt == 0)
		return imp;

	for (i = 0; i < cnt; i++) {
		s16 fp_off = arg_add(at_out[reg].off[i], insn->off);
		int slot = fp_off_to_slot(fp_off);

		if (slot < 0)
			return imp;
		result = __arg_track_join(result, at_stack_out[slot]);
	}
	return result;
}

/*
 * Spill @val to all possible stack slots indicated by the FP offsets in @reg.
 * For an 8-byte store, single candidate slot gets @val. multi-slots are joined.
 * sub-8-byte store joins with ARG_NONE.
 * When exact offset is unknown conservatively add reg values to all slots in at_stack_out.
 */
static void spill_to_stack(struct bpf_insn *insn, struct arg_track *at_out,
			   int reg, struct arg_track *at_stack_out,
			   struct arg_track *val, u32 sz)
{
	struct arg_track none = { .frame = ARG_NONE };
	struct arg_track new_val = sz == 8 ? *val : none;
	int cnt, i;

	if (reg == BPF_REG_FP) {
		int slot = fp_off_to_slot(insn->off);

		if (slot >= 0)
			at_stack_out[slot] = new_val;
		return;
	}
	cnt = at_out[reg].off_cnt;
	if (cnt == 0) {
		for (int slot = 0; slot < MAX_ARG_SPILL_SLOTS; slot++)
			at_stack_out[slot] = __arg_track_join(at_stack_out[slot], new_val);
		return;
	}
	for (i = 0; i < cnt; i++) {
		s16 fp_off = arg_add(at_out[reg].off[i], insn->off);
		int slot = fp_off_to_slot(fp_off);

		if (slot < 0)
			continue;
		if (cnt == 1)
			at_stack_out[slot] = new_val;
		else
			at_stack_out[slot] = __arg_track_join(at_stack_out[slot], new_val);
	}
}

/*
 * Clear stack slots overlapping all possible FP offsets in @reg.
 */
static void clear_stack_for_all_offs(struct bpf_insn *insn,
				     struct arg_track *at_out, int reg,
				     struct arg_track *at_stack_out, u32 sz)
{
	int cnt, i;

	if (reg == BPF_REG_FP) {
		clear_overlapping_stack_slots(at_stack_out, insn->off, sz);
		return;
	}
	cnt = at_out[reg].off_cnt;
	if (cnt == 0) {
		clear_overlapping_stack_slots(at_stack_out, OFF_IMPRECISE, sz);
		return;
	}
	for (i = 0; i < cnt; i++) {
		s16 fp_off = arg_add(at_out[reg].off[i], insn->off);

		clear_overlapping_stack_slots(at_stack_out, fp_off, sz);
	}
}

static void arg_track_log(struct bpf_verifier_env *env, struct bpf_insn *insn, int idx,
			  struct arg_track *at_in, struct arg_track *at_stack_in,
			  struct arg_track *at_out, struct arg_track *at_stack_out)
{
	bool printed = false;
	int i;

	if (!(env->log.level & BPF_LOG_LEVEL2))
		return;
	for (i = 0; i < MAX_BPF_REG; i++) {
		if (arg_track_eq(&at_out[i], &at_in[i]))
			continue;
		if (!printed) {
			verbose(env, "%3d: ", idx);
			bpf_verbose_insn(env, insn);
			bpf_vlog_reset(&env->log, env->log.end_pos - 1);
			printed = true;
		}
		verbose(env, "\tr%d: ", i); verbose_arg_track(env, &at_in[i]);
		verbose(env, " -> "); verbose_arg_track(env, &at_out[i]);
	}
	for (i = 0; i < MAX_ARG_SPILL_SLOTS; i++) {
		if (arg_track_eq(&at_stack_out[i], &at_stack_in[i]))
			continue;
		if (!printed) {
			verbose(env, "%3d: ", idx);
			bpf_verbose_insn(env, insn);
			bpf_vlog_reset(&env->log, env->log.end_pos - 1);
			printed = true;
		}
		verbose(env, "\tfp%+d: ", -(i + 1) * 8); verbose_arg_track(env, &at_stack_in[i]);
		verbose(env, " -> "); verbose_arg_track(env, &at_stack_out[i]);
	}
	if (printed)
		verbose(env, "\n");
}

/*
 * Pure dataflow transfer function for arg_track state.
 * Updates at_out[] based on how the instruction modifies registers.
 * Tracks spill/fill, but not other memory accesses.
 */
static void arg_track_xfer(struct bpf_verifier_env *env, struct bpf_insn *insn,
			   int insn_idx,
			   struct arg_track *at_out, struct arg_track *at_stack_out,
			   struct func_instance *instance,
			   u32 *callsites)
{
	int depth = instance->callchain.curframe;
	u8 class = BPF_CLASS(insn->code);
	u8 code = BPF_OP(insn->code);
	struct arg_track *dst = &at_out[insn->dst_reg];
	struct arg_track *src = &at_out[insn->src_reg];
	struct arg_track none = { .frame = ARG_NONE };
	int r;

	if (class == BPF_ALU64 && BPF_SRC(insn->code) == BPF_K) {
		if (code == BPF_MOV) {
			*dst = none;
		} else if (dst->frame >= 0) {
			if (code == BPF_ADD)
				arg_padd(dst, insn->imm);
			else if (code == BPF_SUB)
				arg_padd(dst, -(s64)insn->imm);
			else
				/* Any other 64-bit alu on the pointer makes it imprecise */
				dst->off_cnt = 0;
		} /* else if dst->frame is imprecise it stays so */
	} else if (class == BPF_ALU64 && BPF_SRC(insn->code) == BPF_X) {
		if (code == BPF_MOV) {
			if (insn->off == 0) {
				*dst = *src;
			} else {
				/* addr_space_cast destroys a pointer */
				*dst = none;
			}
		} else {
			arg_track_alu64(dst, src);
		}
	} else if (class == BPF_ALU) {
		/*
		 * 32-bit alu destroys the pointer.
		 * If src was a pointer it cannot leak into dst
		 */
		*dst = none;
	} else if (class == BPF_JMP && code == BPF_CALL) {
		/*
		 * at_stack_out[slot] is not cleared by the helper and subprog calls.
		 * The fill_from_stack() may return the stale spill — which is an FP-derived arg_track
		 * (the value that was originally spilled there). The loaded register then carries
		 * a phantom FP-derived identity that doesn't correspond to what's actually in the slot.
		 * This phantom FP pointer propagates forward, and wherever it's subsequently used
		 * (as a helper argument, another store, etc.), it sets stack liveness bits.
		 * Those bits correspond to stack accesses that don't actually happen.
		 * So the effect is over-reporting stack liveness — marking slots as live that aren't
		 * actually accessed. The verifier preserves more state than necessary across calls,
		 * which is conservative.
		 *
		 * helpers can scratch stack slots, but they won't make a valid pointer out of it.
		 * subprogs are allowed to write into parent slots, but they cannot write
		 * _any_ FP-derived pointer into it (either their own or parent's FP).
		 */
		for (r = BPF_REG_0; r <= BPF_REG_5; r++)
			at_out[r] = none;
	} else if (class == BPF_LDX) {
		u32 sz = bpf_size_to_bytes(BPF_SIZE(insn->code));
		bool src_is_local_fp = insn->src_reg == BPF_REG_FP || src->frame == depth ||
				       (src->frame == ARG_IMPRECISE && (src->mask & BIT(depth)));

		/*
		 * Reload from callee stack: if src is current-frame FP-derived
		 * and the load is an 8-byte BPF_MEM, try to restore the spill
		 * identity.  For imprecise sources fill_from_stack() returns
		 * ARG_IMPRECISE (off_cnt == 0).
		 */
		if (src_is_local_fp && BPF_MODE(insn->code) == BPF_MEM && sz == 8) {
			*dst = fill_from_stack(insn, at_out, insn->src_reg, at_stack_out, depth);
		} else if (src->frame >= 0 && src->frame < depth &&
			   BPF_MODE(insn->code) == BPF_MEM && sz == 8) {
			struct arg_track *parent_stack =
				env->callsite_at_stack[callsites[src->frame]];

			*dst = fill_from_stack(insn, at_out, insn->src_reg,
					       parent_stack, src->frame);
		} else if (src->frame == ARG_IMPRECISE &&
			   !(src->mask & BIT(depth)) && src->mask &&
			   BPF_MODE(insn->code) == BPF_MEM && sz == 8) {
			/*
			 * Imprecise src with only parent-frame bits:
			 * conservative fallback.
			 */
			*dst = *src;
		} else {
			*dst = none;
		}
	} else if (class == BPF_LD && BPF_MODE(insn->code) == BPF_IMM) {
		*dst = none;
	} else if (class == BPF_STX) {
		u32 sz = bpf_size_to_bytes(BPF_SIZE(insn->code));
		bool dst_is_local_fp;

		/* Track spills to current-frame FP-derived callee stack */
		dst_is_local_fp = insn->dst_reg == BPF_REG_FP || dst->frame == depth;
		if (dst_is_local_fp && BPF_MODE(insn->code) == BPF_MEM)
			spill_to_stack(insn, at_out, insn->dst_reg,
				       at_stack_out, src, sz);

		if (BPF_MODE(insn->code) == BPF_ATOMIC) {
			if (dst_is_local_fp && insn->imm != BPF_LOAD_ACQ)
				clear_stack_for_all_offs(insn, at_out, insn->dst_reg,
							 at_stack_out, sz);

			if (insn->imm == BPF_CMPXCHG)
				at_out[BPF_REG_0] = none;
			else if (insn->imm == BPF_LOAD_ACQ)
				*dst = none;
			else if (insn->imm & BPF_FETCH)
				*src = none;
		}
	} else if (class == BPF_ST && BPF_MODE(insn->code) == BPF_MEM) {
		u32 sz = bpf_size_to_bytes(BPF_SIZE(insn->code));
		bool dst_is_local_fp = insn->dst_reg == BPF_REG_FP || dst->frame == depth;

		/* BPF_ST to FP-derived dst: clear overlapping stack slots */
		if (dst_is_local_fp)
			clear_stack_for_all_offs(insn, at_out, insn->dst_reg,
						 at_stack_out, sz);
	}
}

/*
 * Record access_bytes from helper/kfunc or load/store insn.
 *   access_bytes > 0:      stack read
 *   access_bytes < 0:      stack write
 *   access_bytes == S64_MIN: unknown   — conservative, mark [0..slot] as read
 *   access_bytes == 0:      no access
 *
 */
static int record_stack_access_off(struct bpf_verifier_env *env,
				   struct func_instance *instance, s64 fp_off,
				   s64 access_bytes, u32 frame, u32 insn_idx)
{
	s32 slot_hi, slot_lo;
	spis_t mask;

	if (fp_off >= 0)
		/*
		 * out of bounds stack access doesn't contribute
		 * into actual stack liveness. It will be rejected
		 * by the main verifier pass later.
		 */
		return 0;
	if (access_bytes == S64_MIN) {
		/* helper/kfunc read unknown amount of bytes from fp_off until fp+0 */
		slot_hi = (-fp_off - 1) / STACK_SLOT_SZ;
		mask = SPIS_ZERO;
		spis_or_range(&mask, 0, slot_hi);
		return mark_stack_read(instance, frame, insn_idx, mask);
	}
	if (access_bytes > 0) {
		/* Mark any touched slot as use */
		slot_hi = (-fp_off - 1) / STACK_SLOT_SZ;
		slot_lo = max_t(s32, (-fp_off - access_bytes) / STACK_SLOT_SZ, 0);
		mask = SPIS_ZERO;
		spis_or_range(&mask, slot_lo, slot_hi);
		return mark_stack_read(instance, frame, insn_idx, mask);
	} else if (access_bytes < 0) {
		/* Mark only fully covered slots as def */
		access_bytes = -access_bytes;
		slot_hi = (-fp_off) / STACK_SLOT_SZ - 1;
		slot_lo = max_t(s32, (-fp_off - access_bytes + STACK_SLOT_SZ - 1) / STACK_SLOT_SZ, 0);
		if (slot_lo <= slot_hi) {
			mask = SPIS_ZERO;
			spis_or_range(&mask, slot_lo, slot_hi);
			bpf_mark_stack_write(env, frame, mask);
		}
	}
	return 0;
}

/*
 * 'arg' is FP-derived argument to helper/kfunc or load/store that
 * reads (positive) or writes (negative) 'access_bytes' into 'use' or 'def'.
 */
static int record_stack_access(struct bpf_verifier_env *env,
			       struct func_instance *instance,
			       const struct arg_track *arg,
			       s64 access_bytes, u32 frame, u32 insn_idx)
{
	int i, err;

	if (access_bytes == 0)
		return 0;
	if (arg->off_cnt == 0) {
		if (access_bytes > 0 || access_bytes == S64_MIN)
			return mark_stack_read(instance, frame, insn_idx, SPIS_ALL);
		return 0;
	}
	if (access_bytes != S64_MIN && access_bytes < 0 && arg->off_cnt != 1)
		/* multi-offset write cannot set stack_def */
		return 0;

	for (i = 0; i < arg->off_cnt; i++) {
		err = record_stack_access_off(env, instance, arg->off[i], access_bytes, frame, insn_idx);
		if (err)
			return err;
	}
	return 0;
}

/*
 * When a pointer is ARG_IMPRECISE, conservatively mark every frame in
 * the bitmask as fully used.
 */
static int record_imprecise(struct func_instance *instance, u32 mask, u32 insn_idx)
{
	int depth = instance->callchain.curframe;
	int f, err;

	for (f = 0; mask; f++, mask >>= 1) {
		if (!(mask & 1))
			continue;
		if (f <= depth) {
			err = mark_stack_read(instance, f, insn_idx, SPIS_ALL);
			if (err)
				return err;
		}
	}
	return 0;
}

/* Record load/store access for a given 'at' state of 'insn'. */
static int record_load_store_access(struct bpf_verifier_env *env,
				    struct func_instance *instance,
				    struct arg_track *at, int insn_idx)
{
	struct bpf_insn *insn = &env->prog->insnsi[insn_idx];
	int depth = instance->callchain.curframe;
	s32 sz = bpf_size_to_bytes(BPF_SIZE(insn->code));
	u8 class = BPF_CLASS(insn->code);
	struct arg_track resolved, *ptr;
	int oi;

	switch (class) {
	case BPF_LDX:
		ptr = &at[insn->src_reg];
		break;
	case BPF_STX:
		if (BPF_MODE(insn->code) == BPF_ATOMIC) {
			if (insn->imm == BPF_STORE_REL)
				sz = -sz;
			if (insn->imm == BPF_LOAD_ACQ)
				ptr = &at[insn->src_reg];
			else
				ptr = &at[insn->dst_reg];
		} else {
			ptr = &at[insn->dst_reg];
			sz = -sz;
		}
		break;
	case BPF_ST:
		ptr = &at[insn->dst_reg];
		sz = -sz;
		break;
	default:
		return 0;
	}

	/* Resolve offsets: fold insn->off into arg_track */
	if (ptr->off_cnt > 0) {
		resolved.off_cnt = ptr->off_cnt;
		resolved.frame = ptr->frame;
		for (oi = 0; oi < ptr->off_cnt; oi++) {
			resolved.off[oi] = arg_add(ptr->off[oi], insn->off);
			if (resolved.off[oi] == OFF_IMPRECISE) {
				resolved.off_cnt = 0;
				break;
			}
		}
		ptr = &resolved;
	}

	if (ptr->frame >= 0 && ptr->frame <= depth)
		return record_stack_access(env, instance, ptr, sz, ptr->frame, insn_idx);
	if (ptr->frame == ARG_IMPRECISE)
		return record_imprecise(instance, ptr->mask, insn_idx);
	/* ARG_NONE: not derived from any frame pointer, skip */
	return 0;
}

/* Record stack access for a given 'at' state of helper/kfunc 'insn' */
static int record_call_access(struct bpf_verifier_env *env,
			      struct func_instance *instance,
			      struct arg_track *at,
			      int insn_idx)
{
	struct bpf_insn *insn = &env->prog->insnsi[insn_idx];
	int depth = instance->callchain.curframe;
	struct bpf_call_summary cs;
	int r, err = 0, num_params = 5;

	if (bpf_pseudo_call(insn))
		return 0;

	if (bpf_get_call_summary(env, insn, &cs))
		num_params = cs.num_params;

	for (r = BPF_REG_1; r < BPF_REG_1 + num_params; r++) {
		int frame = at[r].frame;
		s64 bytes;

		if (!arg_is_fp(&at[r]))
			continue;

		if (bpf_helper_call(insn)) {
			bytes = bpf_helper_stack_access_bytes(env, insn, r - 1, insn_idx);
		} else if (bpf_pseudo_kfunc_call(insn)) {
			bytes = bpf_kfunc_stack_access_bytes(env, insn, r - 1, insn_idx);
		} else {
			for (int f = 0; f <= depth; f++) {
				err = mark_stack_read(instance, f, insn_idx, SPIS_ALL);
				if (err)
					return err;
			}
			return 0;
		}
		if (bytes == 0)
			continue;

		if (frame >= 0 && frame <= depth)
			err = record_stack_access(env, instance, &at[r], bytes, frame, insn_idx);
		else if (frame == ARG_IMPRECISE)
			err = record_imprecise(instance, at[r].mask, insn_idx);
		if (err)
			return err;
	}
	return 0;
}

/*
 * For a calls_callback helper, find the callback subprog and determine
 * which caller register maps to which callback register for FP passthrough.
 */
static int find_callback_subprog(struct bpf_verifier_env *env,
				 struct bpf_insn *insn, int insn_idx,
				 int *caller_reg, int *callee_reg)
{
	struct bpf_insn_aux_data *aux = &env->insn_aux_data[insn_idx];
	int cb_reg = -1;

	*caller_reg = -1;
	*callee_reg = -1;

	if (!bpf_helper_call(insn))
		return -1;
	switch (insn->imm) {
	case BPF_FUNC_loop:
		/* bpf_loop(nr, cb, ctx, flags): cb=R2, R3->cb R2 */
		cb_reg = BPF_REG_2;
		*caller_reg = BPF_REG_3;
		*callee_reg = BPF_REG_2;
		break;
	case BPF_FUNC_for_each_map_elem:
		/* for_each_map_elem(map, cb, ctx, flags): cb=R2, R3->cb R4 */
		cb_reg = BPF_REG_2;
		*caller_reg = BPF_REG_3;
		*callee_reg = BPF_REG_4;
		break;
	case BPF_FUNC_find_vma:
		/* find_vma(task, addr, cb, ctx, flags): cb=R3, R4->cb R3 */
		cb_reg = BPF_REG_3;
		*caller_reg = BPF_REG_4;
		*callee_reg = BPF_REG_3;
		break;
	case BPF_FUNC_user_ringbuf_drain:
		/* user_ringbuf_drain(map, cb, ctx, flags): cb=R2, R3->cb R2 */
		cb_reg = BPF_REG_2;
		*caller_reg = BPF_REG_3;
		*callee_reg = BPF_REG_2;
		break;
	default:
		return -1;
	}

	if (!(aux->const_reg_subprog_mask & BIT(cb_reg)))
		return -2;

	return aux->const_reg_vals[cb_reg];
}

/* Per-subprog intermediate state kept alive across analysis phases */
struct subprog_at_info {
	struct arg_track (*at_in)[MAX_BPF_REG];
	int len;
};

static void print_subprog_arg_access(struct bpf_verifier_env *env,
				     int subprog,
				     struct subprog_at_info *info,
				     struct arg_track (*at_stack_in)[MAX_ARG_SPILL_SLOTS])
{
	struct bpf_insn *insns = env->prog->insnsi;
	int start = env->subprog_info[subprog].start;
	int len = info->len;
	int i, r;

	if (!(env->log.level & BPF_LOG_LEVEL2))
		return;

	verbose(env, "subprog#%d %s:\n", subprog,
		env->prog->aux->func_info
		? btf_name_by_offset(env->prog->aux->btf,
				     btf_type_by_id(env->prog->aux->btf,
						    env->prog->aux->func_info[subprog].type_id)->name_off)
		: "");
	for (i = 0; i < len; i++) {
		int idx = start + i;
		bool has_extra = false;
		u8 cls = BPF_CLASS(insns[idx].code);
		bool is_ldx_stx_call = cls == BPF_LDX || cls == BPF_STX ||
				       insns[idx].code == (BPF_JMP | BPF_CALL);

		verbose(env, "%3d: ", idx);
		bpf_verbose_insn(env, &insns[idx]);

		/* Collect what needs printing */
		if (is_ldx_stx_call &&
		    arg_is_visited(&info->at_in[i][0])) {
			for (r = 0; r < MAX_BPF_REG - 1; r++)
				if (arg_is_fp(&info->at_in[i][r]))
					has_extra = true;
		}
		if (is_ldx_stx_call) {
			for (r = 0; r < MAX_ARG_SPILL_SLOTS; r++)
				if (arg_is_fp(&at_stack_in[i][r]))
					has_extra = true;
		}

		if (!has_extra) {
			if (bpf_is_ldimm64(&insns[idx]))
				i++;
			continue;
		}

		bpf_vlog_reset(&env->log, env->log.end_pos - 1);
		verbose(env, " //");

		if (is_ldx_stx_call && info->at_in &&
		    arg_is_visited(&info->at_in[i][0])) {
			for (r = 0; r < MAX_BPF_REG - 1; r++) {
				if (!arg_is_fp(&info->at_in[i][r]))
					continue;
				verbose(env, " r%d=", r);
				verbose_arg_track(env, &info->at_in[i][r]);
			}
		}

		if (is_ldx_stx_call) {
			for (r = 0; r < MAX_ARG_SPILL_SLOTS; r++) {
				if (!arg_is_fp(&at_stack_in[i][r]))
					continue;
				verbose(env, " fp%+d=", -(r + 1) * 8);
				verbose_arg_track(env, &at_stack_in[i][r]);
			}
		}

		verbose(env, "\n");
		if (bpf_is_ldimm64(&insns[idx]))
			i++;
	}
}

/*
 * Compute arg tracking dataflow for a single subprog.
 * Runs forward fixed-point with arg_track_xfer(), then records
 * memory accesses in a single linear pass over converged state.
 *
 * @callee_entry: pre-populated entry state for R1-R5
 *                NULL for main (subprog 0).
 * @info:         stores at_in, len for debug printing.
 */
static int compute_subprog_args(struct bpf_verifier_env *env,
				struct subprog_at_info *info,
				struct arg_track *callee_entry,
				struct func_instance *instance,
				u32 *callsites)
{
	int subprog = instance->subprog;
	struct bpf_insn *insns = env->prog->insnsi;
	int depth = instance->callchain.curframe;
	int start = env->subprog_info[subprog].start;
	int po_start = env->subprog_info[subprog].postorder_start;
	int end = env->subprog_info[subprog + 1].start;
	int po_end = env->subprog_info[subprog + 1].postorder_start;
	int len = end - start;
	struct arg_track (*at_in)[MAX_BPF_REG] = NULL;
	struct arg_track at_out[MAX_BPF_REG];
	struct arg_track (*at_stack_in)[MAX_ARG_SPILL_SLOTS] = NULL;
	struct arg_track *at_stack_out = NULL;
	struct arg_track unvisited = { .frame = ARG_UNVISITED };
	struct arg_track none = { .frame = ARG_NONE };
	bool changed;
	int i, p, r, err = -ENOMEM;

	at_in = kvmalloc_objs(*at_in, len, GFP_KERNEL_ACCOUNT);
	if (!at_in)
		goto err_free;

	at_stack_in = kvmalloc_objs(*at_stack_in, len, GFP_KERNEL_ACCOUNT);
	if (!at_stack_in)
		goto err_free;

	at_stack_out = kvmalloc_objs(*at_stack_out, MAX_ARG_SPILL_SLOTS, GFP_KERNEL_ACCOUNT);
	if (!at_stack_out)
		goto err_free;

	for (i = 0; i < len; i++) {
		for (r = 0; r < MAX_BPF_REG; r++)
			at_in[i][r] = unvisited;
		for (r = 0; r < MAX_ARG_SPILL_SLOTS; r++)
			at_stack_in[i][r] = unvisited;
	}

	for (r = 0; r < MAX_BPF_REG; r++)
		at_in[0][r] = none;

	/* Entry: R10 is always precisely the current frame's FP */
	at_in[0][BPF_REG_FP] = arg_single(depth, 0);

	/* R1-R5: from caller or ARG_NONE for main */
	if (callee_entry) {
		for (r = BPF_REG_1; r <= BPF_REG_5; r++)
			at_in[0][r] = callee_entry[r];
	}

	/* Entry: all stack slots are ARG_NONE */
	for (r = 0; r < MAX_ARG_SPILL_SLOTS; r++)
		at_stack_in[0][r] = none;

	if (env->log.level & BPF_LOG_LEVEL2)
		verbose(env, "subprog#%d: analyzing (depth %d)...\n", subprog, depth);

	/* Forward fixed-point iteration in reverse post order */
redo:
	changed = false;
	for (p = po_end - 1; p >= po_start; p--) {
		int idx = env->cfg.insn_postorder[p];
		int i = idx - start;
		struct bpf_insn *insn = &insns[idx];
		struct bpf_iarray *succ;

		if (!arg_is_visited(&at_in[i][0]) && !arg_is_visited(&at_in[i][1]))
			continue;

		memcpy(at_out, at_in[i], sizeof(at_out));
		memcpy(at_stack_out, at_stack_in[i], MAX_ARG_SPILL_SLOTS * sizeof(*at_stack_out));

		arg_track_xfer(env, insn, idx, at_out, at_stack_out, instance, callsites);
		arg_track_log(env, insn, idx, at_in[i], at_stack_in[i], at_out, at_stack_out);

		/* Propagate to successors within this subprogram */
		succ = bpf_insn_successors(env, idx);
		for (int s = 0; s < succ->cnt; s++) {
			int target = succ->items[s];
			int ti;

			/* Filter: stay within the subprogram's range */
			if (target < start || target >= end)
				continue;
			ti = target - start;

			for (r = 0; r < MAX_BPF_REG; r++)
				changed |= arg_track_join(env, idx, target, r,
							  &at_in[ti][r], at_out[r]);

			for (r = 0; r < MAX_ARG_SPILL_SLOTS; r++)
				changed |= arg_track_join(env, idx, target, -r - 1,
							  &at_stack_in[ti][r], at_stack_out[r]);
		}
	}
	if (changed)
		goto redo;

	/* Record memory accesses using converged at_in (RPO skips dead code) */
	for (p = po_end - 1; p >= po_start; p--) {
		int idx = env->cfg.insn_postorder[p];
		int i = idx - start;
		struct bpf_insn *insn = &insns[idx];

		reset_stack_write_marks(env, instance);
		err = record_load_store_access(env, instance, at_in[i], idx);
		if (err)
			goto err_free;

		if (insn->code == (BPF_JMP | BPF_CALL)) {
			err = record_call_access(env, instance, at_in[i], idx);
			if (err)
				goto err_free;
		}

		if (bpf_pseudo_call(insn) || bpf_calls_callback(env, idx)) {
			kvfree(env->callsite_at_stack[idx]);
			env->callsite_at_stack[idx] =
				kvmalloc_objs(*env->callsite_at_stack[idx],
					      MAX_ARG_SPILL_SLOTS, GFP_KERNEL_ACCOUNT);
			if (!env->callsite_at_stack[idx]) {
				err = -ENOMEM;
				goto err_free;
			}
			memcpy(env->callsite_at_stack[idx],
			       at_stack_in[i], sizeof(struct arg_track) * MAX_ARG_SPILL_SLOTS);
		}
		err = commit_stack_write_marks(env, instance, idx);
		if (err)
			goto err_free;
	}

	info->at_in = at_in;
	at_in = NULL;
	info->len = len;
	print_subprog_arg_access(env, subprog, info, at_stack_in);
	err = 0;

err_free:
	kvfree(at_stack_out);
	kvfree(at_stack_in);
	kvfree(at_in);
	return err;
}

/*
 * Recursively analyze a subprog with specific 'entry_args'.
 * Each callee is analyzed with the exact args from its call site.
 *
 * Args are recomputed for each call because the dataflow result at_in[]
 * depends on the entry args and frame depth. Consider: A->C->D and B->C->D
 * Callsites in A and B pass different args into C, so C is recomputed.
 * Then within C the same callsite passes different args into D.
 */
static int analyze_subprog(struct bpf_verifier_env *env,
			   struct arg_track *entry_args,
			   struct subprog_at_info *info,
			   struct func_instance *instance,
			   u32 *callsites)
{
	int subprog = instance->subprog;
	int depth = instance->callchain.curframe;
	struct bpf_insn *insns = env->prog->insnsi;
	int start = env->subprog_info[subprog].start;
	int po_start = env->subprog_info[subprog].postorder_start;
	int po_end = env->subprog_info[subprog + 1].postorder_start;
	int j, err;

	if (++env->liveness->subprog_calls > 10000) {
		verbose(env, "liveness analysis exceeded complexity limit (%d calls)\n",
			env->liveness->subprog_calls);
		return -E2BIG;
	}

	if (need_resched())
		cond_resched();

	/* Free prior analysis if this subprog was already visited */
	kvfree(info[subprog].at_in);
	info[subprog].at_in = NULL;

	err = compute_subprog_args(env, &info[subprog], entry_args, instance, callsites);
	if (err)
		return err;

	/* For each reachable call site in the subprog, recurse into callees */
	for (int p = po_start; p < po_end; p++) {
		int idx = env->cfg.insn_postorder[p];
		struct arg_track callee_args[BPF_REG_5 + 1];
		struct arg_track none = { .frame = ARG_NONE };
		struct bpf_insn *insn = &insns[idx];
		struct func_instance *callee_instance;
		int callee, target;
		int caller_reg, cb_callee_reg;

		j = idx - start; /* relative index within this subprog */

		if (bpf_pseudo_call(insn)) {
			target = idx + insn->imm + 1;
			callee = bpf_find_subprog(env, target);
			if (callee < 0)
				continue;

			/* Build entry args: R1-R5 from at_in at call site */
			for (int r = BPF_REG_1; r <= BPF_REG_5; r++)
				callee_args[r] = info[subprog].at_in[j][r];
		} else if (bpf_calls_callback(env, idx)) {
			callee = find_callback_subprog(env, insn, idx, &caller_reg, &cb_callee_reg);
			if (callee == -2) {
				/*
				 * same bpf_loop() calls two different callbacks and passes
				 * stack pointer to them
				 */
				if (info[subprog].at_in[j][caller_reg].frame == ARG_NONE)
					continue;
				for (int f = 0; f <= depth; f++) {
					err = mark_stack_read(instance, f, idx, SPIS_ALL);
					if (err)
						return err;
				}
				continue;
			}
			if (callee < 0)
				continue;

			for (int r = BPF_REG_1; r <= BPF_REG_5; r++)
				callee_args[r] = none;
			callee_args[cb_callee_reg] = info[subprog].at_in[j][caller_reg];
		} else {
			continue;
		}

		if (depth == MAX_CALL_FRAMES - 1)
			return -EINVAL;

		callee_instance = call_instance(env, instance, idx, callee);
		if (IS_ERR(callee_instance))
			return PTR_ERR(callee_instance);
		callsites[depth] = idx;
		err = analyze_subprog(env, callee_args, info, callee_instance, callsites);
		if (err)
			return err;
	}

	return update_instance(env, instance);
}

int bpf_compute_subprog_arg_access(struct bpf_verifier_env *env)
{
	u32 callsites[MAX_CALL_FRAMES] = {};
	int insn_cnt = env->prog->len;
	struct func_instance *instance;
	struct subprog_at_info *info;
	int k, err = 0;

	info = kvzalloc_objs(*info, env->subprog_cnt, GFP_KERNEL_ACCOUNT);
	if (!info)
		return -ENOMEM;

	env->callsite_at_stack = kvzalloc_objs(*env->callsite_at_stack, insn_cnt,
					       GFP_KERNEL_ACCOUNT);
	if (!env->callsite_at_stack) {
		kvfree(info);
		return -ENOMEM;
	}

	instance = call_instance(env, NULL, 0, 0);
	if (IS_ERR(instance)) {
		err = PTR_ERR(instance);
		goto out;
	}
	err = analyze_subprog(env, NULL, info, instance, callsites);
	if (err)
		goto out;

	/*
	 * Subprogs and callbacks that don't receive FP-derived arguments
	 * cannot access ancestor stack frames, so they were skipped during
	 * the recursive walk above.  Async callbacks (timer, workqueue) are
	 * also not reachable from the main program's call graph.  Analyze
	 * all unvisited subprogs as independent roots at depth 0.
	 *
	 * Use reverse topological order (callers before callees) so that
	 * each subprog is analyzed before its callees, allowing the
	 * recursive walk inside analyze_subprog() to naturally
	 * reach nested callees that also lack FP-derived args.
	 */
	for (k = env->subprog_cnt - 1; k >= 0; k--) {
		int sub = env->subprog_topo_order[k];

		if (info[sub].at_in && !bpf_subprog_is_global(env, sub))
			continue;
		instance = call_instance(env, NULL, 0, sub);
		if (IS_ERR(instance)) {
			err = PTR_ERR(instance);
			goto out;
		}
		err = analyze_subprog(env, NULL, info, instance, callsites);
		if (err)
			goto out;
	}

out:
	for (k = 0; k < insn_cnt; k++)
		kvfree(env->callsite_at_stack[k]);
	kvfree(env->callsite_at_stack);
	env->callsite_at_stack = NULL;
	for (k = 0; k < env->subprog_cnt; k++)
		kvfree(info[k].at_in);
	kvfree(info);
	return err;
}
