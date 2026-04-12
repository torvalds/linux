// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/filter.h>

#define verbose(env, fmt, args...) bpf_verifier_log_write(env, fmt, ##args)

#define BPF_COMPLEXITY_LIMIT_STATES	64

static bool is_may_goto_insn_at(struct bpf_verifier_env *env, int insn_idx)
{
	return bpf_is_may_goto_insn(&env->prog->insnsi[insn_idx]);
}

static bool is_iter_next_insn(struct bpf_verifier_env *env, int insn_idx)
{
	return env->insn_aux_data[insn_idx].is_iter_next;
}

static void update_peak_states(struct bpf_verifier_env *env)
{
	u32 cur_states;

	cur_states = env->explored_states_size + env->free_list_size + env->num_backedges;
	env->peak_states = max(env->peak_states, cur_states);
}

/* struct bpf_verifier_state->parent refers to states
 * that are in either of env->{expored_states,free_list}.
 * In both cases the state is contained in struct bpf_verifier_state_list.
 */
static struct bpf_verifier_state_list *state_parent_as_list(struct bpf_verifier_state *st)
{
	if (st->parent)
		return container_of(st->parent, struct bpf_verifier_state_list, state);
	return NULL;
}

static bool incomplete_read_marks(struct bpf_verifier_env *env,
				  struct bpf_verifier_state *st);

/* A state can be freed if it is no longer referenced:
 * - is in the env->free_list;
 * - has no children states;
 */
static void maybe_free_verifier_state(struct bpf_verifier_env *env,
				      struct bpf_verifier_state_list *sl)
{
	if (!sl->in_free_list
	    || sl->state.branches != 0
	    || incomplete_read_marks(env, &sl->state))
		return;
	list_del(&sl->node);
	bpf_free_verifier_state(&sl->state, false);
	kfree(sl);
	env->free_list_size--;
}

/* For state @st look for a topmost frame with frame_insn_idx() in some SCC,
 * if such frame exists form a corresponding @callchain as an array of
 * call sites leading to this frame and SCC id.
 * E.g.:
 *
 *    void foo()  { A: loop {... SCC#1 ...}; }
 *    void bar()  { B: loop { C: foo(); ... SCC#2 ... }
 *                  D: loop { E: foo(); ... SCC#3 ... } }
 *    void main() { F: bar(); }
 *
 * @callchain at (A) would be either (F,SCC#2) or (F,SCC#3) depending
 * on @st frame call sites being (F,C,A) or (F,E,A).
 */
static bool compute_scc_callchain(struct bpf_verifier_env *env,
				  struct bpf_verifier_state *st,
				  struct bpf_scc_callchain *callchain)
{
	u32 i, scc, insn_idx;

	memset(callchain, 0, sizeof(*callchain));
	for (i = 0; i <= st->curframe; i++) {
		insn_idx = bpf_frame_insn_idx(st, i);
		scc = env->insn_aux_data[insn_idx].scc;
		if (scc) {
			callchain->scc = scc;
			break;
		} else if (i < st->curframe) {
			callchain->callsites[i] = insn_idx;
		} else {
			return false;
		}
	}
	return true;
}

/* Check if bpf_scc_visit instance for @callchain exists. */
static struct bpf_scc_visit *scc_visit_lookup(struct bpf_verifier_env *env,
					      struct bpf_scc_callchain *callchain)
{
	struct bpf_scc_info *info = env->scc_info[callchain->scc];
	struct bpf_scc_visit *visits = info->visits;
	u32 i;

	if (!info)
		return NULL;
	for (i = 0; i < info->num_visits; i++)
		if (memcmp(callchain, &visits[i].callchain, sizeof(*callchain)) == 0)
			return &visits[i];
	return NULL;
}

/* Allocate a new bpf_scc_visit instance corresponding to @callchain.
 * Allocated instances are alive for a duration of the do_check_common()
 * call and are freed by free_states().
 */
static struct bpf_scc_visit *scc_visit_alloc(struct bpf_verifier_env *env,
					     struct bpf_scc_callchain *callchain)
{
	struct bpf_scc_visit *visit;
	struct bpf_scc_info *info;
	u32 scc, num_visits;
	u64 new_sz;

	scc = callchain->scc;
	info = env->scc_info[scc];
	num_visits = info ? info->num_visits : 0;
	new_sz = sizeof(*info) + sizeof(struct bpf_scc_visit) * (num_visits + 1);
	info = kvrealloc(env->scc_info[scc], new_sz, GFP_KERNEL_ACCOUNT);
	if (!info)
		return NULL;
	env->scc_info[scc] = info;
	info->num_visits = num_visits + 1;
	visit = &info->visits[num_visits];
	memset(visit, 0, sizeof(*visit));
	memcpy(&visit->callchain, callchain, sizeof(*callchain));
	return visit;
}

/* Form a string '(callsite#1,callsite#2,...,scc)' in env->tmp_str_buf */
static char *format_callchain(struct bpf_verifier_env *env, struct bpf_scc_callchain *callchain)
{
	char *buf = env->tmp_str_buf;
	int i, delta = 0;

	delta += snprintf(buf + delta, TMP_STR_BUF_LEN - delta, "(");
	for (i = 0; i < ARRAY_SIZE(callchain->callsites); i++) {
		if (!callchain->callsites[i])
			break;
		delta += snprintf(buf + delta, TMP_STR_BUF_LEN - delta, "%u,",
				  callchain->callsites[i]);
	}
	delta += snprintf(buf + delta, TMP_STR_BUF_LEN - delta, "%u)", callchain->scc);
	return env->tmp_str_buf;
}

/* If callchain for @st exists (@st is in some SCC), ensure that
 * bpf_scc_visit instance for this callchain exists.
 * If instance does not exist or is empty, assign visit->entry_state to @st.
 */
static int maybe_enter_scc(struct bpf_verifier_env *env, struct bpf_verifier_state *st)
{
	struct bpf_scc_callchain *callchain = &env->callchain_buf;
	struct bpf_scc_visit *visit;

	if (!compute_scc_callchain(env, st, callchain))
		return 0;
	visit = scc_visit_lookup(env, callchain);
	visit = visit ?: scc_visit_alloc(env, callchain);
	if (!visit)
		return -ENOMEM;
	if (!visit->entry_state) {
		visit->entry_state = st;
		if (env->log.level & BPF_LOG_LEVEL2)
			verbose(env, "SCC enter %s\n", format_callchain(env, callchain));
	}
	return 0;
}

static int propagate_backedges(struct bpf_verifier_env *env, struct bpf_scc_visit *visit);

/* If callchain for @st exists (@st is in some SCC), make it empty:
 * - set visit->entry_state to NULL;
 * - flush accumulated backedges.
 */
static int maybe_exit_scc(struct bpf_verifier_env *env, struct bpf_verifier_state *st)
{
	struct bpf_scc_callchain *callchain = &env->callchain_buf;
	struct bpf_scc_visit *visit;

	if (!compute_scc_callchain(env, st, callchain))
		return 0;
	visit = scc_visit_lookup(env, callchain);
	if (!visit) {
		/*
		 * If path traversal stops inside an SCC, corresponding bpf_scc_visit
		 * must exist for non-speculative paths. For non-speculative paths
		 * traversal stops when:
		 * a. Verification error is found, maybe_exit_scc() is not called.
		 * b. Top level BPF_EXIT is reached. Top level BPF_EXIT is not a member
		 *    of any SCC.
		 * c. A checkpoint is reached and matched. Checkpoints are created by
		 *    is_state_visited(), which calls maybe_enter_scc(), which allocates
		 *    bpf_scc_visit instances for checkpoints within SCCs.
		 * (c) is the only case that can reach this point.
		 */
		if (!st->speculative) {
			verifier_bug(env, "scc exit: no visit info for call chain %s",
				     format_callchain(env, callchain));
			return -EFAULT;
		}
		return 0;
	}
	if (visit->entry_state != st)
		return 0;
	if (env->log.level & BPF_LOG_LEVEL2)
		verbose(env, "SCC exit %s\n", format_callchain(env, callchain));
	visit->entry_state = NULL;
	env->num_backedges -= visit->num_backedges;
	visit->num_backedges = 0;
	update_peak_states(env);
	return propagate_backedges(env, visit);
}

