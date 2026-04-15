// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/filter.h>
#include <linux/bitmap.h>

#define verbose(env, fmt, args...) bpf_verifier_log_write(env, fmt, ##args)

/* for any branch, call, exit record the history of jmps in the given state */
int bpf_push_jmp_history(struct bpf_verifier_env *env, struct bpf_verifier_state *cur,
			 int insn_flags, u64 linked_regs)
{
	u32 cnt = cur->jmp_history_cnt;
	struct bpf_jmp_history_entry *p;
	size_t alloc_size;

	/* combine instruction flags if we already recorded this instruction */
	if (env->cur_hist_ent) {
		/* atomic instructions push insn_flags twice, for READ and
		 * WRITE sides, but they should agree on stack slot
		 */
		verifier_bug_if((env->cur_hist_ent->flags & insn_flags) &&
				(env->cur_hist_ent->flags & insn_flags) != insn_flags,
				env, "insn history: insn_idx %d cur flags %x new flags %x",
				env->insn_idx, env->cur_hist_ent->flags, insn_flags);
		env->cur_hist_ent->flags |= insn_flags;
		verifier_bug_if(env->cur_hist_ent->linked_regs != 0, env,
				"insn history: insn_idx %d linked_regs: %#llx",
				env->insn_idx, env->cur_hist_ent->linked_regs);
		env->cur_hist_ent->linked_regs = linked_regs;
		return 0;
	}

	cnt++;
	alloc_size = kmalloc_size_roundup(size_mul(cnt, sizeof(*p)));
	p = krealloc(cur->jmp_history, alloc_size, GFP_KERNEL_ACCOUNT);
	if (!p)
		return -ENOMEM;
	cur->jmp_history = p;

	p = &cur->jmp_history[cnt - 1];
	p->idx = env->insn_idx;
	p->prev_idx = env->prev_insn_idx;
	p->flags = insn_flags;
	p->linked_regs = linked_regs;
	cur->jmp_history_cnt = cnt;
	env->cur_hist_ent = p;

	return 0;
}

static bool is_atomic_load_insn(const struct bpf_insn *insn)
{
	return BPF_CLASS(insn->code) == BPF_STX &&
	       BPF_MODE(insn->code) == BPF_ATOMIC &&
	       insn->imm == BPF_LOAD_ACQ;
}

static bool is_atomic_fetch_insn(const struct bpf_insn *insn)
{
	return BPF_CLASS(insn->code) == BPF_STX &&
	       BPF_MODE(insn->code) == BPF_ATOMIC &&
	       (insn->imm & BPF_FETCH);
}

static int insn_stack_access_spi(int insn_flags)
{
	return (insn_flags >> INSN_F_SPI_SHIFT) & INSN_F_SPI_MASK;
}

static int insn_stack_access_frameno(int insn_flags)
{
	return insn_flags & INSN_F_FRAMENO_MASK;
}

/* Backtrack one insn at a time. If idx is not at the top of recorded
 * history then previous instruction came from straight line execution.
 * Return -ENOENT if we exhausted all instructions within given state.
 *
 * It's legal to have a bit of a looping with the same starting and ending
 * insn index within the same state, e.g.: 3->4->5->3, so just because current
 * instruction index is the same as state's first_idx doesn't mean we are
 * done. If there is still some jump history left, we should keep going. We
 * need to take into account that we might have a jump history between given
 * state's parent and itself, due to checkpointing. In this case, we'll have
 * history entry recording a jump from last instruction of parent state and
 * first instruction of given state.
 */
static int get_prev_insn_idx(struct bpf_verifier_state *st, int i,
			     u32 *history)
{
	u32 cnt = *history;

	if (i == st->first_insn_idx) {
		if (cnt == 0)
			return -ENOENT;
		if (cnt == 1 && st->jmp_history[0].idx == i)
			return -ENOENT;
	}

	if (cnt && st->jmp_history[cnt - 1].idx == i) {
		i = st->jmp_history[cnt - 1].prev_idx;
		(*history)--;
	} else {
		i--;
	}
	return i;
}

static struct bpf_jmp_history_entry *get_jmp_hist_entry(struct bpf_verifier_state *st,
						        u32 hist_end, int insn_idx)
{
	if (hist_end > 0 && st->jmp_history[hist_end - 1].idx == insn_idx)
		return &st->jmp_history[hist_end - 1];
	return NULL;
}

static inline void bt_init(struct backtrack_state *bt, u32 frame)
{
	bt->frame = frame;
}

