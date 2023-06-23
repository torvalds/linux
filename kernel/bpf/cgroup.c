// SPDX-License-Identifier: GPL-2.0-only
/*
 * Functions to manage eBPF programs attached to cgroups
 *
 * Copyright (c) 2016 Daniel Mack
 */

#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/cgroup.h>
#include <linux/filter.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/string.h>
#include <linux/bpf.h>
#include <linux/bpf-cgroup.h>
#include <linux/bpf_lsm.h>
#include <linux/bpf_verifier.h>
#include <net/sock.h>
#include <net/bpf_sk_storage.h>

#include "../cgroup/cgroup-internal.h"

DEFINE_STATIC_KEY_ARRAY_FALSE(cgroup_bpf_enabled_key, MAX_CGROUP_BPF_ATTACH_TYPE);
EXPORT_SYMBOL(cgroup_bpf_enabled_key);

/* __always_inline is necessary to prevent indirect call through run_prog
 * function pointer.
 */
static __always_inline int
bpf_prog_run_array_cg(const struct cgroup_bpf *cgrp,
		      enum cgroup_bpf_attach_type atype,
		      const void *ctx, bpf_prog_run_fn run_prog,
		      int retval, u32 *ret_flags)
{
	const struct bpf_prog_array_item *item;
	const struct bpf_prog *prog;
	const struct bpf_prog_array *array;
	struct bpf_run_ctx *old_run_ctx;
	struct bpf_cg_run_ctx run_ctx;
	u32 func_ret;

	run_ctx.retval = retval;
	migrate_disable();
	rcu_read_lock();
	array = rcu_dereference(cgrp->effective[atype]);
	item = &array->items[0];
	old_run_ctx = bpf_set_run_ctx(&run_ctx.run_ctx);
	while ((prog = READ_ONCE(item->prog))) {
		run_ctx.prog_item = item;
		func_ret = run_prog(prog, ctx);
		if (ret_flags) {
			*(ret_flags) |= (func_ret >> 1);
			func_ret &= 1;
		}
		if (!func_ret && !IS_ERR_VALUE((long)run_ctx.retval))
			run_ctx.retval = -EPERM;
		item++;
	}
	bpf_reset_run_ctx(old_run_ctx);
	rcu_read_unlock();
	migrate_enable();
	return run_ctx.retval;
}

unsigned int __cgroup_bpf_run_lsm_sock(const void *ctx,
				       const struct bpf_insn *insn)
{
	const struct bpf_prog *shim_prog;
	struct sock *sk;
	struct cgroup *cgrp;
	int ret = 0;
	u64 *args;

	args = (u64 *)ctx;
	sk = (void *)(unsigned long)args[0];
	/*shim_prog = container_of(insn, struct bpf_prog, insnsi);*/
	shim_prog = (const struct bpf_prog *)((void *)insn - offsetof(struct bpf_prog, insnsi));

	cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	if (likely(cgrp))
		ret = bpf_prog_run_array_cg(&cgrp->bpf,
					    shim_prog->aux->cgroup_atype,
					    ctx, bpf_prog_run, 0, NULL);
	return ret;
}

unsigned int __cgroup_bpf_run_lsm_socket(const void *ctx,
					 const struct bpf_insn *insn)
{
	const struct bpf_prog *shim_prog;
	struct socket *sock;
	struct cgroup *cgrp;
	int ret = 0;
	u64 *args;

	args = (u64 *)ctx;
	sock = (void *)(unsigned long)args[0];
	/*shim_prog = container_of(insn, struct bpf_prog, insnsi);*/
	shim_prog = (const struct bpf_prog *)((void *)insn - offsetof(struct bpf_prog, insnsi));

	cgrp = sock_cgroup_ptr(&sock->sk->sk_cgrp_data);
	if (likely(cgrp))
		ret = bpf_prog_run_array_cg(&cgrp->bpf,
					    shim_prog->aux->cgroup_atype,
					    ctx, bpf_prog_run, 0, NULL);
	return ret;
}

unsigned int __cgroup_bpf_run_lsm_current(const void *ctx,
					  const struct bpf_insn *insn)
{
	const struct bpf_prog *shim_prog;
	struct cgroup *cgrp;
	int ret = 0;

	/*shim_prog = container_of(insn, struct bpf_prog, insnsi);*/
	shim_prog = (const struct bpf_prog *)((void *)insn - offsetof(struct bpf_prog, insnsi));

	/* We rely on trampoline's __bpf_prog_enter_lsm_cgroup to grab RCU read lock. */
	cgrp = task_dfl_cgroup(current);
	if (likely(cgrp))
		ret = bpf_prog_run_array_cg(&cgrp->bpf,
					    shim_prog->aux->cgroup_atype,
					    ctx, bpf_prog_run, 0, NULL);
	return ret;
}

#ifdef CONFIG_BPF_LSM
struct cgroup_lsm_atype {
	u32 attach_btf_id;
	int refcnt;
};

static struct cgroup_lsm_atype cgroup_lsm_atype[CGROUP_LSM_NUM];

static enum cgroup_bpf_attach_type
bpf_cgroup_atype_find(enum bpf_attach_type attach_type, u32 attach_btf_id)
{
	int i;

	lockdep_assert_held(&cgroup_mutex);

	if (attach_type != BPF_LSM_CGROUP)
		return to_cgroup_bpf_attach_type(attach_type);

	for (i = 0; i < ARRAY_SIZE(cgroup_lsm_atype); i++)
		if (cgroup_lsm_atype[i].attach_btf_id == attach_btf_id)
			return CGROUP_LSM_START + i;

	for (i = 0; i < ARRAY_SIZE(cgroup_lsm_atype); i++)
		if (cgroup_lsm_atype[i].attach_btf_id == 0)
			return CGROUP_LSM_START + i;

	return -E2BIG;

}

void bpf_cgroup_atype_get(u32 attach_btf_id, int cgroup_atype)
{
	int i = cgroup_atype - CGROUP_LSM_START;

	lockdep_assert_held(&cgroup_mutex);

	WARN_ON_ONCE(cgroup_lsm_atype[i].attach_btf_id &&
		     cgroup_lsm_atype[i].attach_btf_id != attach_btf_id);

	cgroup_lsm_atype[i].attach_btf_id = attach_btf_id;
	cgroup_lsm_atype[i].refcnt++;
}

void bpf_cgroup_atype_put(int cgroup_atype)
{
	int i = cgroup_atype - CGROUP_LSM_START;

	mutex_lock(&cgroup_mutex);
	if (--cgroup_lsm_atype[i].refcnt <= 0)
		cgroup_lsm_atype[i].attach_btf_id = 0;
	WARN_ON_ONCE(cgroup_lsm_atype[i].refcnt < 0);
	mutex_unlock(&cgroup_mutex);
}
#else
static enum cgroup_bpf_attach_type
bpf_cgroup_atype_find(enum bpf_attach_type attach_type, u32 attach_btf_id)
{
	if (attach_type != BPF_LSM_CGROUP)
		return to_cgroup_bpf_attach_type(attach_type);
	return -EOPNOTSUPP;
}
#endif /* CONFIG_BPF_LSM */

void cgroup_bpf_offline(struct cgroup *cgrp)
{
	cgroup_get(cgrp);
	percpu_ref_kill(&cgrp->bpf.refcnt);
}

static void bpf_cgroup_storages_free(struct bpf_cgroup_storage *storages[])
{
	enum bpf_cgroup_storage_type stype;

	for_each_cgroup_storage_type(stype)
		bpf_cgroup_storage_free(storages[stype]);
}

static int bpf_cgroup_storages_alloc(struct bpf_cgroup_storage *storages[],
				     struct bpf_cgroup_storage *new_storages[],
				     enum bpf_attach_type type,
				     struct bpf_prog *prog,
				     struct cgroup *cgrp)
{
	enum bpf_cgroup_storage_type stype;
	struct bpf_cgroup_storage_key key;
	struct bpf_map *map;

	key.cgroup_inode_id = cgroup_id(cgrp);
	key.attach_type = type;

	for_each_cgroup_storage_type(stype) {
		map = prog->aux->cgroup_storage[stype];
		if (!map)
			continue;

		storages[stype] = cgroup_storage_lookup((void *)map, &key, false);
		if (storages[stype])
			continue;

		storages[stype] = bpf_cgroup_storage_alloc(prog, stype);
		if (IS_ERR(storages[stype])) {
			bpf_cgroup_storages_free(new_storages);
			return -ENOMEM;
		}

		new_storages[stype] = storages[stype];
	}

	return 0;
}

static void bpf_cgroup_storages_assign(struct bpf_cgroup_storage *dst[],
				       struct bpf_cgroup_storage *src[])
{
	enum bpf_cgroup_storage_type stype;

	for_each_cgroup_storage_type(stype)
		dst[stype] = src[stype];
}

static void bpf_cgroup_storages_link(struct bpf_cgroup_storage *storages[],
				     struct cgroup *cgrp,
				     enum bpf_attach_type attach_type)
{
	enum bpf_cgroup_storage_type stype;

	for_each_cgroup_storage_type(stype)
		bpf_cgroup_storage_link(storages[stype], cgrp, attach_type);
}

/* Called when bpf_cgroup_link is auto-detached from dying cgroup.
 * It drops cgroup and bpf_prog refcounts, and marks bpf_link as defunct. It
 * doesn't free link memory, which will eventually be done by bpf_link's
 * release() callback, when its last FD is closed.
 */
static void bpf_cgroup_link_auto_detach(struct bpf_cgroup_link *link)
{
	cgroup_put(link->cgroup);
	link->cgroup = NULL;
}

/**
 * cgroup_bpf_release() - put references of all bpf programs and
 *                        release all cgroup bpf data
 * @work: work structure embedded into the cgroup to modify
 */
