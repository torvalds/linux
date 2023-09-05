// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019 Facebook */
#include <linux/hash.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/ftrace.h>
#include <linux/rbtree_latch.h>
#include <linux/perf_event.h>
#include <linux/btf.h>
#include <linux/rcupdate_trace.h>
#include <linux/rcupdate_wait.h>
#include <linux/static_call.h>
#include <linux/bpf_verifier.h>
#include <linux/bpf_lsm.h>
#include <linux/delay.h>

/* dummy _ops. The verifier will operate on target program's ops. */
const struct bpf_verifier_ops bpf_extension_verifier_ops = {
};
const struct bpf_prog_ops bpf_extension_prog_ops = {
};

/* btf_vmlinux has ~22k attachable functions. 1k htab is enough. */
#define TRAMPOLINE_HASH_BITS 10
#define TRAMPOLINE_TABLE_SIZE (1 << TRAMPOLINE_HASH_BITS)

static struct hlist_head trampoline_table[TRAMPOLINE_TABLE_SIZE];

/* serializes access to trampoline_table */
static DEFINE_MUTEX(trampoline_mutex);

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
static int bpf_trampoline_update(struct bpf_trampoline *tr, bool lock_direct_mutex);

static int bpf_tramp_ftrace_ops_func(struct ftrace_ops *ops, enum ftrace_ops_cmd cmd)
{
	struct bpf_trampoline *tr = ops->private;
	int ret = 0;

	if (cmd == FTRACE_OPS_CMD_ENABLE_SHARE_IPMODIFY_SELF) {
		/* This is called inside register_ftrace_direct_multi(), so
		 * tr->mutex is already locked.
		 */
		lockdep_assert_held_once(&tr->mutex);

		/* Instead of updating the trampoline here, we propagate
		 * -EAGAIN to register_ftrace_direct(). Then we can
		 * retry register_ftrace_direct() after updating the
		 * trampoline.
		 */
		if ((tr->flags & BPF_TRAMP_F_CALL_ORIG) &&
		    !(tr->flags & BPF_TRAMP_F_ORIG_STACK)) {
			if (WARN_ON_ONCE(tr->flags & BPF_TRAMP_F_SHARE_IPMODIFY))
				return -EBUSY;

			tr->flags |= BPF_TRAMP_F_SHARE_IPMODIFY;
			return -EAGAIN;
		}

		return 0;
	}

	/* The normal locking order is
	 *    tr->mutex => direct_mutex (ftrace.c) => ftrace_lock (ftrace.c)
	 *
	 * The following two commands are called from
	 *
	 *   prepare_direct_functions_for_ipmodify
	 *   cleanup_direct_functions_after_ipmodify
	 *
	 * In both cases, direct_mutex is already locked. Use
	 * mutex_trylock(&tr->mutex) to avoid deadlock in race condition
	 * (something else is making changes to this same trampoline).
	 */
	if (!mutex_trylock(&tr->mutex)) {
		/* sleep 1 ms to make sure whatever holding tr->mutex makes
		 * some progress.
		 */
		msleep(1);
		return -EAGAIN;
	}

	switch (cmd) {
	case FTRACE_OPS_CMD_ENABLE_SHARE_IPMODIFY_PEER:
		tr->flags |= BPF_TRAMP_F_SHARE_IPMODIFY;

		if ((tr->flags & BPF_TRAMP_F_CALL_ORIG) &&
		    !(tr->flags & BPF_TRAMP_F_ORIG_STACK))
			ret = bpf_trampoline_update(tr, false /* lock_direct_mutex */);
		break;
	case FTRACE_OPS_CMD_DISABLE_SHARE_IPMODIFY_PEER:
		tr->flags &= ~BPF_TRAMP_F_SHARE_IPMODIFY;

		if (tr->flags & BPF_TRAMP_F_ORIG_STACK)
			ret = bpf_trampoline_update(tr, false /* lock_direct_mutex */);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&tr->mutex);
	return ret;
}
#endif

bool bpf_prog_has_trampoline(const struct bpf_prog *prog)
{
	enum bpf_attach_type eatype = prog->expected_attach_type;
	enum bpf_prog_type ptype = prog->type;

	return (ptype == BPF_PROG_TYPE_TRACING &&
		(eatype == BPF_TRACE_FENTRY || eatype == BPF_TRACE_FEXIT ||
		 eatype == BPF_MODIFY_RETURN)) ||
		(ptype == BPF_PROG_TYPE_LSM && eatype == BPF_LSM_MAC);
}