/* Lookup an bpf_scc_visit instance corresponding to @st callchain
 * and add @backedge to visit->backedges. @st callchain must exist.
 */
static int add_scc_backedge(struct bpf_verifier_env *env,
			    struct bpf_verifier_state *st,
			    struct bpf_scc_backedge *backedge)
{
	struct bpf_scc_callchain *callchain = &env->callchain_buf;
	struct bpf_scc_visit *visit;

	if (!compute_scc_callchain(env, st, callchain)) {
		verifier_bug(env, "add backedge: no SCC in verification path, insn_idx %d",
			     st->insn_idx);
		return -EFAULT;
	}
	visit = scc_visit_lookup(env, callchain);
	if (!visit) {
		verifier_bug(env, "add backedge: no visit info for call chain %s",
			     format_callchain(env, callchain));
		return -EFAULT;
	}
	if (env->log.level & BPF_LOG_LEVEL2)
		verbose(env, "SCC backedge %s\n", format_callchain(env, callchain));
	backedge->next = visit->backedges;
	visit->backedges = backedge;
	visit->num_backedges++;
	env->num_backedges++;
	update_peak_states(env);
	return 0;
}

/* bpf_reg_state->live marks for registers in a state @st are incomplete,
 * if state @st is in some SCC and not all execution paths starting at this
 * SCC are fully explored.
 */
static bool incomplete_read_marks(struct bpf_verifier_env *env,
				  struct bpf_verifier_state *st)
{
	struct bpf_scc_callchain *callchain = &env->callchain_buf;
	struct bpf_scc_visit *visit;

	if (!compute_scc_callchain(env, st, callchain))
		return false;
	visit = scc_visit_lookup(env, callchain);
	if (!visit)
		return false;
	return !!visit->backedges;
}

int bpf_update_branch_counts(struct bpf_verifier_env *env, struct bpf_verifier_state *st)
{
	struct bpf_verifier_state_list *sl = NULL, *parent_sl;
	struct bpf_verifier_state *parent;
	int err;

	while (st) {
		u32 br = --st->branches;

		/* verifier_bug_if(br > 1, ...) technically makes sense here,
		 * but see comment in push_stack(), hence:
		 */
		verifier_bug_if((int)br < 0, env, "%s:branches_to_explore=%d", __func__, br);
		if (br)
			break;
		err = maybe_exit_scc(env, st);
		if (err)
			return err;
		parent = st->parent;
		parent_sl = state_parent_as_list(st);
		if (sl)
			maybe_free_verifier_state(env, sl);
		st = parent;
		sl = parent_sl;
	}
	return 0;
}

/* check %cur's range satisfies %old's */
static bool range_within(const struct bpf_reg_state *old,
			 const struct bpf_reg_state *cur)
{
	return old->umin_value <= cur->umin_value &&
	       old->umax_value >= cur->umax_value &&
	       old->smin_value <= cur->smin_value &&
	       old->smax_value >= cur->smax_value &&
	       old->u32_min_value <= cur->u32_min_value &&
	       old->u32_max_value >= cur->u32_max_value &&
	       old->s32_min_value <= cur->s32_min_value &&
	       old->s32_max_value >= cur->s32_max_value;
}

/* If in the old state two registers had the same id, then they need to have
 * the same id in the new state as well.  But that id could be different from
 * the old state, so we need to track the mapping from old to new ids.
 * Once we have seen that, say, a reg with old id 5 had new id 9, any subsequent
 * regs with old id 5 must also have new id 9 for the new state to be safe.  But
 * regs with a different old id could still have new id 9, we don't care about
 * that.
 * So we look through our idmap to see if this old id has been seen before.  If
 * so, we require the new id to match; otherwise, we add the id pair to the map.
 */
static bool check_ids(u32 old_id, u32 cur_id, struct bpf_idmap *idmap)
{
	struct bpf_id_pair *map = idmap->map;
	unsigned int i;

	/* either both IDs should be set or both should be zero */
	if (!!old_id != !!cur_id)
		return false;

	if (old_id == 0) /* cur_id == 0 as well */
		return true;

	for (i = 0; i < idmap->cnt; i++) {
		if (map[i].old == old_id)
			return map[i].cur == cur_id;
		if (map[i].cur == cur_id)
			return false;
	}

	/* Reached the end of known mappings; haven't seen this id before */
	if (idmap->cnt < BPF_ID_MAP_SIZE) {
		map[idmap->cnt].old = old_id;
		map[idmap->cnt].cur = cur_id;
		idmap->cnt++;
		return true;
	}

	/* We ran out of idmap slots, which should be impossible */
	WARN_ON_ONCE(1);
	return false;
}

/*
 * Compare scalar register IDs for state equivalence.
 *
 * When old_id == 0, the old register is independent - not linked to any
 * other register. Any linking in the current state only adds constraints,
 * making it more restrictive. Since the old state didn't rely on any ID
 * relationships for this register, it's always safe to accept cur regardless
 * of its ID. Hence, return true immediately.
 *
 * When old_id != 0 but cur_id == 0, we need to ensure that different
 * independent registers in cur don't incorrectly satisfy the ID matching
 * requirements of linked registers in old.
 *
 * Example: if old has r6.id=X and r7.id=X (linked), but cur has r6.id=0
 * and r7.id=0 (both independent), without temp IDs both would map old_id=X
 * to cur_id=0 and pass. With temp IDs: r6 maps X->temp1, r7 tries to map
 * X->temp2, but X is already mapped to temp1, so the check fails correctly.
 *
 * When old_id has BPF_ADD_CONST set, the compound id (base | flag) and the
 * base id (flag stripped) must both map consistently. Example: old has
 * r2.id=A, r3.id=A|flag (r3 = r2 + delta), cur has r2.id=B, r3.id=C|flag
 * (r3 derived from unrelated r4). Without the base check, idmap gets two
 * independent entries A->B and A|flag->C|flag, missing that A->C conflicts
 * with A->B. The base ID cross-check catches this.
 */
static bool check_scalar_ids(u32 old_id, u32 cur_id, struct bpf_idmap *idmap)
{
	if (!old_id)
		return true;

	cur_id = cur_id ? cur_id : ++idmap->tmp_id_gen;

	if (!check_ids(old_id, cur_id, idmap))
		return false;
	if (old_id & BPF_ADD_CONST) {
		old_id &= ~BPF_ADD_CONST;
		cur_id &= ~BPF_ADD_CONST;
		if (!check_ids(old_id, cur_id, idmap))
			return false;
	}
	return true;
}