static void cgroup_bpf_release(struct work_struct *work)
{
	struct cgroup *p, *cgrp = container_of(work, struct cgroup,
					       bpf.release_work);
	struct bpf_prog_array *old_array;
	struct list_head *storages = &cgrp->bpf.storages;
	struct bpf_cgroup_storage *storage, *stmp;

	unsigned int atype;

	mutex_lock(&cgroup_mutex);

	for (atype = 0; atype < ARRAY_SIZE(cgrp->bpf.progs); atype++) {
		struct hlist_head *progs = &cgrp->bpf.progs[atype];
		struct bpf_prog_list *pl;
		struct hlist_node *pltmp;

		hlist_for_each_entry_safe(pl, pltmp, progs, node) {
			hlist_del(&pl->node);
			if (pl->prog) {
				if (pl->prog->expected_attach_type == BPF_LSM_CGROUP)
					bpf_trampoline_unlink_cgroup_shim(pl->prog);
				bpf_prog_put(pl->prog);
			}
			if (pl->link) {
				if (pl->link->link.prog->expected_attach_type == BPF_LSM_CGROUP)
					bpf_trampoline_unlink_cgroup_shim(pl->link->link.prog);
				bpf_cgroup_link_auto_detach(pl->link);
			}
			kfree(pl);
			static_branch_dec(&cgroup_bpf_enabled_key[atype]);
		}
		old_array = rcu_dereference_protected(
				cgrp->bpf.effective[atype],
				lockdep_is_held(&cgroup_mutex));
		bpf_prog_array_free(old_array);
	}

	list_for_each_entry_safe(storage, stmp, storages, list_cg) {
		bpf_cgroup_storage_unlink(storage);
		bpf_cgroup_storage_free(storage);
	}

	mutex_unlock(&cgroup_mutex);

	for (p = cgroup_parent(cgrp); p; p = cgroup_parent(p))
		cgroup_bpf_put(p);

	percpu_ref_exit(&cgrp->bpf.refcnt);
	cgroup_put(cgrp);
}

/**
 * cgroup_bpf_release_fn() - callback used to schedule releasing
 *                           of bpf cgroup data
 * @ref: percpu ref counter structure
 */
static void cgroup_bpf_release_fn(struct percpu_ref *ref)
{
	struct cgroup *cgrp = container_of(ref, struct cgroup, bpf.refcnt);

	INIT_WORK(&cgrp->bpf.release_work, cgroup_bpf_release);
	queue_work(system_wq, &cgrp->bpf.release_work);
}

/* Get underlying bpf_prog of bpf_prog_list entry, regardless if it's through
 * link or direct prog.
 */
static struct bpf_prog *prog_list_prog(struct bpf_prog_list *pl)
{
	if (pl->prog)
		return pl->prog;
	if (pl->link)
		return pl->link->link.prog;
	return NULL;
}

/* count number of elements in the list.
 * it's slow but the list cannot be long
 */
static u32 prog_list_length(struct hlist_head *head)
{
	struct bpf_prog_list *pl;
	u32 cnt = 0;

	hlist_for_each_entry(pl, head, node) {
		if (!prog_list_prog(pl))
			continue;
		cnt++;
	}
	return cnt;
}

/* if parent has non-overridable prog attached,
 * disallow attaching new programs to the descendent cgroup.
 * if parent has overridable or multi-prog, allow attaching
 */
static bool hierarchy_allows_attach(struct cgroup *cgrp,
				    enum cgroup_bpf_attach_type atype)
{
	struct cgroup *p;

	p = cgroup_parent(cgrp);
	if (!p)
		return true;
	do {
		u32 flags = p->bpf.flags[atype];
		u32 cnt;

		if (flags & BPF_F_ALLOW_MULTI)
			return true;
		cnt = prog_list_length(&p->bpf.progs[atype]);
		WARN_ON_ONCE(cnt > 1);
		if (cnt == 1)
			return !!(flags & BPF_F_ALLOW_OVERRIDE);
		p = cgroup_parent(p);
	} while (p);
	return true;
}

/* compute a chain of effective programs for a given cgroup:
 * start from the list of programs in this cgroup and add
 * all parent programs.
 * Note that parent's F_ALLOW_OVERRIDE-type program is yielding
 * to programs in this cgroup
 */
static int compute_effective_progs(struct cgroup *cgrp,
				   enum cgroup_bpf_attach_type atype,
				   struct bpf_prog_array **array)
{
	struct bpf_prog_array_item *item;
	struct bpf_prog_array *progs;
	struct bpf_prog_list *pl;
	struct cgroup *p = cgrp;
	int cnt = 0;

	/* count number of effective programs by walking parents */
	do {
		if (cnt == 0 || (p->bpf.flags[atype] & BPF_F_ALLOW_MULTI))
			cnt += prog_list_length(&p->bpf.progs[atype]);
		p = cgroup_parent(p);
	} while (p);

	progs = bpf_prog_array_alloc(cnt, GFP_KERNEL);
	if (!progs)
		return -ENOMEM;

	/* populate the array with effective progs */
	cnt = 0;
	p = cgrp;
	do {
		if (cnt > 0 && !(p->bpf.flags[atype] & BPF_F_ALLOW_MULTI))
			continue;

		hlist_for_each_entry(pl, &p->bpf.progs[atype], node) {
			if (!prog_list_prog(pl))
				continue;

			item = &progs->items[cnt];
			item->prog = prog_list_prog(pl);
			bpf_cgroup_storages_assign(item->cgroup_storage,
						   pl->storage);
			cnt++;
		}
	} while ((p = cgroup_parent(p)));

	*array = progs;
	return 0;
}

static void activate_effective_progs(struct cgroup *cgrp,
				     enum cgroup_bpf_attach_type atype,
				     struct bpf_prog_array *old_array)
{
	old_array = rcu_replace_pointer(cgrp->bpf.effective[atype], old_array,
					lockdep_is_held(&cgroup_mutex));
	/* free prog array after grace period, since __cgroup_bpf_run_*()
	 * might be still walking the array
	 */
	bpf_prog_array_free(old_array);
}

/**
 * cgroup_bpf_inherit() - inherit effective programs from parent
 * @cgrp: the cgroup to modify
 */
int cgroup_bpf_inherit(struct cgroup *cgrp)
{
/* has to use marco instead of const int, since compiler thinks
 * that array below is variable length
 */
#define	NR ARRAY_SIZE(cgrp->bpf.effective)
	struct bpf_prog_array *arrays[NR] = {};
	struct cgroup *p;
	int ret, i;

	ret = percpu_ref_init(&cgrp->bpf.refcnt, cgroup_bpf_release_fn, 0,
			      GFP_KERNEL);
	if (ret)
		return ret;

	for (p = cgroup_parent(cgrp); p; p = cgroup_parent(p))
		cgroup_bpf_get(p);

	for (i = 0; i < NR; i++)
		INIT_HLIST_HEAD(&cgrp->bpf.progs[i]);

	INIT_LIST_HEAD(&cgrp->bpf.storages);

	for (i = 0; i < NR; i++)
		if (compute_effective_progs(cgrp, i, &arrays[i]))
			goto cleanup;

	for (i = 0; i < NR; i++)
		activate_effective_progs(cgrp, i, arrays[i]);

	return 0;
cleanup:
	for (i = 0; i < NR; i++)
		bpf_prog_array_free(arrays[i]);

	for (p = cgroup_parent(cgrp); p; p = cgroup_parent(p))
		cgroup_bpf_put(p);

	percpu_ref_exit(&cgrp->bpf.refcnt);

	return -ENOMEM;
}

static int update_effective_progs(struct cgroup *cgrp,
				  enum cgroup_bpf_attach_type atype)
{
	struct cgroup_subsys_state *css;
	int err;

	/* allocate and recompute effective prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		if (percpu_ref_is_zero(&desc->bpf.refcnt))
			continue;

		err = compute_effective_progs(desc, atype, &desc->bpf.inactive);
		if (err)
			goto cleanup;
	}

	/* all allocations were successful. Activate all prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		if (percpu_ref_is_zero(&desc->bpf.refcnt)) {
			if (unlikely(desc->bpf.inactive)) {
				bpf_prog_array_free(desc->bpf.inactive);
				desc->bpf.inactive = NULL;
			}
			continue;
		}

		activate_effective_progs(desc, atype, desc->bpf.inactive);
		desc->bpf.inactive = NULL;
	}

	return 0;

cleanup:
	/* oom while computing effective. Free all computed effective arrays
	 * since they were not activated
	 */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		bpf_prog_array_free(desc->bpf.inactive);
		desc->bpf.inactive = NULL;
	}

	return err;
}

#define BPF_CGROUP_MAX_PROGS 64

static struct bpf_prog_list *find_attach_entry(struct hlist_head *progs,
					       struct bpf_prog *prog,
					       struct bpf_cgroup_link *link,
					       struct bpf_prog *replace_prog,
					       bool allow_multi)
{
	struct bpf_prog_list *pl;

	/* single-attach case */
	if (!allow_multi) {
		if (hlist_empty(progs))
			return NULL;
		return hlist_entry(progs->first, typeof(*pl), node);
	}

	hlist_for_each_entry(pl, progs, node) {
		if (prog && pl->prog == prog && prog != replace_prog)
			/* disallow attaching the same prog twice */
			return ERR_PTR(-EINVAL);
		if (link && pl->link == link)
			/* disallow attaching the same link twice */
			return ERR_PTR(-EINVAL);
	}

	/* direct prog multi-attach w/ replacement case */
	if (replace_prog) {
		hlist_for_each_entry(pl, progs, node) {
			if (pl->prog == replace_prog)
				/* a match found */
				return pl;
		}
		/* prog to replace not found for cgroup */
		return ERR_PTR(-ENOENT);
	}

	return NULL;
}

/**
 * __cgroup_bpf_attach() - Attach the program or the link to a cgroup, and
 *                         propagate the change to descendants
 * @cgrp: The cgroup which descendants to traverse
 * @prog: A program to attach
 * @link: A link to attach
 * @replace_prog: Previously attached program to replace if BPF_F_REPLACE is set
 * @type: Type of attach operation
 * @flags: Option flags
 *
 * Exactly one of @prog or @link can be non-null.
 * Must be called with cgroup_mutex held.
 */
static int __cgroup_bpf_attach(struct cgroup *cgrp,
			       struct bpf_prog *prog, struct bpf_prog *replace_prog,
			       struct bpf_cgroup_link *link,
			       enum bpf_attach_type type, u32 flags)
{
	u32 saved_flags = (flags & (BPF_F_ALLOW_OVERRIDE | BPF_F_ALLOW_MULTI));
	struct bpf_prog *old_prog = NULL;
	struct bpf_cgroup_storage *storage[MAX_BPF_CGROUP_STORAGE_TYPE] = {};
	struct bpf_cgroup_storage *new_storage[MAX_BPF_CGROUP_STORAGE_TYPE] = {};
	struct bpf_prog *new_prog = prog ? : link->link.prog;
	enum cgroup_bpf_attach_type atype;
	struct bpf_prog_list *pl;
	struct hlist_head *progs;
	int err;