void bpf_image_ksym_add(void *data, struct bpf_ksym *ksym)
{
	ksym->start = (unsigned long) data;
	ksym->end = ksym->start + PAGE_SIZE;
	bpf_ksym_add(ksym);
	perf_event_ksymbol(PERF_RECORD_KSYMBOL_TYPE_BPF, ksym->start,
			   PAGE_SIZE, false, ksym->name);
}

void bpf_image_ksym_del(struct bpf_ksym *ksym)
{
	bpf_ksym_del(ksym);
	perf_event_ksymbol(PERF_RECORD_KSYMBOL_TYPE_BPF, ksym->start,
			   PAGE_SIZE, true, ksym->name);
}

static struct bpf_trampoline *bpf_trampoline_lookup(u64 key)
{
	struct bpf_trampoline *tr;
	struct hlist_head *head;
	int i;

	mutex_lock(&trampoline_mutex);
	head = &trampoline_table[hash_64(key, TRAMPOLINE_HASH_BITS)];
	hlist_for_each_entry(tr, head, hlist) {
		if (tr->key == key) {
			refcount_inc(&tr->refcnt);
			goto out;
		}
	}
	tr = kzalloc(sizeof(*tr), GFP_KERNEL);
	if (!tr)
		goto out;
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	tr->fops = kzalloc(sizeof(struct ftrace_ops), GFP_KERNEL);
	if (!tr->fops) {
		kfree(tr);
		tr = NULL;
		goto out;
	}
	tr->fops->private = tr;
	tr->fops->ops_func = bpf_tramp_ftrace_ops_func;
#endif

	tr->key = key;
	INIT_HLIST_NODE(&tr->hlist);
	hlist_add_head(&tr->hlist, head);
	refcount_set(&tr->refcnt, 1);
	mutex_init(&tr->mutex);
	for (i = 0; i < BPF_TRAMP_MAX; i++)
		INIT_HLIST_HEAD(&tr->progs_hlist[i]);
out:
	mutex_unlock(&trampoline_mutex);
	return tr;
}

static int unregister_fentry(struct bpf_trampoline *tr, void *old_addr)
{
	void *ip = tr->func.addr;
	int ret;

	if (tr->func.ftrace_managed)
		ret = unregister_ftrace_direct(tr->fops, (long)old_addr, false);
	else
		ret = bpf_arch_text_poke(ip, BPF_MOD_CALL, old_addr, NULL);

	return ret;
}

static int modify_fentry(struct bpf_trampoline *tr, void *old_addr, void *new_addr,
			 bool lock_direct_mutex)
{
	void *ip = tr->func.addr;
	int ret;

	if (tr->func.ftrace_managed) {
		if (lock_direct_mutex)
			ret = modify_ftrace_direct(tr->fops, (long)new_addr);
		else
			ret = modify_ftrace_direct_nolock(tr->fops, (long)new_addr);
	} else {
		ret = bpf_arch_text_poke(ip, BPF_MOD_CALL, old_addr, new_addr);
	}
	return ret;
}

/* first time registering */
static int register_fentry(struct bpf_trampoline *tr, void *new_addr)
{
	void *ip = tr->func.addr;
	unsigned long faddr;
	int ret;

	faddr = ftrace_location((unsigned long)ip);
	if (faddr) {
		if (!tr->fops)
			return -ENOTSUPP;
		tr->func.ftrace_managed = true;
	}

	if (tr->func.ftrace_managed) {
		ftrace_set_filter_ip(tr->fops, (unsigned long)ip, 0, 1);
		ret = register_ftrace_direct(tr->fops, (long)new_addr);
	} else {
		ret = bpf_arch_text_poke(ip, BPF_MOD_CALL, NULL, new_addr);
	}

	return ret;
}

static struct bpf_tramp_links *
bpf_trampoline_get_progs(const struct bpf_trampoline *tr, int *total, bool *ip_arg)
{
	struct bpf_tramp_link *link;
	struct bpf_tramp_links *tlinks;
	struct bpf_tramp_link **links;
	int kind;

	*total = 0;
	tlinks = kcalloc(BPF_TRAMP_MAX, sizeof(*tlinks), GFP_KERNEL);
	if (!tlinks)
		return ERR_PTR(-ENOMEM);

	for (kind = 0; kind < BPF_TRAMP_MAX; kind++) {
		tlinks[kind].nr_links = tr->progs_cnt[kind];
		*total += tr->progs_cnt[kind];
		links = tlinks[kind].links;

		hlist_for_each_entry(link, &tr->progs_hlist[kind], tramp_hlist) {
			*ip_arg |= link->link.prog->call_get_func_ip;
			*links++ = link;
		}
	}
	return tlinks;
}

