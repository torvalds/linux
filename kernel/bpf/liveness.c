// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf_verifier.h>
#include <linux/btf.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/sort.h>

#define verbose(env, fmt, args...) bpf_verifier_log_write(env, fmt, ##args)

/*
 * Stack liveness is tracked with a 4-byte (half register) granularity.
 * Half-slot 0 covers [fp-4, fp), half-slot 1 covers [fp-8, fp-4), and so on,
 * hence FRAME_HALF_SPIS - 1 is the deepest half-slot a frame can have.
 */
#define FRAME_HALF_SPIS		(MAX_BPF_STACK_JIT / BPF_HALF_REG_SIZE)
#define FRAME_MAX_WORDS		BITS_TO_LONGS(FRAME_HALF_SPIS)

/* Masks tracked for each instruction of a frame */
enum {
	FM_MAY_READ,	/* stack slots that may be read by this instruction */
	FM_MUST_WRITE,	/* stack slots written by this instruction */
	FM_LIVE_BEFORE,	/* stack slots that may be read by this insn and its successors */
	FM_MAY_WRITE,	/* stack slots that may be written by this instruction */
	FM_MASK_CNT,
};

/*
 * Per instruction stack masks for one frame of a function instance.
 *
 * Most frames use only a fraction of the stack budget, so instead of masks
 * wide enough for every half-slot of the largest possible frame, all masks
 * of one array share the width @words, and the marking functions widen the
 * array as deeper half-slots are recorded. Mask @kind of the instruction at
 * relative index @i is at &bits[(i * FM_MASK_CNT + kind) * words]. A
 * half-slot at or past @words * BITS_PER_LONG is never read by this frame,
 * hence never live. An instruction that may read the whole frame, such as a
 * call passing a frame pointer to another subprog, widens the array to the
 * program's stack budget, the deepest an accepted program can reach, so
 * that the read cannot lose half-slots to a later widening.
 */
struct frame_masks {
	u32 words;
	unsigned long bits[];
};

/*
 * A function instance keyed by (callsite, depth).
 * Encapsulates read and write marks for each instruction in the function.
 * Marks are tracked for each frame up to @depth.
 */
struct func_instance {
	struct hlist_node hl_node;
	u32 callsite;		/* call insn that invoked this subprog (subprog_start for depth 0) */
	u32 depth;		/* call depth (0 = entry subprog) */
	u32 subprog;		/* subprog index */
	u32 subprog_start;	/* cached env->subprog_info[subprog].start */
	u32 insn_cnt;		/* cached number of insns in the function */
	/* Per frame, per instruction masks, frames allocated lazily. */
	struct frame_masks *frames[MAX_CALL_FRAMES];
	bool must_write_initialized;
};

struct live_stack_query {
	struct func_instance *instances[MAX_CALL_FRAMES]; /* valid in range [0..curframe] */
	u32 callsites[MAX_CALL_FRAMES]; /* callsite[i] = insn calling frame i+1 */
	u32 curframe;
	u32 insn_idx;
};

struct bpf_liveness {
	DECLARE_HASHTABLE(func_instances, 8);		/* maps (depth, callsite) to func_instance */
	struct live_stack_query live_stack_query;	/* cache to avoid repetitive ht lookups */
	u32 subprog_calls;				/* analyze_subprog() invocations */
};

/*
 * Hash/compare key for func_instance: (depth, callsite).
 * For depth == 0 (entry subprog), @callsite is the subprog start insn.
 * For depth > 0, @callsite is the call instruction index that invoked the subprog.
 */
static u32 instance_hash(u32 callsite, u32 depth)
{
	u32 key[2] = { depth, callsite };

	return jhash2(key, 2, 0);
}

static struct func_instance *find_instance(struct bpf_verifier_env *env,
					   u32 callsite, u32 depth)
{
	struct bpf_liveness *liveness = env->liveness;
	struct func_instance *f;
	u32 key = instance_hash(callsite, depth);

	hash_for_each_possible(liveness->func_instances, f, hl_node, key)
		if (f->depth == depth && f->callsite == callsite)
			return f;
	return NULL;
}

static struct func_instance *call_instance(struct bpf_verifier_env *env,
					   struct func_instance *caller,
					   u32 callsite, int subprog)
{
	u32 depth = caller ? caller->depth + 1 : 0;
	u32 subprog_start = env->subprog_info[subprog].start;
	u32 lookup_key = depth > 0 ? callsite : subprog_start;
	struct func_instance *f;
	u32 hash;

	f = find_instance(env, lookup_key, depth);
	if (f)
		return f;

	f = kvzalloc_obj(*f, GFP_KERNEL_ACCOUNT);
	if (!f)
		return ERR_PTR(-ENOMEM);
	f->callsite = lookup_key;
	f->depth = depth;
	f->subprog = subprog;
	f->subprog_start = subprog_start;
	f->insn_cnt = (env->subprog_info + subprog + 1)->start - subprog_start;
	hash = instance_hash(lookup_key, depth);
	hash_add(env->liveness->func_instances, &f->hl_node, hash);
	return f;
}

static struct func_instance *lookup_instance(struct bpf_verifier_env *env,
					     struct bpf_verifier_state *st,
					     u32 frameno)
{
	u32 callsite, subprog_start;
	struct func_instance *f;
	u32 key, depth;

	subprog_start = env->subprog_info[st->frame[frameno]->subprogno].start;
	callsite = frameno > 0 ? st->frame[frameno]->callsite : subprog_start;

	for (depth = frameno; ; depth--) {
		key = depth > 0 ? callsite : subprog_start;
		f = find_instance(env, key, depth);
		if (f || depth == 0)
			return f;
	}
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
		for (i = 0; i <= instance->depth; i++)
			kvfree(instance->frames[i]);
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
	return insn_idx - instance->subprog_start;
}

static u32 frame_mask_bits(struct frame_masks *fm)
{
	return fm->words * BITS_PER_LONG;
}

static size_t frame_mask_words(struct func_instance *instance, u32 words)
{
	return (size_t)instance->insn_cnt * FM_MASK_CNT * words;
}

/* Mask @kind of the instruction at relative index @rel */
static unsigned long *rel_mask(struct frame_masks *fm, u32 rel, u32 kind)
{
	return fm->bits + ((size_t)rel * FM_MASK_CNT + kind) * fm->words;
}

/*
 * Make sure @frame has a mask array at least @words wide, allocating it or
 * copying the existing masks into the wider stride as needed.
 * @words must be in range [1, FRAME_MAX_WORDS].
 */
static struct frame_masks *widen_frame_masks(struct func_instance *instance,
					     u32 frame, u32 words)
{
	struct frame_masks *old = instance->frames[frame], *new;
	u32 i, kind;

	if (old && old->words >= words)
		return old;
	new = kvzalloc_flex(*new, bits, frame_mask_words(instance, words), GFP_KERNEL_ACCOUNT);
	if (!new)
		return NULL;
	new->words = words;
	if (old) {
		for (i = 0; i < instance->insn_cnt; i++)
			for (kind = 0; kind < FM_MASK_CNT; kind++)
				memcpy(rel_mask(new, i, kind), rel_mask(old, i, kind),
				       old->words * sizeof(*old->bits));
		kvfree(old);
	}
	instance->frames[frame] = new;
	return new;
}

/*
 * Set the inclusive half-slot range [lo, hi] in mask @kind of @frame at @insn_idx.
 * An empty range, including one with a negative @hi as computed for a write
 * that does not fully cover any half-slot, marks nothing.
 */
static int mark_stack_range(struct func_instance *instance, u32 frame, u32 insn_idx,
			    u32 kind, s32 lo, s32 hi)
{
	struct frame_masks *fm;

	/*
	 * An access past the frame bottom is rejected by the main verifier
	 * pass later, liveness only has to avoid running off the masks.
	 */
	hi = min_t(s32, hi, FRAME_HALF_SPIS - 1);
	if (lo > hi)
		return 0;
	fm = widen_frame_masks(instance, frame, BITS_TO_LONGS(hi + 1));
	if (!fm)
		return -ENOMEM;
	bitmap_set(rel_mask(fm, relative_idx(instance, insn_idx), kind), lo, hi - lo + 1);
	return 0;
}

/* Accumulate may_read for half-slots [lo, hi] of @frame at @insn_idx */
static int mark_stack_read(struct func_instance *instance, u32 frame, u32 insn_idx,
			   s32 lo, s32 hi)
{
	return mark_stack_range(instance, frame, insn_idx, FM_MAY_READ, lo, hi);
}

/* Accumulate must_write for half-slots [lo, hi] of @frame at @insn_idx */
static int mark_stack_write(struct func_instance *instance, u32 frame, u32 insn_idx,
			    s32 lo, s32 hi)
{
	return mark_stack_range(instance, frame, insn_idx, FM_MUST_WRITE, lo, hi);
}

static int mark_stack_may_write(struct func_instance *instance, u32 frame, u32 insn_idx,
				s32 lo, s32 hi)
{
	return mark_stack_range(instance, frame, insn_idx, FM_MAY_WRITE, lo, hi);
}

/*
 * Mark every half-slot of @frame as possibly read by @insn_idx. This widens
 * the masks to the program's stack budget: a full read recorded at a narrower
 * width would leave the bits added by a later widening clear and lose part of
 * it. No verifier state holds a slot past the budget, so no liveness query
 * reaches beyond it; an access past the budget in unreachable code may still
 * widen a frame's masks further, which is harmless.
 */
static int mark_stack_read_all(struct bpf_verifier_env *env, struct func_instance *instance,
			       u32 frame, u32 insn_idx)
{
	return mark_stack_read(instance, frame, insn_idx, 0,
			       env->stack_limit / BPF_HALF_REG_SIZE - 1);
}

static int mark_stack_may_write_all(struct bpf_verifier_env *env, struct func_instance *instance,
				    u32 frame, u32 insn_idx)
{
	return mark_stack_may_write(instance, frame, insn_idx, 0,
				    env->stack_limit / BPF_HALF_REG_SIZE - 1);
}

