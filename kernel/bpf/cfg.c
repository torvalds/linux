// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/filter.h>
#include <linux/sort.h>

#define verbose(env, fmt, args...) bpf_verifier_log_write(env, fmt, ##args)

/* non-recursive DFS pseudo code
 * 1  procedure DFS-iterative(G,v):
 * 2      label v as discovered
 * 3      let S be a stack
 * 4      S.push(v)
 * 5      while S is not empty
 * 6            t <- S.peek()
 * 7            if t is what we're looking for:
 * 8                return t
 * 9            for all edges e in G.adjacentEdges(t) do
 * 10               if edge e is already labelled
 * 11                   continue with the next edge
 * 12               w <- G.adjacentVertex(t,e)
 * 13               if vertex w is not discovered and not explored
 * 14                   label e as tree-edge
 * 15                   label w as discovered
 * 16                   S.push(w)
 * 17                   continue at 5
 * 18               else if vertex w is discovered
 * 19                   label e as back-edge
 * 20               else
 * 21                   // vertex w is explored
 * 22                   label e as forward- or cross-edge
 * 23           label t as explored
 * 24           S.pop()
 *
 * convention:
 * 0x10 - discovered
 * 0x11 - discovered and fall-through edge labelled
 * 0x12 - discovered and fall-through and branch edges labelled
 * 0x20 - explored
 */

enum {
	DISCOVERED = 0x10,
	EXPLORED = 0x20,
	FALLTHROUGH = 1,
	BRANCH = 2,
};


static void mark_subprog_changes_pkt_data(struct bpf_verifier_env *env, int off)
{
	struct bpf_subprog_info *subprog;

	subprog = bpf_find_containing_subprog(env, off);
	subprog->changes_pkt_data = true;
}

static void mark_subprog_might_sleep(struct bpf_verifier_env *env, int off)
{
	struct bpf_subprog_info *subprog;

	subprog = bpf_find_containing_subprog(env, off);
	subprog->might_sleep = true;
}

/* 't' is an index of a call-site.
 * 'w' is a callee entry point.
 * Eventually this function would be called when env->cfg.insn_state[w] == EXPLORED.
 * Rely on DFS traversal order and absence of recursive calls to guarantee that
 * callee's change_pkt_data marks would be correct at that moment.
 */
static void merge_callee_effects(struct bpf_verifier_env *env, int t, int w)
{
	struct bpf_subprog_info *caller, *callee;

	caller = bpf_find_containing_subprog(env, t);
	callee = bpf_find_containing_subprog(env, w);
	caller->changes_pkt_data |= callee->changes_pkt_data;
	caller->might_sleep |= callee->might_sleep;
}

enum {
	DONE_EXPLORING = 0,
	KEEP_EXPLORING = 1,
};

/* t, w, e - match pseudo-code above:
 * t - index of current instruction
 * w - next instruction
 * e - edge
 */
static int push_insn(int t, int w, int e, struct bpf_verifier_env *env)
{
	int *insn_stack = env->cfg.insn_stack;
	int *insn_state = env->cfg.insn_state;

	if (e == FALLTHROUGH && insn_state[t] >= (DISCOVERED | FALLTHROUGH))
		return DONE_EXPLORING;

	if (e == BRANCH && insn_state[t] >= (DISCOVERED | BRANCH))
		return DONE_EXPLORING;

	if (w < 0 || w >= env->prog->len) {
		verbose_linfo(env, t, "%d: ", t);
		verbose(env, "jump out of range from insn %d to %d\n", t, w);
		return -EINVAL;
	}

	if (e == BRANCH) {
		/* mark branch target for state pruning */
		mark_prune_point(env, w);
		mark_jmp_point(env, w);
	}

	if (insn_state[w] == 0) {
		/* tree-edge */
		insn_state[t] = DISCOVERED | e;
		insn_state[w] = DISCOVERED;
		if (env->cfg.cur_stack >= env->prog->len)
			return -E2BIG;
		insn_stack[env->cfg.cur_stack++] = w;
		return KEEP_EXPLORING;
	} else if ((insn_state[w] & 0xF0) == DISCOVERED) {
		if (env->bpf_capable)
			return DONE_EXPLORING;
		verbose_linfo(env, t, "%d: ", t);
		verbose_linfo(env, w, "%d: ", w);
		verbose(env, "back-edge from insn %d to %d\n", t, w);
		return -EINVAL;
	} else if (insn_state[w] == EXPLORED) {
		/* forward- or cross-edge */
		insn_state[t] = DISCOVERED | e;
	} else {
		verifier_bug(env, "insn state internal bug");
		return -EFAULT;
	}
	return DONE_EXPLORING;
}