static void bpf_tramp_image_free(struct bpf_tramp_image *im)
{
	bpf_image_ksym_del(&im->ksym);
	bpf_jit_free_exec(im->image);
	bpf_jit_uncharge_modmem(PAGE_SIZE);
	percpu_ref_exit(&im->pcref);
	kfree_rcu(im, rcu);
}

static void __bpf_tramp_image_put_deferred(struct work_struct *work)
{
	struct bpf_tramp_image *im;

	im = container_of(work, struct bpf_tramp_image, work);
	bpf_tramp_image_free(im);
}

/* callback, fexit step 3 or fentry step 2 */
static void __bpf_tramp_image_put_rcu(struct rcu_head *rcu)
{
	struct bpf_tramp_image *im;

	im = container_of(rcu, struct bpf_tramp_image, rcu);
	INIT_WORK(&im->work, __bpf_tramp_image_put_deferred);
	schedule_work(&im->work);
}

/* callback, fexit step 2. Called after percpu_ref_kill confirms. */
static void __bpf_tramp_image_release(struct percpu_ref *pcref)
{
	struct bpf_tramp_image *im;

	im = container_of(pcref, struct bpf_tramp_image, pcref);
	call_rcu_tasks(&im->rcu, __bpf_tramp_image_put_rcu);
}

/* callback, fexit or fentry step 1 */
static void __bpf_tramp_image_put_rcu_tasks(struct rcu_head *rcu)
{
	struct bpf_tramp_image *im;

	im = container_of(rcu, struct bpf_tramp_image, rcu);
	if (im->ip_after_call)
		/* the case of fmod_ret/fexit trampoline and CONFIG_PREEMPTION=y */
		percpu_ref_kill(&im->pcref);
	else
		/* the case of fentry trampoline */
		call_rcu_tasks(&im->rcu, __bpf_tramp_image_put_rcu);
}

static void bpf_tramp_image_put(struct bpf_tramp_image *im)
{
	/* The trampoline image that calls original function is using:
	 * rcu_read_lock_trace to protect sleepable bpf progs
	 * rcu_read_lock to protect normal bpf progs
	 * percpu_ref to protect trampoline itself
	 * rcu tasks to protect trampoline asm not covered by percpu_ref
	 * (which are few asm insns before __bpf_tramp_enter and
	 *  after __bpf_tramp_exit)
	 *
	 * The trampoline is unreachable before bpf_tramp_image_put().
	 *
	 * First, patch the trampoline to avoid calling into fexit progs.
	 * The progs will be freed even if the original function is still
	 * executing or sleeping.
	 * In case of CONFIG_PREEMPT=y use call_rcu_tasks() to wait on
	 * first few asm instructions to execute and call into
	 * __bpf_tramp_enter->percpu_ref_get.
	 * Then use percpu_ref_kill to wait for the trampoline and the original
	 * function to finish.
	 * Then use call_rcu_tasks() to make sure few asm insns in
	 * the trampoline epilogue are done as well.
	 *
	 * In !PREEMPT case the task that got interrupted in the first asm
	 * insns won't go through an RCU quiescent state which the
	 * percpu_ref_kill will be waiting for. Hence the first
	 * call_rcu_tasks() is not necessary.
	 */
	if (im->ip_after_call) {
		int err = bpf_arch_text_poke(im->ip_after_call, BPF_MOD_JUMP,
					     NULL, im->ip_epilogue);
		WARN_ON(err);
		if (IS_ENABLED(CONFIG_PREEMPTION))
			call_rcu_tasks(&im->rcu, __bpf_tramp_image_put_rcu_tasks);
		else
			percpu_ref_kill(&im->pcref);
		return;
	}

	/* The trampoline without fexit and fmod_ret progs doesn't call original
	 * function and doesn't use percpu_ref.
	 * Use call_rcu_tasks_trace() to wait for sleepable progs to finish.
	 * Then use call_rcu_tasks() to wait for the rest of trampoline asm
	 * and normal progs.
	 */
	call_rcu_tasks_trace(&im->rcu, __bpf_tramp_image_put_rcu_tasks);
}

static struct bpf_tramp_image *bpf_tramp_image_alloc(u64 key)
{
	struct bpf_tramp_image *im;
	struct bpf_ksym *ksym;
	void *image;
	int err = -ENOMEM;

	im = kzalloc(sizeof(*im), GFP_KERNEL);
	if (!im)
		goto out;

	err = bpf_jit_charge_modmem(PAGE_SIZE);
	if (err)
		goto out_free_im;

	err = -ENOMEM;
	im->image = image = bpf_jit_alloc_exec(PAGE_SIZE);
	if (!image)
		goto out_uncharge;
	set_vm_flush_reset_perms(image);