	if (((flags & BPF_F_ALLOW_OVERRIDE) && (flags & BPF_F_ALLOW_MULTI)) ||
	    ((flags & BPF_F_REPLACE) && !(flags & BPF_F_ALLOW_MULTI)))
		/* invalid combination */
		return -EINVAL;
	if (link && (prog || replace_prog))
		/* only either link or prog/replace_prog can be specified */
		return -EINVAL;
	if (!!replace_prog != !!(flags & BPF_F_REPLACE))
		/* replace_prog implies BPF_F_REPLACE, and vice versa */
		return -EINVAL;

	atype = bpf_cgroup_atype_find(type, new_prog->aux->attach_btf_id);
	if (atype < 0)
		return -EINVAL;

	progs = &cgrp->bpf.progs[atype];

	if (!hierarchy_allows_attach(cgrp, atype))
		return -EPERM;

	if (!hlist_empty(progs) && cgrp->bpf.flags[atype] != saved_flags)
		/* Disallow attaching non-overridable on top
		 * of existing overridable in this cgroup.
		 * Disallow attaching multi-prog if overridable or none
		 */
		return -EPERM;

	if (prog_list_length(progs) >= BPF_CGROUP_MAX_PROGS)
		return -E2BIG;

	pl = find_attach_entry(progs, prog, link, replace_prog,
			       flags & BPF_F_ALLOW_MULTI);
	if (IS_ERR(pl))
		return PTR_ERR(pl);

	if (bpf_cgroup_storages_alloc(storage, new_storage, type,
				      prog ? : link->link.prog, cgrp))
		return -ENOMEM;

	if (pl) {
		old_prog = pl->prog;
	} else {
		struct hlist_node *last = NULL;

		pl = kmalloc(sizeof(*pl), GFP_KERNEL);
		if (!pl) {
			bpf_cgroup_storages_free(new_storage);
			return -ENOMEM;
		}
		if (hlist_empty(progs))
			hlist_add_head(&pl->node, progs);
		else
			hlist_for_each(last, progs) {
				if (last->next)
					continue;
				hlist_add_behind(&pl->node, last);
				break;
			}
	}

	pl->prog = prog;
	pl->link = link;
	bpf_cgroup_storages_assign(pl->storage, storage);
	cgrp->bpf.flags[atype] = saved_flags;

	if (type == BPF_LSM_CGROUP) {
		err = bpf_trampoline_link_cgroup_shim(new_prog, atype);
		if (err)
			goto cleanup;
	}

	err = update_effective_progs(cgrp, atype);
	if (err)
		goto cleanup_trampoline;

	if (old_prog) {
		if (type == BPF_LSM_CGROUP)
			bpf_trampoline_unlink_cgroup_shim(old_prog);
		bpf_prog_put(old_prog);
	} else {
		static_branch_inc(&cgroup_bpf_enabled_key[atype]);
	}
	bpf_cgroup_storages_link(new_storage, cgrp, type);
	return 0;

cleanup_trampoline:
	if (type == BPF_LSM_CGROUP)
		bpf_trampoline_unlink_cgroup_shim(new_prog);

cleanup:
	if (old_prog) {
		pl->prog = old_prog;
		pl->link = NULL;
	}
	bpf_cgroup_storages_free(new_storage);
	if (!old_prog) {
		hlist_del(&pl->node);
		kfree(pl);
	}
	return err;
}

static int cgroup_bpf_attach(struct cgroup *cgrp,
			     struct bpf_prog *prog, struct bpf_prog *replace_prog,
			     struct bpf_cgroup_link *link,
			     enum bpf_attach_type type,
			     u32 flags)
{
	int ret;

	mutex_lock(&cgroup_mutex);
	ret = __cgroup_bpf_attach(cgrp, prog, replace_prog, link, type, flags);
	mutex_unlock(&cgroup_mutex);
	return ret;
}

/* Swap updated BPF program for given link in effective program arrays across
 * all descendant cgroups. This function is guaranteed to succeed.
 */
static void replace_effective_prog(struct cgroup *cgrp,
				   enum cgroup_bpf_attach_type atype,
				   struct bpf_cgroup_link *link)
{
	struct bpf_prog_array_item *item;
	struct cgroup_subsys_state *css;
	struct bpf_prog_array *progs;
	struct bpf_prog_list *pl;
	struct hlist_head *head;
	struct cgroup *cg;
	int pos;

	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		if (percpu_ref_is_zero(&desc->bpf.refcnt))
			continue;

		/* find position of link in effective progs array */
		for (pos = 0, cg = desc; cg; cg = cgroup_parent(cg)) {
			if (pos && !(cg->bpf.flags[atype] & BPF_F_ALLOW_MULTI))
				continue;

			head = &cg->bpf.progs[atype];
			hlist_for_each_entry(pl, head, node) {
				if (!prog_list_prog(pl))
					continue;
				if (pl->link == link)
					goto found;
				pos++;
			}
		}
found:
		BUG_ON(!cg);
		progs = rcu_dereference_protected(
				desc->bpf.effective[atype],
				lockdep_is_held(&cgroup_mutex));
		item = &progs->items[pos];
		WRITE_ONCE(item->prog, link->link.prog);
	}
}

/**
 * __cgroup_bpf_replace() - Replace link's program and propagate the change
 *                          to descendants
 * @cgrp: The cgroup which descendants to traverse
 * @link: A link for which to replace BPF program
 * @type: Type of attach operation
 *
 * Must be called with cgroup_mutex held.
 */
static int __cgroup_bpf_replace(struct cgroup *cgrp,
				struct bpf_cgroup_link *link,
				struct bpf_prog *new_prog)
{
	enum cgroup_bpf_attach_type atype;
	struct bpf_prog *old_prog;
	struct bpf_prog_list *pl;
	struct hlist_head *progs;
	bool found = false;

	atype = bpf_cgroup_atype_find(link->type, new_prog->aux->attach_btf_id);
	if (atype < 0)
		return -EINVAL;

	progs = &cgrp->bpf.progs[atype];

	if (link->link.prog->type != new_prog->type)
		return -EINVAL;

	hlist_for_each_entry(pl, progs, node) {
		if (pl->link == link) {
			found = true;
			break;
		}
	}
	if (!found)
		return -ENOENT;

	old_prog = xchg(&link->link.prog, new_prog);
	replace_effective_prog(cgrp, atype, link);
	bpf_prog_put(old_prog);
	return 0;
}

static int cgroup_bpf_replace(struct bpf_link *link, struct bpf_prog *new_prog,
			      struct bpf_prog *old_prog)
{
	struct bpf_cgroup_link *cg_link;
	int ret;

	cg_link = container_of(link, struct bpf_cgroup_link, link);

	mutex_lock(&cgroup_mutex);
	/* link might have been auto-released by dying cgroup, so fail */
	if (!cg_link->cgroup) {
		ret = -ENOLINK;
		goto out_unlock;
	}
	if (old_prog && link->prog != old_prog) {
		ret = -EPERM;
		goto out_unlock;
	}
	ret = __cgroup_bpf_replace(cg_link->cgroup, cg_link, new_prog);
out_unlock:
	mutex_unlock(&cgroup_mutex);
	return ret;
}

static struct bpf_prog_list *find_detach_entry(struct hlist_head *progs,
					       struct bpf_prog *prog,
					       struct bpf_cgroup_link *link,
					       bool allow_multi)
{
	struct bpf_prog_list *pl;

	if (!allow_multi) {
		if (hlist_empty(progs))
			/* report error when trying to detach and nothing is attached */
			return ERR_PTR(-ENOENT);

		/* to maintain backward compatibility NONE and OVERRIDE cgroups
		 * allow detaching with invalid FD (prog==NULL) in legacy mode
		 */
		return hlist_entry(progs->first, typeof(*pl), node);
	}

	if (!prog && !link)
		/* to detach MULTI prog the user has to specify valid FD
		 * of the program or link to be detached
		 */
		return ERR_PTR(-EINVAL);

	/* find the prog or link and detach it */
	hlist_for_each_entry(pl, progs, node) {
		if (pl->prog == prog && pl->link == link)
			return pl;
	}
	return ERR_PTR(-ENOENT);
}

/**
 * purge_effective_progs() - After compute_effective_progs fails to alloc new
 *                           cgrp->bpf.inactive table we can recover by
 *                           recomputing the array in place.
 *
 * @cgrp: The cgroup which descendants to travers
 * @prog: A program to detach or NULL
 * @link: A link to detach or NULL
 * @atype: Type of detach operation
 */
static void purge_effective_progs(struct cgroup *cgrp, struct bpf_prog *prog,
				  struct bpf_cgroup_link *link,
				  enum cgroup_bpf_attach_type atype)
{
	struct cgroup_subsys_state *css;
	struct bpf_prog_array *progs;
	struct bpf_prog_list *pl;
	struct hlist_head *head;
	struct cgroup *cg;
	int pos;

	/* recompute effective prog array in place */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		if (percpu_ref_is_zero(&desc->bpf.refcnt))
			continue;

		/* find position of link or prog in effective progs array */
		for (pos = 0, cg = desc; cg; cg = cgroup_parent(cg)) {
			if (pos && !(cg->bpf.flags[atype] & BPF_F_ALLOW_MULTI))
				continue;

			head = &cg->bpf.progs[atype];
			hlist_for_each_entry(pl, head, node) {
				if (!prog_list_prog(pl))
					continue;
				if (pl->prog == prog && pl->link == link)
					goto found;
				pos++;
			}
		}

		/* no link or prog match, skip the cgroup of this layer */
		continue;
found:
		progs = rcu_dereference_protected(
				desc->bpf.effective[atype],
				lockdep_is_held(&cgroup_mutex));

		/* Remove the program from the array */
		WARN_ONCE(bpf_prog_array_delete_safe_at(progs, pos),
			  "Failed to purge a prog from array at index %d", pos);
	}
}