static inline void bt_reset(struct backtrack_state *bt)
{
	struct bpf_verifier_env *env = bt->env;

	memset(bt, 0, sizeof(*bt));
	bt->env = env;
}

static inline u32 bt_empty(struct backtrack_state *bt)
{
	u64 mask = 0;
	int i;

	for (i = 0; i <= bt->frame; i++)
		mask |= bt->reg_masks[i] | bt->stack_masks[i];

	return mask == 0;
}

static inline int bt_subprog_enter(struct backtrack_state *bt)
{
	if (bt->frame == MAX_CALL_FRAMES - 1) {
		verifier_bug(bt->env, "subprog enter from frame %d", bt->frame);
		return -EFAULT;
	}
	bt->frame++;
	return 0;
}

static inline int bt_subprog_exit(struct backtrack_state *bt)
{
	if (bt->frame == 0) {
		verifier_bug(bt->env, "subprog exit from frame 0");
		return -EFAULT;
	}
	bt->frame--;
	return 0;
}

static inline void bt_clear_frame_reg(struct backtrack_state *bt, u32 frame, u32 reg)
{
	bt->reg_masks[frame] &= ~(1 << reg);
}

static inline void bt_set_reg(struct backtrack_state *bt, u32 reg)
{
	bpf_bt_set_frame_reg(bt, bt->frame, reg);
}

static inline void bt_clear_reg(struct backtrack_state *bt, u32 reg)
{
	bt_clear_frame_reg(bt, bt->frame, reg);
}

static inline void bt_clear_frame_slot(struct backtrack_state *bt, u32 frame, u32 slot)
{
	bt->stack_masks[frame] &= ~(1ull << slot);
}

static inline u32 bt_frame_reg_mask(struct backtrack_state *bt, u32 frame)
{
	return bt->reg_masks[frame];
}

static inline u32 bt_reg_mask(struct backtrack_state *bt)
{
	return bt->reg_masks[bt->frame];
}

static inline u64 bt_frame_stack_mask(struct backtrack_state *bt, u32 frame)
{
	return bt->stack_masks[frame];
}

static inline u64 bt_stack_mask(struct backtrack_state *bt)
{
	return bt->stack_masks[bt->frame];
}

static inline bool bt_is_reg_set(struct backtrack_state *bt, u32 reg)
{
	return bt->reg_masks[bt->frame] & (1 << reg);
}


/* format registers bitmask, e.g., "r0,r2,r4" for 0x15 mask */
static void fmt_reg_mask(char *buf, ssize_t buf_sz, u32 reg_mask)
{
	DECLARE_BITMAP(mask, 64);
	bool first = true;
	int i, n;

	buf[0] = '\0';

	bitmap_from_u64(mask, reg_mask);
	for_each_set_bit(i, mask, 32) {
		n = snprintf(buf, buf_sz, "%sr%d", first ? "" : ",", i);
		first = false;
		buf += n;
		buf_sz -= n;
		if (buf_sz < 0)
			break;
	}
}
/* format stack slots bitmask, e.g., "-8,-24,-40" for 0x15 mask */
void bpf_fmt_stack_mask(char *buf, ssize_t buf_sz, u64 stack_mask)
{
	DECLARE_BITMAP(mask, 64);
	bool first = true;
	int i, n;

	buf[0] = '\0';

	bitmap_from_u64(mask, stack_mask);
	for_each_set_bit(i, mask, 64) {
		n = snprintf(buf, buf_sz, "%s%d", first ? "" : ",", -(i + 1) * 8);
		first = false;
		buf += n;
		buf_sz -= n;
		if (buf_sz < 0)
			break;
	}
}


/* For given verifier state backtrack_insn() is called from the last insn to
 * the first insn. Its purpose is to compute a bitmask of registers and
 * stack slots that needs precision in the parent verifier state.
 *
 * @idx is an index of the instruction we are currently processing;
 * @subseq_idx is an index of the subsequent instruction that:
 *   - *would be* executed next, if jump history is viewed in forward order;
 *   - *was* processed previously during backtracking.
 */
static int backtrack_insn(struct bpf_verifier_env *env, int idx, int subseq_idx,
			  struct bpf_jmp_history_entry *hist, struct backtrack_state *bt)
{
	struct bpf_insn *insn = env->prog->insnsi + idx;
	u8 class = BPF_CLASS(insn->code);
	u8 opcode = BPF_OP(insn->code);
	u8 mode = BPF_MODE(insn->code);
	u32 dreg = insn->dst_reg;
	u32 sreg = insn->src_reg;
	u32 spi, i, fr;