	err = percpu_ref_init(&im->pcref, __bpf_tramp_image_release, 0, GFP_KERNEL);
	if (err)
		goto out_free_image;

	ksym = &im->ksym;
	INIT_LIST_HEAD_RCU(&ksym->lnode);
	snprintf(ksym->name, KSYM_NAME_LEN, "bpf_trampoline_%llu", key);
	bpf_image_ksym_add(image, ksym);
	return im;

out_free_image:
	bpf_jit_free_exec(im->image);
out_uncharge:
	bpf_jit_uncharge_modmem(PAGE_SIZE);
out_free_im:
	kfree(im);
out:
	return ERR_PTR(err);
}

static int bpf_trampoline_update(struct bpf_trampoline *tr, bool lock_direct_mutex)
{
	struct bpf_tramp_image *im;
	struct bpf_tramp_links *tlinks;
	u32 orig_flags = tr->flags;
	bool ip_arg = false;
	int err, total;

	tlinks = bpf_trampoline_get_progs(tr, &total, &ip_arg);
	if (IS_ERR(tlinks))
		return PTR_ERR(tlinks);

	if (total == 0) {
		err = unregister_fentry(tr, tr->cur_image->image);
		bpf_tramp_image_put(tr->cur_image);
		tr->cur_image = NULL;
		goto out;
	}

	im = bpf_tramp_image_alloc(tr->key);
	if (IS_ERR(im)) {
		err = PTR_ERR(im);
		goto out;
	}

	/* clear all bits except SHARE_IPMODIFY */
	tr->flags &= BPF_TRAMP_F_SHARE_IPMODIFY;

	if (tlinks[BPF_TRAMP_FEXIT].nr_links ||
	    tlinks[BPF_TRAMP_MODIFY_RETURN].nr_links) {
		/* NOTE: BPF_TRAMP_F_RESTORE_REGS and BPF_TRAMP_F_SKIP_FRAME
		 * should not be set together.
		 */
		tr->flags |= BPF_TRAMP_F_CALL_ORIG | BPF_TRAMP_F_SKIP_FRAME;
	} else {
		tr->flags |= BPF_TRAMP_F_RESTORE_REGS;
	}

	if (ip_arg)
		tr->flags |= BPF_TRAMP_F_IP_ARG;

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
again:
	if ((tr->flags & BPF_TRAMP_F_SHARE_IPMODIFY) &&
	    (tr->flags & BPF_TRAMP_F_CALL_ORIG))
		tr->flags |= BPF_TRAMP_F_ORIG_STACK;
#endif

	err = arch_prepare_bpf_trampoline(im, im->image, im->image + PAGE_SIZE,
					  &tr->func.model, tr->flags, tlinks,
					  tr->func.addr);
	if (err < 0)
		goto out_free;

	set_memory_rox((long)im->image, 1);

	WARN_ON(tr->cur_image && total == 0);
	if (tr->cur_image)
		/* progs already running at this address */
		err = modify_fentry(tr, tr->cur_image->image, im->image, lock_direct_mutex);
	else
		/* first time registering */
		err = register_fentry(tr, im->image);

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	if (err == -EAGAIN) {
		/* -EAGAIN from bpf_tramp_ftrace_ops_func. Now
		 * BPF_TRAMP_F_SHARE_IPMODIFY is set, we can generate the
		 * trampoline again, and retry register.
		 */
		/* reset fops->func and fops->trampoline for re-register */
		tr->fops->func = NULL;
		tr->fops->trampoline = 0;

		/* reset im->image memory attr for arch_prepare_bpf_trampoline */
		set_memory_nx((long)im->image, 1);
		set_memory_rw((long)im->image, 1);
		goto again;
	}
#endif
	if (err)
		goto out_free;

	if (tr->cur_image)
		bpf_tramp_image_put(tr->cur_image);
	tr->cur_image = im;
out:
	/* If any error happens, restore previous flags */
	if (err)
		tr->flags = orig_flags;
	kfree(tlinks);
	return err;

out_free:
	bpf_tramp_image_free(im);
	goto out;
}

static enum bpf_tramp_prog_type bpf_attach_type_to_tramp(struct bpf_prog *prog)
{
	switch (prog->expected_attach_type) {
	case BPF_TRACE_FENTRY:
		return BPF_TRAMP_FENTRY;
	case BPF_MODIFY_RETURN:
		return BPF_TRAMP_MODIFY_RETURN;
	case BPF_TRACE_FEXIT:
		return BPF_TRAMP_FEXIT;
	case BPF_LSM_MAC:
		if (!prog->aux->attach_func_proto->type)
			/* The function returns void, we cannot modify its
			 * return value.
			 */
			return BPF_TRAMP_FEXIT;
		else
			return BPF_TRAMP_MODIFY_RETURN;
	default:
		return BPF_TRAMP_REPLACE;
	}
}