/* Accumulate @src, a mask @src_words wide, into @kind mask of @frame at @insn_idx */
static int mark_stack_mask(struct func_instance *instance, u32 frame, u32 insn_idx, u32 kind,
			   const unsigned long *src, u32 src_words)
{
	u32 nbits = src_words * BITS_PER_LONG;
	struct frame_masks *fm;
	unsigned long *dst;
	u32 last, w;

	last = find_last_bit(src, nbits);
	if (last == nbits)
		return 0;
	fm = widen_frame_masks(instance, frame, BITS_TO_LONGS(last + 1));
	if (!fm)
		return -ENOMEM;
	dst = rel_mask(fm, relative_idx(instance, insn_idx), kind);
	/* @src has no bits set past @last, hence none past @fm->words either */
	src_words = min(src_words, fm->words);
	for (w = 0; w < src_words; w++)
		dst[w] |= src[w];
	return 0;
}

static int mark_stack_read_mask(struct func_instance *instance, u32 frame, u32 insn_idx,
				const unsigned long *src, u32 src_words)
{
	return mark_stack_mask(instance, frame, insn_idx, FM_MAY_READ, src, src_words);
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

	/* pre-allocated array of size up to 2; reset cnt, as it may have been used already */
	succ = env->succ;
	succ->cnt = 0;

	/* All gotox of a subprogram share its jump table, see compute_subprog_jts() */
	if (unlikely(insn_is_gotox(insn))) {
		jt = bpf_find_containing_subprog(env, idx)->jt;
		if (verifier_bug_if(!jt, env, "no jump table for gotox insn %u", idx))
			return succ;
		return jt;
	}

	opcode_info = &opcode_info_tbl[BPF_CLASS(insn->code) | BPF_OP(insn->code)];
	insn_sz = bpf_is_ldimm64(insn) ? 2 : 1;
	if (opcode_info->can_fallthrough)
		succ->items[succ->cnt++] = idx + insn_sz;

	if (opcode_info->can_jump)
		succ->items[succ->cnt++] = idx + bpf_jmp_offset(insn) + 1;

	/*
	 * tail_call upon success and ld_{abs,ind} upon load failure return
	 * from the subprogram, which is a hidden edge to its exit. A subprogram
	 * without an exit insn has no exit to model.
	 */
	if (unlikely((bpf_helper_call(insn) && insn->imm == BPF_FUNC_tail_call) ||
		     (BPF_CLASS(insn->code) == BPF_LD &&
		      (BPF_MODE(insn->code) == BPF_ABS || BPF_MODE(insn->code) == BPF_IND)))) {
		u32 exit_idx = bpf_find_containing_subprog(env, idx)->exit_idx;

		if (exit_idx != U32_MAX)
			succ->items[succ->cnt++] = exit_idx;
	}

	return succ;
}

__diag_pop();

static inline bool update_insn(struct bpf_verifier_env *env,
			       struct func_instance *instance, u32 frame, u32 insn_idx)
{
	unsigned long new_after[FRAME_MAX_WORDS] = {};
	unsigned long *may_read, *must_write, *live_before;
	struct frame_masks *fm = instance->frames[frame];
	u32 rel = relative_idx(instance, insn_idx);
	struct bpf_iarray *succ;
	bool changed = false;
	u32 s, w;

	succ = bpf_insn_successors(env, insn_idx);
	if (succ->cnt == 0)
		return false;

	/* All instructions of one frame array share the same mask width */
	for (s = 0; s < succ->cnt; ++s) {
		unsigned long *succ_live;

		succ_live = rel_mask(fm, relative_idx(instance, succ->items[s]), FM_LIVE_BEFORE);
		for (w = 0; w < fm->words; w++)
			new_after[w] |= succ_live[w];
	}
	may_read = rel_mask(fm, rel, FM_MAY_READ);
	must_write = rel_mask(fm, rel, FM_MUST_WRITE);
	live_before = rel_mask(fm, rel, FM_LIVE_BEFORE);
	/*
	 * New "live_before" is a union of all "live_before" of successors
	 * minus slots written by instruction plus slots read by instruction.
	 * new_before = (new_after & ~insn->must_write) | insn->may_read
	 */
	for (w = 0; w < fm->words; w++) {
		unsigned long new_before = (new_after[w] & ~must_write[w]) | may_read[w];

		if (new_before != live_before[w]) {
			live_before[w] = new_before;
			changed = true;
		}
	}
	return changed;
}

/* Fixed-point computation of @live_before marks */
static void update_instance(struct bpf_verifier_env *env, struct func_instance *instance)
{
	u32 i, frame, po_start, po_end;
	int *insn_postorder = env->cfg.insn_postorder;
	struct bpf_subprog_info *subprog;
	bool changed;

	instance->must_write_initialized = true;
	subprog = &env->subprog_info[instance->subprog];
	po_start = subprog->postorder_start;
	po_end = (subprog + 1)->postorder_start;
	/* repeat until fixed point is reached */
	do {
		changed = false;
		for (frame = 0; frame <= instance->depth; frame++) {
			if (!instance->frames[frame])
				continue;

			for (i = po_start; i < po_end; i++)
				changed |= update_insn(env, instance, frame, insn_postorder[i]);
		}
	} while (changed);
}

static bool is_live_before(struct func_instance *instance, u32 insn_idx, u32 frameno, u32 half_spi)
{
	struct frame_masks *fm = instance->frames[frameno];

	/* No recorded access reaches past the masks, so nothing there is live */
	if (!fm || half_spi >= frame_mask_bits(fm))
		return false;
	return test_bit(half_spi, rel_mask(fm, relative_idx(instance, insn_idx), FM_LIVE_BEFORE));
}

int bpf_live_stack_query_init(struct bpf_verifier_env *env, struct bpf_verifier_state *st)
{
	struct live_stack_query *q = &env->liveness->live_stack_query;
	struct func_instance *instance;
	u32 frame;

	memset(q, 0, sizeof(*q));
	for (frame = 0; frame <= st->curframe; frame++) {
		instance = lookup_instance(env, st, frame);
		if (IS_ERR_OR_NULL(instance))
			q->instances[frame] = NULL;
		else
			q->instances[frame] = instance;
		if (frame < st->curframe)
			q->callsites[frame] = st->frame[frame + 1]->callsite;
	}
	q->curframe = st->curframe;
	q->insn_idx = st->insn_idx;
	return 0;
}

/*
 * Stack slots of outer frames that the callee may read are accounted as read
 * by the @callsite insn itself, i.e. they are live before @callsite and may be
 * dead after it, when:
 * - @callsite calls a callback, which may be called several times;
 * - @callsite is callx. Its callee is not known when stack liveness is
 *   computed, so there is no func instance for it at @callsite. While
 *   the verifier is in the callee lookup_instance() finds its standalone
 *   instance that knows nothing about outer frames. What the callee may read
 *   there is recorded by record_call_access().
 */
static bool callee_stack_access_at_callsite(struct bpf_verifier_env *env, u32 callsite)
{
	return bpf_calls_callback(env, callsite) ||
	       bpf_is_callx(&env->prog->insnsi[callsite]);
}

bool bpf_stack_slot_alive(struct bpf_verifier_env *env, u32 frameno, u32 half_spi)
{
	/*
	 * Slot is alive if it is read before q->insn_idx in current func instance,
	 * or if for some outer func instance:
	 * - alive before callsite if callsite calls callback or is callx, otherwise
	 * - alive after callsite
	 */
	struct live_stack_query *q = &env->liveness->live_stack_query;
	struct func_instance *instance, *curframe_instance;
	u32 i, callsite, rel;
	int cur_delta, delta;
	bool alive = false;

	curframe_instance = q->instances[q->curframe];
	if (!curframe_instance)
		return true;
	cur_delta = (int)curframe_instance->depth - (int)q->curframe;
	rel = frameno + cur_delta;
	if (rel <= curframe_instance->depth)
		alive = is_live_before(curframe_instance, q->insn_idx, rel, half_spi);

	if (alive)
		return true;

	for (i = frameno; i < q->curframe; i++) {
		instance = q->instances[i];
		if (!instance)
			return true;
		/* Map actual frameno to frame index within this instance */
		delta = (int)instance->depth - (int)i;
		rel = frameno + delta;
		if (rel > instance->depth)
			return true;

		/* Get callsite from verifier state, not from instance callchain */
		callsite = q->callsites[i];

		alive = callee_stack_access_at_callsite(env, callsite)
			? is_live_before(instance, callsite, rel, half_spi)
			: is_live_before(instance, callsite + 1, rel, half_spi);
		if (alive)
			return true;
	}

	return false;
}

static char *fmt_subprog(struct bpf_verifier_env *env, int subprog)
{
	const char *name = env->subprog_info[subprog].name;

	snprintf(env->tmp_str_buf, sizeof(env->tmp_str_buf),
		 "subprog#%d%s%s", subprog, name ? " " : "", name ? name : "");
	return env->tmp_str_buf;
}

static char *fmt_instance(struct bpf_verifier_env *env, struct func_instance *instance)
{
	snprintf(env->tmp_str_buf, sizeof(env->tmp_str_buf),
		 "(d%d,cs%d)", instance->depth, instance->callsite);
	return env->tmp_str_buf;
}

static int spi_off(int spi)
{
	return -(spi + 1) * BPF_REG_SIZE;
}

/*
 * When both halves of an 8-byte SPI are set, print as "-8","-16",...
 * When only one half is set, print as "-4h","-8h",...
 * Runs of 3+ consecutive fully-set SPIs are collapsed: "fp0-8..-24"
 */