/**
 * __cgroup_bpf_detach() - Detach the program or link from a cgroup, and
 *                         propagate the change to descendants
 * @cgrp: The cgroup which descendants to traverse
 * @prog: A program to detach or NULL
 * @link: A link to detach or NULL
 * @type: Type of detach operation
 *
 * At most one of @prog or @link can be non-NULL.
 * Must be called with cgroup_mutex held.
 */
static int __cgroup_bpf_detach(struct cgroup *cgrp, struct bpf_prog *prog,
			       struct bpf_cgroup_link *link, enum bpf_attach_type type)
{
	enum cgroup_bpf_attach_type atype;
	struct bpf_prog *old_prog;
	struct bpf_prog_list *pl;
	struct hlist_head *progs;
	u32 attach_btf_id = 0;
	u32 flags;

	if (prog)
		attach_btf_id = prog->aux->attach_btf_id;
	if (link)
		attach_btf_id = link->link.prog->aux->attach_btf_id;

	atype = bpf_cgroup_atype_find(type, attach_btf_id);
	if (atype < 0)
		return -EINVAL;

	progs = &cgrp->bpf.progs[atype];
	flags = cgrp->bpf.flags[atype];

	if (prog && link)
		/* only one of prog or link can be specified */
		return -EINVAL;

	pl = find_detach_entry(progs, prog, link, flags & BPF_F_ALLOW_MULTI);
	if (IS_ERR(pl))
		return PTR_ERR(pl);

	/* mark it deleted, so it's ignored while recomputing effective */
	old_prog = pl->prog;
	pl->prog = NULL;
	pl->link = NULL;

	if (update_effective_progs(cgrp, atype)) {
		/* if update effective array failed replace the prog with a dummy prog*/
		pl->prog = old_prog;
		pl->link = link;
		purge_effective_progs(cgrp, old_prog, link, atype);
	}

	/* now can actually delete it from this cgroup list */
	hlist_del(&pl->node);

	kfree(pl);
	if (hlist_empty(progs))
		/* last program was detached, reset flags to zero */
		cgrp->bpf.flags[atype] = 0;
	if (old_prog) {
		if (type == BPF_LSM_CGROUP)
			bpf_trampoline_unlink_cgroup_shim(old_prog);
		bpf_prog_put(old_prog);
	}
	static_branch_dec(&cgroup_bpf_enabled_key[atype]);
	return 0;
}

static int cgroup_bpf_detach(struct cgroup *cgrp, struct bpf_prog *prog,
			     enum bpf_attach_type type)
{
	int ret;

	mutex_lock(&cgroup_mutex);
	ret = __cgroup_bpf_detach(cgrp, prog, NULL, type);
	mutex_unlock(&cgroup_mutex);
	return ret;
}

/* Must be called with cgroup_mutex held to avoid races. */
static int __cgroup_bpf_query(struct cgroup *cgrp, const union bpf_attr *attr,
			      union bpf_attr __user *uattr)
{
	__u32 __user *prog_attach_flags = u64_to_user_ptr(attr->query.prog_attach_flags);
	bool effective_query = attr->query.query_flags & BPF_F_QUERY_EFFECTIVE;
	__u32 __user *prog_ids = u64_to_user_ptr(attr->query.prog_ids);
	enum bpf_attach_type type = attr->query.attach_type;
	enum cgroup_bpf_attach_type from_atype, to_atype;
	enum cgroup_bpf_attach_type atype;
	struct bpf_prog_array *effective;
	int cnt, ret = 0, i;
	int total_cnt = 0;
	u32 flags;

	if (effective_query && prog_attach_flags)
		return -EINVAL;

	if (type == BPF_LSM_CGROUP) {
		if (!effective_query && attr->query.prog_cnt &&
		    prog_ids && !prog_attach_flags)
			return -EINVAL;

		from_atype = CGROUP_LSM_START;
		to_atype = CGROUP_LSM_END;
		flags = 0;
	} else {
		from_atype = to_cgroup_bpf_attach_type(type);
		if (from_atype < 0)
			return -EINVAL;
		to_atype = from_atype;
		flags = cgrp->bpf.flags[from_atype];
	}

	for (atype = from_atype; atype <= to_atype; atype++) {
		if (effective_query) {
			effective = rcu_dereference_protected(cgrp->bpf.effective[atype],
							      lockdep_is_held(&cgroup_mutex));
			total_cnt += bpf_prog_array_length(effective);
		} else {
			total_cnt += prog_list_length(&cgrp->bpf.progs[atype]);
		}
	}

	/* always output uattr->query.attach_flags as 0 during effective query */
	flags = effective_query ? 0 : flags;
	if (copy_to_user(&uattr->query.attach_flags, &flags, sizeof(flags)))
		return -EFAULT;
	if (copy_to_user(&uattr->query.prog_cnt, &total_cnt, sizeof(total_cnt)))
		return -EFAULT;
	if (attr->query.prog_cnt == 0 || !prog_ids || !total_cnt)
		/* return early if user requested only program count + flags */
		return 0;

	if (attr->query.prog_cnt < total_cnt) {
		total_cnt = attr->query.prog_cnt;
		ret = -ENOSPC;
	}

	for (atype = from_atype; atype <= to_atype && total_cnt; atype++) {
		if (effective_query) {
			effective = rcu_dereference_protected(cgrp->bpf.effective[atype],
							      lockdep_is_held(&cgroup_mutex));
			cnt = min_t(int, bpf_prog_array_length(effective), total_cnt);
			ret = bpf_prog_array_copy_to_user(effective, prog_ids, cnt);
		} else {
			struct hlist_head *progs;
			struct bpf_prog_list *pl;
			struct bpf_prog *prog;
			u32 id;

			progs = &cgrp->bpf.progs[atype];
			cnt = min_t(int, prog_list_length(progs), total_cnt);
			i = 0;
			hlist_for_each_entry(pl, progs, node) {
				prog = prog_list_prog(pl);
				id = prog->aux->id;
				if (copy_to_user(prog_ids + i, &id, sizeof(id)))
					return -EFAULT;
				if (++i == cnt)
					break;
			}

			if (prog_attach_flags) {
				flags = cgrp->bpf.flags[atype];

				for (i = 0; i < cnt; i++)
					if (copy_to_user(prog_attach_flags + i,
							 &flags, sizeof(flags)))
						return -EFAULT;
				prog_attach_flags += cnt;
			}
		}

		prog_ids += cnt;
		total_cnt -= cnt;
	}
	return ret;
}

static int cgroup_bpf_query(struct cgroup *cgrp, const union bpf_attr *attr,
			    union bpf_attr __user *uattr)
{
	int ret;

	mutex_lock(&cgroup_mutex);
	ret = __cgroup_bpf_query(cgrp, attr, uattr);
	mutex_unlock(&cgroup_mutex);
	return ret;
}

int cgroup_bpf_prog_attach(const union bpf_attr *attr,
			   enum bpf_prog_type ptype, struct bpf_prog *prog)
{
	struct bpf_prog *replace_prog = NULL;
	struct cgroup *cgrp;
	int ret;

	cgrp = cgroup_get_from_fd(attr->target_fd);
	if (IS_ERR(cgrp))
		return PTR_ERR(cgrp);

	if ((attr->attach_flags & BPF_F_ALLOW_MULTI) &&
	    (attr->attach_flags & BPF_F_REPLACE)) {
		replace_prog = bpf_prog_get_type(attr->replace_bpf_fd, ptype);
		if (IS_ERR(replace_prog)) {
			cgroup_put(cgrp);
			return PTR_ERR(replace_prog);
		}
	}

	ret = cgroup_bpf_attach(cgrp, prog, replace_prog, NULL,
				attr->attach_type, attr->attach_flags);

	if (replace_prog)
		bpf_prog_put(replace_prog);
	cgroup_put(cgrp);
	return ret;
}

int cgroup_bpf_prog_detach(const union bpf_attr *attr, enum bpf_prog_type ptype)
{
	struct bpf_prog *prog;
	struct cgroup *cgrp;
	int ret;

	cgrp = cgroup_get_from_fd(attr->target_fd);
	if (IS_ERR(cgrp))
		return PTR_ERR(cgrp);

	prog = bpf_prog_get_type(attr->attach_bpf_fd, ptype);
	if (IS_ERR(prog))
		prog = NULL;

	ret = cgroup_bpf_detach(cgrp, prog, attr->attach_type);
	if (prog)
		bpf_prog_put(prog);

	cgroup_put(cgrp);
	return ret;
}

static void bpf_cgroup_link_release(struct bpf_link *link)
{
	struct bpf_cgroup_link *cg_link =
		container_of(link, struct bpf_cgroup_link, link);
	struct cgroup *cg;

	/* link might have been auto-detached by dying cgroup already,
	 * in that case our work is done here
	 */
	if (!cg_link->cgroup)
		return;

	mutex_lock(&cgroup_mutex);

	/* re-check cgroup under lock again */
	if (!cg_link->cgroup) {
		mutex_unlock(&cgroup_mutex);
		return;
	}

	WARN_ON(__cgroup_bpf_detach(cg_link->cgroup, NULL, cg_link,
				    cg_link->type));
	if (cg_link->type == BPF_LSM_CGROUP)
		bpf_trampoline_unlink_cgroup_shim(cg_link->link.prog);

	cg = cg_link->cgroup;
	cg_link->cgroup = NULL;

	mutex_unlock(&cgroup_mutex);

	cgroup_put(cg);
}

static void bpf_cgroup_link_dealloc(struct bpf_link *link)
{
	struct bpf_cgroup_link *cg_link =
		container_of(link, struct bpf_cgroup_link, link);

	kfree(cg_link);
}

static int bpf_cgroup_link_detach(struct bpf_link *link)
{
	bpf_cgroup_link_release(link);

	return 0;
}

static void bpf_cgroup_link_show_fdinfo(const struct bpf_link *link,
					struct seq_file *seq)
{
	struct bpf_cgroup_link *cg_link =
		container_of(link, struct bpf_cgroup_link, link);
	u64 cg_id = 0;

	mutex_lock(&cgroup_mutex);
	if (cg_link->cgroup)
		cg_id = cgroup_id(cg_link->cgroup);
	mutex_unlock(&cgroup_mutex);