	if (insn->code == 0)
		return 0;
	if (env->log.level & BPF_LOG_LEVEL2) {
		fmt_reg_mask(env->tmp_str_buf, TMP_STR_BUF_LEN, bt_reg_mask(bt));
		verbose(env, "mark_precise: frame%d: regs=%s ",
			bt->frame, env->tmp_str_buf);
		bpf_fmt_stack_mask(env->tmp_str_buf, TMP_STR_BUF_LEN, bt_stack_mask(bt));
		verbose(env, "stack=%s before ", env->tmp_str_buf);
		verbose(env, "%d: ", idx);
		bpf_verbose_insn(env, insn);
	}

	/* If there is a history record that some registers gained range at this insn,
	 * propagate precision marks to those registers, so that bt_is_reg_set()
	 * accounts for these registers.
	 */
	bpf_bt_sync_linked_regs(bt, hist);

	if (class == BPF_ALU || class == BPF_ALU64) {
		if (!bt_is_reg_set(bt, dreg))
			return 0;
		if (opcode == BPF_END || opcode == BPF_NEG) {
			/* sreg is reserved and unused
			 * dreg still need precision before this insn
			 */
			return 0;
		} else if (opcode == BPF_MOV) {
			if (BPF_SRC(insn->code) == BPF_X) {
				/* dreg = sreg or dreg = (s8, s16, s32)sreg
				 * dreg needs precision after this insn
				 * sreg needs precision before this insn
				 */
				bt_clear_reg(bt, dreg);
				if (sreg != BPF_REG_FP)
					bt_set_reg(bt, sreg);
			} else {
				/* dreg = K
				 * dreg needs precision after this insn.
				 * Corresponding register is already marked
				 * as precise=true in this verifier state.
				 * No further markings in parent are necessary
				 */
				bt_clear_reg(bt, dreg);
			}
		} else {
			if (BPF_SRC(insn->code) == BPF_X) {
				/* dreg += sreg
				 * both dreg and sreg need precision
				 * before this insn
				 */
				if (sreg != BPF_REG_FP)
					bt_set_reg(bt, sreg);
			} /* else dreg += K
			   * dreg still needs precision before this insn
			   */
		}
	} else if (class == BPF_LDX ||
		   is_atomic_load_insn(insn) ||
		   is_atomic_fetch_insn(insn)) {
		u32 load_reg = dreg;

		/*
		 * Atomic fetch operation writes the old value into
		 * a register (sreg or r0) and if it was tracked for
		 * precision, propagate to the stack slot like we do
		 * in regular ldx.
		 */
		if (is_atomic_fetch_insn(insn))
			load_reg = insn->imm == BPF_CMPXCHG ?
				   BPF_REG_0 : sreg;

		if (!bt_is_reg_set(bt, load_reg))
			return 0;
		bt_clear_reg(bt, load_reg);

		/* scalars can only be spilled into stack w/o losing precision.
		 * Load from any other memory can be zero extended.
		 * The desire to keep that precision is already indicated
		 * by 'precise' mark in corresponding register of this state.
		 * No further tracking necessary.
		 */
		if (!hist || !(hist->flags & INSN_F_STACK_ACCESS))
			return 0;
		/* dreg = *(u64 *)[fp - off] was a fill from the stack.
		 * that [fp - off] slot contains scalar that needs to be
		 * tracked with precision
		 */
		spi = insn_stack_access_spi(hist->flags);
		fr = insn_stack_access_frameno(hist->flags);
		bpf_bt_set_frame_slot(bt, fr, spi);
	} else if (class == BPF_STX || class == BPF_ST) {
		if (bt_is_reg_set(bt, dreg))
			/* stx & st shouldn't be using _scalar_ dst_reg
			 * to access memory. It means backtracking
			 * encountered a case of pointer subtraction.
			 */
			return -ENOTSUPP;
		/* scalars can only be spilled into stack */
		if (!hist || !(hist->flags & INSN_F_STACK_ACCESS))
			return 0;
		spi = insn_stack_access_spi(hist->flags);
		fr = insn_stack_access_frameno(hist->flags);
		if (!bt_is_frame_slot_set(bt, fr, spi))
			return 0;
		bt_clear_frame_slot(bt, fr, spi);
		if (class == BPF_STX)
			bt_set_reg(bt, sreg);
	} else if (class == BPF_JMP || class == BPF_JMP32) {
		if (bpf_pseudo_call(insn)) {
			int subprog_insn_idx, subprog;

			subprog_insn_idx = idx + insn->imm + 1;
			subprog = bpf_find_subprog(env, subprog_insn_idx);
			if (subprog < 0)
				return -EFAULT;

			if (bpf_subprog_is_global(env, subprog)) {
				/* check that jump history doesn't have any
				 * extra instructions from subprog; the next
				 * instruction after call to global subprog
				 * should be literally next instruction in
				 * caller program
				 */
				verifier_bug_if(idx + 1 != subseq_idx, env,
						"extra insn from subprog");
				/* r1-r5 are invalidated after subprog call,
				 * so for global func call it shouldn't be set
				 * anymore
				 */
				if (bt_reg_mask(bt) & BPF_REGMASK_ARGS) {
					verifier_bug(env, "global subprog unexpected regs %x",
						     bt_reg_mask(bt));
					return -EFAULT;
				}
				/* global subprog always sets R0 */
				bt_clear_reg(bt, BPF_REG_0);
				return 0;
			} else {
				/* static subprog call instruction, which
				 * means that we are exiting current subprog,
				 * so only r1-r5 could be still requested as
				 * precise, r0 and r6-r10 or any stack slot in
				 * the current frame should be zero by now
				 */
				if (bt_reg_mask(bt) & ~BPF_REGMASK_ARGS) {
					verifier_bug(env, "static subprog unexpected regs %x",
						     bt_reg_mask(bt));
					return -EFAULT;
				}
				/* we are now tracking register spills correctly,
				 * so any instance of leftover slots is a bug
				 */
				if (bt_stack_mask(bt) != 0) {
					verifier_bug(env,
						     "static subprog leftover stack slots %llx",
						     bt_stack_mask(bt));
					return -EFAULT;
				}
				/* propagate r1-r5 to the caller */
				for (i = BPF_REG_1; i <= BPF_REG_5; i++) {
					if (bt_is_reg_set(bt, i)) {
						bt_clear_reg(bt, i);
						bpf_bt_set_frame_reg(bt, bt->frame - 1, i);
					}
				}
				if (bt_subprog_exit(bt))
					return -EFAULT;
				return 0;
			}
		} else if (bpf_is_sync_callback_calling_insn(insn) && idx != subseq_idx - 1) {
			/* exit from callback subprog to callback-calling helper or
			 * kfunc call. Use idx/subseq_idx check to discern it from
			 * straight line code backtracking.
			 * Unlike the subprog call handling above, we shouldn't
			 * propagate precision of r1-r5 (if any requested), as they are
			 * not actually arguments passed directly to callback subprogs
			 */
			if (bt_reg_mask(bt) & ~BPF_REGMASK_ARGS) {
				verifier_bug(env, "callback unexpected regs %x",
					     bt_reg_mask(bt));
				return -EFAULT;
			}
			if (bt_stack_mask(bt) != 0) {
				verifier_bug(env, "callback leftover stack slots %llx",
					     bt_stack_mask(bt));
				return -EFAULT;
			}
			/* clear r1-r5 in callback subprog's mask */
			for (i = BPF_REG_1; i <= BPF_REG_5; i++)
				bt_clear_reg(bt, i);
			if (bt_subprog_exit(bt))
				return -EFAULT;
			return 0;
		} else if (opcode == BPF_CALL) {
			/* kfunc with imm==0 is invalid and fixup_kfunc_call will
			 * catch this error later. Make backtracking conservative
			 * with ENOTSUPP.
			 */
			if (insn->src_reg == BPF_PSEUDO_KFUNC_CALL && insn->imm == 0)
				return -ENOTSUPP;
			/* regular helper call sets R0 */
			bt_clear_reg(bt, BPF_REG_0);
			if (bt_reg_mask(bt) & BPF_REGMASK_ARGS) {
				/* if backtracking was looking for registers R1-R5
				 * they should have been found already.
				 */
				verifier_bug(env, "backtracking call unexpected regs %x",
					     bt_reg_mask(bt));
				return -EFAULT;
			}
			if (insn->src_reg == BPF_REG_0 && insn->imm == BPF_FUNC_tail_call
			    && subseq_idx - idx != 1) {
				if (bt_subprog_enter(bt))
					return -EFAULT;
			}
		} else if (opcode == BPF_EXIT) {
			bool r0_precise;

			/* Backtracking to a nested function call, 'idx' is a part of
			 * the inner frame 'subseq_idx' is a part of the outer frame.
			 * In case of a regular function call, instructions giving
			 * precision to registers R1-R5 should have been found already.
			 * In case of a callback, it is ok to have R1-R5 marked for
			 * backtracking, as these registers are set by the function
			 * invoking callback.
			 */
			if (subseq_idx >= 0 && bpf_calls_callback(env, subseq_idx))
				for (i = BPF_REG_1; i <= BPF_REG_5; i++)
					bt_clear_reg(bt, i);
			if (bt_reg_mask(bt) & BPF_REGMASK_ARGS) {
				verifier_bug(env, "backtracking exit unexpected regs %x",
					     bt_reg_mask(bt));
				return -EFAULT;
			}

			/* BPF_EXIT in subprog or callback always returns
			 * right after the call instruction, so by checking
			 * whether the instruction at subseq_idx-1 is subprog
			 * call or not we can distinguish actual exit from
			 * *subprog* from exit from *callback*. In the former
			 * case, we need to propagate r0 precision, if
			 * necessary. In the former we never do that.
			 */
			r0_precise = subseq_idx - 1 >= 0 &&
				     bpf_pseudo_call(&env->prog->insnsi[subseq_idx - 1]) &&
				     bt_is_reg_set(bt, BPF_REG_0);

			bt_clear_reg(bt, BPF_REG_0);
			if (bt_subprog_enter(bt))
				return -EFAULT;

			if (r0_precise)
				bt_set_reg(bt, BPF_REG_0);
			/* r6-r9 and stack slots will stay set in caller frame
			 * bitmasks until we return back from callee(s)
			 */
			return 0;
		} else if (BPF_SRC(insn->code) == BPF_X) {
			if (!bt_is_reg_set(bt, dreg) && !bt_is_reg_set(bt, sreg))
				return 0;
			/* dreg <cond> sreg
			 * Both dreg and sreg need precision before
			 * this insn. If only sreg was marked precise
			 * before it would be equally necessary to
			 * propagate it to dreg.
			 */
			if (!hist || !(hist->flags & INSN_F_SRC_REG_STACK))
				bt_set_reg(bt, sreg);
			if (!hist || !(hist->flags & INSN_F_DST_REG_STACK))
				bt_set_reg(bt, dreg);
		} else if (BPF_SRC(insn->code) == BPF_K) {
			 /* dreg <cond> K
			  * Only dreg still needs precision before
			  * this insn, so for the K-based conditional
			  * there is nothing new to be marked.
			  */
		}
	} else if (class == BPF_LD) {
		if (!bt_is_reg_set(bt, dreg))
			return 0;
		bt_clear_reg(bt, dreg);
		/* It's ld_imm64 or ld_abs or ld_ind.
		 * For ld_imm64 no further tracking of precision
		 * into parent is necessary
		 */
		if (mode == BPF_IND || mode == BPF_ABS)
			/* to be analyzed */
			return -ENOTSUPP;
	}
	/* Propagate precision marks to linked registers, to account for
	 * registers marked as precise in this function.
	 */
	bpf_bt_sync_linked_regs(bt, hist);
	return 0;
}