static void __clean_func_state(struct bpf_verifier_env *env,
			       struct bpf_func_state *st,
			       u16 live_regs, int frame)
{
	int i, j;

	for (i = 0; i < BPF_REG_FP; i++) {
		/* liveness must not touch this register anymore */
		if (!(live_regs & BIT(i)))
			/* since the register is unused, clear its state
			 * to make further comparison simpler
			 */
			bpf_mark_reg_not_init(env, &st->regs[i]);
	}

	/*
	 * Clean dead 4-byte halves within each SPI independently.
	 * half_spi 2*i   → lower half: slot_type[0..3] (closer to FP)
	 * half_spi 2*i+1 → upper half: slot_type[4..7] (farther from FP)
	 */
	for (i = 0; i < st->allocated_stack / BPF_REG_SIZE; i++) {
		bool lo_live = bpf_stack_slot_alive(env, frame, i * 2);
		bool hi_live = bpf_stack_slot_alive(env, frame, i * 2 + 1);

		if (!hi_live || !lo_live) {
			int start = !lo_live ? 0 : BPF_REG_SIZE / 2;
			int end = !hi_live ? BPF_REG_SIZE : BPF_REG_SIZE / 2;
			u8 stype = st->stack[i].slot_type[7];

			/*
			 * Don't clear special slots.
			 * destroy_if_dynptr_stack_slot() needs STACK_DYNPTR to
			 * detect overwrites and invalidate associated data slices.
			 * is_iter_reg_valid_uninit() and is_irq_flag_reg_valid_uninit()
			 * check for their respective slot types to detect double-create.
			 */
			if (stype == STACK_DYNPTR || stype == STACK_ITER ||
			    stype == STACK_IRQ_FLAG)
				continue;

			/*
			 * Only destroy spilled_ptr when hi half is dead.
			 * If hi half is still live with STACK_SPILL, the
			 * spilled_ptr metadata is needed for correct state
			 * comparison in stacksafe().
			 * is_spilled_reg() is using slot_type[7], but
			 * is_spilled_scalar_after() check either slot_type[0] or [4]
			 */
			if (!hi_live) {
				struct bpf_reg_state *spill = &st->stack[i].spilled_ptr;

				if (lo_live && stype == STACK_SPILL) {
					u8 val = STACK_MISC;

					/*
					 * 8 byte spill of scalar 0 where half slot is dead
					 * should become STACK_ZERO in lo 4 bytes.
					 */
					if (bpf_register_is_null(spill))
						val = STACK_ZERO;
					for (j = 0; j < 4; j++) {
						u8 *t = &st->stack[i].slot_type[j];

						if (*t == STACK_SPILL)
							*t = val;
					}
				}
				bpf_mark_reg_not_init(env, spill);
			}
			for (j = start; j < end; j++)
				st->stack[i].slot_type[j] = STACK_POISON;
		}
	}
}

static int clean_verifier_state(struct bpf_verifier_env *env,
				 struct bpf_verifier_state *st)
{
	int i, err;

	err = bpf_live_stack_query_init(env, st);
	if (err)
		return err;
	for (i = 0; i <= st->curframe; i++) {
		u32 ip = bpf_frame_insn_idx(st, i);
		u16 live_regs = env->insn_aux_data[ip].live_regs_before;

		__clean_func_state(env, st->frame[i], live_regs, i);
	}
	return 0;
}

static bool regs_exact(const struct bpf_reg_state *rold,
		       const struct bpf_reg_state *rcur,
		       struct bpf_idmap *idmap)
{
	return memcmp(rold, rcur, offsetof(struct bpf_reg_state, id)) == 0 &&
	       check_ids(rold->id, rcur->id, idmap) &&
	       check_ids(rold->ref_obj_id, rcur->ref_obj_id, idmap);
}

enum exact_level {
	NOT_EXACT,
	EXACT,
	RANGE_WITHIN
};

/* Returns true if (rold safe implies rcur safe) */
static bool regsafe(struct bpf_verifier_env *env, struct bpf_reg_state *rold,
		    struct bpf_reg_state *rcur, struct bpf_idmap *idmap,
		    enum exact_level exact)
{
	if (exact == EXACT)
		return regs_exact(rold, rcur, idmap);

	if (rold->type == NOT_INIT)
		/* explored state can't have used this */
		return true;

	/* Enforce that register types have to match exactly, including their
	 * modifiers (like PTR_MAYBE_NULL, MEM_RDONLY, etc), as a general
	 * rule.
	 *
	 * One can make a point that using a pointer register as unbounded
	 * SCALAR would be technically acceptable, but this could lead to
	 * pointer leaks because scalars are allowed to leak while pointers
	 * are not. We could make this safe in special cases if root is
	 * calling us, but it's probably not worth the hassle.
	 *
	 * Also, register types that are *not* MAYBE_NULL could technically be
	 * safe to use as their MAYBE_NULL variants (e.g., PTR_TO_MAP_VALUE
	 * is safe to be used as PTR_TO_MAP_VALUE_OR_NULL, provided both point
	 * to the same map).
	 * However, if the old MAYBE_NULL register then got NULL checked,
	 * doing so could have affected others with the same id, and we can't
	 * check for that because we lost the id when we converted to
	 * a non-MAYBE_NULL variant.
	 * So, as a general rule we don't allow mixing MAYBE_NULL and
	 * non-MAYBE_NULL registers as well.
	 */
	if (rold->type != rcur->type)
		return false;