static int visit_func_call_insn(int t, struct bpf_insn *insns,
				struct bpf_verifier_env *env,
				bool visit_callee)
{
	int ret, insn_sz;
	int w;

	insn_sz = bpf_is_ldimm64(&insns[t]) ? 2 : 1;
	ret = push_insn(t, t + insn_sz, FALLTHROUGH, env);
	if (ret)
		return ret;

	mark_prune_point(env, t + insn_sz);
	/* when we exit from subprog, we need to record non-linear history */
	mark_jmp_point(env, t + insn_sz);

	if (visit_callee) {
		w = t + insns[t].imm + 1;
		mark_prune_point(env, t);
		merge_callee_effects(env, t, w);
		ret = push_insn(t, w, BRANCH, env);
	}
	return ret;
}

struct bpf_iarray *bpf_iarray_realloc(struct bpf_iarray *old, size_t n_elem)
{
	size_t new_size = sizeof(struct bpf_iarray) + n_elem * sizeof(old->items[0]);
	struct bpf_iarray *new;

	new = kvrealloc(old, new_size, GFP_KERNEL_ACCOUNT);
	if (!new) {
		/* this is what callers always want, so simplify the call site */
		kvfree(old);
		return NULL;
	}

	new->cnt = n_elem;
	return new;
}

static int copy_insn_array(struct bpf_map *map, u32 start, u32 end, u32 *items)
{
	struct bpf_insn_array_value *value;
	u32 i;

	for (i = start; i <= end; i++) {
		value = map->ops->map_lookup_elem(map, &i);
		/*
		 * map_lookup_elem of an array map will never return an error,
		 * but not checking it makes some static analysers to worry
		 */
		if (IS_ERR(value))
			return PTR_ERR(value);
		else if (!value)
			return -EINVAL;
		items[i - start] = value->xlated_off;
	}
	return 0;
}

static int cmp_ptr_to_u32(const void *a, const void *b)
{
	return *(u32 *)a - *(u32 *)b;
}

static int sort_insn_array_uniq(u32 *items, int cnt)
{
	int unique = 1;
	int i;

	sort(items, cnt, sizeof(items[0]), cmp_ptr_to_u32, NULL);

	for (i = 1; i < cnt; i++)
		if (items[i] != items[unique - 1])
			items[unique++] = items[i];

	return unique;
}

/*
 * sort_unique({map[start], ..., map[end]}) into off
 */
int bpf_copy_insn_array_uniq(struct bpf_map *map, u32 start, u32 end, u32 *off)
{
	u32 n = end - start + 1;
	int err;

	err = copy_insn_array(map, start, end, off);
	if (err)
		return err;

	return sort_insn_array_uniq(off, n);
}

/*
 * Copy all unique offsets from the map
 */
static struct bpf_iarray *jt_from_map(struct bpf_map *map)
{
	struct bpf_iarray *jt;
	int err;
	int n;

	jt = bpf_iarray_realloc(NULL, map->max_entries);
	if (!jt)
		return ERR_PTR(-ENOMEM);

	n = bpf_copy_insn_array_uniq(map, 0, map->max_entries - 1, jt->items);
	if (n < 0) {
		err = n;
		goto err_free;
	}
	if (n == 0) {
		err = -EINVAL;
		goto err_free;
	}
	jt->cnt = n;
	return jt;

err_free:
	kvfree(jt);
	return ERR_PTR(err);
}

/*
 * Find and collect all maps which fit in the subprog. Return the result as one
 * combined jump table in jt->items (allocated with kvcalloc)
 */