	seq_printf(seq,
		   "cgroup_id:\t%llu\n"
		   "attach_type:\t%d\n",
		   cg_id,
		   cg_link->type);
}

static int bpf_cgroup_link_fill_link_info(const struct bpf_link *link,
					  struct bpf_link_info *info)
{
	struct bpf_cgroup_link *cg_link =
		container_of(link, struct bpf_cgroup_link, link);
	u64 cg_id = 0;

	mutex_lock(&cgroup_mutex);
	if (cg_link->cgroup)
		cg_id = cgroup_id(cg_link->cgroup);
	mutex_unlock(&cgroup_mutex);

	info->cgroup.cgroup_id = cg_id;
	info->cgroup.attach_type = cg_link->type;
	return 0;
}

static const struct bpf_link_ops bpf_cgroup_link_lops = {
	.release = bpf_cgroup_link_release,
	.dealloc = bpf_cgroup_link_dealloc,
	.detach = bpf_cgroup_link_detach,
	.update_prog = cgroup_bpf_replace,
	.show_fdinfo = bpf_cgroup_link_show_fdinfo,
	.fill_link_info = bpf_cgroup_link_fill_link_info,
};

int cgroup_bpf_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct bpf_link_primer link_primer;
	struct bpf_cgroup_link *link;
	struct cgroup *cgrp;
	int err;

	if (attr->link_create.flags)
		return -EINVAL;

	cgrp = cgroup_get_from_fd(attr->link_create.target_fd);
	if (IS_ERR(cgrp))
		return PTR_ERR(cgrp);

	link = kzalloc(sizeof(*link), GFP_USER);
	if (!link) {
		err = -ENOMEM;
		goto out_put_cgroup;
	}
	bpf_link_init(&link->link, BPF_LINK_TYPE_CGROUP, &bpf_cgroup_link_lops,
		      prog);
	link->cgroup = cgrp;
	link->type = attr->link_create.attach_type;

	err = bpf_link_prime(&link->link, &link_primer);
	if (err) {
		kfree(link);
		goto out_put_cgroup;
	}

	err = cgroup_bpf_attach(cgrp, NULL, NULL, link,
				link->type, BPF_F_ALLOW_MULTI);
	if (err) {
		bpf_link_cleanup(&link_primer);
		goto out_put_cgroup;
	}

	return bpf_link_settle(&link_primer);

out_put_cgroup:
	cgroup_put(cgrp);
	return err;
}

int cgroup_bpf_prog_query(const union bpf_attr *attr,
			  union bpf_attr __user *uattr)
{
	struct cgroup *cgrp;
	int ret;

	cgrp = cgroup_get_from_fd(attr->query.target_fd);
	if (IS_ERR(cgrp))
		return PTR_ERR(cgrp);

	ret = cgroup_bpf_query(cgrp, attr, uattr);

	cgroup_put(cgrp);
	return ret;
}

/**
 * __cgroup_bpf_run_filter_skb() - Run a program for packet filtering
 * @sk: The socket sending or receiving traffic
 * @skb: The skb that is being sent or received
 * @type: The type of program to be executed
 *
 * If no socket is passed, or the socket is not of type INET or INET6,
 * this function does nothing and returns 0.
 *
 * The program type passed in via @type must be suitable for network
 * filtering. No further check is performed to assert that.
 *
 * For egress packets, this function can return:
 *   NET_XMIT_SUCCESS    (0)	- continue with packet output
 *   NET_XMIT_DROP       (1)	- drop packet and notify TCP to call cwr
 *   NET_XMIT_CN         (2)	- continue with packet output and notify TCP
 *				  to call cwr
 *   -err			- drop packet
 *
 * For ingress packets, this function will return -EPERM if any
 * attached program was found and if it returned != 1 during execution.
 * Otherwise 0 is returned.
 */
int __cgroup_bpf_run_filter_skb(struct sock *sk,
				struct sk_buff *skb,
				enum cgroup_bpf_attach_type atype)
{
	unsigned int offset = skb->data - skb_network_header(skb);
	struct sock *save_sk;
	void *saved_data_end;
	struct cgroup *cgrp;
	int ret;

	if (!sk || !sk_fullsock(sk))
		return 0;

	if (sk->sk_family != AF_INET && sk->sk_family != AF_INET6)
		return 0;

	cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	save_sk = skb->sk;
	skb->sk = sk;
	__skb_push(skb, offset);

	/* compute pointers for the bpf prog */
	bpf_compute_and_save_data_end(skb, &saved_data_end);

	if (atype == CGROUP_INET_EGRESS) {
		u32 flags = 0;
		bool cn;

		ret = bpf_prog_run_array_cg(&cgrp->bpf, atype, skb,
					    __bpf_prog_run_save_cb, 0, &flags);

		/* Return values of CGROUP EGRESS BPF programs are:
		 *   0: drop packet
		 *   1: keep packet
		 *   2: drop packet and cn
		 *   3: keep packet and cn
		 *
		 * The returned value is then converted to one of the NET_XMIT
		 * or an error code that is then interpreted as drop packet
		 * (and no cn):
		 *   0: NET_XMIT_SUCCESS  skb should be transmitted
		 *   1: NET_XMIT_DROP     skb should be dropped and cn
		 *   2: NET_XMIT_CN       skb should be transmitted and cn
		 *   3: -err              skb should be dropped
		 */

		cn = flags & BPF_RET_SET_CN;
		if (ret && !IS_ERR_VALUE((long)ret))
			ret = -EFAULT;
		if (!ret)
			ret = (cn ? NET_XMIT_CN : NET_XMIT_SUCCESS);
		else
			ret = (cn ? NET_XMIT_DROP : ret);
	} else {
		ret = bpf_prog_run_array_cg(&cgrp->bpf, atype,
					    skb, __bpf_prog_run_save_cb, 0,
					    NULL);
		if (ret && !IS_ERR_VALUE((long)ret))
			ret = -EFAULT;
	}
	bpf_restore_data_end(skb, saved_data_end);
	__skb_pull(skb, offset);
	skb->sk = save_sk;

	return ret;
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_skb);

/**
 * __cgroup_bpf_run_filter_sk() - Run a program on a sock
 * @sk: sock structure to manipulate
 * @type: The type of program to be executed
 *
 * socket is passed is expected to be of type INET or INET6.
 *
 * The program type passed in via @type must be suitable for sock
 * filtering. No further check is performed to assert that.
 *
 * This function will return %-EPERM if any if an attached program was found
 * and if it returned != 1 during execution. In all other cases, 0 is returned.
 */
int __cgroup_bpf_run_filter_sk(struct sock *sk,
			       enum cgroup_bpf_attach_type atype)
{
	struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);

	return bpf_prog_run_array_cg(&cgrp->bpf, atype, sk, bpf_prog_run, 0,
				     NULL);
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_sk);

/**
 * __cgroup_bpf_run_filter_sock_addr() - Run a program on a sock and
 *                                       provided by user sockaddr
 * @sk: sock struct that will use sockaddr
 * @uaddr: sockaddr struct provided by user
 * @type: The type of program to be executed
 * @t_ctx: Pointer to attach type specific context
 * @flags: Pointer to u32 which contains higher bits of BPF program
 *         return value (OR'ed together).
 *
 * socket is expected to be of type INET or INET6.
 *
 * This function will return %-EPERM if an attached program is found and
 * returned value != 1 during execution. In all other cases, 0 is returned.
 */
int __cgroup_bpf_run_filter_sock_addr(struct sock *sk,
				      struct sockaddr *uaddr,
				      enum cgroup_bpf_attach_type atype,
				      void *t_ctx,
				      u32 *flags)
{
	struct bpf_sock_addr_kern ctx = {
		.sk = sk,
		.uaddr = uaddr,
		.t_ctx = t_ctx,
	};
	struct sockaddr_storage unspec;
	struct cgroup *cgrp;

	/* Check socket family since not all sockets represent network
	 * endpoint (e.g. AF_UNIX).
	 */
	if (sk->sk_family != AF_INET && sk->sk_family != AF_INET6)
		return 0;

	if (!ctx.uaddr) {
		memset(&unspec, 0, sizeof(unspec));
		ctx.uaddr = (struct sockaddr *)&unspec;
	}

	cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	return bpf_prog_run_array_cg(&cgrp->bpf, atype, &ctx, bpf_prog_run,
				     0, flags);
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_sock_addr);

/**
 * __cgroup_bpf_run_filter_sock_ops() - Run a program on a sock
 * @sk: socket to get cgroup from
 * @sock_ops: bpf_sock_ops_kern struct to pass to program. Contains
 * sk with connection information (IP addresses, etc.) May not contain
 * cgroup info if it is a req sock.
 * @type: The type of program to be executed
 *
 * socket passed is expected to be of type INET or INET6.
 *
 * The program type passed in via @type must be suitable for sock_ops
 * filtering. No further check is performed to assert that.
 *
 * This function will return %-EPERM if any if an attached program was found
 * and if it returned != 1 during execution. In all other cases, 0 is returned.
 */
int __cgroup_bpf_run_filter_sock_ops(struct sock *sk,
				     struct bpf_sock_ops_kern *sock_ops,
				     enum cgroup_bpf_attach_type atype)
{
	struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);

	return bpf_prog_run_array_cg(&cgrp->bpf, atype, sock_ops, bpf_prog_run,
				     0, NULL);
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_sock_ops);

int __cgroup_bpf_check_dev_permission(short dev_type, u32 major, u32 minor,
				      short access, enum cgroup_bpf_attach_type atype)
{
	struct cgroup *cgrp;
	struct bpf_cgroup_dev_ctx ctx = {
		.access_type = (access << 16) | dev_type,
		.major = major,
		.minor = minor,
	};
	int ret;

	rcu_read_lock();
	cgrp = task_dfl_cgroup(current);
	ret = bpf_prog_run_array_cg(&cgrp->bpf, atype, &ctx, bpf_prog_run, 0,
				    NULL);
	rcu_read_unlock();

	return ret;
}