static int __bpf_trampoline_link_prog(struct bpf_tramp_link *link, struct bpf_trampoline *tr)
{
	enum bpf_tramp_prog_type kind;
	struct bpf_tramp_link *link_exiting;
	int err = 0;
	int cnt = 0, i;

	kind = bpf_attach_type_to_tramp(link->link.prog);
	if (tr->extension_prog)
		/* cannot attach fentry/fexit if extension prog is attached.
		 * cannot overwrite extension prog either.
		 */
		return -EBUSY;

	for (i = 0; i < BPF_TRAMP_MAX; i++)
		cnt += tr->progs_cnt[i];

	if (kind == BPF_TRAMP_REPLACE) {
		/* Cannot attach extension if fentry/fexit are in use. */
		if (cnt)
			return -EBUSY;
		tr->extension_prog = link->link.prog;
		return bpf_arch_text_poke(tr->func.addr, BPF_MOD_JUMP, NULL,
					  link->link.prog->bpf_func);
	}
	if (cnt >= BPF_MAX_TRAMP_LINKS)
		return -E2BIG;
	if (!hlist_unhashed(&link->tramp_hlist))
		/* prog already linked */
		return -EBUSY;
	hlist_for_each_entry(link_exiting, &tr->progs_hlist[kind], tramp_hlist) {
		if (link_exiting->link.prog != link->link.prog)
			continue;
		/* prog already linked */
		return -EBUSY;
	}

	hlist_add_head(&link->tramp_hlist, &tr->progs_hlist[kind]);
	tr->progs_cnt[kind]++;
	err = bpf_trampoline_update(tr, true /* lock_direct_mutex */);
	if (err) {
		hlist_del_init(&link->tramp_hlist);
		tr->progs_cnt[kind]--;
	}
	return err;
}

int bpf_trampoline_link_prog(struct bpf_tramp_link *link, struct bpf_trampoline *tr)
{
	int err;

	mutex_lock(&tr->mutex);
	err = __bpf_trampoline_link_prog(link, tr);
	mutex_unlock(&tr->mutex);
	return err;
}

static int __bpf_trampoline_unlink_prog(struct bpf_tramp_link *link, struct bpf_trampoline *tr)
{
	enum bpf_tramp_prog_type kind;
	int err;

	kind = bpf_attach_type_to_tramp(link->link.prog);
	if (kind == BPF_TRAMP_REPLACE) {
		WARN_ON_ONCE(!tr->extension_prog);
		err = bpf_arch_text_poke(tr->func.addr, BPF_MOD_JUMP,
					 tr->extension_prog->bpf_func, NULL);
		tr->extension_prog = NULL;
		return err;
	}
	hlist_del_init(&link->tramp_hlist);
	tr->progs_cnt[kind]--;
	return bpf_trampoline_update(tr, true /* lock_direct_mutex */);
}

/* bpf_trampoline_unlink_prog() should never fail. */
int bpf_trampoline_unlink_prog(struct bpf_tramp_link *link, struct bpf_trampoline *tr)
{
	int err;

	mutex_lock(&tr->mutex);
	err = __bpf_trampoline_unlink_prog(link, tr);
	mutex_unlock(&tr->mutex);
	return err;
}

#if defined(CONFIG_CGROUP_BPF) && defined(CONFIG_BPF_LSM)
static void bpf_shim_tramp_link_release(struct bpf_link *link)
{
	struct bpf_shim_tramp_link *shim_link =
		container_of(link, struct bpf_shim_tramp_link, link.link);

	/* paired with 'shim_link->trampoline = tr' in bpf_trampoline_link_cgroup_shim */
	if (!shim_link->trampoline)
		return;

	WARN_ON_ONCE(bpf_trampoline_unlink_prog(&shim_link->link, shim_link->trampoline));
	bpf_trampoline_put(shim_link->trampoline);
}

static void bpf_shim_tramp_link_dealloc(struct bpf_link *link)
{
	struct bpf_shim_tramp_link *shim_link =
		container_of(link, struct bpf_shim_tramp_link, link.link);

	kfree(shim_link);
}

static const struct bpf_link_ops bpf_shim_tramp_link_lops = {
	.release = bpf_shim_tramp_link_release,
	.dealloc = bpf_shim_tramp_link_dealloc,
};