static struct bpf_iarray *jt_from_subprog(struct bpf_verifier_env *env,
					  int subprog_start, int subprog_end)
{
	struct bpf_iarray *jt = NULL;
	struct bpf_map *map;
	struct bpf_iarray *jt_cur;
	int i;

	for (i = 0; i < env->insn_array_map_cnt; i++) {
		/*
		 * TODO (when needed): collect only jump tables, not static keys
		 * or maps for indirect calls
		 */
		map = env->insn_array_maps[i];

		jt_cur = jt_from_map(map);
		if (IS_ERR(jt_cur)) {
			kvfree(jt);
			return jt_cur;
		}

		/*
		 * This is enough to check one element. The full table is
		 * checked to fit inside the subprog later in create_jt()
		 */
		if (jt_cur->items[0] >= subprog_start && jt_cur->items[0] < subprog_end) {
			u32 old_cnt = jt ? jt->cnt : 0;
			jt = bpf_iarray_realloc(jt, old_cnt + jt_cur->cnt);
			if (!jt) {
				kvfree(jt_cur);
				return ERR_PTR(-ENOMEM);
			}
			memcpy(jt->items + old_cnt, jt_cur->items, jt_cur->cnt << 2);
		}

		kvfree(jt_cur);
	}

	if (!jt) {
		verbose(env, "no jump tables found for subprog starting at %u\n", subprog_start);
		return ERR_PTR(-EINVAL);
	}

	jt->cnt = sort_insn_array_uniq(jt->items, jt->cnt);
	return jt;
}

static struct bpf_iarray *
create_jt(int t, struct bpf_verifier_env *env)
{
	struct bpf_subprog_info *subprog;
	int subprog_start, subprog_end;
	struct bpf_iarray *jt;
	int i;

	subprog = bpf_find_containing_subprog(env, t);
	subprog_start = subprog->start;
	subprog_end = (subprog + 1)->start;
	jt = jt_from_subprog(env, subprog_start, subprog_end);
	if (IS_ERR(jt))
		return jt;

	/* Check that the every element of the jump table fits within the given subprogram */
	for (i = 0; i < jt->cnt; i++) {
		if (jt->items[i] < subprog_start || jt->items[i] >= subprog_end) {
			verbose(env, "jump table for insn %d points outside of the subprog [%u,%u]\n",
					t, subprog_start, subprog_end);
			kvfree(jt);
			return ERR_PTR(-EINVAL);
		}
	}

	return jt;
}

/* "conditional jump with N edges" */
static int visit_gotox_insn(int t, struct bpf_verifier_env *env)
{
	int *insn_stack = env->cfg.insn_stack;
	int *insn_state = env->cfg.insn_state;
	bool keep_exploring = false;
	struct bpf_iarray *jt;
	int i, w;

	jt = env->insn_aux_data[t].jt;
	if (!jt) {
		jt = create_jt(t, env);
		if (IS_ERR(jt))
			return PTR_ERR(jt);

		env->insn_aux_data[t].jt = jt;
	}

	mark_prune_point(env, t);
	for (i = 0; i < jt->cnt; i++) {
		w = jt->items[i];
		if (w < 0 || w >= env->prog->len) {
			verbose(env, "indirect jump out of range from insn %d to %d\n", t, w);
			return -EINVAL;
		}

		mark_jmp_point(env, w);

		/* EXPLORED || DISCOVERED */
		if (insn_state[w])
			continue;

		if (env->cfg.cur_stack >= env->prog->len)
			return -E2BIG;

		insn_stack[env->cfg.cur_stack++] = w;
		insn_state[w] |= DISCOVERED;
		keep_exploring = true;
	}

	return keep_exploring ? KEEP_EXPLORING : DONE_EXPLORING;
}

/*
 * Instructions that can abnormally return from a subprog (tail_call
 * upon success, ld_{abs,ind} upon load failure) have a hidden exit
 * that the verifier must account for.
 */
static int visit_abnormal_return_insn(struct bpf_verifier_env *env, int t)
{
	struct bpf_subprog_info *subprog;
	struct bpf_iarray *jt;

	if (env->insn_aux_data[t].jt)
		return 0;

	jt = bpf_iarray_realloc(NULL, 2);
	if (!jt)
		return -ENOMEM;

	subprog = bpf_find_containing_subprog(env, t);
	jt->items[0] = t + 1;
	jt->items[1] = subprog->exit_idx;
	env->insn_aux_data[t].jt = jt;
	return 0;
}

/* Visits the instruction at index t and returns one of the following:
 *  < 0 - an error occurred
 *  DONE_EXPLORING - the instruction was fully explored
 *  KEEP_EXPLORING - there is still work to be done before it is fully explored
 */