static char *fmt_spis_mask(struct bpf_verifier_env *env, int frame, bool first,
			   const unsigned long *spis, u32 words)
{
	int buf_sz = sizeof(env->tmp_str_buf);
	int spi_cnt = words * BITS_PER_LONG / 2;
	char *buf = env->tmp_str_buf;
	int spi, n, run_start;

	buf[0] = '\0';

	for (spi = 0; spi < spi_cnt && buf_sz > 0; spi++) {
		bool lo = test_bit(spi * 2, spis);
		bool hi = test_bit(spi * 2 + 1, spis);
		const char *space = first ? "" : " ";

		if (!lo && !hi)
			continue;

		if (!lo || !hi) {
			/* half-spi */
			n = scnprintf(buf, buf_sz, "%sfp%d%d%s",
				      space, frame, spi_off(spi) + (lo ? STACK_SLOT_SZ : 0), "h");
		} else if (spi + 2 < spi_cnt &&
			   test_bit(spi * 2 + 2, spis) &&
			   test_bit(spi * 2 + 3, spis) &&
			   test_bit(spi * 2 + 4, spis) &&
			   test_bit(spi * 2 + 5, spis)) {
			/* 3+ consecutive full spis */
			run_start = spi;
			while (spi + 1 < spi_cnt &&
			       test_bit((spi + 1) * 2, spis) &&
			       test_bit((spi + 1) * 2 + 1, spis))
				spi++;
			n = scnprintf(buf, buf_sz, "%sfp%d%d..%d",
				      space, frame, spi_off(run_start), spi_off(spi));
		} else {
			/* just a full spi */
			n = scnprintf(buf, buf_sz, "%sfp%d%d", space, frame, spi_off(spi));
		}
		first = false;
		buf += n;
		buf_sz -= n;
	}
	return env->tmp_str_buf;
}

/* Print mask @kind of the instruction at relative index @i for every frame, if any bit is set. */
static bool print_mask(struct bpf_verifier_env *env, struct func_instance *instance, int i,
		       const char *name, u32 kind)
{
	struct frame_masks *fm;
	bool printed = false;
	unsigned long *mask;
	int frame;
	u64 pos;

	pos = env->log.end_pos;
	verbose(env, "%s", name);
	for (frame = instance->depth; frame >= 0; --frame) {
		fm = instance->frames[frame];
		if (!fm)
			continue;
		mask = rel_mask(fm, i, kind);
		if (bitmap_empty(mask, frame_mask_bits(fm)))
			continue;
		verbose(env, "%s", fmt_spis_mask(env, frame, !printed, mask, fm->words));
		printed = true;
	}
	if (!printed)
		bpf_vlog_reset(&env->log, pos);
	return printed;
}

static void print_instance(struct bpf_verifier_env *env, struct func_instance *instance)
{
	int start = env->subprog_info[instance->subprog].start;
	struct bpf_insn *insns = env->prog->insnsi;
	int len = instance->insn_cnt;
	u64 pos, insn_pos;
	int insn_idx, i;
	bool printed;

	if (!(env->log.level & BPF_LOG_LEVEL2))
		return;

	verbose(env, "stack use/def %s ", fmt_subprog(env, instance->subprog));
	verbose(env, "%s:\n", fmt_instance(env, instance));
	for (i = 0; i < len; i++) {
		insn_idx = start + i;
		pos = env->log.end_pos;
		verbose(env, "%3d: ", insn_idx);
		bpf_verbose_insn(env, &insns[insn_idx]);
		insn_pos = env->log.end_pos;
		verbose(env, "%*c;", bpf_vlog_alignment(insn_pos - pos), ' ');
		printed = false;
		printed |= print_mask(env, instance, i, " use: ", FM_MAY_READ);
		printed |= print_mask(env, instance, i, " def: ", FM_MUST_WRITE);
		printed |= print_mask(env, instance, i, " may_def: ", FM_MAY_WRITE);
		if (!printed)
			bpf_vlog_reset(&env->log, insn_pos);
		verbose(env, "\n");
		if (bpf_is_ldimm64(&insns[insn_idx]))
			i++;
	}
}

static int cmp_instances(const void *pa, const void *pb)
{
	struct func_instance *a = *(struct func_instance **)pa;
	struct func_instance *b = *(struct func_instance **)pb;
	int dcallsite = (int)a->callsite - b->callsite;
	int ddepth = (int)a->depth - b->depth;

	if (dcallsite)
		return dcallsite;
	if (ddepth)
		return ddepth;
	return 0;
}

/* OR the 8-byte slots touched by a half-slot (4-byte) mask into @slots. */
static void half_spis_to_slots(unsigned long *slots, const unsigned long *mask, u32 nbits)
{
	u32 slot;

	for (slot = 0; slot < MAX_BPF_STACK_SLOTS && slot * 2 + 1 < nbits; slot++)
		if (test_bit(slot * 2, mask) || test_bit(slot * 2 + 1, mask))
			__set_bit(slot, slots);
}

/*
 * Precompute, for each instruction, the OR of may_write and live_before masks
 * over its top frame across all func_instances reaching it, stash them in the
 * insn_aux_data.
 */
static void compute_may_write_masks(struct bpf_verifier_env *env)
{
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	struct bpf_liveness *liveness = env->liveness;
	struct func_instance *instance;
	struct frame_masks *fm;
	u32 nbits;
	int bkt, i;

	hash_for_each(liveness->func_instances, bkt, instance, hl_node) {
		fm = instance->frames[instance->depth];
		if (!fm)
			continue;
		nbits = frame_mask_bits(fm);
		for (i = 0; i < instance->insn_cnt; i++) {
			half_spis_to_slots(aux[instance->subprog_start + i].may_write_mask,
					   rel_mask(fm, i, FM_MAY_WRITE), nbits);
			half_spis_to_slots(aux[instance->subprog_start + i].live_stack_before,
					   rel_mask(fm, i, FM_LIVE_BEFORE), nbits);
		}
	}
}

const unsigned long *bpf_may_write_mask(struct bpf_verifier_env *env, u32 insn_idx)
{
	return env->insn_aux_data[insn_idx].may_write_mask;
}