/* the scalar precision tracking algorithm:
 * . at the start all registers have precise=false.
 * . scalar ranges are tracked as normal through alu and jmp insns.
 * . once precise value of the scalar register is used in:
 *   .  ptr + scalar alu
 *   . if (scalar cond K|scalar)
 *   .  helper_call(.., scalar, ...) where ARG_CONST is expected
 *   backtrack through the verifier states and mark all registers and
 *   stack slots with spilled constants that these scalar registers
 *   should be precise.
 * . during state pruning two registers (or spilled stack slots)
 *   are equivalent if both are not precise.
 *
 * Note the verifier cannot simply walk register parentage chain,
 * since many different registers and stack slots could have been
 * used to compute single precise scalar.
 *
 * The approach of starting with precise=true for all registers and then
 * backtrack to mark a register as not precise when the verifier detects
 * that program doesn't care about specific value (e.g., when helper
 * takes register as ARG_ANYTHING parameter) is not safe.
 *
 * It's ok to walk single parentage chain of the verifier states.
 * It's possible that this backtracking will go all the way till 1st insn.
 * All other branches will be explored for needing precision later.
 *
 * The backtracking needs to deal with cases like:
 *   R8=map_value(id=0,off=0,ks=4,vs=1952,imm=0) R9_w=map_value(id=0,off=40,ks=4,vs=1952,imm=0)
 * r9 -= r8
 * r5 = r9
 * if r5 > 0x79f goto pc+7
 *    R5_w=inv(id=0,umax_value=1951,var_off=(0x0; 0x7ff))
 * r5 += 1
 * ...
 * call bpf_perf_event_output#25
 *   where .arg5_type = ARG_CONST_SIZE_OR_ZERO
 *
 * and this case:
 * r6 = 1
 * call foo // uses callee's r6 inside to compute r0
 * r0 += r6
 * if r0 == 0 goto
 *
 * to track above reg_mask/stack_mask needs to be independent for each frame.
 *
 * Also if parent's curframe > frame where backtracking started,
 * the verifier need to mark registers in both frames, otherwise callees
 * may incorrectly prune callers. This is similar to
 * commit 7640ead93924 ("bpf: verifier: make sure callees don't prune with caller differences")
 *
 * For now backtracking falls back into conservative marking.
 */