static int visit_insn(int t, struct bpf_verifier_env *env)
{
	struct bpf_insn *insns = env->prog->insnsi, *insn = &insns[t];
	int ret, off, insn_sz;

	if (bpf_pseudo_func(insn))
		return visit_func_call_insn(t, insns, env, true);

	/* All non-branch instructions have a single fall-through edge. */
	if (BPF_CLASS(insn->code) != BPF_JMP &&
	    BPF_CLASS(insn->code) != BPF_JMP32) {
		if (BPF_CLASS(insn->code) == BPF_LD &&
		    (BPF_MODE(insn->code) == BPF_ABS ||
		     BPF_MODE(insn->code) == BPF_IND)) {
			ret = visit_abnormal_return_insn(env, t);
			if (ret)
				return ret;
		}
		insn_sz = bpf_is_ldimm64(insn) ? 2 : 1;
		return push_insn(t, t + insn_sz, FALLTHROUGH, env);
	}

	switch (BPF_OP(insn->code)) {
	case BPF_EXIT:
		return DONE_EXPLORING;

	case BPF_CALL:
		if (bpf_is_async_callback_calling_insn(insn))
			/* Mark this call insn as a prune point to trigger
			 * is_state_visited() check before call itself is
			 * processed by __check_func_call(). Otherwise new
			 * async state will be pushed for further exploration.
			 */
			mark_prune_point(env, t);
		/* For functions that invoke callbacks it is not known how many times
		 * callback would be called. Verifier models callback calling functions
		 * by repeatedly visiting callback bodies and returning to origin call
		 * instruction.
		 * In order to stop such iteration verifier needs to identify when a
		 * state identical some state from a previous iteration is reached.
		 * Check below forces creation of checkpoint before callback calling
		 * instruction to allow search for such identical states.
		 */
		if (bpf_is_sync_callback_calling_insn(insn)) {
			mark_calls_callback(env, t);
			mark_force_checkpoint(env, t);
			mark_prune_point(env, t);
			mark_jmp_point(env, t);
		}
		if (bpf_helper_call(insn)) {
			const struct bpf_func_proto *fp;

			ret = bpf_get_helper_proto(env, insn->imm, &fp);
			/* If called in a non-sleepable context program will be
			 * rejected anyway, so we should end up with precise
			 * sleepable marks on subprogs, except for dead code
			 * elimination.
			 */
			if (ret == 0 && fp->might_sleep)
				mark_subprog_might_sleep(env, t);
			if (bpf_helper_changes_pkt_data(insn->imm))
				mark_subprog_changes_pkt_data(env, t);
			if (insn->imm == BPF_FUNC_tail_call) {
				ret = visit_abnormal_return_insn(env, t);
				if (ret)
					return ret;
			}
		} else if (insn->src_reg == BPF_PSEUDO_KFUNC_CALL) {
			struct bpf_kfunc_call_arg_meta meta;

			ret = bpf_fetch_kfunc_arg_meta(env, insn->imm, insn->off, &meta);
			if (ret == 0 && bpf_is_iter_next_kfunc(&meta)) {
				mark_prune_point(env, t);
				/* Checking and saving state checkpoints at iter_next() call
				 * is crucial for fast convergence of open-coded iterator loop
				 * logic, so we need to force it. If we don't do that,
				 * is_state_visited() might skip saving a checkpoint, causing
				 * unnecessarily long sequence of not checkpointed
				 * instructions and jumps, leading to exhaustion of jump
				 * history buffer, and potentially other undesired outcomes.
				 * It is expected that with correct open-coded iterators
				 * convergence will happen quickly, so we don't run a risk of
				 * exhausting memory.
				 */
				mark_force_checkpoint(env, t);
			}
			/* Same as helpers, if called in a non-sleepable context
			 * program will be rejected anyway, so we should end up
			 * with precise sleepable marks on subprogs, except for
			 * dead code elimination.
			 */
			if (ret == 0 && bpf_is_kfunc_sleepable(&meta))
				mark_subprog_might_sleep(env, t);
			if (ret == 0 && bpf_is_kfunc_pkt_changing(&meta))
				mark_subprog_changes_pkt_data(env, t);
		}
		return visit_func_call_insn(t, insns, env, insn->src_reg == BPF_PSEUDO_CALL);

	case BPF_JA:
		if (BPF_SRC(insn->code) == BPF_X)
			return visit_gotox_insn(t, env);

		if (BPF_CLASS(insn->code) == BPF_JMP)
			off = insn->off;
		else
			off = insn->imm;

		/* unconditional jump with single edge */
		ret = push_insn(t, t + off + 1, FALLTHROUGH, env);
		if (ret)
			return ret;

		mark_prune_point(env, t + off + 1);
		mark_jmp_point(env, t + off + 1);

		return ret;

	default:
		/* conditional jump with two edges */
		mark_prune_point(env, t);
		if (bpf_is_may_goto_insn(insn))
			mark_force_checkpoint(env, t);

		ret = push_insn(t, t + 1, FALLTHROUGH, env);
		if (ret)
			return ret;

		return push_insn(t, t + insn->off + 1, BRANCH, env);
	}
}