	switch (base_type(rold->type)) {
	case SCALAR_VALUE:
		if (env->explore_alu_limits) {
			/* explore_alu_limits disables tnum_in() and range_within()
			 * logic and requires everything to be strict
			 */
			return memcmp(rold, rcur, offsetof(struct bpf_reg_state, id)) == 0 &&
			       check_scalar_ids(rold->id, rcur->id, idmap);
		}
		if (!rold->precise && exact == NOT_EXACT)
			return true;
		/*
		 * Linked register tracking uses rold->id to detect relationships.
		 * When rold->id == 0, the register is independent and any linking
		 * in rcur only adds constraints. When rold->id != 0, we must verify
		 * id mapping and (for BPF_ADD_CONST) offset consistency.
		 *
		 * +------------------+-----------+------------------+---------------+
		 * |                  | rold->id  | rold + ADD_CONST | rold->id == 0 |
		 * |------------------+-----------+------------------+---------------|
		 * | rcur->id         | range,ids | false            | range         |
		 * | rcur + ADD_CONST | false     | range,ids,off    | range         |
		 * | rcur->id == 0    | range,ids | false            | range         |
		 * +------------------+-----------+------------------+---------------+
		 *
		 * Why check_ids() for scalar registers?
		 *
		 * Consider the following BPF code:
		 *   1: r6 = ... unbound scalar, ID=a ...
		 *   2: r7 = ... unbound scalar, ID=b ...
		 *   3: if (r6 > r7) goto +1
		 *   4: r6 = r7
		 *   5: if (r6 > X) goto ...
		 *   6: ... memory operation using r7 ...
		 *
		 * First verification path is [1-6]:
		 * - at (4) same bpf_reg_state::id (b) would be assigned to r6 and r7;
		 * - at (5) r6 would be marked <= X, sync_linked_regs() would also mark
		 *   r7 <= X, because r6 and r7 share same id.
		 * Next verification path is [1-4, 6].
		 *
		 * Instruction (6) would be reached in two states:
		 *   I.  r6{.id=b}, r7{.id=b} via path 1-6;
		 *   II. r6{.id=a}, r7{.id=b} via path 1-4, 6.
		 *
		 * Use check_ids() to distinguish these states.
		 * ---
		 * Also verify that new value satisfies old value range knowledge.
		 */

		/*
		 * ADD_CONST flags must match exactly: BPF_ADD_CONST32 and
		 * BPF_ADD_CONST64 have different linking semantics in
		 * sync_linked_regs() (alu32 zero-extends, alu64 does not),
		 * so pruning across different flag types is unsafe.
		 */
		if (rold->id &&
		    (rold->id & BPF_ADD_CONST) != (rcur->id & BPF_ADD_CONST))
			return false;

		/* Both have offset linkage: offsets must match */
		if ((rold->id & BPF_ADD_CONST) && rold->delta != rcur->delta)
			return false;

		if (!check_scalar_ids(rold->id, rcur->id, idmap))
			return false;

		return range_within(rold, rcur) && tnum_in(rold->var_off, rcur->var_off);
	case PTR_TO_MAP_KEY:
	case PTR_TO_MAP_VALUE:
	case PTR_TO_MEM:
	case PTR_TO_BUF:
	case PTR_TO_TP_BUFFER:
		/* If the new min/max/var_off satisfy the old ones and
		 * everything else matches, we are OK.
		 */
		return memcmp(rold, rcur, offsetof(struct bpf_reg_state, var_off)) == 0 &&
		       range_within(rold, rcur) &&
		       tnum_in(rold->var_off, rcur->var_off) &&
		       check_ids(rold->id, rcur->id, idmap) &&
		       check_ids(rold->ref_obj_id, rcur->ref_obj_id, idmap);
	case PTR_TO_PACKET_META:
	case PTR_TO_PACKET:
		/* We must have at least as much range as the old ptr
		 * did, so that any accesses which were safe before are
		 * still safe.  This is true even if old range < old off,
		 * since someone could have accessed through (ptr - k), or
		 * even done ptr -= k in a register, to get a safe access.
		 */
		if (rold->range < 0 || rcur->range < 0) {
			/* special case for [BEYOND|AT]_PKT_END */
			if (rold->range != rcur->range)
				return false;
		} else if (rold->range > rcur->range) {
			return false;
		}
		/* id relations must be preserved */
		if (!check_ids(rold->id, rcur->id, idmap))
			return false;
		/* new val must satisfy old val knowledge */
		return range_within(rold, rcur) &&
		       tnum_in(rold->var_off, rcur->var_off);
	case PTR_TO_STACK:
		/* two stack pointers are equal only if they're pointing to
		 * the same stack frame, since fp-8 in foo != fp-8 in bar
		 */
		return regs_exact(rold, rcur, idmap) && rold->frameno == rcur->frameno;
	case PTR_TO_ARENA:
		return true;
	case PTR_TO_INSN:
		return memcmp(rold, rcur, offsetof(struct bpf_reg_state, var_off)) == 0 &&
		       range_within(rold, rcur) && tnum_in(rold->var_off, rcur->var_off);
	default:
		return regs_exact(rold, rcur, idmap);
	}
}

static struct bpf_reg_state unbound_reg;

static __init int unbound_reg_init(void)
{
	bpf_mark_reg_unknown_imprecise(&unbound_reg);
	return 0;
}
late_initcall(unbound_reg_init);

static bool is_spilled_scalar_after(const struct bpf_stack_state *stack, int im)
{
	return stack->slot_type[im] == STACK_SPILL &&
	       stack->spilled_ptr.type == SCALAR_VALUE;
}

static bool is_stack_misc_after(struct bpf_verifier_env *env,
				struct bpf_stack_state *stack, int im)
{
	u32 i;

	for (i = im; i < ARRAY_SIZE(stack->slot_type); ++i) {
		if ((stack->slot_type[i] == STACK_MISC) ||
		    ((stack->slot_type[i] == STACK_INVALID || stack->slot_type[i] == STACK_POISON) &&
		     env->allow_uninit_stack))
			continue;
		return false;
	}

	return true;
}

static struct bpf_reg_state *scalar_reg_for_stack(struct bpf_verifier_env *env,
						  struct bpf_stack_state *stack, int im)
{
	if (is_spilled_scalar_after(stack, im))
		return &stack->spilled_ptr;

	if (is_stack_misc_after(env, stack, im))
		return &unbound_reg;

	return NULL;
}

static bool stacksafe(struct bpf_verifier_env *env, struct bpf_func_state *old,
		      struct bpf_func_state *cur, struct bpf_idmap *idmap,
		      enum exact_level exact)
{
	int i, spi;

	/* walk slots of the explored stack and ignore any additional
	 * slots in the current stack, since explored(safe) state
	 * didn't use them
	 */
	for (i = 0; i < old->allocated_stack; i++) {
		struct bpf_reg_state *old_reg, *cur_reg;
		int im = i % BPF_REG_SIZE;

		spi = i / BPF_REG_SIZE;

		if (exact == EXACT) {
			u8 old_type = old->stack[spi].slot_type[i % BPF_REG_SIZE];
			u8 cur_type = i < cur->allocated_stack ?
				      cur->stack[spi].slot_type[i % BPF_REG_SIZE] : STACK_INVALID;

			/* STACK_INVALID and STACK_POISON are equivalent for pruning */
			if (old_type == STACK_POISON)
				old_type = STACK_INVALID;
			if (cur_type == STACK_POISON)
				cur_type = STACK_INVALID;
			if (i >= cur->allocated_stack || old_type != cur_type)
				return false;
		}

		if (old->stack[spi].slot_type[i % BPF_REG_SIZE] == STACK_INVALID ||
		    old->stack[spi].slot_type[i % BPF_REG_SIZE] == STACK_POISON)
			continue;

		if (env->allow_uninit_stack &&
		    old->stack[spi].slot_type[i % BPF_REG_SIZE] == STACK_MISC)
			continue;

		/* explored stack has more populated slots than current stack
		 * and these slots were used
		 */
		if (i >= cur->allocated_stack)
			return false;

		/*
		 * 64 and 32-bit scalar spills vs MISC/INVALID slots and vice versa.
		 * Load from MISC/INVALID slots produces unbound scalar.
		 * Construct a fake register for such stack and call
		 * regsafe() to ensure scalar ids are compared.
		 */
		if (im == 0 || im == 4) {
			old_reg = scalar_reg_for_stack(env, &old->stack[spi], im);
			cur_reg = scalar_reg_for_stack(env, &cur->stack[spi], im);
			if (old_reg && cur_reg) {
				if (!regsafe(env, old_reg, cur_reg, idmap, exact))
					return false;
				i += (im == 0 ? BPF_REG_SIZE - 1 : 3);
				continue;
			}
		}

		/* if old state was safe with misc data in the stack
		 * it will be safe with zero-initialized stack.
		 * The opposite is not true
		 */
		if (old->stack[spi].slot_type[i % BPF_REG_SIZE] == STACK_MISC &&
		    cur->stack[spi].slot_type[i % BPF_REG_SIZE] == STACK_ZERO)
			continue;
		if (old->stack[spi].slot_type[i % BPF_REG_SIZE] !=
		    cur->stack[spi].slot_type[i % BPF_REG_SIZE])
			/* Ex: old explored (safe) state has STACK_SPILL in
			 * this stack slot, but current has STACK_MISC ->
			 * this verifier states are not equivalent,
			 * return false to continue verification of this path
			 */
			return false;
		if (i % BPF_REG_SIZE != BPF_REG_SIZE - 1)
			continue;
		/* Both old and cur are having same slot_type */
		switch (old->stack[spi].slot_type[BPF_REG_SIZE - 1]) {
		case STACK_SPILL:
			/* when explored and current stack slot are both storing
			 * spilled registers, check that stored pointers types
			 * are the same as well.
			 * Ex: explored safe path could have stored
			 * (bpf_reg_state) {.type = PTR_TO_STACK, .off = -8}
			 * but current path has stored:
			 * (bpf_reg_state) {.type = PTR_TO_STACK, .off = -16}
			 * such verifier states are not equivalent.
			 * return false to continue verification of this path
			 */
			if (!regsafe(env, &old->stack[spi].spilled_ptr,
				     &cur->stack[spi].spilled_ptr, idmap, exact))
				return false;
			break;
		case STACK_DYNPTR:
			old_reg = &old->stack[spi].spilled_ptr;
			cur_reg = &cur->stack[spi].spilled_ptr;
			if (old_reg->dynptr.type != cur_reg->dynptr.type ||
			    old_reg->dynptr.first_slot != cur_reg->dynptr.first_slot ||
			    !check_ids(old_reg->ref_obj_id, cur_reg->ref_obj_id, idmap))
				return false;
			break;
		case STACK_ITER:
			old_reg = &old->stack[spi].spilled_ptr;
			cur_reg = &cur->stack[spi].spilled_ptr;
			/* iter.depth is not compared between states as it
			 * doesn't matter for correctness and would otherwise
			 * prevent convergence; we maintain it only to prevent
			 * infinite loop check triggering, see
			 * iter_active_depths_differ()
			 */
			if (old_reg->iter.btf != cur_reg->iter.btf ||
			    old_reg->iter.btf_id != cur_reg->iter.btf_id ||
			    old_reg->iter.state != cur_reg->iter.state ||
			    /* ignore {old_reg,cur_reg}->iter.depth, see above */
			    !check_ids(old_reg->ref_obj_id, cur_reg->ref_obj_id, idmap))
				return false;
			break;
		case STACK_IRQ_FLAG:
			old_reg = &old->stack[spi].spilled_ptr;
			cur_reg = &cur->stack[spi].spilled_ptr;
			if (!check_ids(old_reg->ref_obj_id, cur_reg->ref_obj_id, idmap) ||
			    old_reg->irq.kfunc_class != cur_reg->irq.kfunc_class)
				return false;
			break;
		case STACK_MISC:
		case STACK_ZERO:
		case STACK_INVALID:
		case STACK_POISON:
			continue;
		/* Ensure that new unhandled slot types return false by default */
		default:
			return false;
		}
	}
	return true;
}