void bpf_mark_all_scalars_precise(struct bpf_verifier_env *env,
				 struct bpf_verifier_state *st)
{
	struct bpf_func_state *func;
	struct bpf_reg_state *reg;
	int i, j;

	if (env->log.level & BPF_LOG_LEVEL2) {
		verbose(env, "mark_precise: frame%d: falling back to forcing all scalars precise\n",
			st->curframe);
	}

	/* big hammer: mark all scalars precise in this path.
	 * pop_stack may still get !precise scalars.
	 * We also skip current state and go straight to first parent state,
	 * because precision markings in current non-checkpointed state are
	 * not needed. See why in the comment in __mark_chain_precision below.
	 */
	for (st = st->parent; st; st = st->parent) {
		for (i = 0; i <= st->curframe; i++) {
			func = st->frame[i];
			for (j = 0; j < BPF_REG_FP; j++) {
				reg = &func->regs[j];
				if (reg->type != SCALAR_VALUE || reg->precise)
					continue;
				reg->precise = true;
				if (env->log.level & BPF_LOG_LEVEL2) {
					verbose(env, "force_precise: frame%d: forcing r%d to be precise\n",
						i, j);
				}
			}
			for (j = 0; j < func->allocated_stack / BPF_REG_SIZE; j++) {
				if (!bpf_is_spilled_reg(&func->stack[j]))
					continue;
				reg = &func->stack[j].spilled_ptr;
				if (reg->type != SCALAR_VALUE || reg->precise)
					continue;
				reg->precise = true;
				if (env->log.level & BPF_LOG_LEVEL2) {
					verbose(env, "force_precise: frame%d: forcing fp%d to be precise\n",
						i, -(j + 1) * 8);
				}
			}
		}
	}
}