/* non-recursive depth-first-search to detect loops in BPF program
 * loop == back-edge in directed graph
 */
int bpf_check_cfg(struct bpf_verifier_env *env)
{
	int insn_cnt = env->prog->len;
	int *insn_stack, *insn_state;
	int ex_insn_beg, i, ret = 0;

	insn_state = env->cfg.insn_state = kvzalloc_objs(int, insn_cnt,
							 GFP_KERNEL_ACCOUNT);
	if (!insn_state)
		return -ENOMEM;

	insn_stack = env->cfg.insn_stack = kvzalloc_objs(int, insn_cnt,
							 GFP_KERNEL_ACCOUNT);
	if (!insn_stack) {
		kvfree(insn_state);
		return -ENOMEM;
	}

	ex_insn_beg = env->exception_callback_subprog
		      ? env->subprog_info[env->exception_callback_subprog].start
		      : 0;

	insn_state[0] = DISCOVERED; /* mark 1st insn as discovered */
	insn_stack[0] = 0; /* 0 is the first instruction */
	env->cfg.cur_stack = 1;

walk_cfg:
	while (env->cfg.cur_stack > 0) {
		int t = insn_stack[env->cfg.cur_stack - 1];

		ret = visit_insn(t, env);
		switch (ret) {
		case DONE_EXPLORING:
			insn_state[t] = EXPLORED;
			env->cfg.cur_stack--;
			break;
		case KEEP_EXPLORING:
			break;
		default:
			if (ret > 0) {
				verifier_bug(env, "visit_insn internal bug");
				ret = -EFAULT;
			}
			goto err_free;
		}
	}

	if (env->cfg.cur_stack < 0) {
		verifier_bug(env, "pop stack internal bug");
		ret = -EFAULT;
		goto err_free;
	}

	if (ex_insn_beg && insn_state[ex_insn_beg] != EXPLORED) {
		insn_state[ex_insn_beg] = DISCOVERED;
		insn_stack[0] = ex_insn_beg;
		env->cfg.cur_stack = 1;
		goto walk_cfg;
	}

	for (i = 0; i < insn_cnt; i++) {
		struct bpf_insn *insn = &env->prog->insnsi[i];

		if (insn_state[i] != EXPLORED) {
			verbose(env, "unreachable insn %d\n", i);
			ret = -EINVAL;
			goto err_free;
		}
		if (bpf_is_ldimm64(insn)) {
			if (insn_state[i + 1] != 0) {
				verbose(env, "jump into the middle of ldimm64 insn %d\n", i);
				ret = -EINVAL;
				goto err_free;
			}
			i++; /* skip second half of ldimm64 */
		}
	}
	ret = 0; /* cfg looks good */
	env->prog->aux->changes_pkt_data = env->subprog_info[0].changes_pkt_data;
	env->prog->aux->might_sleep = env->subprog_info[0].might_sleep;

err_free:
	kvfree(insn_state);
	kvfree(insn_stack);
	env->cfg.insn_state = env->cfg.insn_stack = NULL;
	return ret;
}

/*
 * For each subprogram 'i' fill array env->cfg.insn_subprogram sub-range
 * [env->subprog_info[i].postorder_start, env->subprog_info[i+1].postorder_start)
 * with indices of 'i' instructions in postorder.
 */