static struct bpf_shim_tramp_link *cgroup_shim_alloc(const struct bpf_prog *prog,
						     bpf_func_t bpf_func,
						     int cgroup_atype)
{
	struct bpf_shim_tramp_link *shim_link = NULL;
	struct bpf_prog *p;

	shim_link = kzalloc(sizeof(*shim_link), GFP_USER);
	if (!shim_link)
		return NULL;

	p = bpf_prog_alloc(1, 0);
	if (!p) {
		kfree(shim_link);
		return NULL;
	}

	p->jited = false;
	p->bpf_func = bpf_func;

	p->aux->cgroup_atype = cgroup_atype;
	p->aux->attach_func_proto = prog->aux->attach_func_proto;
	p->aux->attach_btf_id = prog->aux->attach_btf_id;
	p->aux->attach_btf = prog->aux->attach_btf;
	btf_get(p->aux->attach_btf);
	p->type = BPF_PROG_TYPE_LSM;
	p->expected_attach_type = BPF_LSM_MAC;
	bpf_prog_inc(p);
	bpf_link_init(&shim_link->link.link, BPF_LINK_TYPE_UNSPEC,
		      &bpf_shim_tramp_link_lops, p);
	bpf_cgroup_atype_get(p->aux->attach_btf_id, cgroup_atype);

	return shim_link;
}

static struct bpf_shim_tramp_link *cgroup_shim_find(struct bpf_trampoline *tr,
						    bpf_func_t bpf_func)
{
	struct bpf_tramp_link *link;
	int kind;

	for (kind = 0; kind < BPF_TRAMP_MAX; kind++) {
		hlist_for_each_entry(link, &tr->progs_hlist[kind], tramp_hlist) {
			struct bpf_prog *p = link->link.prog;

			if (p->bpf_func == bpf_func)
				return container_of(link, struct bpf_shim_tramp_link, link);
		}
	}

	return NULL;
}

int bpf_trampoline_link_cgroup_shim(struct bpf_prog *prog,
				    int cgroup_atype)
{
	struct bpf_shim_tramp_link *shim_link = NULL;
	struct bpf_attach_target_info tgt_info = {};
	struct bpf_trampoline *tr;
	bpf_func_t bpf_func;
	u64 key;
	int err;

	err = bpf_check_attach_target(NULL, prog, NULL,
				      prog->aux->attach_btf_id,
				      &tgt_info);
	if (err)
		return err;

	key = bpf_trampoline_compute_key(NULL, prog->aux->attach_btf,
					 prog->aux->attach_btf_id);

	bpf_lsm_find_cgroup_shim(prog, &bpf_func);
	tr = bpf_trampoline_get(key, &tgt_info);
	if (!tr)
		return  -ENOMEM;

	mutex_lock(&tr->mutex);

	shim_link = cgroup_shim_find(tr, bpf_func);
	if (shim_link) {
		/* Reusing existing shim attached by the other program. */
		bpf_link_inc(&shim_link->link.link);

		mutex_unlock(&tr->mutex);
		bpf_trampoline_put(tr); /* bpf_trampoline_get above */
		return 0;
	}

	/* Allocate and install new shim. */

	shim_link = cgroup_shim_alloc(prog, bpf_func, cgroup_atype);
	if (!shim_link) {
		err = -ENOMEM;
		goto err;
	}

	err = __bpf_trampoline_link_prog(&shim_link->link, tr);
	if (err)
		goto err;

	shim_link->trampoline = tr;
	/* note, we're still holding tr refcnt from above */

	mutex_unlock(&tr->mutex);

	return 0;
err:
	mutex_unlock(&tr->mutex);

	if (shim_link)
		bpf_link_put(&shim_link->link.link);

	/* have to release tr while _not_ holding its mutex */
	bpf_trampoline_put(tr); /* bpf_trampoline_get above */

	return err;
}

void bpf_trampoline_unlink_cgroup_shim(struct bpf_prog *prog)
{
	struct bpf_shim_tramp_link *shim_link = NULL;
	struct bpf_trampoline *tr;
	bpf_func_t bpf_func;
	u64 key;

	key = bpf_trampoline_compute_key(NULL, prog->aux->attach_btf,
					 prog->aux->attach_btf_id);

	bpf_lsm_find_cgroup_shim(prog, &bpf_func);
	tr = bpf_trampoline_lookup(key);
	if (WARN_ON_ONCE(!tr))
		return;

	mutex_lock(&tr->mutex);
	shim_link = cgroup_shim_find(tr, bpf_func);
	mutex_unlock(&tr->mutex);

	if (shim_link)
		bpf_link_put(&shim_link->link.link);

	bpf_trampoline_put(tr); /* bpf_trampoline_lookup above */
}
#endif

struct bpf_trampoline *bpf_trampoline_get(u64 key,
					  struct bpf_attach_target_info *tgt_info)
{
	struct bpf_trampoline *tr;