/*
 * bpf_mark_chain_precision() backtracks BPF program instruction sequence and
 * chain of verifier states making sure that register *regno* (if regno >= 0)
 * and/or stack slot *spi* (if spi >= 0) are marked as precisely tracked
 * SCALARS, as well as any other registers and slots that contribute to
 * a tracked state of given registers/stack slots, depending on specific BPF
 * assembly instructions (see backtrack_insns() for exact instruction handling
 * logic). This backtracking relies on recorded jmp_history and is able to
 * traverse entire chain of parent states. This process ends only when all the
 * necessary registers/slots and their transitive dependencies are marked as
 * precise.
 *
 * One important and subtle aspect is that precise marks *do not matter* in
 * the currently verified state (current state). It is important to understand
 * why this is the case.
 *
 * First, note that current state is the state that is not yet "checkpointed",
 * i.e., it is not yet put into env->explored_states, and it has no children
 * states as well. It's ephemeral, and can end up either a) being discarded if
 * compatible explored state is found at some point or BPF_EXIT instruction is
 * reached or b) checkpointed and put into env->explored_states, branching out
 * into one or more children states.
 *
 * In the former case, precise markings in current state are completely
 * ignored by state comparison code (see regsafe() for details). Only
 * checkpointed ("old") state precise markings are important, and if old
 * state's register/slot is precise, regsafe() assumes current state's
 * register/slot as precise and checks value ranges exactly and precisely. If
 * states turn out to be compatible, current state's necessary precise
 * markings and any required parent states' precise markings are enforced
 * after the fact with propagate_precision() logic, after the fact. But it's
 * important to realize that in this case, even after marking current state
 * registers/slots as precise, we immediately discard current state. So what
 * actually matters is any of the precise markings propagated into current
 * state's parent states, which are always checkpointed (due to b) case above).
 * As such, for scenario a) it doesn't matter if current state has precise
 * markings set or not.
 *
 * Now, for the scenario b), checkpointing and forking into child(ren)
 * state(s). Note that before current state gets to checkpointing step, any
 * processed instruction always assumes precise SCALAR register/slot
 * knowledge: if precise value or range is useful to prune jump branch, BPF
 * verifier takes this opportunity enthusiastically. Similarly, when
 * register's value is used to calculate offset or memory address, exact
 * knowledge of SCALAR range is assumed, checked, and enforced. So, similar to
 * what we mentioned above about state comparison ignoring precise markings
 * during state comparison, BPF verifier ignores and also assumes precise
 * markings *at will* during instruction verification process. But as verifier
 * assumes precision, it also propagates any precision dependencies across
 * parent states, which are not yet finalized, so can be further restricted
 * based on new knowledge gained from restrictions enforced by their children
 * states. This is so that once those parent states are finalized, i.e., when
 * they have no more active children state, state comparison logic in
 * is_state_visited() would enforce strict and precise SCALAR ranges, if
 * required for correctness.
 *
 * To build a bit more intuition, note also that once a state is checkpointed,
 * the path we took to get to that state is not important. This is crucial
 * property for state pruning. When state is checkpointed and finalized at
 * some instruction index, it can be correctly and safely used to "short
 * circuit" any *compatible* state that reaches exactly the same instruction
 * index. I.e., if we jumped to that instruction from a completely different
 * code path than original finalized state was derived from, it doesn't
 * matter, current state can be discarded because from that instruction
 * forward having a compatible state will ensure we will safely reach the
 * exit. States describe preconditions for further exploration, but completely
 * forget the history of how we got here.
 *
 * This also means that even if we needed precise SCALAR range to get to
 * finalized state, but from that point forward *that same* SCALAR register is
 * never used in a precise context (i.e., it's precise value is not needed for
 * correctness), it's correct and safe to mark such register as "imprecise"
 * (i.e., precise marking set to false). This is what we rely on when we do
 * not set precise marking in current state. If no child state requires
 * precision for any given SCALAR register, it's safe to dictate that it can
 * be imprecise. If any child state does require this register to be precise,
 * we'll mark it precise later retroactively during precise markings
 * propagation from child state to parent states.
 *
 * Skipping precise marking setting in current state is a mild version of
 * relying on the above observation. But we can utilize this property even
 * more aggressively by proactively forgetting any precise marking in the
 * current state (which we inherited from the parent state), right before we
 * checkpoint it and branch off into new child state. This is done by
 * mark_all_scalars_imprecise() to hopefully get more permissive and generic
 * finalized states which help in short circuiting more future states.
 */