BPF_CALL_2(bpf_get_local_storage, struct bpf_map *, map, u64, flags)
{
	/* flags argument is not used now,
	 * but provides an ability to extend the API.
	 * verifier checks that its value is correct.
	 */
	enum bpf_cgroup_storage_type stype = cgroup_storage_type(map);
	struct bpf_cgroup_storage *storage;
	struct bpf_cg_run_ctx *ctx;
	void *ptr;

	/* get current cgroup storage from BPF run context */
	ctx = container_of(current->bpf_ctx, struct bpf_cg_run_ctx, run_ctx);
	storage = ctx->prog_item->cgroup_storage[stype];

	if (stype == BPF_CGROUP_STORAGE_SHARED)
		ptr = &READ_ONCE(storage->buf)->data[0];
	else
		ptr = this_cpu_ptr(storage->percpu_buf);

	return (unsigned long)ptr;
}

const struct bpf_func_proto bpf_get_local_storage_proto = {
	.func		= bpf_get_local_storage,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_MAP_VALUE,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_0(bpf_get_retval)
{
	struct bpf_cg_run_ctx *ctx =
		container_of(current->bpf_ctx, struct bpf_cg_run_ctx, run_ctx);

	return ctx->retval;
}

const struct bpf_func_proto bpf_get_retval_proto = {
	.func		= bpf_get_retval,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_1(bpf_set_retval, int, retval)
{
	struct bpf_cg_run_ctx *ctx =
		container_of(current->bpf_ctx, struct bpf_cg_run_ctx, run_ctx);

	ctx->retval = retval;
	return 0;
}

const struct bpf_func_proto bpf_set_retval_proto = {
	.func		= bpf_set_retval,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
};

static const struct bpf_func_proto *
cgroup_dev_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	const struct bpf_func_proto *func_proto;

	func_proto = cgroup_common_func_proto(func_id, prog);
	if (func_proto)
		return func_proto;

	func_proto = cgroup_current_func_proto(func_id, prog);
	if (func_proto)
		return func_proto;

	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_event_output_data_proto;
	default:
		return bpf_base_func_proto(func_id);
	}
}

static bool cgroup_dev_is_valid_access(int off, int size,
				       enum bpf_access_type type,
				       const struct bpf_prog *prog,
				       struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);

	if (type == BPF_WRITE)
		return false;

	if (off < 0 || off + size > sizeof(struct bpf_cgroup_dev_ctx))
		return false;
	/* The verifier guarantees that size > 0. */
	if (off % size != 0)
		return false;

	switch (off) {
	case bpf_ctx_range(struct bpf_cgroup_dev_ctx, access_type):
		bpf_ctx_record_field_size(info, size_default);
		if (!bpf_ctx_narrow_access_ok(off, size, size_default))
			return false;
		break;
	default:
		if (size != size_default)
			return false;
	}

	return true;
}

const struct bpf_prog_ops cg_dev_prog_ops = {
};

const struct bpf_verifier_ops cg_dev_verifier_ops = {
	.get_func_proto		= cgroup_dev_func_proto,
	.is_valid_access	= cgroup_dev_is_valid_access,
};

/**
 * __cgroup_bpf_run_filter_sysctl - Run a program on sysctl
 *
 * @head: sysctl table header
 * @table: sysctl table
 * @write: sysctl is being read (= 0) or written (= 1)
 * @buf: pointer to buffer (in and out)
 * @pcount: value-result argument: value is size of buffer pointed to by @buf,
 *	result is size of @new_buf if program set new value, initial value
 *	otherwise
 * @ppos: value-result argument: value is position at which read from or write
 *	to sysctl is happening, result is new position if program overrode it,
 *	initial value otherwise
 * @type: type of program to be executed
 *
 * Program is run when sysctl is being accessed, either read or written, and
 * can allow or deny such access.
 *
 * This function will return %-EPERM if an attached program is found and
 * returned value != 1 during execution. In all other cases 0 is returned.
 */
int __cgroup_bpf_run_filter_sysctl(struct ctl_table_header *head,
				   struct ctl_table *table, int write,
				   char **buf, size_t *pcount, loff_t *ppos,
				   enum cgroup_bpf_attach_type atype)
{
	struct bpf_sysctl_kern ctx = {
		.head = head,
		.table = table,
		.write = write,
		.ppos = ppos,
		.cur_val = NULL,
		.cur_len = PAGE_SIZE,
		.new_val = NULL,
		.new_len = 0,
		.new_updated = 0,
	};
	struct cgroup *cgrp;
	loff_t pos = 0;
	int ret;

	ctx.cur_val = kmalloc_track_caller(ctx.cur_len, GFP_KERNEL);
	if (!ctx.cur_val ||
	    table->proc_handler(table, 0, ctx.cur_val, &ctx.cur_len, &pos)) {
		/* Let BPF program decide how to proceed. */
		ctx.cur_len = 0;
	}

	if (write && *buf && *pcount) {
		/* BPF program should be able to override new value with a
		 * buffer bigger than provided by user.
		 */
		ctx.new_val = kmalloc_track_caller(PAGE_SIZE, GFP_KERNEL);
		ctx.new_len = min_t(size_t, PAGE_SIZE, *pcount);
		if (ctx.new_val) {
			memcpy(ctx.new_val, *buf, ctx.new_len);
		} else {
			/* Let BPF program decide how to proceed. */
			ctx.new_len = 0;
		}
	}

	rcu_read_lock();
	cgrp = task_dfl_cgroup(current);
	ret = bpf_prog_run_array_cg(&cgrp->bpf, atype, &ctx, bpf_prog_run, 0,
				    NULL);
	rcu_read_unlock();

	kfree(ctx.cur_val);

	if (ret == 1 && ctx.new_updated) {
		kfree(*buf);
		*buf = ctx.new_val;
		*pcount = ctx.new_len;
	} else {
		kfree(ctx.new_val);
	}

	return ret;
}

#ifdef CONFIG_NET
static int sockopt_alloc_buf(struct bpf_sockopt_kern *ctx, int max_optlen,
			     struct bpf_sockopt_buf *buf)
{
	if (unlikely(max_optlen < 0))
		return -EINVAL;

	if (unlikely(max_optlen > PAGE_SIZE)) {
		/* We don't expose optvals that are greater than PAGE_SIZE
		 * to the BPF program.
		 */
		max_optlen = PAGE_SIZE;
	}

	if (max_optlen <= sizeof(buf->data)) {
		/* When the optval fits into BPF_SOCKOPT_KERN_BUF_SIZE
		 * bytes avoid the cost of kzalloc.
		 */
		ctx->optval = buf->data;
		ctx->optval_end = ctx->optval + max_optlen;
		return max_optlen;
	}

	ctx->optval = kzalloc(max_optlen, GFP_USER);
	if (!ctx->optval)
		return -ENOMEM;

	ctx->optval_end = ctx->optval + max_optlen;

	return max_optlen;
}

static void sockopt_free_buf(struct bpf_sockopt_kern *ctx,
			     struct bpf_sockopt_buf *buf)
{
	if (ctx->optval == buf->data)
		return;
	kfree(ctx->optval);
}

static bool sockopt_buf_allocated(struct bpf_sockopt_kern *ctx,
				  struct bpf_sockopt_buf *buf)
{
	return ctx->optval != buf->data;
}

int __cgroup_bpf_run_filter_setsockopt(struct sock *sk, int *level,
				       int *optname, char __user *optval,
				       int *optlen, char **kernel_optval)
{
	struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	struct bpf_sockopt_buf buf = {};
	struct bpf_sockopt_kern ctx = {
		.sk = sk,
		.level = *level,
		.optname = *optname,
	};
	int ret, max_optlen;

	/* Allocate a bit more than the initial user buffer for
	 * BPF program. The canonical use case is overriding
	 * TCP_CONGESTION(nv) to TCP_CONGESTION(cubic).
	 */
	max_optlen = max_t(int, 16, *optlen);
	max_optlen = sockopt_alloc_buf(&ctx, max_optlen, &buf);
	if (max_optlen < 0)
		return max_optlen;

	ctx.optlen = *optlen;

	if (copy_from_user(ctx.optval, optval, min(*optlen, max_optlen)) != 0) {
		ret = -EFAULT;
		goto out;
	}

	lock_sock(sk);
	ret = bpf_prog_run_array_cg(&cgrp->bpf, CGROUP_SETSOCKOPT,
				    &ctx, bpf_prog_run, 0, NULL);
	release_sock(sk);

	if (ret)
		goto out;

	if (ctx.optlen == -1) {
		/* optlen set to -1, bypass kernel */
		ret = 1;
	} else if (ctx.optlen > max_optlen || ctx.optlen < -1) {
		/* optlen is out of bounds */
		ret = -EFAULT;
	} else {
		/* optlen within bounds, run kernel handler */
		ret = 0;

		/* export any potential modifications */
		*level = ctx.level;
		*optname = ctx.optname;

		/* optlen == 0 from BPF indicates that we should
		 * use original userspace data.
		 */
		if (ctx.optlen != 0) {
			*optlen = ctx.optlen;
			/* We've used bpf_sockopt_kern->buf as an intermediary
			 * storage, but the BPF program indicates that we need
			 * to pass this data to the kernel setsockopt handler.
			 * No way to export on-stack buf, have to allocate a
			 * new buffer.
			 */
			if (!sockopt_buf_allocated(&ctx, &buf)) {
				void *p = kmalloc(ctx.optlen, GFP_USER);

				if (!p) {
					ret = -ENOMEM;
					goto out;
				}
				memcpy(p, ctx.optval, ctx.optlen);
				*kernel_optval = p;
			} else {
				*kernel_optval = ctx.optval;
			}
			/* export and don't free sockopt buf */
			return 0;
		}
	}

out:
	sockopt_free_buf(&ctx, &buf);
	return ret;
}