/* print use/def slots for all instances ordered by callsite first, then by depth */
static int print_instances(struct bpf_verifier_env *env)
{
	struct func_instance *instance, **sorted_instances;
	struct bpf_liveness *liveness = env->liveness;
	int i, bkt, cnt;

	cnt = 0;
	hash_for_each(liveness->func_instances, bkt, instance, hl_node)
		cnt++;
	sorted_instances = kvmalloc_objs(*sorted_instances, cnt, GFP_KERNEL_ACCOUNT);
	if (!sorted_instances)
		return -ENOMEM;
	cnt = 0;
	hash_for_each(liveness->func_instances, bkt, instance, hl_node)
		sorted_instances[cnt++] = instance;
	sort(sorted_instances, cnt, sizeof(*sorted_instances), cmp_instances, NULL);
	for (i = 0; i < cnt; i++)
		print_instance(env, sorted_instances[i]);
	kvfree(sorted_instances);
	return 0;
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
 *   offset-imprecise {frame=N, cnt=0}
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
 * At memory access sites (LDX/STX/ST), offset-imprecise marks the known
 * frame as fully read, while fully-imprecise iterates bits in the bitmask
 * and routes each frame to its target.
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

/*
 * The tracker follows as many spill slots as a subprog addresses directly
 * through R10, see subprog_spill_slots(). A frame's slots at a call site are
 * kept in a snapshot for the callees that receive pointers into that frame.
 */
struct spill_snapshot {
	u32 slots;
	struct arg_track at[];
};

/*
 * Combined register + stack arg tracking: R0-R10 at indices 0-10,
 * outgoing stack arg slots at indices MAX_BPF_REG..MAX_BPF_REG+6.
 */
#define MAX_AT_TRACK_REGS (MAX_BPF_REG + MAX_STACK_ARG_SLOTS)

static int stack_arg_off_to_slot(s16 off)
{
	int aoff = off < 0 ? -off : off;

	if (aoff / 8 > MAX_STACK_ARG_SLOTS)
		return -1;
	return aoff / 8 - 1;
}

static bool arg_is_visited(const struct arg_track *at)
{
	return at->frame != ARG_UNVISITED;
}

static bool arg_is_fp(const struct arg_track *at)
{
	return at->frame >= 0 || at->frame == ARG_IMPRECISE;
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
	if (r >= MAX_BPF_REG)
		verbose(env, "sa%d: ", r - MAX_BPF_REG);
	else if (r >= 0)
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

static bool arg_add(s16 off, s64 delta, s16 *out)
{
	s16 d = delta;

	if (d != delta)
		return true;
	return check_add_overflow(off, d, out);
}

static void arg_padd(struct arg_track *at, s64 delta)
{
	int i;

	if (at->off_cnt == 0)
		return;
	for (i = 0; i < at->off_cnt; i++) {
		s16 new_off;

		if (arg_add(at->off[i], delta, &new_off)) {
			at->off_cnt = 0;
			return;
		}
		at->off[i] = new_off;
	}
}

/*
 * Slots the spill tracker may follow for a subprog of @len instructions
 * without its per-instruction tables costing more than they could for the
 * largest program while every frame stayed within MAX_BPF_STACK: as many
 * entries as 64 slots need for BPF_COMPLEXITY_LIMIT_INSNS instructions.
 */
static u32 spill_slots_affordable(int len)
{
	u32 base = MAX_BPF_STACK / BPF_REG_SIZE;

	return max_t(u32, base, base * BPF_COMPLEXITY_LIMIT_INSNS / len);
}

/*
 * Number of 8-byte spill slots to track for the instructions in [@start, @end):
 * the 64 slots of a MAX_BPF_STACK frame, which the tracker has always
 * followed, or the deepest 8-byte stack access made directly through R10 when
 * that reaches further, up to the program's stack budget. Spills and fills are
 * compiled as direct R10 accesses, so the count covers them whatever the frame
 * size; a fill through a derived pointer that reaches past it is treated as
 * imprecise, as one past the frame always was.
 */
static u32 subprog_spill_slots(struct bpf_verifier_env *env, int start, int end)
{
	struct bpf_insn *insns = env->prog->insnsi;
	int deepest = 0;

	for (int i = start; i < end; i++) {
		struct bpf_insn *insn = &insns[i];
		u8 class = BPF_CLASS(insn->code);
		u8 base;

		if (BPF_SIZE(insn->code) != BPF_DW)
			continue;
		if (class == BPF_LDX)
			base = insn->src_reg;
		else if (class == BPF_STX || class == BPF_ST)
			base = insn->dst_reg;
		else
			continue;
		if (base == BPF_REG_FP && insn->off < 0)
			deepest = max(deepest, -insn->off);
	}
	return clamp_t(u32, DIV_ROUND_UP(deepest, 8), MAX_BPF_STACK / BPF_REG_SIZE,
		       env->stack_limit / BPF_REG_SIZE);
}

/*
 * Convert a byte offset from FP to one of the @nslots tracked stack slots.
 * Returns -1 if out of range or not 8-byte aligned.
 * Slot 0 = fp-8, slot 1 = fp-16, ..., slot 7 = fp-64, ....
 */
static int fp_off_to_slot(s16 off, u32 nslots)
{
	if (off >= 0 || off < -(int)(nslots * 8))
		return -1;
	if (off % 8)
		return -1;
	return (-off) / 8 - 1;
}

static struct arg_track fill_from_stack(struct bpf_insn *insn,
					struct arg_track *at_out, int reg,
					struct arg_track *at_stack_out, u32 nslots,
					int depth)
{
	struct arg_track imp = {
		.mask = (1u << (depth + 1)) - 1,
		.frame = ARG_IMPRECISE
	};
	struct arg_track result = { .frame = ARG_NONE };
	int cnt, i;

	if (reg == BPF_REG_FP) {
		int slot = fp_off_to_slot(insn->off, nslots);

		return slot >= 0 ? at_stack_out[slot] : imp;
	}
	cnt = at_out[reg].off_cnt;
	if (cnt == 0)
		return imp;

	for (i = 0; i < cnt; i++) {
		s16 fp_off, slot;

		if (arg_add(at_out[reg].off[i], insn->off, &fp_off))
			return imp;
		slot = fp_off_to_slot(fp_off, nslots);
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
			   int reg, struct arg_track *at_stack_out, u32 nslots,
			   struct arg_track *val, u32 sz)
{
	struct arg_track none = { .frame = ARG_NONE };
	struct arg_track new_val = sz == 8 ? *val : none;
	int cnt, i;

	if (reg == BPF_REG_FP) {
		int slot = fp_off_to_slot(insn->off, nslots);

		if (slot >= 0)
			at_stack_out[slot] = new_val;
		return;
	}
	cnt = at_out[reg].off_cnt;
	if (cnt == 0) {
		for (int slot = 0; slot < nslots; slot++)
			at_stack_out[slot] = __arg_track_join(at_stack_out[slot], new_val);
		return;
	}
	for (i = 0; i < cnt; i++) {
		s16 fp_off;
		int slot;

		if (arg_add(at_out[reg].off[i], insn->off, &fp_off))
			continue;
		slot = fp_off_to_slot(fp_off, nslots);
		if (slot < 0)
			continue;
		if (cnt == 1)
			at_stack_out[slot] = new_val;
		else
			at_stack_out[slot] = __arg_track_join(at_stack_out[slot], new_val);
	}
}

/*
 * Clear all tracked callee stack slots overlapping the byte range
 * [off, off+sz-1] where off is a negative FP-relative offset.
 */
static void clear_overlapping_stack_slots(struct arg_track *at_stack, u32 nslots, s16 off,
					  u32 sz, int cnt)
{
	struct arg_track none = { .frame = ARG_NONE };

	if (cnt == 0) {
		for (int i = 0; i < nslots; i++)
			at_stack[i] = __arg_track_join(at_stack[i], none);
		return;
	}
	for (int i = 0; i < nslots; i++) {
		int slot_start = -((i + 1) * 8);
		int slot_end = slot_start + 8;

		if (slot_start < off + (int)sz && slot_end > off) {
			if (cnt == 1)
				at_stack[i] = none;
			else
				at_stack[i] = __arg_track_join(at_stack[i], none);
		}
	}
}

/*
 * Clear stack slots overlapping all possible FP offsets in @reg.
 */
static void clear_stack_for_all_offs(struct bpf_insn *insn,
				     struct arg_track *at_out, int reg,
				     struct arg_track *at_stack_out, u32 nslots, u32 sz)
{
	int cnt, i;

	if (reg == BPF_REG_FP) {
		clear_overlapping_stack_slots(at_stack_out, nslots, insn->off, sz, 1);
		return;
	}
	cnt = at_out[reg].off_cnt;
	if (cnt == 0) {
		clear_overlapping_stack_slots(at_stack_out, nslots, 0, sz, cnt);
		return;
	}
	for (i = 0; i < cnt; i++) {
		s16 fp_off;

		if (arg_add(at_out[reg].off[i], insn->off, &fp_off)) {
			clear_overlapping_stack_slots(at_stack_out, nslots, 0, sz, 0);
			break;
		}
		clear_overlapping_stack_slots(at_stack_out, nslots, fp_off, sz, cnt);
	}
}

static void arg_track_log(struct bpf_verifier_env *env, struct bpf_insn *insn, int idx,
			  struct arg_track *at_in, struct arg_track *at_stack_in,
			  struct arg_track *at_out, struct arg_track *at_stack_out, u32 nslots)
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
			printed = true;
		}
		verbose(env, "\tr%d: ", i); verbose_arg_track(env, &at_in[i]);
		verbose(env, " -> "); verbose_arg_track(env, &at_out[i]);
	}
	/* Log outgoing stack arg slot transitions at indices MAX_BPF_REG..MAX_AT_TRACK_REGS-1 */
	for (i = 0; i < MAX_STACK_ARG_SLOTS; i++) {
		int ai = MAX_BPF_REG + i;

		if (arg_track_eq(&at_out[ai], &at_in[ai]))
			continue;
		if (!printed) {
			verbose(env, "%3d: ", idx);
			bpf_verbose_insn(env, insn);
			printed = true;
		}
		verbose(env, "\tsa%d: ", i); verbose_arg_track(env, &at_in[ai]);
		verbose(env, " -> "); verbose_arg_track(env, &at_out[ai]);
	}
	for (i = 0; i < nslots; i++) {
		if (arg_track_eq(&at_stack_out[i], &at_stack_in[i]))
			continue;
		if (!printed) {
			verbose(env, "%3d: ", idx);
			bpf_verbose_insn(env, insn);
			printed = true;
		}
		verbose(env, "\tfp%+d: ", -(i + 1) * 8); verbose_arg_track(env, &at_stack_in[i]);
		verbose(env, " -> "); verbose_arg_track(env, &at_stack_out[i]);
	}
	if (printed)
		verbose(env, "\n");
}

static bool can_be_local_fp(int depth, int regno, struct arg_track *at)
{
	return regno == BPF_REG_FP || at->frame == depth ||
	       (at->frame == ARG_IMPRECISE && (at->mask & BIT(depth)));
}

/*
 * Model 8-byte reads through @reg, including load-acquire
 * and the old value fetched by atomic operations.
 */
static struct arg_track arg_track_deref(struct bpf_verifier_env *env, struct bpf_insn *insn,
					struct arg_track *at_out, int reg,
					struct arg_track *at_stack_out, u32 nslots,
					int depth, u32 *callsites)
{
	struct arg_track *ptr = &at_out[reg];

	if (can_be_local_fp(depth, reg, ptr))
		return fill_from_stack(insn, at_out, reg, at_stack_out, nslots, depth);
	if (ptr->frame >= 0 && ptr->frame < depth) {
		struct spill_snapshot *parent = env->callsite_at_stack[callsites[ptr->frame]];

		return fill_from_stack(insn, at_out, reg, parent->at, parent->slots, ptr->frame);
	}
	if (ptr->frame == ARG_IMPRECISE && !(ptr->mask & BIT(depth)) && ptr->mask)
		return *ptr;
	return (struct arg_track){ .frame = ARG_NONE };
}

/*
 * Pure dataflow transfer function for arg_track state.
 * Updates at_out[] based on how the instruction modifies registers.
 * Tracks spill/fill, but not other memory accesses.
 */