int bpf_compute_postorder(struct bpf_verifier_env *env)
{
	u32 cur_postorder, i, top, stack_sz, s;
	int *stack = NULL, *postorder = NULL, *state = NULL;
	struct bpf_iarray *succ;

	postorder = kvzalloc_objs(int, env->prog->len, GFP_KERNEL_ACCOUNT);
	state = kvzalloc_objs(int, env->prog->len, GFP_KERNEL_ACCOUNT);
	stack = kvzalloc_objs(int, env->prog->len, GFP_KERNEL_ACCOUNT);
	if (!postorder || !state || !stack) {
		kvfree(postorder);
		kvfree(state);
		kvfree(stack);
		return -ENOMEM;
	}
	cur_postorder = 0;
	for (i = 0; i < env->subprog_cnt; i++) {
		env->subprog_info[i].postorder_start = cur_postorder;
		stack[0] = env->subprog_info[i].start;
		stack_sz = 1;
		do {
			top = stack[stack_sz - 1];
			state[top] |= DISCOVERED;
			if (state[top] & EXPLORED) {
				postorder[cur_postorder++] = top;
				stack_sz--;
				continue;
			}
			succ = bpf_insn_successors(env, top);
			for (s = 0; s < succ->cnt; ++s) {
				if (!state[succ->items[s]]) {
					stack[stack_sz++] = succ->items[s];
					state[succ->items[s]] |= DISCOVERED;
				}
			}
			state[top] |= EXPLORED;
		} while (stack_sz);
	}
	env->subprog_info[i].postorder_start = cur_postorder;
	env->cfg.insn_postorder = postorder;
	env->cfg.cur_postorder = cur_postorder;
	kvfree(stack);
	kvfree(state);
	return 0;
}

/*
 * Compute strongly connected components (SCCs) on the CFG.
 * Assign an SCC number to each instruction, recorded in env->insn_aux[*].scc.
 * If instruction is a sole member of its SCC and there are no self edges,
 * assign it SCC number of zero.
 * Uses a non-recursive adaptation of Tarjan's algorithm for SCC computation.
 */