int bpf_mark_chain_precision(struct bpf_verifier_env *env,
			    struct bpf_verifier_state *starting_state,
			    int regno,
			    bool *changed)
{
	struct bpf_verifier_state *st = starting_state;
	struct backtrack_state *bt = &env->bt;
	int first_idx = st->first_insn_idx;
	int last_idx = starting_state->insn_idx;
	int subseq_idx = -1;
	struct bpf_func_state *func;
	bool tmp, skip_first = true;
	struct bpf_reg_state *reg;
	int i, fr, err;

	if (!env->bpf_capable)
		return 0;

	changed = changed ?: &tmp;
	/* set frame number from which we are starting to backtrack */
	bt_init(bt, starting_state->curframe);

	/* Do sanity checks against current state of register and/or stack
	 * slot, but don't set precise flag in current state, as precision
	 * tracking in the current state is unnecessary.
	 */
	func = st->frame[bt->frame];
	if (regno >= 0) {
		reg = &func->regs[regno];
		if (reg->type != SCALAR_VALUE) {
			verifier_bug(env, "backtracking misuse");
			return -EFAULT;
		}
		bt_set_reg(bt, regno);
	}

	if (bt_empty(bt))
		return 0;

	for (;;) {
		DECLARE_BITMAP(mask, 64);
		u32 history = st->jmp_history_cnt;
		struct bpf_jmp_history_entry *hist;

		if (env->log.level & BPF_LOG_LEVEL2) {
			verbose(env, "mark_precise: frame%d: last_idx %d first_idx %d subseq_idx %d \n",
				bt->frame, last_idx, first_idx, subseq_idx);
		}

		if (last_idx < 0) {
			/* we are at the entry into subprog, which
			 * is expected for global funcs, but only if
			 * requested precise registers are R1-R5
			 * (which are global func's input arguments)
			 */
			if (st->curframe == 0 &&
			    st->frame[0]->subprogno > 0 &&
			    st->frame[0]->callsite == BPF_MAIN_FUNC &&
			    bt_stack_mask(bt) == 0 &&
			    (bt_reg_mask(bt) & ~BPF_REGMASK_ARGS) == 0) {
				bitmap_from_u64(mask, bt_reg_mask(bt));
				for_each_set_bit(i, mask, 32) {
					reg = &st->frame[0]->regs[i];
					bt_clear_reg(bt, i);
					if (reg->type == SCALAR_VALUE) {
						reg->precise = true;
						*changed = true;
					}
				}
				return 0;
			}

			verifier_bug(env, "backtracking func entry subprog %d reg_mask %x stack_mask %llx",
				     st->frame[0]->subprogno, bt_reg_mask(bt), bt_stack_mask(bt));
			return -EFAULT;
		}

		for (i = last_idx;;) {
			if (skip_first) {
				err = 0;
				skip_first = false;
			} else {
				hist = get_jmp_hist_entry(st, history, i);
				err = backtrack_insn(env, i, subseq_idx, hist, bt);
			}
			if (err == -ENOTSUPP) {
				bpf_mark_all_scalars_precise(env, starting_state);
				bt_reset(bt);
				return 0;
			} else if (err) {
				return err;
			}
			if (bt_empty(bt))
				/* Found assignment(s) into tracked register in this state.
				 * Since this state is already marked, just return.
				 * Nothing to be tracked further in the parent state.
				 */
				return 0;
			subseq_idx = i;
			i = get_prev_insn_idx(st, i, &history);
			if (i == -ENOENT)
				break;
			if (i >= env->prog->len) {
				/* This can happen if backtracking reached insn 0
				 * and there are still reg_mask or stack_mask
				 * to backtrack.
				 * It means the backtracking missed the spot where
				 * particular register was initialized with a constant.
				 */
				verifier_bug(env, "backtracking idx %d", i);
				return -EFAULT;
			}
		}
		st = st->parent;
		if (!st)
			break;

		for (fr = bt->frame; fr >= 0; fr--) {
			func = st->frame[fr];
			bitmap_from_u64(mask, bt_frame_reg_mask(bt, fr));
			for_each_set_bit(i, mask, 32) {
				reg = &func->regs[i];
				if (reg->type != SCALAR_VALUE) {
					bt_clear_frame_reg(bt, fr, i);
					continue;
				}
				if (reg->precise) {
					bt_clear_frame_reg(bt, fr, i);
				} else {
					reg->precise = true;
					*changed = true;
				}
			}

			bitmap_from_u64(mask, bt_frame_stack_mask(bt, fr));
			for_each_set_bit(i, mask, 64) {
				if (verifier_bug_if(i >= func->allocated_stack / BPF_REG_SIZE,
						    env, "stack slot %d, total slots %d",
						    i, func->allocated_stack / BPF_REG_SIZE))
					return -EFAULT;

				if (!bpf_is_spilled_scalar_reg(&func->stack[i])) {
					bt_clear_frame_slot(bt, fr, i);
					continue;
				}
				reg = &func->stack[i].spilled_ptr;
				if (reg->precise) {
					bt_clear_frame_slot(bt, fr, i);
				} else {
					reg->precise = true;
					*changed = true;
				}
			}
			if (env->log.level & BPF_LOG_LEVEL2) {
				fmt_reg_mask(env->tmp_str_buf, TMP_STR_BUF_LEN,
					     bt_frame_reg_mask(bt, fr));
				verbose(env, "mark_precise: frame%d: parent state regs=%s ",
					fr, env->tmp_str_buf);
				bpf_fmt_stack_mask(env->tmp_str_buf, TMP_STR_BUF_LEN,
					       bt_frame_stack_mask(bt, fr));
				verbose(env, "stack=%s: ", env->tmp_str_buf);
				print_verifier_state(env, st, fr, true);
			}
		}

		if (bt_empty(bt))
			return 0;

		subseq_idx = first_idx;
		last_idx = st->last_insn_idx;
		first_idx = st->first_insn_idx;
	}

	/* if we still have requested precise regs or slots, we missed
	 * something (e.g., stack access through non-r10 register), so
	 * fallback to marking all precise
	 */
	if (!bt_empty(bt)) {
		bpf_mark_all_scalars_precise(env, starting_state);
		bt_reset(bt);
	}

	return 0;
}