static void arg_track_xfer(struct bpf_verifier_env *env, struct bpf_insn *insn,
			   int insn_idx,
			   struct arg_track *at_out, struct arg_track *at_stack_out, u32 nslots,
			   const struct arg_track *at_stack_arg_entry,
			   struct func_instance *instance,
			   u32 *callsites)
{
	int depth = instance->depth;
	u8 class = BPF_CLASS(insn->code);
	u8 code = BPF_OP(insn->code);
	struct arg_track *dst = &at_out[insn->dst_reg];
	struct arg_track *src = &at_out[insn->src_reg];
	struct arg_track none = { .frame = ARG_NONE };
	int r, slot;

	/* Handle stack arg stores and loads. */
	if (is_stack_arg_st(insn) || is_stack_arg_stx(insn)) {
		slot = stack_arg_off_to_slot(insn->off);
		if (slot >= 0) {
			if (is_stack_arg_stx(insn))
				at_out[MAX_BPF_REG + slot] = at_out[insn->src_reg];
			else
				at_out[MAX_BPF_REG + slot] = none;
		}
	} else if (is_stack_arg_ldx(insn)) {
		slot = stack_arg_off_to_slot(insn->off);
		at_out[insn->dst_reg] = (slot >= 0) ? at_stack_arg_entry[slot] : none;
	} else if (class == BPF_ALU64 && BPF_SRC(insn->code) == BPF_K) {
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
		/* Only an 8-byte BPF_MEM load can be a fill, see arg_track_deref(). */
		if (BPF_MODE(insn->code) == BPF_MEM && BPF_SIZE(insn->code) == BPF_DW)
			*dst = arg_track_deref(env, insn, at_out, insn->src_reg,
					       at_stack_out, nslots, depth, callsites);
		else
			*dst = none;
	} else if (class == BPF_LD && BPF_MODE(insn->code) == BPF_IMM) {
		*dst = none;
	} else if (class == BPF_STX) {
		u32 sz = bpf_size_to_bytes(BPF_SIZE(insn->code));
		bool dst_is_local_fp = can_be_local_fp(depth, insn->dst_reg, dst);
		u8 mode = BPF_MODE(insn->code);

		if (mode == BPF_MEM || (mode == BPF_ATOMIC && insn->imm == BPF_STORE_REL)) {
			if (dst_is_local_fp)
				spill_to_stack(insn, at_out, insn->dst_reg,
					       at_stack_out, nslots, src, sz);
		} else if (mode == BPF_ATOMIC && insn->imm == BPF_LOAD_ACQ) {
			if (sz == 8)
				*dst = arg_track_deref(env, insn, at_out, insn->src_reg,
						       at_stack_out, nslots, depth, callsites);
			else
				*dst = none;
		} else if (mode == BPF_ATOMIC) {
			/* BPF_XCHG, BPF_CMPXCHG or BPF_{ADD,AND,OR,XOR} | [BPF_FETCH] */
			struct arg_track old = none;

			r = bpf_atomic_load_reg(insn);
			if (r >= 0 && sz == 8)
				old = arg_track_deref(env, insn, at_out, insn->dst_reg,
						      at_stack_out, nslots, depth, callsites);
			if (dst_is_local_fp)
				clear_stack_for_all_offs(insn, at_out, insn->dst_reg,
							 at_stack_out, nslots, sz);
			if (r >= 0)
				at_out[r] = old;
		}
	} else if (class == BPF_ST && BPF_MODE(insn->code) == BPF_MEM) {
		u32 sz = bpf_size_to_bytes(BPF_SIZE(insn->code));
		bool dst_is_local_fp = can_be_local_fp(depth, insn->dst_reg, dst);

		/* BPF_ST to FP-derived dst: clear overlapping stack slots */
		if (dst_is_local_fp)
			clear_stack_for_all_offs(insn, at_out, insn->dst_reg,
						 at_stack_out, nslots, sz);
	}
}

/*
 * Record possible reads and writes for every touched half-slot. A definite
 * write requires full coverage, a known size, and a single possible offset.
 */
static int record_stack_access_off(struct func_instance *instance, const struct arg_track *arg,
				   s64 off_idx, struct arg_access_info info,
				   u32 frame, u32 insn_idx)
{
	s64 fp_off = arg->off[off_idx];
	s32 slot_hi, slot_lo;
	int err;

	if (fp_off >= 0)
		/*
		 * out of bounds stack access doesn't contribute
		 * into actual stack liveness. It will be rejected
		 * by the main verifier pass later.
		 */
		return 0;

	/*
	 * Read and may_write marks include partially covered slots.
	 * An unknown access size may reach from fp_off to the frame top.
	 */
	slot_hi = (-fp_off - 1) / STACK_SLOT_SZ;
	slot_lo = info.size == U32_MAX
		  ? 0
		  : max_t(s32, (-fp_off - info.size) / STACK_SLOT_SZ, 0);
	if (info.may_read) {
		err = mark_stack_read(instance, frame, insn_idx, slot_lo, slot_hi);
		if (err)
			return err;
	}
	if (info.may_write) {
		err = mark_stack_may_write(instance, frame, insn_idx, slot_lo, slot_hi);
		if (err)
			return err;
	}
	if (info.must_write && info.size != U32_MAX && arg->off_cnt == 1) {
		/* Mark only fully covered slots as def */
		slot_hi = (-fp_off) / STACK_SLOT_SZ - 1;
		slot_lo = max_t(s32, (-fp_off - info.size + STACK_SLOT_SZ - 1) / STACK_SLOT_SZ, 0);
		return mark_stack_write(instance, frame, insn_idx, slot_lo, slot_hi);
	}
	return 0;
}

/* Record access through a pointer with a known frame and possibly known offsets. */
static int record_stack_access(struct bpf_verifier_env *env,
			       struct func_instance *instance,
			       const struct arg_track *arg,
			       struct arg_access_info info, u32 frame, u32 insn_idx)
{
	int i, err;

	if (!info.size)
		return 0;
	if (arg->off_cnt == 0) {
		if (info.may_read) {
			err = mark_stack_read_all(env, instance, frame, insn_idx);
			if (err)
				return err;
		}
		if (info.may_write) {
			err = mark_stack_may_write_all(env, instance, frame, insn_idx);
			if (err)
				return err;
		}
		return 0;
	}
	for (i = 0; i < arg->off_cnt; i++) {
		err = record_stack_access_off(instance, arg, i, info, frame, insn_idx);
		if (err)
			return err;
	}
	return 0;
}

/* Record possible effects on every candidate frame of an imprecise pointer. */
static int record_imprecise(struct bpf_verifier_env *env, struct func_instance *instance,
			    struct arg_access_info info, u32 mask, u32 insn_idx)
{
	int depth = instance->depth;
	int f, err;