int __cgroup_bpf_run_filter_getsockopt(struct sock *sk, int level,
				       int optname, char __user *optval,
				       int __user *optlen, int max_optlen,
				       int retval)
{
	struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	struct bpf_sockopt_buf buf = {};
	struct bpf_sockopt_kern ctx = {
		.sk = sk,
		.level = level,
		.optname = optname,
		.current_task = current,
	};
	int ret;

	ctx.optlen = max_optlen;
	max_optlen = sockopt_alloc_buf(&ctx, max_optlen, &buf);
	if (max_optlen < 0)
		return max_optlen;

	if (!retval) {
		/* If kernel getsockopt finished successfully,
		 * copy whatever was returned to the user back
		 * into our temporary buffer. Set optlen to the
		 * one that kernel returned as well to let
		 * BPF programs inspect the value.
		 */

		if (get_user(ctx.optlen, optlen)) {
			ret = -EFAULT;
			goto out;
		}

		if (ctx.optlen < 0) {
			ret = -EFAULT;
			goto out;
		}

		if (copy_from_user(ctx.optval, optval,
				   min(ctx.optlen, max_optlen)) != 0) {
			ret = -EFAULT;
			goto out;
		}
	}

	lock_sock(sk);
	ret = bpf_prog_run_array_cg(&cgrp->bpf, CGROUP_GETSOCKOPT,
				    &ctx, bpf_prog_run, retval, NULL);
	release_sock(sk);

	if (ret < 0)
		goto out;

	if (ctx.optlen > max_optlen || ctx.optlen < 0) {
		ret = -EFAULT;
		goto out;
	}

	if (ctx.optlen != 0) {
		if (copy_to_user(optval, ctx.optval, ctx.optlen) ||
		    put_user(ctx.optlen, optlen)) {
			ret = -EFAULT;
			goto out;
		}
	}

out:
	sockopt_free_buf(&ctx, &buf);
	return ret;
}

int __cgroup_bpf_run_filter_getsockopt_kern(struct sock *sk, int level,
					    int optname, void *optval,
					    int *optlen, int retval)
{
	struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	struct bpf_sockopt_kern ctx = {
		.sk = sk,
		.level = level,
		.optname = optname,
		.optlen = *optlen,
		.optval = optval,
		.optval_end = optval + *optlen,
		.current_task = current,
	};
	int ret;

	/* Note that __cgroup_bpf_run_filter_getsockopt doesn't copy
	 * user data back into BPF buffer when reval != 0. This is
	 * done as an optimization to avoid extra copy, assuming
	 * kernel won't populate the data in case of an error.
	 * Here we always pass the data and memset() should
	 * be called if that data shouldn't be "exported".
	 */

	ret = bpf_prog_run_array_cg(&cgrp->bpf, CGROUP_GETSOCKOPT,
				    &ctx, bpf_prog_run, retval, NULL);
	if (ret < 0)
		return ret;

	if (ctx.optlen > *optlen)
		return -EFAULT;

	/* BPF programs can shrink the buffer, export the modifications.
	 */
	if (ctx.optlen != 0)
		*optlen = ctx.optlen;

	return ret;
}
#endif

static ssize_t sysctl_cpy_dir(const struct ctl_dir *dir, char **bufp,
			      size_t *lenp)
{
	ssize_t tmp_ret = 0, ret;

	if (dir->header.parent) {
		tmp_ret = sysctl_cpy_dir(dir->header.parent, bufp, lenp);
		if (tmp_ret < 0)
			return tmp_ret;
	}

	ret = strscpy(*bufp, dir->header.ctl_table[0].procname, *lenp);
	if (ret < 0)
		return ret;
	*bufp += ret;
	*lenp -= ret;
	ret += tmp_ret;

	/* Avoid leading slash. */
	if (!ret)
		return ret;

	tmp_ret = strscpy(*bufp, "/", *lenp);
	if (tmp_ret < 0)
		return tmp_ret;
	*bufp += tmp_ret;
	*lenp -= tmp_ret;

	return ret + tmp_ret;
}

BPF_CALL_4(bpf_sysctl_get_name, struct bpf_sysctl_kern *, ctx, char *, buf,
	   size_t, buf_len, u64, flags)
{
	ssize_t tmp_ret = 0, ret;

	if (!buf)
		return -EINVAL;

	if (!(flags & BPF_F_SYSCTL_BASE_NAME)) {
		if (!ctx->head)
			return -EINVAL;
		tmp_ret = sysctl_cpy_dir(ctx->head->parent, &buf, &buf_len);
		if (tmp_ret < 0)
			return tmp_ret;
	}

	ret = strscpy(buf, ctx->table->procname, buf_len);

	return ret < 0 ? ret : tmp_ret + ret;
}

static const struct bpf_func_proto bpf_sysctl_get_name_proto = {
	.func		= bpf_sysctl_get_name,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

static int copy_sysctl_value(char *dst, size_t dst_len, char *src,
			     size_t src_len)
{
	if (!dst)
		return -EINVAL;

	if (!dst_len)
		return -E2BIG;

	if (!src || !src_len) {
		memset(dst, 0, dst_len);
		return -EINVAL;
	}

	memcpy(dst, src, min(dst_len, src_len));

	if (dst_len > src_len) {
		memset(dst + src_len, '\0', dst_len - src_len);
		return src_len;
	}

	dst[dst_len - 1] = '\0';

	return -E2BIG;
}

BPF_CALL_3(bpf_sysctl_get_current_value, struct bpf_sysctl_kern *, ctx,
	   char *, buf, size_t, buf_len)
{
	return copy_sysctl_value(buf, buf_len, ctx->cur_val, ctx->cur_len);
}

static const struct bpf_func_proto bpf_sysctl_get_current_value_proto = {
	.func		= bpf_sysctl_get_current_value,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE,
};

BPF_CALL_3(bpf_sysctl_get_new_value, struct bpf_sysctl_kern *, ctx, char *, buf,
	   size_t, buf_len)
{
	if (!ctx->write) {
		if (buf && buf_len)
			memset(buf, '\0', buf_len);
		return -EINVAL;
	}
	return copy_sysctl_value(buf, buf_len, ctx->new_val, ctx->new_len);
}

static const struct bpf_func_proto bpf_sysctl_get_new_value_proto = {
	.func		= bpf_sysctl_get_new_value,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE,
};

BPF_CALL_3(bpf_sysctl_set_new_value, struct bpf_sysctl_kern *, ctx,
	   const char *, buf, size_t, buf_len)
{
	if (!ctx->write || !ctx->new_val || !ctx->new_len || !buf || !buf_len)
		return -EINVAL;

	if (buf_len > PAGE_SIZE - 1)
		return -E2BIG;

	memcpy(ctx->new_val, buf, buf_len);
	ctx->new_len = buf_len;
	ctx->new_updated = 1;

	return 0;
}

static const struct bpf_func_proto bpf_sysctl_set_new_value_proto = {
	.func		= bpf_sysctl_set_new_value,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg3_type	= ARG_CONST_SIZE,
};

static const struct bpf_func_proto *
sysctl_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	const struct bpf_func_proto *func_proto;

	func_proto = cgroup_common_func_proto(func_id, prog);
	if (func_proto)
		return func_proto;

	func_proto = cgroup_current_func_proto(func_id, prog);
	if (func_proto)
		return func_proto;

	switch (func_id) {
	case BPF_FUNC_sysctl_get_name:
		return &bpf_sysctl_get_name_proto;
	case BPF_FUNC_sysctl_get_current_value:
		return &bpf_sysctl_get_current_value_proto;
	case BPF_FUNC_sysctl_get_new_value:
		return &bpf_sysctl_get_new_value_proto;
	case BPF_FUNC_sysctl_set_new_value:
		return &bpf_sysctl_set_new_value_proto;
	case BPF_FUNC_ktime_get_coarse_ns:
		return &bpf_ktime_get_coarse_ns_proto;
	case BPF_FUNC_perf_event_output:
		return &bpf_event_output_data_proto;
	default:
		return bpf_base_func_proto(func_id);
	}
}

static bool sysctl_is_valid_access(int off, int size, enum bpf_access_type type,
				   const struct bpf_prog *prog,
				   struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);

	if (off < 0 || off + size > sizeof(struct bpf_sysctl) || off % size)
		return false;

	switch (off) {
	case bpf_ctx_range(struct bpf_sysctl, write):
		if (type != BPF_READ)
			return false;
		bpf_ctx_record_field_size(info, size_default);
		return bpf_ctx_narrow_access_ok(off, size, size_default);
	case bpf_ctx_range(struct bpf_sysctl, file_pos):
		if (type == BPF_READ) {
			bpf_ctx_record_field_size(info, size_default);
			return bpf_ctx_narrow_access_ok(off, size, size_default);
		} else {
			return size == size_default;
		}
	default:
		return false;
	}
}

static u32 sysctl_convert_ctx_access(enum bpf_access_type type,
				     const struct bpf_insn *si,
				     struct bpf_insn *insn_buf,
				     struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;
	u32 read_size;

	switch (si->off) {
	case offsetof(struct bpf_sysctl, write):
		*insn++ = BPF_LDX_MEM(
			BPF_SIZE(si->code), si->dst_reg, si->src_reg,
			bpf_target_off(struct bpf_sysctl_kern, write,
				       sizeof_field(struct bpf_sysctl_kern,
						    write),
				       target_size));
		break;
	case offsetof(struct bpf_sysctl, file_pos):
		/* ppos is a pointer so it should be accessed via indirect
		 * loads and stores. Also for stores additional temporary
		 * register is used since neither src_reg nor dst_reg can be
		 * overridden.
		 */
		if (type == BPF_WRITE) {
			int treg = BPF_REG_9;

			if (si->src_reg == treg || si->dst_reg == treg)
				--treg;
			if (si->src_reg == treg || si->dst_reg == treg)
				--treg;
			*insn++ = BPF_STX_MEM(
				BPF_DW, si->dst_reg, treg,
				offsetof(struct bpf_sysctl_kern, tmp_reg));
			*insn++ = BPF_LDX_MEM(
				BPF_FIELD_SIZEOF(struct bpf_sysctl_kern, ppos),
				treg, si->dst_reg,
				offsetof(struct bpf_sysctl_kern, ppos));
			*insn++ = BPF_STX_MEM(
				BPF_SIZEOF(u32), treg, si->src_reg,
				bpf_ctx_narrow_access_offset(
					0, sizeof(u32), sizeof(loff_t)));
			*insn++ = BPF_LDX_MEM(
				BPF_DW, treg, si->dst_reg,
				offsetof(struct bpf_sysctl_kern, tmp_reg));
		} else {
			*insn++ = BPF_LDX_MEM(
				BPF_FIELD_SIZEOF(struct bpf_sysctl_kern, ppos),
				si->dst_reg, si->src_reg,
				offsetof(struct bpf_sysctl_kern, ppos));
			read_size = bpf_size_to_bytes(BPF_SIZE(si->code));
			*insn++ = BPF_LDX_MEM(
				BPF_SIZE(si->code), si->dst_reg, si->dst_reg,
				bpf_ctx_narrow_access_offset(
					0, read_size, sizeof(loff_t)));
		}
		*target_size = sizeof(u32);
		break;
	}

	return insn - insn_buf;
}