static bool refsafe(struct bpf_verifier_state *old, struct bpf_verifier_state *cur,
		    struct bpf_idmap *idmap)
{
	int i;

	if (old->acquired_refs != cur->acquired_refs)
		return false;

	if (old->active_locks != cur->active_locks)
		return false;

	if (old->active_preempt_locks != cur->active_preempt_locks)
		return false;

	if (old->active_rcu_locks != cur->active_rcu_locks)
		return false;

	if (!check_ids(old->active_irq_id, cur->active_irq_id, idmap))
		return false;

	if (!check_ids(old->active_lock_id, cur->active_lock_id, idmap) ||
	    old->active_lock_ptr != cur->active_lock_ptr)
		return false;

	for (i = 0; i < old->acquired_refs; i++) {
		if (!check_ids(old->refs[i].id, cur->refs[i].id, idmap) ||
		    old->refs[i].type != cur->refs[i].type)
			return false;
		switch (old->refs[i].type) {
		case REF_TYPE_PTR:
		case REF_TYPE_IRQ:
			break;
		case REF_TYPE_LOCK:
		case REF_TYPE_RES_LOCK:
		case REF_TYPE_RES_LOCK_IRQ:
			if (old->refs[i].ptr != cur->refs[i].ptr)
				return false;
			break;
		default:
			WARN_ONCE(1, "Unhandled enum type for reference state: %d\n", old->refs[i].type);
			return false;
		}
	}

	return true;
}

/* compare two verifier states
 *
 * all states stored in state_list are known to be valid, since
 * verifier reached 'bpf_exit' instruction through them
 *
 * this function is called when verifier exploring different branches of
 * execution popped from the state stack. If it sees an old state that has
 * more strict register state and more strict stack state then this execution
 * branch doesn't need to be explored further, since verifier already
 * concluded that more strict state leads to valid finish.
 *
 * Therefore two states are equivalent if register state is more conservative
 * and explored stack state is more conservative than the current one.
 * Example:
 *       explored                   current
 * (slot1=INV slot2=MISC) == (slot1=MISC slot2=MISC)
 * (slot1=MISC slot2=MISC) != (slot1=INV slot2=MISC)
 *
 * In other words if current stack state (one being explored) has more
 * valid slots than old one that already passed validation, it means
 * the verifier can stop exploring and conclude that current state is valid too
 *
 * Similarly with registers. If explored state has register type as invalid
 * whereas register type in current state is meaningful, it means that
 * the current state will reach 'bpf_exit' instruction safely
 */
static bool func_states_equal(struct bpf_verifier_env *env, struct bpf_func_state *old,
			      struct bpf_func_state *cur, u32 insn_idx, enum exact_level exact)
{
	u16 live_regs = env->insn_aux_data[insn_idx].live_regs_before;
	u16 i;

	if (old->callback_depth > cur->callback_depth)
		return false;

	for (i = 0; i < MAX_BPF_REG; i++)
		if (((1 << i) & live_regs) &&
		    !regsafe(env, &old->regs[i], &cur->regs[i],
			     &env->idmap_scratch, exact))
			return false;

	if (!stacksafe(env, old, cur, &env->idmap_scratch, exact))
		return false;

	return true;
}

static void reset_idmap_scratch(struct bpf_verifier_env *env)
{
	struct bpf_idmap *idmap = &env->idmap_scratch;

	idmap->tmp_id_gen = env->id_gen;
	idmap->cnt = 0;
}

static bool states_equal(struct bpf_verifier_env *env,
			 struct bpf_verifier_state *old,
			 struct bpf_verifier_state *cur,
			 enum exact_level exact)
{
	u32 insn_idx;
	int i;

	if (old->curframe != cur->curframe)
		return false;

	reset_idmap_scratch(env);

	/* Verification state from speculative execution simulation
	 * must never prune a non-speculative execution one.
	 */
	if (old->speculative && !cur->speculative)
		return false;

	if (old->in_sleepable != cur->in_sleepable)
		return false;

	if (!refsafe(old, cur, &env->idmap_scratch))
		return false;

	/* for states to be equal callsites have to be the same
	 * and all frame states need to be equivalent
	 */
	for (i = 0; i <= old->curframe; i++) {
		insn_idx = bpf_frame_insn_idx(old, i);
		if (old->frame[i]->callsite != cur->frame[i]->callsite)
			return false;
		if (!func_states_equal(env, old->frame[i], cur->frame[i], insn_idx, exact))
			return false;
	}
	return true;
}

/* find precise scalars in the previous equivalent state and
 * propagate them into the current state
 */