	for (f = 0; mask; f++, mask >>= 1) {
		if (!(mask & 1))
			continue;
		if (f <= depth) {
			if (info.may_read) {
				err = mark_stack_read_all(env, instance, f, insn_idx);
				if (err)
					return err;
			}
			if (info.may_write) {
				err = mark_stack_may_write_all(env, instance, f, insn_idx);
				if (err)
					return err;
			}
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
	struct arg_access_info info = {};
	int depth = instance->depth;
	u8 class = BPF_CLASS(insn->code);
	bool read = false, write = false;
	struct arg_track resolved, *ptr;
	int oi;

	/*
	 * Stack arg insns use dst_reg/src_reg=BPF_REG_PARAMS(11). Since at[]
	 * is extended to MAX_AT_TRACK_REGS, at[11] holds the arg_track for
	 * outgoing stack arg slot 0 — not the pointer used for the memory
	 * access. Skip so the slot's tracked value isn't confused with the
	 * base register that record_stack_access() expects.
	 */
	if (is_stack_arg_stx(insn) || is_stack_arg_st(insn) || is_stack_arg_ldx(insn))
		return 0;

	switch (class) {
	case BPF_LDX:
		ptr = &at[insn->src_reg];
		read = true;
		break;
	case BPF_STX:
		if (BPF_MODE(insn->code) == BPF_ATOMIC) {
			switch (insn->imm) {
			case BPF_LOAD_ACQ:
				ptr = &at[insn->src_reg];
				read = true;
				break;
			case BPF_STORE_REL:
				ptr = &at[insn->dst_reg];
				write = true;
				break;
			default:
				/* ADD/AND/OR/XOR(+FETCH), XCHG, CMPXCHG */
				ptr = &at[insn->dst_reg];
				read = true;
				write = true;
				break;
			}
		} else {
			ptr = &at[insn->dst_reg];
			write = true;
		}
		break;
	case BPF_ST:
		ptr = &at[insn->dst_reg];
		write = true;
		break;
	default:
		return 0;
	}

	/* Resolve offsets: fold insn->off into arg_track */
	if (ptr->off_cnt > 0) {
		resolved.off_cnt = ptr->off_cnt;
		resolved.frame = ptr->frame;
		for (oi = 0; oi < ptr->off_cnt; oi++) {
			if (arg_add(ptr->off[oi], insn->off, &resolved.off[oi])) {
				resolved.off_cnt = 0;
				break;
			}
		}
		ptr = &resolved;
	}

	info.may_read = read;
	info.may_write = write;
	info.must_write = write;
	info.size = bpf_size_to_bytes(BPF_SIZE(insn->code));
	if (ptr->frame >= 0 && ptr->frame <= depth)
		return record_stack_access(env, instance, ptr, info, ptr->frame, insn_idx);
	if (ptr->frame == ARG_IMPRECISE)
		return record_imprecise(env, instance, info, ptr->mask, insn_idx);
	/* ARG_NONE: not derived from any frame pointer, skip */
	return 0;
}

static int record_arg_access(struct bpf_verifier_env *env,
			     struct func_instance *instance,
			     struct bpf_insn *insn,
			     struct arg_track *at, int arg_idx,
			     int insn_idx)
{
	struct arg_access_info info;
	int depth = instance->depth;
	int frame = at->frame;
	int err = 0;

	if (!arg_is_fp(at))
		return 0;

	if (bpf_helper_call(insn)) {
		info = bpf_helper_stack_access_bytes(env, insn, arg_idx, insn_idx);
	} else if (bpf_pseudo_kfunc_call(insn)) {
		info = bpf_kfunc_stack_access_bytes(env, insn, arg_idx, insn_idx);
	} else if (bpf_pseudo_call(insn)) {
		info = bpf_global_subprog_stack_access_bytes(env, insn, arg_idx, insn_idx);
	} else {
		for (int f = 0; f <= depth; f++) {
			err = mark_stack_read_all(env, instance, f, insn_idx);
			if (err)
				return err;
			err = mark_stack_may_write_all(env, instance, f, insn_idx);
			if (err)
				return err;
		}
		return 0;
	}
	if (!info.size)
		return 0;

	if (frame >= 0 && frame <= depth)
		err = record_stack_access(env, instance, at, info, frame, insn_idx);
	else if (frame == ARG_IMPRECISE)
		err = record_imprecise(env, instance, info, at->mask, insn_idx);
	return err;
}

/* Record stack access for a given 'at' state of helper/kfunc/global subprog 'insn' */
static int record_call_access(struct bpf_verifier_env *env,
			      struct func_instance *instance,
			      struct arg_track *at,
			      int insn_idx)
{
	struct bpf_insn *insn = &env->prog->insnsi[insn_idx];
	struct bpf_call_summary cs;
	int r, err, callee, arg_slot_cnt = 5;

	if (bpf_pseudo_call(insn)) {
		/*
		 * analyze_subprog() handles static callees. For global callees,
		 * derive effects from argument types so may_write marks remain
		 * conservative under freplace.
		 */
		callee = bpf_find_subprog(env, insn_idx + insn->imm + 1);
		if (callee < 0 || !bpf_subprog_is_global(env, callee))
			return 0;
		arg_slot_cnt = MAX_BPF_FUNC_ARGS;
	} else if (bpf_is_callx(insn))
		/*
		 * The callee is not known statically. Assume that all arg
		 * slots are passed and let record_arg_access() conservatively
		 * mark the stack of all frames as read if any of them is
		 * derived from a frame pointer.
		 */
		arg_slot_cnt = MAX_BPF_FUNC_REG_ARGS + MAX_STACK_ARG_SLOTS;
	else if (bpf_get_call_summary(env, insn, &cs))
		arg_slot_cnt = cs.arg_slot_cnt;

	for (r = BPF_REG_1; r < BPF_REG_1 + min(arg_slot_cnt, MAX_BPF_FUNC_REG_ARGS); r++) {
		err = record_arg_access(env, instance, insn, &at[r], r - 1, insn_idx);
		if (err)
			return err;
	}

	for (r = 0; r < MAX_STACK_ARG_SLOTS && r < arg_slot_cnt - MAX_BPF_FUNC_REG_ARGS; r++) {
		err = record_arg_access(env, instance, insn, &at[MAX_BPF_REG + r],
					r + MAX_BPF_FUNC_REG_ARGS, insn_idx);
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
	struct arg_track (*at_in)[MAX_AT_TRACK_REGS];
	int len;
};

static void print_subprog_arg_access(struct bpf_verifier_env *env,
				     int subprog,
				     struct subprog_at_info *info,
				     struct arg_track *at_stack_in, u32 nslots)
{
	struct bpf_insn *insns = env->prog->insnsi;
	int start = env->subprog_info[subprog].start;
	int len = info->len;
	int i, r;

	if (!(env->log.level & BPF_LOG_LEVEL2))
		return;

	verbose(env, "%s:\n", fmt_subprog(env, subprog));
	for (i = 0; i < len; i++) {
		int idx = start + i;
		bool has_extra = false;
		u8 cls = BPF_CLASS(insns[idx].code);
		bool is_ldx_stx_call = cls == BPF_LDX || cls == BPF_STX ||
				       (cls == BPF_JMP && BPF_OP(insns[idx].code) == BPF_CALL);

		verbose(env, "%3d: ", idx);
		bpf_verbose_insn(env, &insns[idx]);
		verbose(env, "\n");

		/* Collect what needs printing */
		if (is_ldx_stx_call &&
		    arg_is_visited(&info->at_in[i][0])) {
			for (r = 0; r < MAX_BPF_REG - 1; r++)
				if (arg_is_fp(&info->at_in[i][r]))
					has_extra = true;
			for (r = 0; r < MAX_STACK_ARG_SLOTS; r++)
				if (arg_is_fp(&info->at_in[i][MAX_BPF_REG + r]))
					has_extra = true;
		}
		if (is_ldx_stx_call) {
			for (r = 0; r < nslots; r++)
				if (arg_is_fp(&at_stack_in[(size_t)i * nslots + r]))
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
			for (r = 0; r < MAX_STACK_ARG_SLOTS; r++) {
				if (!arg_is_fp(&info->at_in[i][MAX_BPF_REG + r]))
					continue;
				verbose(env, " sa%d=", r);
				verbose_arg_track(env, &info->at_in[i][MAX_BPF_REG + r]);
			}
		}

		if (is_ldx_stx_call) {
			for (r = 0; r < nslots; r++) {
				if (!arg_is_fp(&at_stack_in[(size_t)i * nslots + r]))
					continue;
				verbose(env, " fp%+d=", -(r + 1) * 8);
				verbose_arg_track(env, &at_stack_in[(size_t)i * nslots + r]);
			}
		}

		verbose(env, "\n");
		if (bpf_is_ldimm64(&insns[idx]))
			i++;
	}
}

static void record_stack_ptrs(struct bpf_verifier_env *env, int idx, struct arg_track *at_in)
{
	u16 r, mask = 0;

	for (r = 0; r < MAX_BPF_REG; r++)
		if (arg_is_fp(&at_in[r]))
			mask |= BIT(r);

	env->insn_aux_data[idx].stack_ptrs |= mask;
}

/*
 * Compute arg tracking dataflow for a single subprog.
 * Runs forward fixed-point with arg_track_xfer(), then records
 * memory accesses in a single linear pass over converged state.
 *
 * @callee_entry: pre-populated entry state for R1-R5 and stack args
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
	int depth = instance->depth;
	int start = env->subprog_info[subprog].start;
	int po_start = env->subprog_info[subprog].postorder_start;
	int end = env->subprog_info[subprog + 1].start;
	int po_end = env->subprog_info[subprog + 1].postorder_start;
	int len = end - start;
	u32 nslots = min(subprog_spill_slots(env, start, end), spill_slots_affordable(len));
	struct arg_track (*at_in)[MAX_AT_TRACK_REGS] = NULL;
	struct arg_track at_out[MAX_AT_TRACK_REGS];
	struct arg_track *at_stack_in = NULL;
	struct arg_track *at_stack_out = NULL;
	struct arg_track at_stack_arg_entry[MAX_STACK_ARG_SLOTS];
	struct arg_track unvisited = { .frame = ARG_UNVISITED };
	struct arg_track none = { .frame = ARG_NONE };
	bool changed;
	int i, p, r, err = -ENOMEM;

	at_in = kvmalloc_objs(*at_in, len, GFP_KERNEL_ACCOUNT);
	if (!at_in)
		goto err_free;

	at_stack_in = kvmalloc_objs(*at_stack_in, (size_t)len * nslots, GFP_KERNEL_ACCOUNT);
	if (!at_stack_in)
		goto err_free;

	at_stack_out = kvmalloc_objs(*at_stack_out, nslots, GFP_KERNEL_ACCOUNT);
	if (!at_stack_out)
		goto err_free;

	for (i = 0; i < len; i++) {
		for (r = 0; r < MAX_AT_TRACK_REGS; r++)
			at_in[i][r] = unvisited;
		for (r = 0; r < nslots; r++)
			at_stack_in[(size_t)i * nslots + r] = unvisited;
	}

	for (r = 0; r < MAX_AT_TRACK_REGS; r++)
		at_in[0][r] = none;

	/* Entry: R10 is always precisely the current frame's FP */
	at_in[0][BPF_REG_FP] = arg_single(depth, 0);

	/* R1-R5: from caller or ARG_NONE for main */
	if (callee_entry) {
		for (r = BPF_REG_1; r <= BPF_REG_5; r++)
			at_in[0][r] = callee_entry[r];
	}

	/* Entry: all stack slots are ARG_NONE */
	for (r = 0; r < nslots; r++)
		at_stack_in[r] = none;

	/* Entry: incoming stack args from caller, or ARG_NONE for main */
	for (r = 0; r < MAX_STACK_ARG_SLOTS; r++)
		at_stack_arg_entry[r] = callee_entry ? callee_entry[MAX_BPF_REG + r] : none;

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
		memcpy(at_stack_out, &at_stack_in[(size_t)i * nslots], nslots * sizeof(*at_stack_out));

		arg_track_xfer(env, insn, idx, at_out, at_stack_out, nslots,
			       at_stack_arg_entry, instance, callsites);
		arg_track_log(env, insn, idx, at_in[i], &at_stack_in[(size_t)i * nslots], at_out,
			      at_stack_out, nslots);

		/* Propagate to successors within this subprogram */
		succ = bpf_insn_successors(env, idx);
		for (int s = 0; s < succ->cnt; s++) {
			int target = succ->items[s];
			int ti;

			/* Filter: stay within the subprogram's range */
			if (target < start || target >= end)
				continue;
			ti = target - start;

			for (r = 0; r < MAX_AT_TRACK_REGS; r++)
				changed |= arg_track_join(env, idx, target, r,
							  &at_in[ti][r], at_out[r]);

			for (r = 0; r < nslots; r++)
				changed |= arg_track_join(env, idx, target, -r - 1,
							  &at_stack_in[(size_t)ti * nslots + r],
							  at_stack_out[r]);
		}
	}
	if (changed)
		goto redo;

	/* Record memory accesses using converged at_in (RPO skips dead code) */
	for (p = po_end - 1; p >= po_start; p--) {
		int idx = env->cfg.insn_postorder[p];
		int i = idx - start;
		struct bpf_insn *insn = &insns[idx];

		err = record_load_store_access(env, instance, at_in[i], idx);
		if (err)
			goto err_free;

		if (BPF_CLASS(insn->code) == BPF_JMP && BPF_OP(insn->code) == BPF_CALL) {
			err = record_call_access(env, instance, at_in[i], idx);
			if (err)
				goto err_free;
		}

		if (bpf_pseudo_call(insn) || bpf_calls_callback(env, idx)) {
			struct spill_snapshot *snap;

			kvfree(env->callsite_at_stack[idx]);
			env->callsite_at_stack[idx] = kvmalloc_flex(*snap, at, nslots, GFP_KERNEL_ACCOUNT);
			snap = env->callsite_at_stack[idx];
			if (!snap) {
				err = -ENOMEM;
				goto err_free;
			}
			snap->slots = nslots;
			memcpy(snap->at, &at_stack_in[(size_t)i * nslots], nslots * sizeof(*snap->at));
		}

		record_stack_ptrs(env, idx, at_in[i]);
	}

	info->at_in = at_in;
	at_in = NULL;
	info->len = len;
	print_subprog_arg_access(env, subprog, info, at_stack_in, nslots);
	err = 0;

err_free:
	kvfree(at_stack_out);
	kvfree(at_stack_in);
	kvfree(at_in);
	return err;
}

/* Return true if any of R1-R5 or stack args is derived from a frame pointer. */
static bool has_fp_args(struct arg_track *args)
{
	for (int r = BPF_REG_1; r <= BPF_REG_5; r++)
		if (arg_is_fp(&args[r]))
			return true;
	for (int r = 0; r < MAX_STACK_ARG_SLOTS; r++)
		if (arg_is_fp(&args[MAX_BPF_REG + r]))
			return true;
	return false;
}

/*
 * Merge a freshly analyzed instance into the original.
 * may_read: union (any pass might read the slot).
 * may_write: union (slots written on ANY pass).
 * must_write: intersection (only slots written on ALL passes are guaranteed).
 * live_before is recomputed by a subsequent update_instance() on @dst.
 *
 * The two instances may have settled on different mask widths for the same
 * frame, so @dst is widened to cover @src first. A word only @dst has counts
 * as zero on the @src side: it unions into may_read as a no-op and intersects
 * must_write to empty.
 */
static int merge_instances(struct func_instance *dst, struct func_instance *src)
{
	struct frame_masks *d, *s;
	u32 f, i, w;

	for (f = 0; f <= dst->depth; f++) {
		s = src->frames[f];
		d = dst->frames[f];
		if (!s) {
			/* This pass didn't touch frame f — must_write intersects with empty. */
			if (d)
				for (i = 0; i < dst->insn_cnt; i++)
					bitmap_zero(rel_mask(d, i, FM_MUST_WRITE),
						    frame_mask_bits(d));
			continue;
		}
		if (!d) {
			/* Previous pass didn't touch frame f — take src, zero must_write. */
			dst->frames[f] = s;
			src->frames[f] = NULL;
			for (i = 0; i < dst->insn_cnt; i++)
				bitmap_zero(rel_mask(s, i, FM_MUST_WRITE), frame_mask_bits(s));
			continue;
		}
		d = widen_frame_masks(dst, f, s->words);
		if (!d)
			return -ENOMEM;
		for (i = 0; i < dst->insn_cnt; i++) {
			unsigned long *dst_read = rel_mask(d, i, FM_MAY_READ);
			unsigned long *dst_write = rel_mask(d, i, FM_MUST_WRITE);
			unsigned long *dst_may_write = rel_mask(d, i, FM_MAY_WRITE);
			unsigned long *src_read = rel_mask(s, i, FM_MAY_READ);
			unsigned long *src_write = rel_mask(s, i, FM_MUST_WRITE);
			unsigned long *src_may_write = rel_mask(s, i, FM_MAY_WRITE);

			for (w = 0; w < d->words; w++) {
				dst_read[w] |= w < s->words ? src_read[w] : 0;
				dst_write[w] &= w < s->words ? src_write[w] : 0;
				dst_may_write[w] |= w < s->words ? src_may_write[w] : 0;
			}
		}
	}
	return 0;
}

/*
 * Fold a fully analyzed callee instance writes to upper frames as
 * may_write marks at callsite in caller's frames.
 */
static int merge_may_write(struct func_instance *caller, struct func_instance *callee)
{
	DECLARE_BITMAP(acc, FRAME_HALF_SPIS);
	u32 call_idx = callee->callsite;
	struct frame_masks *fm;
	u32 f, i, nbits;
	int err;

	for (f = 0; f < callee->depth; f++) {
		fm = callee->frames[f];
		if (!fm)
			continue;
		nbits = frame_mask_bits(fm);
		bitmap_zero(acc, nbits);
		for (i = 0; i < callee->insn_cnt; i++)
			bitmap_or(acc, acc, rel_mask(fm, i, FM_MAY_WRITE), nbits);
		if (bitmap_empty(acc, nbits))
			continue;
		err = mark_stack_mask(caller, f, call_idx, FM_MAY_WRITE, acc, fm->words);
		if (err)
			return err;
	}
	return 0;
}

static struct func_instance *fresh_instance(struct func_instance *src)
{
	struct func_instance *f;