const struct bpf_verifier_ops cg_sysctl_verifier_ops = {
	.get_func_proto		= sysctl_func_proto,
	.is_valid_access	= sysctl_is_valid_access,
	.convert_ctx_access	= sysctl_convert_ctx_access,
};

const struct bpf_prog_ops cg_sysctl_prog_ops = {
};

#ifdef CONFIG_NET
BPF_CALL_1(bpf_get_netns_cookie_sockopt, struct bpf_sockopt_kern *, ctx)
{
	const struct net *net = ctx ? sock_net(ctx->sk) : &init_net;

	return net->net_cookie;
}

static const struct bpf_func_proto bpf_get_netns_cookie_sockopt_proto = {
	.func		= bpf_get_netns_cookie_sockopt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX_OR_NULL,
};
#endif

static const struct bpf_func_proto *
cg_sockopt_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	const struct bpf_func_proto *func_proto;

	func_proto = cgroup_common_func_proto(func_id, prog);
	if (func_proto)
		return func_proto;

	func_proto = cgroup_current_func_proto(func_id, prog);
	if (func_proto)
		return func_proto;

	switch (func_id) {
#ifdef CONFIG_NET
	case BPF_FUNC_get_netns_cookie:
		return &bpf_get_netns_cookie_sockopt_proto;
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_proto;
	case BPF_FUNC_sk_storage_delete:
		return &bpf_sk_storage_delete_proto;
	case BPF_FUNC_setsockopt:
		if (prog->expected_attach_type == BPF_CGROUP_SETSOCKOPT)
			return &bpf_sk_setsockopt_proto;
		return NULL;
	case BPF_FUNC_getsockopt:
		if (prog->expected_attach_type == BPF_CGROUP_SETSOCKOPT)
			return &bpf_sk_getsockopt_proto;
		return NULL;
#endif
#ifdef CONFIG_INET
	case BPF_FUNC_tcp_sock:
		return &bpf_tcp_sock_proto;
#endif
	case BPF_FUNC_perf_event_output:
		return &bpf_event_output_data_proto;
	default:
		return bpf_base_func_proto(func_id);
	}
}

static bool cg_sockopt_is_valid_access(int off, int size,
				       enum bpf_access_type type,
				       const struct bpf_prog *prog,
				       struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);

	if (off < 0 || off >= sizeof(struct bpf_sockopt))
		return false;

	if (off % size != 0)
		return false;

	if (type == BPF_WRITE) {
		switch (off) {
		case offsetof(struct bpf_sockopt, retval):
			if (size != size_default)
				return false;
			return prog->expected_attach_type ==
				BPF_CGROUP_GETSOCKOPT;
		case offsetof(struct bpf_sockopt, optname):
			fallthrough;
		case offsetof(struct bpf_sockopt, level):
			if (size != size_default)
				return false;
			return prog->expected_attach_type ==
				BPF_CGROUP_SETSOCKOPT;
		case offsetof(struct bpf_sockopt, optlen):
			return size == size_default;
		default:
			return false;
		}
	}

	switch (off) {
	case offsetof(struct bpf_sockopt, sk):
		if (size != sizeof(__u64))
			return false;
		info->reg_type = PTR_TO_SOCKET;
		break;
	case offsetof(struct bpf_sockopt, optval):
		if (size != sizeof(__u64))
			return false;
		info->reg_type = PTR_TO_PACKET;
		break;
	case offsetof(struct bpf_sockopt, optval_end):
		if (size != sizeof(__u64))
			return false;
		info->reg_type = PTR_TO_PACKET_END;
		break;
	case offsetof(struct bpf_sockopt, retval):
		if (size != size_default)
			return false;
		return prog->expected_attach_type == BPF_CGROUP_GETSOCKOPT;
	default:
		if (size != size_default)
			return false;
		break;
	}
	return true;
}

#define CG_SOCKOPT_ACCESS_FIELD(T, F)					\
	T(BPF_FIELD_SIZEOF(struct bpf_sockopt_kern, F),			\
	  si->dst_reg, si->src_reg,					\
	  offsetof(struct bpf_sockopt_kern, F))

static u32 cg_sockopt_convert_ctx_access(enum bpf_access_type type,
					 const struct bpf_insn *si,
					 struct bpf_insn *insn_buf,
					 struct bpf_prog *prog,
					 u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;

	switch (si->off) {
	case offsetof(struct bpf_sockopt, sk):
		*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, sk);
		break;
	case offsetof(struct bpf_sockopt, level):
		if (type == BPF_WRITE)
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_STX_MEM, level);
		else
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, level);
		break;
	case offsetof(struct bpf_sockopt, optname):
		if (type == BPF_WRITE)
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_STX_MEM, optname);
		else
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, optname);
		break;
	case offsetof(struct bpf_sockopt, optlen):
		if (type == BPF_WRITE)
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_STX_MEM, optlen);
		else
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, optlen);
		break;
	case offsetof(struct bpf_sockopt, retval):
		BUILD_BUG_ON(offsetof(struct bpf_cg_run_ctx, run_ctx) != 0);

		if (type == BPF_WRITE) {
			int treg = BPF_REG_9;

			if (si->src_reg == treg || si->dst_reg == treg)
				--treg;
			if (si->src_reg == treg || si->dst_reg == treg)
				--treg;
			*insn++ = BPF_STX_MEM(BPF_DW, si->dst_reg, treg,
					      offsetof(struct bpf_sockopt_kern, tmp_reg));
			*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_sockopt_kern, current_task),
					      treg, si->dst_reg,
					      offsetof(struct bpf_sockopt_kern, current_task));
			*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct task_struct, bpf_ctx),
					      treg, treg,
					      offsetof(struct task_struct, bpf_ctx));
			*insn++ = BPF_STX_MEM(BPF_FIELD_SIZEOF(struct bpf_cg_run_ctx, retval),
					      treg, si->src_reg,
					      offsetof(struct bpf_cg_run_ctx, retval));
			*insn++ = BPF_LDX_MEM(BPF_DW, treg, si->dst_reg,
					      offsetof(struct bpf_sockopt_kern, tmp_reg));
		} else {
			*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_sockopt_kern, current_task),
					      si->dst_reg, si->src_reg,
					      offsetof(struct bpf_sockopt_kern, current_task));
			*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct task_struct, bpf_ctx),
					      si->dst_reg, si->dst_reg,
					      offsetof(struct task_struct, bpf_ctx));
			*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_cg_run_ctx, retval),
					      si->dst_reg, si->dst_reg,
					      offsetof(struct bpf_cg_run_ctx, retval));
		}
		break;
	case offsetof(struct bpf_sockopt, optval):
		*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, optval);
		break;
	case offsetof(struct bpf_sockopt, optval_end):
		*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, optval_end);
		break;
	}

	return insn - insn_buf;
}

static int cg_sockopt_get_prologue(struct bpf_insn *insn_buf,
				   bool direct_write,
				   const struct bpf_prog *prog)
{
	/* Nothing to do for sockopt argument. The data is kzalloc'ated.
	 */
	return 0;
}

const struct bpf_verifier_ops cg_sockopt_verifier_ops = {
	.get_func_proto		= cg_sockopt_func_proto,
	.is_valid_access	= cg_sockopt_is_valid_access,
	.convert_ctx_access	= cg_sockopt_convert_ctx_access,
	.gen_prologue		= cg_sockopt_get_prologue,
};

const struct bpf_prog_ops cg_sockopt_prog_ops = {
};

/* Common helpers for cgroup hooks. */
const struct bpf_func_proto *
cgroup_common_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_get_local_storage:
		return &bpf_get_local_storage_proto;
	case BPF_FUNC_get_retval:
		switch (prog->expected_attach_type) {
		case BPF_CGROUP_INET_INGRESS:
		case BPF_CGROUP_INET_EGRESS:
		case BPF_CGROUP_SOCK_OPS:
		case BPF_CGROUP_UDP4_RECVMSG:
		case BPF_CGROUP_UDP6_RECVMSG:
		case BPF_CGROUP_INET4_GETPEERNAME:
		case BPF_CGROUP_INET6_GETPEERNAME:
		case BPF_CGROUP_INET4_GETSOCKNAME:
		case BPF_CGROUP_INET6_GETSOCKNAME:
			return NULL;
		default:
			return &bpf_get_retval_proto;
		}
	case BPF_FUNC_set_retval:
		switch (prog->expected_attach_type) {
		case BPF_CGROUP_INET_INGRESS:
		case BPF_CGROUP_INET_EGRESS:
		case BPF_CGROUP_SOCK_OPS:
		case BPF_CGROUP_UDP4_RECVMSG:
		case BPF_CGROUP_UDP6_RECVMSG:
		case BPF_CGROUP_INET4_GETPEERNAME:
		case BPF_CGROUP_INET6_GETPEERNAME:
		case BPF_CGROUP_INET4_GETSOCKNAME:
		case BPF_CGROUP_INET6_GETSOCKNAME:
			return NULL;
		default:
			return &bpf_set_retval_proto;
		}
	default:
		return NULL;
	}
}

/* Common helpers for cgroup hooks with valid process context. */
const struct bpf_func_proto *
cgroup_current_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
	case BPF_FUNC_get_current_pid_tgid:
		return &bpf_get_current_pid_tgid_proto;
	case BPF_FUNC_get_current_comm:
		return &bpf_get_current_comm_proto;
	case BPF_FUNC_get_current_cgroup_id:
		return &bpf_get_current_cgroup_id_proto;
	case BPF_FUNC_get_current_ancestor_cgroup_id:
		return &bpf_get_current_ancestor_cgroup_id_proto;
#ifdef CONFIG_CGROUP_NET_CLASSID
	case BPF_FUNC_get_cgroup_classid:
		return &bpf_get_cgroup_classid_curr_proto;
#endif
	default:
		return NULL;
	}
}