	tr = bpf_trampoline_lookup(key);
	if (!tr)
		return NULL;

	mutex_lock(&tr->mutex);
	if (tr->func.addr)
		goto out;

	memcpy(&tr->func.model, &tgt_info->fmodel, sizeof(tgt_info->fmodel));
	tr->func.addr = (void *)tgt_info->tgt_addr;
out:
	mutex_unlock(&tr->mutex);
	return tr;
}

void bpf_trampoline_put(struct bpf_trampoline *tr)
{
	int i;

	if (!tr)
		return;
	mutex_lock(&trampoline_mutex);
	if (!refcount_dec_and_test(&tr->refcnt))
		goto out;
	WARN_ON_ONCE(mutex_is_locked(&tr->mutex));

	for (i = 0; i < BPF_TRAMP_MAX; i++)
		if (WARN_ON_ONCE(!hlist_empty(&tr->progs_hlist[i])))
			goto out;

	/* This code will be executed even when the last bpf_tramp_image
	 * is alive. All progs are detached from the trampoline and the
	 * trampoline image is patched with jmp into epilogue to skip
	 * fexit progs. The fentry-only trampoline will be freed via
	 * multiple rcu callbacks.
	 */
	hlist_del(&tr->hlist);
	if (tr->fops) {
		ftrace_free_filter(tr->fops);
		kfree(tr->fops);
	}
	kfree(tr);
out:
	mutex_unlock(&trampoline_mutex);
}

#define NO_START_TIME 1
static __always_inline u64 notrace bpf_prog_start_time(void)
{
	u64 start = NO_START_TIME;

	if (static_branch_unlikely(&bpf_stats_enabled_key)) {
		start = sched_clock();
		if (unlikely(!start))
			start = NO_START_TIME;
	}
	return start;
}

/* The logic is similar to bpf_prog_run(), but with an explicit
 * rcu_read_lock() and migrate_disable() which are required
 * for the trampoline. The macro is split into
 * call __bpf_prog_enter
 * call prog->bpf_func
 * call __bpf_prog_exit
 *
 * __bpf_prog_enter returns:
 * 0 - skip execution of the bpf prog
 * 1 - execute bpf prog
 * [2..MAX_U64] - execute bpf prog and record execution time.
 *     This is start time.
 */
static u64 notrace __bpf_prog_enter_recur(struct bpf_prog *prog, struct bpf_tramp_run_ctx *run_ctx)
	__acquires(RCU)
{
	rcu_read_lock();
	migrate_disable();

	run_ctx->saved_run_ctx = bpf_set_run_ctx(&run_ctx->run_ctx);

	if (unlikely(this_cpu_inc_return(*(prog->active)) != 1)) {
		bpf_prog_inc_misses_counter(prog);
		return 0;
	}
	return bpf_prog_start_time();
}

static void notrace update_prog_stats(struct bpf_prog *prog,
				      u64 start)
{
	struct bpf_prog_stats *stats;

	if (static_branch_unlikely(&bpf_stats_enabled_key) &&
	    /* static_key could be enabled in __bpf_prog_enter*
	     * and disabled in __bpf_prog_exit*.
	     * And vice versa.
	     * Hence check that 'start' is valid.
	     */
	    start > NO_START_TIME) {
		unsigned long flags;

		stats = this_cpu_ptr(prog->stats);
		flags = u64_stats_update_begin_irqsave(&stats->syncp);
		u64_stats_inc(&stats->cnt);
		u64_stats_add(&stats->nsecs, sched_clock() - start);
		u64_stats_update_end_irqrestore(&stats->syncp, flags);
	}
}

static void notrace __bpf_prog_exit_recur(struct bpf_prog *prog, u64 start,
					  struct bpf_tramp_run_ctx *run_ctx)
	__releases(RCU)
{
	bpf_reset_run_ctx(run_ctx->saved_run_ctx);

	update_prog_stats(prog, start);
	this_cpu_dec(*(prog->active));
	migrate_enable();
	rcu_read_unlock();
}

static u64 notrace __bpf_prog_enter_lsm_cgroup(struct bpf_prog *prog,
					       struct bpf_tramp_run_ctx *run_ctx)
	__acquires(RCU)
{
	/* Runtime stats are exported via actual BPF_LSM_CGROUP
	 * programs, not the shims.
	 */
	rcu_read_lock();
	migrate_disable();

	run_ctx->saved_run_ctx = bpf_set_run_ctx(&run_ctx->run_ctx);

	return NO_START_TIME;
}