	f = kvzalloc_obj(*f, GFP_KERNEL_ACCOUNT);
	if (!f)
		return ERR_PTR(-ENOMEM);
	f->callsite = src->callsite;
	f->depth = src->depth;
	f->subprog = src->subprog;
	f->subprog_start = src->subprog_start;
	f->insn_cnt = src->insn_cnt;
	return f;
}

static void free_instance(struct func_instance *instance)
{
	int i;

	for (i = 0; i <= instance->depth; i++)
		kvfree(instance->frames[i]);
	kvfree(instance);
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
	int depth = instance->depth;
	struct bpf_insn *insns = env->prog->insnsi;
	int start = env->subprog_info[subprog].start;
	int po_start = env->subprog_info[subprog].postorder_start;
	int po_end = env->subprog_info[subprog + 1].postorder_start;
	struct func_instance *prev_instance = NULL;
	int j, err;

	if (++env->liveness->subprog_calls > 10000) {
		verbose(env, "liveness analysis exceeded complexity limit (%d calls)\n",
			env->liveness->subprog_calls);
		return -E2BIG;
	}

	if (need_resched())
		cond_resched();

	/*
	 * When an instance is reused (must_write_initialized == true),
	 * record into a fresh instance and merge afterward.  This avoids
	 * stale must_write marks for instructions not reached in this pass.
	 */
	if (instance->must_write_initialized) {
		struct func_instance *fresh = fresh_instance(instance);

		if (IS_ERR(fresh))
			return PTR_ERR(fresh);
		prev_instance = instance;
		instance = fresh;
	}

	/* Free prior analysis if this subprog was already visited */
	kvfree(info[subprog].at_in);
	info[subprog].at_in = NULL;

	err = compute_subprog_args(env, &info[subprog], entry_args, instance, callsites);
	if (err)
		goto out_free;

	/* For each reachable call site in the subprog, recurse into callees */
	for (int p = po_start; p < po_end; p++) {
		int idx = env->cfg.insn_postorder[p];
		struct arg_track callee_args[MAX_AT_TRACK_REGS] = {};
		struct arg_track none = { .frame = ARG_NONE };
		struct bpf_insn *insn = &insns[idx];
		struct func_instance *callee_instance;
		int callee, target;
		int caller_reg, cb_callee_reg;

		j = idx - start; /* relative index within this subprog */

		if (bpf_pseudo_call(insn)) {
			target = idx + insn->imm + 1;
			callee = bpf_find_subprog(env, target);
			/*
			 * Global subprograms can be freplaced, so conservatively derive
			 * their stack effects from their BTF signatures instead of
			 * analyzing them in the context of their callers.
			 */
			if (callee < 0 || bpf_subprog_is_global(env, callee))
				continue;

			/* Build entry args: R1-R5 and stack args from at_in at call site */
			for (int r = BPF_REG_1; r <= BPF_REG_5; r++)
				callee_args[r] = info[subprog].at_in[j][r];
			for (int r = 0; r < MAX_STACK_ARG_SLOTS; r++)
				callee_args[MAX_BPF_REG + r] = info[subprog].at_in[j][MAX_BPF_REG + r];
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
					err = mark_stack_read_all(env, instance, f, idx);
					if (err)
						goto out_free;
					err = mark_stack_may_write_all(env, instance, f, idx);
					if (err)
						goto out_free;
				}
				continue;
			}
			if (callee < 0)
				continue;

			for (int r = BPF_REG_1; r <= BPF_REG_5; r++)
				callee_args[r] = none;
			for (int r = 0; r < MAX_STACK_ARG_SLOTS; r++)
				callee_args[MAX_BPF_REG + r] = none;
			callee_args[cb_callee_reg] = info[subprog].at_in[j][caller_reg];
		} else {
			continue;
		}

		if (!has_fp_args(callee_args))
			continue;

		if (depth == MAX_CALL_FRAMES - 1) {
			err = -EINVAL;
			goto out_free;
		}

		callee_instance = call_instance(env, instance, idx, callee);
		if (IS_ERR(callee_instance)) {
			err = PTR_ERR(callee_instance);
			goto out_free;
		}
		callsites[depth] = idx;
		err = analyze_subprog(env, callee_args, info, callee_instance, callsites);
		if (err)
			goto out_free;

		/* Pull callee's entry liveness back to caller's callsite */
		{
			u32 callee_start = callee_instance->subprog_start;
			struct frame_masks *callee_fm;

			for (int f = 0; f < callee_instance->depth; f++) {
				callee_fm = callee_instance->frames[f];
				if (!callee_fm)
					continue;
				err = mark_stack_read_mask(instance, f, idx,
							   rel_mask(callee_fm,
								    relative_idx(callee_instance,
										 callee_start),
								    FM_LIVE_BEFORE),
							   callee_fm->words);
				if (err)
					goto out_free;
			}
		}

		/* Summarize callee's writes to ancestor frames onto the callsite */
		err = merge_may_write(instance, callee_instance);
		if (err)
			goto out_free;
	}

	if (prev_instance) {
		err = merge_instances(prev_instance, instance);
		free_instance(instance);
		instance = prev_instance;
		if (err)
			return err;
	}
	update_instance(env, instance);
	return 0;

out_free:
	if (prev_instance)
		free_instance(instance);
	return err;
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

	/*
	 * Analyze every subprog in reverse topological order (callers
	 * before callees) so that each subprog is analyzed before its
	 * callees, allowing the recursive walk inside analyze_subprog()
	 * to naturally reach callees that receive FP-derived args.
	 *
	 * Subprogs and callbacks that don't receive FP-derived arguments
	 * cannot access ancestor stack frames are analyzed independently.
	 * Async callbacks (timer, workqueue) are handled the same way.
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

	compute_may_write_masks(env);

	if (env->log.level & BPF_LOG_LEVEL2)
		err = print_instances(env);

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

/* Each field is a register bitmask */
struct insn_live_regs {
	u32 use;	/* registers read by instruction */
	u32 def;	/* registers written by instruction */
	u32 in;		/* registers that may be alive before instruction */
	u32 out;	/* registers that may be alive after instruction */
};