static int propagate_precision(struct bpf_verifier_env *env,
			       const struct bpf_verifier_state *old,
			       struct bpf_verifier_state *cur,
			       bool *changed)
{
	struct bpf_reg_state *state_reg;
	struct bpf_func_state *state;
	int i, err = 0, fr;
	bool first;

	for (fr = old->curframe; fr >= 0; fr--) {
		state = old->frame[fr];
		state_reg = state->regs;
		first = true;
		for (i = 0; i < BPF_REG_FP; i++, state_reg++) {
			if (state_reg->type != SCALAR_VALUE ||
			    !state_reg->precise)
				continue;
			if (env->log.level & BPF_LOG_LEVEL2) {
				if (first)
					verbose(env, "frame %d: propagating r%d", fr, i);
				else
					verbose(env, ",r%d", i);
			}
			bpf_bt_set_frame_reg(&env->bt, fr, i);
			first = false;
		}

		for (i = 0; i < state->allocated_stack / BPF_REG_SIZE; i++) {
			if (!bpf_is_spilled_reg(&state->stack[i]))
				continue;
			state_reg = &state->stack[i].spilled_ptr;
			if (state_reg->type != SCALAR_VALUE ||
			    !state_reg->precise)
				continue;
			if (env->log.level & BPF_LOG_LEVEL2) {
				if (first)
					verbose(env, "frame %d: propagating fp%d",
						fr, (-i - 1) * BPF_REG_SIZE);
				else
					verbose(env, ",fp%d", (-i - 1) * BPF_REG_SIZE);
			}
			bpf_bt_set_frame_slot(&env->bt, fr, i);
			first = false;
		}
		if (!first && (env->log.level & BPF_LOG_LEVEL2))
			verbose(env, "\n");
	}

	err = bpf_mark_chain_precision(env, cur, -1, changed);
	if (err < 0)
		return err;

	return 0;
}

#define MAX_BACKEDGE_ITERS 64

/* Propagate read and precision marks from visit->backedges[*].state->equal_state
 * to corresponding parent states of visit->backedges[*].state until fixed point is reached,
 * then free visit->backedges.
 * After execution of this function incomplete_read_marks() will return false
 * for all states corresponding to @visit->callchain.
 */
static int propagate_backedges(struct bpf_verifier_env *env, struct bpf_scc_visit *visit)
{
	struct bpf_scc_backedge *backedge;
	struct bpf_verifier_state *st;
	bool changed;
	int i, err;

	i = 0;
	do {
		if (i++ > MAX_BACKEDGE_ITERS) {
			if (env->log.level & BPF_LOG_LEVEL2)
				verbose(env, "%s: too many iterations\n", __func__);
			for (backedge = visit->backedges; backedge; backedge = backedge->next)
				bpf_mark_all_scalars_precise(env, &backedge->state);
			break;
		}
		changed = false;
		for (backedge = visit->backedges; backedge; backedge = backedge->next) {
			st = &backedge->state;
			err = propagate_precision(env, st->equal_state, st, &changed);
			if (err)
				return err;
		}
	} while (changed);

	bpf_free_backedges(visit);
	return 0;
}

static bool states_maybe_looping(struct bpf_verifier_state *old,
				 struct bpf_verifier_state *cur)
{
	struct bpf_func_state *fold, *fcur;
	int i, fr = cur->curframe;

	if (old->curframe != fr)
		return false;

	fold = old->frame[fr];
	fcur = cur->frame[fr];
	for (i = 0; i < MAX_BPF_REG; i++)
		if (memcmp(&fold->regs[i], &fcur->regs[i],
			   offsetof(struct bpf_reg_state, frameno)))
			return false;
	return true;
}

/* is_state_visited() handles iter_next() (see process_iter_next_call() for
 * terminology) calls specially: as opposed to bounded BPF loops, it *expects*
 * states to match, which otherwise would look like an infinite loop. So while
 * iter_next() calls are taken care of, we still need to be careful and
 * prevent erroneous and too eager declaration of "infinite loop", when
 * iterators are involved.
 *
 * Here's a situation in pseudo-BPF assembly form:
 *
 *   0: again:                          ; set up iter_next() call args
 *   1:   r1 = &it                      ; <CHECKPOINT HERE>
 *   2:   call bpf_iter_num_next        ; this is iter_next() call
 *   3:   if r0 == 0 goto done
 *   4:   ... something useful here ...
 *   5:   goto again                    ; another iteration
 *   6: done:
 *   7:   r1 = &it
 *   8:   call bpf_iter_num_destroy     ; clean up iter state
 *   9:   exit
 *
 * This is a typical loop. Let's assume that we have a prune point at 1:,
 * before we get to `call bpf_iter_num_next` (e.g., because of that `goto
 * again`, assuming other heuristics don't get in a way).
 *
 * When we first time come to 1:, let's say we have some state X. We proceed
 * to 2:, fork states, enqueue ACTIVE, validate NULL case successfully, exit.
 * Now we come back to validate that forked ACTIVE state. We proceed through
 * 3-5, come to goto, jump to 1:. Let's assume our state didn't change, so we
 * are converging. But the problem is that we don't know that yet, as this
 * convergence has to happen at iter_next() call site only. So if nothing is
 * done, at 1: verifier will use bounded loop logic and declare infinite
 * looping (and would be *technically* correct, if not for iterator's
 * "eventual sticky NULL" contract, see process_iter_next_call()). But we
 * don't want that. So what we do in process_iter_next_call() when we go on
 * another ACTIVE iteration, we bump slot->iter.depth, to mark that it's
 * a different iteration. So when we suspect an infinite loop, we additionally
 * check if any of the *ACTIVE* iterator states depths differ. If yes, we
 * pretend we are not looping and wait for next iter_next() call.
 *
 * This only applies to ACTIVE state. In DRAINED state we don't expect to
 * loop, because that would actually mean infinite loop, as DRAINED state is
 * "sticky", and so we'll keep returning into the same instruction with the
 * same state (at least in one of possible code paths).
 *
 * This approach allows to keep infinite loop heuristic even in the face of
 * active iterator. E.g., C snippet below is and will be detected as
 * infinitely looping:
 *
 *   struct bpf_iter_num it;
 *   int *p, x;
 *
 *   bpf_iter_num_new(&it, 0, 10);
 *   while ((p = bpf_iter_num_next(&t))) {
 *       x = p;
 *       while (x--) {} // <<-- infinite loop here
 *   }
 *
 */
static bool iter_active_depths_differ(struct bpf_verifier_state *old, struct bpf_verifier_state *cur)
{
	struct bpf_reg_state *slot, *cur_slot;
	struct bpf_func_state *state;
	int i, fr;

	for (fr = old->curframe; fr >= 0; fr--) {
		state = old->frame[fr];
		for (i = 0; i < state->allocated_stack / BPF_REG_SIZE; i++) {
			if (state->stack[i].slot_type[0] != STACK_ITER)
				continue;

			slot = &state->stack[i].spilled_ptr;
			if (slot->iter.state != BPF_ITER_STATE_ACTIVE)
				continue;

			cur_slot = &cur->frame[fr]->stack[i].spilled_ptr;
			if (cur_slot->iter.depth != slot->iter.depth)
				return true;
		}
	}
	return false;
}

static void mark_all_scalars_imprecise(struct bpf_verifier_env *env, struct bpf_verifier_state *st)
{
	struct bpf_func_state *func;
	struct bpf_reg_state *reg;
	int i, j;

	for (i = 0; i <= st->curframe; i++) {
		func = st->frame[i];
		for (j = 0; j < BPF_REG_FP; j++) {
			reg = &func->regs[j];
			if (reg->type != SCALAR_VALUE)
				continue;
			reg->precise = false;
		}
		for (j = 0; j < func->allocated_stack / BPF_REG_SIZE; j++) {
			if (!bpf_is_spilled_reg(&func->stack[j]))
				continue;
			reg = &func->stack[j].spilled_ptr;
			if (reg->type != SCALAR_VALUE)
				continue;
			reg->precise = false;
		}
	}
}