int bpf_compute_scc(struct bpf_verifier_env *env)
{
	const u32 NOT_ON_STACK = U32_MAX;

	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	const u32 insn_cnt = env->prog->len;
	int stack_sz, dfs_sz, err = 0;
	u32 *stack, *pre, *low, *dfs;
	u32 i, j, t, w;
	u32 next_preorder_num;
	u32 next_scc_id;
	bool assign_scc;
	struct bpf_iarray *succ;

	next_preorder_num = 1;
	next_scc_id = 1;
	/*
	 * - 'stack' accumulates vertices in DFS order, see invariant comment below;
	 * - 'pre[t] == p' => preorder number of vertex 't' is 'p';
	 * - 'low[t] == n' => smallest preorder number of the vertex reachable from 't' is 'n';
	 * - 'dfs' DFS traversal stack, used to emulate explicit recursion.
	 */
	stack = kvcalloc(insn_cnt, sizeof(int), GFP_KERNEL_ACCOUNT);
	pre = kvcalloc(insn_cnt, sizeof(int), GFP_KERNEL_ACCOUNT);
	low = kvcalloc(insn_cnt, sizeof(int), GFP_KERNEL_ACCOUNT);
	dfs = kvcalloc(insn_cnt, sizeof(*dfs), GFP_KERNEL_ACCOUNT);
	if (!stack || !pre || !low || !dfs) {
		err = -ENOMEM;
		goto exit;
	}
	/*
	 * References:
	 * [1] R. Tarjan "Depth-First Search and Linear Graph Algorithms"
	 * [2] D. J. Pearce "A Space-Efficient Algorithm for Finding Strongly Connected Components"
	 *
	 * The algorithm maintains the following invariant:
	 * - suppose there is a path 'u' ~> 'v', such that 'pre[v] < pre[u]';
	 * - then, vertex 'u' remains on stack while vertex 'v' is on stack.
	 *
	 * Consequently:
	 * - If 'low[v] < pre[v]', there is a path from 'v' to some vertex 'u',
	 *   such that 'pre[u] == low[v]'; vertex 'u' is currently on the stack,
	 *   and thus there is an SCC (loop) containing both 'u' and 'v'.
	 * - If 'low[v] == pre[v]', loops containing 'v' have been explored,
	 *   and 'v' can be considered the root of some SCC.
	 *
	 * Here is a pseudo-code for an explicitly recursive version of the algorithm:
	 *
	 *    NOT_ON_STACK = insn_cnt + 1
	 *    pre = [0] * insn_cnt
	 *    low = [0] * insn_cnt
	 *    scc = [0] * insn_cnt
	 *    stack = []
	 *
	 *    next_preorder_num = 1
	 *    next_scc_id = 1
	 *
	 *    def recur(w):
	 *        nonlocal next_preorder_num
	 *        nonlocal next_scc_id
	 *
	 *        pre[w] = next_preorder_num
	 *        low[w] = next_preorder_num
	 *        next_preorder_num += 1
	 *        stack.append(w)
	 *        for s in successors(w):
	 *            # Note: for classic algorithm the block below should look as:
	 *            #
	 *            # if pre[s] == 0:
	 *            #     recur(s)
	 *            #     low[w] = min(low[w], low[s])
	 *            # elif low[s] != NOT_ON_STACK:
	 *            #     low[w] = min(low[w], pre[s])
	 *            #
	 *            # But replacing both 'min' instructions with 'low[w] = min(low[w], low[s])'
	 *            # does not break the invariant and makes iterative version of the algorithm
	 *            # simpler. See 'Algorithm #3' from [2].
	 *
	 *            # 's' not yet visited
	 *            if pre[s] == 0:
	 *                recur(s)
	 *            # if 's' is on stack, pick lowest reachable preorder number from it;
	 *            # if 's' is not on stack 'low[s] == NOT_ON_STACK > low[w]',
	 *            # so 'min' would be a noop.
	 *            low[w] = min(low[w], low[s])
	 *
	 *        if low[w] == pre[w]:
	 *            # 'w' is the root of an SCC, pop all vertices
	 *            # below 'w' on stack and assign same SCC to them.
	 *            while True:
	 *                t = stack.pop()
	 *                low[t] = NOT_ON_STACK
	 *                scc[t] = next_scc_id
	 *                if t == w:
	 *                    break
	 *            next_scc_id += 1
	 *
	 *    for i in range(0, insn_cnt):
	 *        if pre[i] == 0:
	 *            recur(i)
	 *
	 * Below implementation replaces explicit recursion with array 'dfs'.
	 */
	for (i = 0; i < insn_cnt; i++) {
		if (pre[i])
			continue;
		stack_sz = 0;
		dfs_sz = 1;
		dfs[0] = i;
dfs_continue:
		while (dfs_sz) {
			w = dfs[dfs_sz - 1];
			if (pre[w] == 0) {
				low[w] = next_preorder_num;
				pre[w] = next_preorder_num;
				next_preorder_num++;
				stack[stack_sz++] = w;
			}
			/* Visit 'w' successors */
			succ = bpf_insn_successors(env, w);
			for (j = 0; j < succ->cnt; ++j) {
				if (pre[succ->items[j]]) {
					low[w] = min(low[w], low[succ->items[j]]);
				} else {
					dfs[dfs_sz++] = succ->items[j];
					goto dfs_continue;
				}
			}
			/*
			 * Preserve the invariant: if some vertex above in the stack
			 * is reachable from 'w', keep 'w' on the stack.
			 */
			if (low[w] < pre[w]) {
				dfs_sz--;
				goto dfs_continue;
			}
			/*
			 * Assign SCC number only if component has two or more elements,
			 * or if component has a self reference, or if instruction is a
			 * callback calling function (implicit loop).
			 */
			assign_scc = stack[stack_sz - 1] != w;	/* two or more elements? */
			for (j = 0; j < succ->cnt; ++j) {	/* self reference? */
				if (succ->items[j] == w) {
					assign_scc = true;
					break;
				}
			}
			if (bpf_calls_callback(env, w)) /* implicit loop? */
				assign_scc = true;
			/* Pop component elements from stack */
			do {
				t = stack[--stack_sz];
				low[t] = NOT_ON_STACK;
				if (assign_scc)
					aux[t].scc = next_scc_id;
			} while (t != w);
			if (assign_scc)
				next_scc_id++;
			dfs_sz--;
		}
	}
	env->scc_info = kvzalloc_objs(*env->scc_info, next_scc_id,
				      GFP_KERNEL_ACCOUNT);
	if (!env->scc_info) {
		err = -ENOMEM;
		goto exit;
	}
	env->scc_cnt = next_scc_id;
exit:
	kvfree(stack);
	kvfree(pre);
	kvfree(low);
	kvfree(dfs);
	return err;
}