/* Bitmask with 1s for all caller saved registers */
#define ALL_CALLER_SAVED_REGS ((1u << CALLER_SAVED_REGS) - 1)

static inline u32 reg32_mask(u32 n) { return BIT(n); }
static inline u32 reg64_mask(u32 n) { return BIT(n) | BIT(n + 16); }
static inline u32 mask_widen(u32 m) { return m | (m << 16); }
static inline u16 mask_lo(u32 m) { return (u16)m; }
static inline u16 mask_hi(u32 m) { return (u16)(m >> 16); }

/* Index of the subprogram called by pseudo call @insn */
static int callee_subprog(struct bpf_verifier_env *env, const struct bpf_insn *insn)
{
	return bpf_find_subprog(env, insn - env->prog->insnsi + insn->imm + 1);
}

/* Compute info->{use,def} fields for the instruction */
static void compute_insn_live_regs(struct bpf_verifier_env *env,
				   struct bpf_insn *insn,
				   struct insn_live_regs *info,
				   bool ret_reg_pair)
{
	struct bpf_call_summary cs;
	const u8 class = BPF_CLASS(insn->code);
	const u8 code = BPF_OP(insn->code);
	const u8 mode = BPF_MODE(insn->code);
	const u8 size = BPF_SIZE(insn->code);
	const u32 src = reg64_mask(insn->src_reg);
	const u32 dst = reg64_mask(insn->dst_reg);
	const u32 src32 = mask_lo(src);
	const u32 dst32 = mask_lo(dst);
	const u32 r0  = reg64_mask(0);
	const u32 r2  = reg64_mask(BPF_REG_2);
	int subprog, reg_args;
	u32 def = 0;
	u32 use = U32_MAX;

	switch (class) {
	case BPF_LD:
		switch (mode) {
		case BPF_IMM:
			if (BPF_SIZE(insn->code) == BPF_DW) {
				def = dst;
				use = 0;
			}
			break;
		case BPF_ABS:
		case BPF_IND:
			/* stick with defaults */
			break;
		}
		break;
	case BPF_LDX:
		switch (mode) {
		case BPF_MEM:
			/* a narrow load still redefines the whole register */
			def = dst;
			use = src;
			break;
		case BPF_MEMSX:
			/*
			 * sign extension defines the whole register;
			 * src holds a pointer, hence is used as 64-bit.
			 */
			def = dst;
			use = src;
			break;
		}
		break;
	case BPF_ST:
		switch (mode) {
		case BPF_MEM:
			def = 0;
			use = dst;
			break;
		}
		break;
	case BPF_STX:
		switch (mode) {
		case BPF_MEM:
			def = 0;
			use = dst | (size == BPF_DW ? src : src32);
			break;
		case BPF_ATOMIC: {
			/*
			 * dst holds a pointer and is always used as 64-bit;
			 * the value operand and r0 are read as 32-bit for BPF_W atomics.
			 */
			u32 srcv = size == BPF_DW ? src : src32;
			u32 r0v  = size == BPF_DW ? r0 : mask_lo(r0);

			switch (insn->imm) {
			case BPF_CMPXCHG:
				use = r0v | dst | srcv;
				def = r0;
				break;
			case BPF_LOAD_ACQ:
				def = dst;
				use = src;
				break;
			case BPF_STORE_REL:
				def = 0;
				use = dst | srcv;
				break;
			default:
				use = dst | srcv;
				if (insn->imm & BPF_FETCH)
					def = src;
				else
					def = 0;
			}
			break;
		}
		}
		break;
	case BPF_ALU:
	case BPF_ALU64:
		switch (code) {
		case BPF_END:
			use = dst;
			def = dst;
			break;
		case BPF_MOV:
			def = dst;
			if (BPF_SRC(insn->code) == BPF_K)
				use = 0;
			else
				use = class == BPF_ALU64 ? src : src32;
			break;
		default:
			def = dst;
			if (BPF_SRC(insn->code) == BPF_K)
				use = class == BPF_ALU64 ? dst : dst32;
			else
				use = class == BPF_ALU64 ? (dst | src) : (dst32 | src32);
		}
		break;
	case BPF_JMP:
	case BPF_JMP32:
		switch (code) {
		case BPF_JA:
			def = 0;
			if (BPF_SRC(insn->code) == BPF_X)
				use = dst;
			else
				use = 0;
			break;
		case BPF_JCOND:
			def = 0;
			use = 0;
			break;
		case BPF_EXIT:
			def = 0;
			use = ret_reg_pair ? (r0 | r2) : r0;
			break;
		case BPF_CALL:
			def = ALL_CALLER_SAVED_REGS;
			use = def & ~BIT(BPF_REG_0);
			if (bpf_get_call_summary(env, insn, &cs)) {
				use = GENMASK(min_t(u8, cs.arg_slot_cnt, MAX_BPF_FUNC_REG_ARGS), 1);
			} else if (bpf_pseudo_call(insn)) {
				/* a static callee's reads are added in bpf_compute_live_registers() */
				subprog = callee_subprog(env, insn);
				if (bpf_subprog_is_global(env, subprog)) {
					reg_args = min_t(u8, env->subprog_info[subprog].arg_slot_cnt, MAX_BPF_FUNC_REG_ARGS);
					use = GENMASK(reg_args, 1);
				} else {
					use = 0;
				}
			}
			def = mask_widen(def);
			use = mask_widen(use);
			/* callx reads the address of the callee from dst_reg */
			if (bpf_is_callx(insn))
				use |= dst;
			break;
		default:
			def = 0;
			use = class == BPF_JMP ? dst : dst32;
			if (BPF_SRC(insn->code) == BPF_X)
				use |= class == BPF_JMP ? src : src32;
		}
		break;
	}

	info->def = def;
	info->use = use;
}

/* Compute may-live registers after each instruction in the program.
 * The register is live after the instruction I if it is read by some
 * instruction S following I during program execution and is not
 * overwritten between I and S.
 *
 * Store result in env->insn_aux_data[i].live_regs.
 */
int bpf_compute_live_registers(struct bpf_verifier_env *env)
{
	struct bpf_insn_aux_data *insn_aux = env->insn_aux_data;
	struct bpf_insn *insns = env->prog->insnsi;
	struct insn_live_regs *state;
	int insn_cnt = env->prog->len;
	int err = 0, i, subprog, start, end;
	bool changed, ret_reg_pair;

	/* Use the following algorithm:
	 * - define the following:
	 *   - I.use : a set of all registers read by instruction I;
	 *   - I.def : a set of all registers written by instruction I;
	 *   - I.in  : a set of all registers that may be alive before I execution;
	 *   - I.out : a set of all registers that may be alive after I execution;
	 *   - insn_successors(I): a set of instructions S that might immediately
	 *                         follow I for some program execution;
	 * - associate separate empty sets 'I.in' and 'I.out' with each instruction;
	 * - visit each instruction in a postorder and update
	 *   state[i].in, state[i].out as follows:
	 *
	 *       state[i].out = U [state[s].in for S in insn_successors(i)]
	 *       state[i].in  = (state[i].out / state[i].def) U state[i].use
	 *
	 *   (where U stands for set union, / stands for set difference)
	 * - repeat the computation while {in,out} fields changes for
	 *   any instruction.
	 */
	state = kvzalloc_objs(*state, insn_cnt, GFP_KERNEL_ACCOUNT);
	if (!state) {
		err = -ENOMEM;
		goto out;
	}

	for (subprog = 0; subprog < env->subprog_cnt; subprog++) {
		start = env->subprog_info[subprog].start;
		end = env->subprog_info[subprog + 1].start;
		ret_reg_pair = bpf_ret_reg_pair(env, subprog);

		for (i = start; i < end; ++i)
			compute_insn_live_regs(env, &insns[i], &state[i], ret_reg_pair);
	}

	/* Forward pass: resolve stack access through FP-derived pointers */
	err = bpf_compute_subprog_arg_access(env);
	if (err)
		goto out;

	changed = true;
	while (changed) {
		changed = false;
		for (i = 0; i < env->cfg.cur_postorder; ++i) {
			int insn_idx = env->cfg.insn_postorder[i];
			struct insn_live_regs *live = &state[insn_idx];
			struct bpf_iarray *succ;
			u32 new_out = 0;
			u32 new_in = 0;
			u32 use;

			succ = bpf_insn_successors(env, insn_idx);
			for (int s = 0; s < succ->cnt; ++s)
				new_out |= state[succ->items[s]].in;
			use = live->use;
			/*
			 * For a call to a static subprogram include the callee's
			 * entry 'in' as computed so far.
			 */
			if (bpf_pseudo_call(&insns[insn_idx]) &&
			    !bpf_subprog_is_global(env, callee_subprog(env, &insns[insn_idx])))
				use |= state[insn_idx + insns[insn_idx].imm + 1].in &
				       mask_widen(GENMASK(BPF_REG_5, BPF_REG_1));
			new_in = (new_out & ~live->def) | use;
			if (new_out != live->out || new_in != live->in) {
				live->in = new_in;
				live->out = new_out;
				changed = true;
			}
		}
	}

	for (i = 0; i < insn_cnt; ++i) {
		int def32 = bpf_insn_def32(env->prog, &insns[i]);
		u32 out = state[i].out;
		u32 in = state[i].in;

		insn_aux[i].live_regs_before = mask_lo(in) | mask_hi(in);
		/*
		 * On architectures where 32-bit operations do not reset upper halves
		 * of the registers, the verifier needs to zero extend a destination
		 * register if an instruction defines a 32-bit subregister and the
		 * upper half of that register is alive after the instruction.
		 */
		insn_aux[i].zext_dst = def32 >= 0 && (mask_hi(out) & BIT(def32));
	}

out:
	kvfree(state);
	return err;
}