static void notrace __bpf_prog_exit_lsm_cgroup(struct bpf_prog *prog, u64 start,
					       struct bpf_tramp_run_ctx *run_ctx)
	__releases(RCU)
{
	bpf_reset_run_ctx(run_ctx->saved_run_ctx);

	migrate_enable();
	rcu_read_unlock();
}

u64 notrace __bpf_prog_enter_sleepable_recur(struct bpf_prog *prog,
					     struct bpf_tramp_run_ctx *run_ctx)
{
	rcu_read_lock_trace();
	migrate_disable();
	might_fault();

	if (unlikely(this_cpu_inc_return(*(prog->active)) != 1)) {
		bpf_prog_inc_misses_counter(prog);
		return 0;
	}

	run_ctx->saved_run_ctx = bpf_set_run_ctx(&run_ctx->run_ctx);

	return bpf_prog_start_time();
}

void notrace __bpf_prog_exit_sleepable_recur(struct bpf_prog *prog, u64 start,
					     struct bpf_tramp_run_ctx *run_ctx)
{
	bpf_reset_run_ctx(run_ctx->saved_run_ctx);

	update_prog_stats(prog, start);
	this_cpu_dec(*(prog->active));
	migrate_enable();
	rcu_read_unlock_trace();
}

static u64 notrace __bpf_prog_enter_sleepable(struct bpf_prog *prog,
					      struct bpf_tramp_run_ctx *run_ctx)
{
	rcu_read_lock_trace();
	migrate_disable();
	might_fault();

	run_ctx->saved_run_ctx = bpf_set_run_ctx(&run_ctx->run_ctx);

	return bpf_prog_start_time();
}

static void notrace __bpf_prog_exit_sleepable(struct bpf_prog *prog, u64 start,
					      struct bpf_tramp_run_ctx *run_ctx)
{
	bpf_reset_run_ctx(run_ctx->saved_run_ctx);

	update_prog_stats(prog, start);
	migrate_enable();
	rcu_read_unlock_trace();
}

static u64 notrace __bpf_prog_enter(struct bpf_prog *prog,
				    struct bpf_tramp_run_ctx *run_ctx)
	__acquires(RCU)
{
	rcu_read_lock();
	migrate_disable();

	run_ctx->saved_run_ctx = bpf_set_run_ctx(&run_ctx->run_ctx);

	return bpf_prog_start_time();
}

static void notrace __bpf_prog_exit(struct bpf_prog *prog, u64 start,
				    struct bpf_tramp_run_ctx *run_ctx)
	__releases(RCU)
{
	bpf_reset_run_ctx(run_ctx->saved_run_ctx);

	update_prog_stats(prog, start);
	migrate_enable();
	rcu_read_unlock();
}

void notrace __bpf_tramp_enter(struct bpf_tramp_image *tr)
{
	percpu_ref_get(&tr->pcref);
}

void notrace __bpf_tramp_exit(struct bpf_tramp_image *tr)
{
	percpu_ref_put(&tr->pcref);
}

bpf_trampoline_enter_t bpf_trampoline_enter(const struct bpf_prog *prog)
{
	bool sleepable = prog->aux->sleepable;

	if (bpf_prog_check_recur(prog))
		return sleepable ? __bpf_prog_enter_sleepable_recur :
			__bpf_prog_enter_recur;

	if (resolve_prog_type(prog) == BPF_PROG_TYPE_LSM &&
	    prog->expected_attach_type == BPF_LSM_CGROUP)
		return __bpf_prog_enter_lsm_cgroup;

	return sleepable ? __bpf_prog_enter_sleepable : __bpf_prog_enter;
}

bpf_trampoline_exit_t bpf_trampoline_exit(const struct bpf_prog *prog)
{
	bool sleepable = prog->aux->sleepable;

	if (bpf_prog_check_recur(prog))
		return sleepable ? __bpf_prog_exit_sleepable_recur :
			__bpf_prog_exit_recur;

	if (resolve_prog_type(prog) == BPF_PROG_TYPE_LSM &&
	    prog->expected_attach_type == BPF_LSM_CGROUP)
		return __bpf_prog_exit_lsm_cgroup;

	return sleepable ? __bpf_prog_exit_sleepable : __bpf_prog_exit;
}

int __weak
arch_prepare_bpf_trampoline(struct bpf_tramp_image *tr, void *image, void *image_end,
			    const struct btf_func_model *m, u32 flags,
			    struct bpf_tramp_links *tlinks,
			    void *orig_call)
{
	return -ENOTSUPP;
}

static int __init init_trampolines(void)
{
	int i;

	for (i = 0; i < TRAMPOLINE_TABLE_SIZE; i++)
		INIT_HLIST_HEAD(&trampoline_table[i]);
	return 0;
}
late_initcall(init_trampolines);