int bpf_is_state_visited(struct bpf_verifier_env *env, int insn_idx)
{
	struct bpf_verifier_state_list *new_sl;
	struct bpf_verifier_state_list *sl;
	struct bpf_verifier_state *cur = env->cur_state, *new;
	bool force_new_state, add_new_state, loop;
	int n, err, states_cnt = 0;
	struct list_head *pos, *tmp, *head;

	force_new_state = env->test_state_freq || bpf_is_force_checkpoint(env, insn_idx) ||
			  /* Avoid accumulating infinitely long jmp history */
			  cur->jmp_history_cnt > 40;

	/* bpf progs typically have pruning point every 4 instructions
	 * http://vger.kernel.org/bpfconf2019.html#session-1
	 * Do not add new state for future pruning if the verifier hasn't seen
	 * at least 2 jumps and at least 8 instructions.
	 * This heuristics helps decrease 'total_states' and 'peak_states' metric.
	 * In tests that amounts to up to 50% reduction into total verifier
	 * memory consumption and 20% verifier time speedup.
	 */
	add_new_state = force_new_state;
	if (env->jmps_processed - env->prev_jmps_processed >= 2 &&
	    env->insn_processed - env->prev_insn_processed >= 8)
		add_new_state = true;

	/* keep cleaning the current state as registers/stack become dead */
	err = clean_verifier_state(env, cur);
	if (err)
		return err;

	loop = false;
	head = bpf_explored_state(env, insn_idx);
	list_for_each_safe(pos, tmp, head) {
		sl = container_of(pos, struct bpf_verifier_state_list, node);
		states_cnt++;
		if (sl->state.insn_idx != insn_idx)
			continue;

		if (sl->state.branches) {
			struct bpf_func_state *frame = sl->state.frame[sl->state.curframe];

			if (frame->in_async_callback_fn &&
			    frame->async_entry_cnt != cur->frame[cur->curframe]->async_entry_cnt) {
				/* Different async_entry_cnt means that the verifier is
				 * processing another entry into async callback.
				 * Seeing the same state is not an indication of infinite
				 * loop or infinite recursion.
				 * But finding the same state doesn't mean that it's safe
				 * to stop processing the current state. The previous state
				 * hasn't yet reached bpf_exit, since state.branches > 0.
				 * Checking in_async_callback_fn alone is not enough either.
				 * Since the verifier still needs to catch infinite loops
				 * inside async callbacks.
				 */
				goto skip_inf_loop_check;
			}
			/* BPF open-coded iterators loop detection is special.
			 * states_maybe_looping() logic is too simplistic in detecting
			 * states that *might* be equivalent, because it doesn't know
			 * about ID remapping, so don't even perform it.
			 * See process_iter_next_call() and iter_active_depths_differ()
			 * for overview of the logic. When current and one of parent
			 * states are detected as equivalent, it's a good thing: we prove
			 * convergence and can stop simulating further iterations.
			 * It's safe to assume that iterator loop will finish, taking into
			 * account iter_next() contract of eventually returning
			 * sticky NULL result.
			 *
			 * Note, that states have to be compared exactly in this case because
			 * read and precision marks might not be finalized inside the loop.
			 * E.g. as in the program below:
			 *
			 *     1. r7 = -16
			 *     2. r6 = bpf_get_prandom_u32()
			 *     3. while (bpf_iter_num_next(&fp[-8])) {
			 *     4.   if (r6 != 42) {
			 *     5.     r7 = -32
			 *     6.     r6 = bpf_get_prandom_u32()
			 *     7.     continue
			 *     8.   }
			 *     9.   r0 = r10
			 *    10.   r0 += r7
			 *    11.   r8 = *(u64 *)(r0 + 0)
			 *    12.   r6 = bpf_get_prandom_u32()
			 *    13. }
			 *
			 * Here verifier would first visit path 1-3, create a checkpoint at 3
			 * with r7=-16, continue to 4-7,3. Existing checkpoint at 3 does
			 * not have read or precision mark for r7 yet, thus inexact states
			 * comparison would discard current state with r7=-32
			 * => unsafe memory access at 11 would not be caught.
			 */
			if (is_iter_next_insn(env, insn_idx)) {
				if (states_equal(env, &sl->state, cur, RANGE_WITHIN)) {
					struct bpf_func_state *cur_frame;
					struct bpf_reg_state *iter_state, *iter_reg;
					int spi;

					cur_frame = cur->frame[cur->curframe];
					/* btf_check_iter_kfuncs() enforces that
					 * iter state pointer is always the first arg
					 */
					iter_reg = &cur_frame->regs[BPF_REG_1];
					/* current state is valid due to states_equal(),
					 * so we can assume valid iter and reg state,
					 * no need for extra (re-)validations
					 */
					spi = bpf_get_spi(iter_reg->var_off.value);
					iter_state = &bpf_func(env, iter_reg)->stack[spi].spilled_ptr;
					if (iter_state->iter.state == BPF_ITER_STATE_ACTIVE) {
						loop = true;
						goto hit;
					}
				}
				goto skip_inf_loop_check;
			}
			if (is_may_goto_insn_at(env, insn_idx)) {
				if (sl->state.may_goto_depth != cur->may_goto_depth &&
				    states_equal(env, &sl->state, cur, RANGE_WITHIN)) {
					loop = true;
					goto hit;
				}
			}
			if (bpf_calls_callback(env, insn_idx)) {
				if (states_equal(env, &sl->state, cur, RANGE_WITHIN)) {
					loop = true;
					goto hit;
				}
				goto skip_inf_loop_check;
			}
			/* attempt to detect infinite loop to avoid unnecessary doomed work */
			if (states_maybe_looping(&sl->state, cur) &&
			    states_equal(env, &sl->state, cur, EXACT) &&
			    !iter_active_depths_differ(&sl->state, cur) &&
			    sl->state.may_goto_depth == cur->may_goto_depth &&
			    sl->state.callback_unroll_depth == cur->callback_unroll_depth) {
				verbose_linfo(env, insn_idx, "; ");
				verbose(env, "infinite loop detected at insn %d\n", insn_idx);
				verbose(env, "cur state:");
				print_verifier_state(env, cur, cur->curframe, true);
				verbose(env, "old state:");
				print_verifier_state(env, &sl->state, cur->curframe, true);
				return -EINVAL;
			}
			/* if the verifier is processing a loop, avoid adding new state
			 * too often, since different loop iterations have distinct
			 * states and may not help future pruning.
			 * This threshold shouldn't be too low to make sure that
			 * a loop with large bound will be rejected quickly.
			 * The most abusive loop will be:
			 * r1 += 1
			 * if r1 < 1000000 goto pc-2
			 * 1M insn_procssed limit / 100 == 10k peak states.
			 * This threshold shouldn't be too high either, since states
			 * at the end of the loop are likely to be useful in pruning.
			 */
skip_inf_loop_check:
			if (!force_new_state &&
			    env->jmps_processed - env->prev_jmps_processed < 20 &&
			    env->insn_processed - env->prev_insn_processed < 100)
				add_new_state = false;
			goto miss;
		}
		/* See comments for mark_all_regs_read_and_precise() */
		loop = incomplete_read_marks(env, &sl->state);
		if (states_equal(env, &sl->state, cur, loop ? RANGE_WITHIN : NOT_EXACT)) {
hit:
			sl->hit_cnt++;

			/* if previous state reached the exit with precision and
			 * current state is equivalent to it (except precision marks)
			 * the precision needs to be propagated back in
			 * the current state.
			 */
			err = 0;
			if (bpf_is_jmp_point(env, env->insn_idx))
				err = bpf_push_jmp_history(env, cur, 0, 0);
			err = err ? : propagate_precision(env, &sl->state, cur, NULL);
			if (err)
				return err;
			/* When processing iterator based loops above propagate_liveness and
			 * propagate_precision calls are not sufficient to transfer all relevant
			 * read and precision marks. E.g. consider the following case:
			 *
			 *  .-> A --.  Assume the states are visited in the order A, B, C.
			 *  |   |   |  Assume that state B reaches a state equivalent to state A.
			 *  |   v   v  At this point, state C is not processed yet, so state A
			 *  '-- B   C  has not received any read or precision marks from C.
			 *             Thus, marks propagated from A to B are incomplete.
			 *
			 * The verifier mitigates this by performing the following steps:
			 *
			 * - Prior to the main verification pass, strongly connected components
			 *   (SCCs) are computed over the program's control flow graph,
			 *   intraprocedurally.
			 *
			 * - During the main verification pass, `maybe_enter_scc()` checks
			 *   whether the current verifier state is entering an SCC. If so, an
			 *   instance of a `bpf_scc_visit` object is created, and the state
			 *   entering the SCC is recorded as the entry state.
			 *
			 * - This instance is associated not with the SCC itself, but with a
			 *   `bpf_scc_callchain`: a tuple consisting of the call sites leading to
			 *   the SCC and the SCC id. See `compute_scc_callchain()`.
			 *
			 * - When a verification path encounters a `states_equal(...,
			 *   RANGE_WITHIN)` condition, there exists a call chain describing the
			 *   current state and a corresponding `bpf_scc_visit` instance. A copy
			 *   of the current state is created and added to
			 *   `bpf_scc_visit->backedges`.
			 *
			 * - When a verification path terminates, `maybe_exit_scc()` is called
			 *   from `bpf_update_branch_counts()`. For states with `branches == 0`, it
			 *   checks whether the state is the entry state of any `bpf_scc_visit`
			 *   instance. If it is, this indicates that all paths originating from
			 *   this SCC visit have been explored. `propagate_backedges()` is then
			 *   called, which propagates read and precision marks through the
			 *   backedges until a fixed point is reached.
			 *   (In the earlier example, this would propagate marks from A to B,
			 *    from C to A, and then again from A to B.)
			 *
			 * A note on callchains
			 * --------------------
			 *
			 * Consider the following example:
			 *
			 *     void foo() { loop { ... SCC#1 ... } }
			 *     void main() {
			 *       A: foo();
			 *       B: ...
			 *       C: foo();
			 *     }
			 *
			 * Here, there are two distinct callchains leading to SCC#1:
			 * - (A, SCC#1)
			 * - (C, SCC#1)
			 *
			 * Each callchain identifies a separate `bpf_scc_visit` instance that
			 * accumulates backedge states. The `propagate_{liveness,precision}()`
			 * functions traverse the parent state of each backedge state, which
			 * means these parent states must remain valid (i.e., not freed) while
			 * the corresponding `bpf_scc_visit` instance exists.
			 *
			 * Associating `bpf_scc_visit` instances directly with SCCs instead of
			 * callchains would break this invariant:
			 * - States explored during `C: foo()` would contribute backedges to
			 *   SCC#1, but SCC#1 would only be exited once the exploration of
			 *   `A: foo()` completes.
			 * - By that time, the states explored between `A: foo()` and `C: foo()`
			 *   (i.e., `B: ...`) may have already been freed, causing the parent
			 *   links for states from `C: foo()` to become invalid.
			 */
			if (loop) {
				struct bpf_scc_backedge *backedge;

				backedge = kzalloc_obj(*backedge,
						       GFP_KERNEL_ACCOUNT);
				if (!backedge)
					return -ENOMEM;
				err = bpf_copy_verifier_state(&backedge->state, cur);
				backedge->state.equal_state = &sl->state;
				backedge->state.insn_idx = insn_idx;
				err = err ?: add_scc_backedge(env, &sl->state, backedge);
				if (err) {
					bpf_free_verifier_state(&backedge->state, false);
					kfree(backedge);
					return err;
				}
			}
			return 1;
		}
miss:
		/* when new state is not going to be added do not increase miss count.
		 * Otherwise several loop iterations will remove the state
		 * recorded earlier. The goal of these heuristics is to have
		 * states from some iterations of the loop (some in the beginning
		 * and some at the end) to help pruning.
		 */
		if (add_new_state)
			sl->miss_cnt++;
		/* heuristic to determine whether this state is beneficial
		 * to keep checking from state equivalence point of view.
		 * Higher numbers increase max_states_per_insn and verification time,
		 * but do not meaningfully decrease insn_processed.
		 * 'n' controls how many times state could miss before eviction.
		 * Use bigger 'n' for checkpoints because evicting checkpoint states
		 * too early would hinder iterator convergence.
		 */
		n = bpf_is_force_checkpoint(env, insn_idx) && sl->state.branches > 0 ? 64 : 3;
		if (sl->miss_cnt > sl->hit_cnt * n + n) {
			/* the state is unlikely to be useful. Remove it to
			 * speed up verification
			 */
			sl->in_free_list = true;
			list_del(&sl->node);
			list_add(&sl->node, &env->free_list);
			env->free_list_size++;
			env->explored_states_size--;
			maybe_free_verifier_state(env, sl);
		}
	}

	if (env->max_states_per_insn < states_cnt)
		env->max_states_per_insn = states_cnt;

	if (!env->bpf_capable && states_cnt > BPF_COMPLEXITY_LIMIT_STATES)
		return 0;

	if (!add_new_state)
		return 0;

	/* There were no equivalent states, remember the current one.
	 * Technically the current state is not proven to be safe yet,
	 * but it will either reach outer most bpf_exit (which means it's safe)
	 * or it will be rejected. When there are no loops the verifier won't be
	 * seeing this tuple (frame[0].callsite, frame[1].callsite, .. insn_idx)
	 * again on the way to bpf_exit.
	 * When looping the sl->state.branches will be > 0 and this state
	 * will not be considered for equivalence until branches == 0.
	 */
	new_sl = kzalloc_obj(struct bpf_verifier_state_list, GFP_KERNEL_ACCOUNT);
	if (!new_sl)
		return -ENOMEM;
	env->total_states++;
	env->explored_states_size++;
	update_peak_states(env);
	env->prev_jmps_processed = env->jmps_processed;
	env->prev_insn_processed = env->insn_processed;

	/* forget precise markings we inherited, see __mark_chain_precision */
	if (env->bpf_capable)
		mark_all_scalars_imprecise(env, cur);

	bpf_clear_singular_ids(env, cur);

	/* add new state to the head of linked list */
	new = &new_sl->state;
	err = bpf_copy_verifier_state(new, cur);
	if (err) {
		bpf_free_verifier_state(new, false);
		kfree(new_sl);
		return err;
	}
	new->insn_idx = insn_idx;
	verifier_bug_if(new->branches != 1, env,
			"%s:branches_to_explore=%d insn %d",
			__func__, new->branches, insn_idx);
	err = maybe_enter_scc(env, new);
	if (err) {
		bpf_free_verifier_state(new, false);
		kfree(new_sl);
		return err;
	}

	cur->parent = new;
	cur->first_insn_idx = insn_idx;
	cur->dfs_depth = new->dfs_depth + 1;
	bpf_clear_jmp_history(cur);
	list_add(&new_sl->node, head);
	return 0;
}
