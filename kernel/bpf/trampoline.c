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

static struct hlist_head trampoline_key_table[TRAMPOLINE_TABLE_SIZE];
static struct hlist_head trampoline_ip_table[TRAMPOLINE_TABLE_SIZE];

/* serializes access to trampoline tables */
static DEFINE_MUTEX(trampoline_mutex);

/*
 * Keep 32 trampoline locks (5 bits) in the pool so trampoline_lock_all()
 * stays below MAX_LOCK_DEPTH.  Each pool slot has a distinct lockdep
 * class because trampoline_lock_all() takes all pool mutexes at once;
 * otherwise lockdep would report recursive locking on same-class mutexes.
 */
#define TRAMPOLINE_LOCKS_BITS 5
#define TRAMPOLINE_LOCKS_TABLE_SIZE (1 << TRAMPOLINE_LOCKS_BITS)

static struct {
	struct mutex mutex;
	struct lock_class_key key;
} trampoline_locks[TRAMPOLINE_LOCKS_TABLE_SIZE];

static struct mutex *select_trampoline_lock(struct bpf_trampoline *tr)
{
	return &trampoline_locks[hash_ptr(tr, TRAMPOLINE_LOCKS_BITS)].mutex;
}

static void trampoline_lock(struct bpf_trampoline *tr)
{
	mutex_lock(select_trampoline_lock(tr));
}

static void trampoline_unlock(struct bpf_trampoline *tr)
{
	mutex_unlock(select_trampoline_lock(tr));
}

struct bpf_trampoline_ops {
	int (*register_fentry)(struct bpf_trampoline *tr, struct bpf_tramp_image *im, void *data);
	int (*unregister_fentry)(struct bpf_trampoline *tr, u32 orig_flags, void *data);
	int (*modify_fentry)(struct bpf_trampoline *tr, u32 orig_flags, struct bpf_tramp_image *im,
			     bool lock_direct_mutex, void *data);
};

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
static int bpf_trampoline_update(struct bpf_trampoline *tr, bool lock_direct_mutex,
				 const struct bpf_trampoline_ops *ops, void *data);
static const struct bpf_trampoline_ops trampoline_ops;

#ifdef CONFIG_HAVE_SINGLE_FTRACE_DIRECT_OPS
static struct bpf_trampoline *direct_ops_ip_lookup(struct ftrace_ops *ops, unsigned long ip)
{
	struct hlist_head *head_ip;
	struct bpf_trampoline *tr;

	mutex_lock(&trampoline_mutex);
	head_ip = &trampoline_ip_table[hash_64(ip, TRAMPOLINE_HASH_BITS)];
	hlist_for_each_entry(tr, head_ip, hlist_ip) {
		if (tr->ip == ip)
			goto out;
	}
	tr = NULL;
out:
	mutex_unlock(&trampoline_mutex);
	return tr;
}
#else
static struct bpf_trampoline *direct_ops_ip_lookup(struct ftrace_ops *ops, unsigned long ip)
{
	return ops->private;
}
#endif /* CONFIG_HAVE_SINGLE_FTRACE_DIRECT_OPS */

static int bpf_tramp_ftrace_ops_func(struct ftrace_ops *ops, unsigned long ip,
				     enum ftrace_ops_cmd cmd)
{
	struct bpf_trampoline *tr;
	int ret = 0;

	tr = direct_ops_ip_lookup(ops, ip);
	if (!tr)
		return -EINVAL;

	if (cmd == FTRACE_OPS_CMD_ENABLE_SHARE_IPMODIFY_SELF) {
		/* This is called inside register_ftrace_direct_multi(), so
		 * trampoline's mutex is already locked.
		 */
		lockdep_assert_held_once(select_trampoline_lock(tr));

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
	 *    select_trampoline_lock(tr) => direct_mutex (ftrace.c) => ftrace_lock (ftrace.c)
	 *
	 * The following two commands are called from
	 *
	 *   prepare_direct_functions_for_ipmodify
	 *   cleanup_direct_functions_after_ipmodify
	 *
	 * In both cases, direct_mutex is already locked. Use
	 * mutex_trylock(select_trampoline_lock(tr)) to avoid deadlock in race condition
	 * (something else holds the same pool lock).
	 */
	if (!mutex_trylock(select_trampoline_lock(tr))) {
		/* sleep 1 ms to make sure whatever holding select_trampoline_lock(tr)
		 * makes some progress.
		 */
		msleep(1);
		return -EAGAIN;
	}

	switch (cmd) {
	case FTRACE_OPS_CMD_ENABLE_SHARE_IPMODIFY_PEER:
		tr->flags |= BPF_TRAMP_F_SHARE_IPMODIFY;

		if ((tr->flags & BPF_TRAMP_F_CALL_ORIG) &&
		    !(tr->flags & BPF_TRAMP_F_ORIG_STACK))
			ret = bpf_trampoline_update(tr, false /* lock_direct_mutex */,
						    &trampoline_ops, NULL);
		break;
	case FTRACE_OPS_CMD_DISABLE_SHARE_IPMODIFY_PEER:
		tr->flags &= ~BPF_TRAMP_F_SHARE_IPMODIFY;

		if (tr->flags & BPF_TRAMP_F_ORIG_STACK)
			ret = bpf_trampoline_update(tr, false /* lock_direct_mutex */,
						    &trampoline_ops, NULL);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	trampoline_unlock(tr);
	return ret;
}
#endif

bool bpf_prog_has_trampoline(const struct bpf_prog *prog)
{
	enum bpf_attach_type eatype = prog->expected_attach_type;
	enum bpf_prog_type ptype = prog->type;

	switch (ptype) {
	case BPF_PROG_TYPE_TRACING:
		if (eatype == BPF_TRACE_FENTRY || eatype == BPF_TRACE_FEXIT ||
		    eatype == BPF_MODIFY_RETURN || eatype == BPF_TRACE_FSESSION ||
		    eatype == BPF_TRACE_FENTRY_MULTI || eatype == BPF_TRACE_FEXIT_MULTI ||
		    eatype == BPF_TRACE_FSESSION_MULTI)
			return true;
		return false;
	case BPF_PROG_TYPE_LSM:
		return eatype == BPF_LSM_MAC;
	default:
		return false;
	}
}

void bpf_image_ksym_init(void *data, unsigned int size, struct bpf_ksym *ksym)
{
	ksym->start = (unsigned long) data;
	ksym->end = ksym->start + size;
}

void bpf_image_ksym_add(struct bpf_ksym *ksym)
{
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

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
#ifdef CONFIG_HAVE_SINGLE_FTRACE_DIRECT_OPS
/*
 * We have only single direct_ops which contains all the direct call
 * sites and is the only global ftrace_ops for all trampolines.
 *
 * We use 'update_ftrace_direct_*' api for attachment.
 */
struct ftrace_ops direct_ops = {
	.ops_func = bpf_tramp_ftrace_ops_func,
};

static int direct_ops_alloc(struct bpf_trampoline *tr)
{
	tr->fops = &direct_ops;
	return 0;
}

static void direct_ops_free(struct bpf_trampoline *tr) { }

static struct ftrace_hash *hash_from_ip(struct bpf_trampoline *tr, void *ptr)
{
	unsigned long ip, addr = (unsigned long) ptr;
	struct ftrace_hash *hash;

	ip = ftrace_location(tr->ip);
	if (!ip)
		return NULL;
	hash = alloc_ftrace_hash(FTRACE_HASH_DEFAULT_BITS);
	if (!hash)
		return NULL;
	if (bpf_trampoline_use_jmp(tr->flags))
		addr = ftrace_jmp_set(addr);
	if (!add_ftrace_hash_entry_direct(hash, ip, addr)) {
		free_ftrace_hash(hash);
		return NULL;
	}
	return hash;
}

static int direct_ops_add(struct bpf_trampoline *tr, void *addr)
{
	struct ftrace_hash *hash = hash_from_ip(tr, addr);
	int err;

	if (!hash)
		return -ENOMEM;
	err = update_ftrace_direct_add(tr->fops, hash);
	free_ftrace_hash(hash);
	return err;
}

static int direct_ops_del(struct bpf_trampoline *tr, void *addr)
{
	struct ftrace_hash *hash = hash_from_ip(tr, addr);
	int err;

	if (!hash)
		return -ENOMEM;
	err = update_ftrace_direct_del(tr->fops, hash);
	free_ftrace_hash(hash);
	return err;
}

static int direct_ops_mod(struct bpf_trampoline *tr, void *addr, bool lock_direct_mutex)
{
	struct ftrace_hash *hash = hash_from_ip(tr, addr);
	int err;

	if (!hash)
		return -ENOMEM;
	err = update_ftrace_direct_mod(tr->fops, hash, lock_direct_mutex);
	free_ftrace_hash(hash);
	return err;
}
#else
/*
 * We allocate ftrace_ops object for each trampoline and it contains
 * call site specific for that trampoline.
 *
 * We use *_ftrace_direct api for attachment.
 */
static int direct_ops_alloc(struct bpf_trampoline *tr)
{
	tr->fops = kzalloc_obj(struct ftrace_ops);
	if (!tr->fops)
		return -ENOMEM;
	tr->fops->private = tr;
	tr->fops->ops_func = bpf_tramp_ftrace_ops_func;
	return 0;
}

static void direct_ops_free(struct bpf_trampoline *tr)
{
	if (!tr->fops)
		return;
	ftrace_free_filter(tr->fops);
	kfree(tr->fops);
}

static int direct_ops_add(struct bpf_trampoline *tr, void *ptr)
{
	unsigned long addr = (unsigned long) ptr;
	struct ftrace_ops *ops = tr->fops;
	int ret;

	if (bpf_trampoline_use_jmp(tr->flags))
		addr = ftrace_jmp_set(addr);

	ret = ftrace_set_filter_ip(ops, tr->ip, 0, 1);
	if (ret)
		return ret;
	return register_ftrace_direct(ops, addr);
}

static int direct_ops_del(struct bpf_trampoline *tr, void *addr)
{
	return unregister_ftrace_direct(tr->fops, (long)addr, false);
}

static int direct_ops_mod(struct bpf_trampoline *tr, void *ptr, bool lock_direct_mutex)
{
	unsigned long addr = (unsigned long) ptr;
	struct ftrace_ops *ops = tr->fops;

	if (bpf_trampoline_use_jmp(tr->flags))
		addr = ftrace_jmp_set(addr);
	if (lock_direct_mutex)
		return modify_ftrace_direct(ops, addr);
	return modify_ftrace_direct_nolock(ops, addr);
}
#endif /* CONFIG_HAVE_SINGLE_FTRACE_DIRECT_OPS */
#else
static void direct_ops_free(struct bpf_trampoline *tr) { }

static int direct_ops_alloc(struct bpf_trampoline *tr)
{
	return 0;
}

static int direct_ops_add(struct bpf_trampoline *tr, void *addr)
{
	return -ENODEV;
}

static int direct_ops_del(struct bpf_trampoline *tr, void *addr)
{
	return -ENODEV;
}

static int direct_ops_mod(struct bpf_trampoline *tr, void *ptr, bool lock_direct_mutex)
{
	return -ENODEV;
}
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS */

static struct bpf_trampoline *bpf_trampoline_lookup(u64 key, unsigned long ip)
{
	struct bpf_trampoline *tr;
	struct hlist_head *head;
	int i;

	mutex_lock(&trampoline_mutex);
	head = &trampoline_key_table[hash_64(key, TRAMPOLINE_HASH_BITS)];
	hlist_for_each_entry(tr, head, hlist_key) {
		if (tr->key == key) {
			refcount_inc(&tr->refcnt);
			goto out;
		}
	}
	tr = kzalloc_obj(*tr);
	if (!tr)
		goto out;
	if (direct_ops_alloc(tr)) {
		kfree(tr);
		tr = NULL;
		goto out;
	}

	tr->key = key;
	tr->ip = ftrace_location(ip);
	INIT_HLIST_NODE(&tr->hlist_key);
	INIT_HLIST_NODE(&tr->hlist_ip);
	hlist_add_head(&tr->hlist_key, head);
	head = &trampoline_ip_table[hash_64(tr->ip, TRAMPOLINE_HASH_BITS)];
	hlist_add_head(&tr->hlist_ip, head);
	refcount_set(&tr->refcnt, 1);
	for (i = 0; i < BPF_TRAMP_MAX; i++)
		INIT_HLIST_HEAD(&tr->progs_hlist[i]);
out:
	mutex_unlock(&trampoline_mutex);
	return tr;
}

static int bpf_trampoline_update_fentry(struct bpf_trampoline *tr, u32 orig_flags,
					void *old_addr, void *new_addr)
{
	enum bpf_text_poke_type new_t = BPF_MOD_CALL, old_t = BPF_MOD_CALL;
	void *ip = tr->func.addr;

	if (!new_addr)
		new_t = BPF_MOD_NOP;
	else if (bpf_trampoline_use_jmp(tr->flags))
		new_t = BPF_MOD_JUMP;

	if (!old_addr)
		old_t = BPF_MOD_NOP;
	else if (bpf_trampoline_use_jmp(orig_flags))
		old_t = BPF_MOD_JUMP;

	return bpf_arch_text_poke(ip, old_t, new_t, old_addr, new_addr);
}

static void bpf_tramp_image_put(struct bpf_tramp_image *im);

static int unregister_fentry(struct bpf_trampoline *tr, u32 orig_flags, void *data __maybe_unused)
{
	void *old_addr = tr->cur_image->image;
	int ret;

	if (tr->func.ftrace_managed)
		ret = direct_ops_del(tr, old_addr);
	else
		ret = bpf_trampoline_update_fentry(tr, orig_flags, old_addr, NULL);

	if (ret)
		return ret;

	bpf_tramp_image_put(tr->cur_image);
	tr->cur_image = NULL;
	return 0;
}

static int modify_fentry(struct bpf_trampoline *tr, u32 orig_flags, struct bpf_tramp_image *im,
			 bool lock_direct_mutex, void *data __maybe_unused)
{
	void *old_addr = tr->cur_image->image;
	void *new_addr = im->image;
	int ret;

	if (tr->func.ftrace_managed) {
		ret = direct_ops_mod(tr, new_addr, lock_direct_mutex);
	} else {
		ret = bpf_trampoline_update_fentry(tr, orig_flags, old_addr,
						   new_addr);
	}

	if (ret)
		return ret;

	bpf_tramp_image_put(tr->cur_image);
	tr->cur_image = im;
	return 0;
}

/* first time registering */
static int register_fentry(struct bpf_trampoline *tr, struct bpf_tramp_image *im,
			   void *data __maybe_unused)
{
	void *new_addr = im->image;
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
		ret = direct_ops_add(tr, new_addr);
	} else {
		ret = bpf_trampoline_update_fentry(tr, 0, NULL, new_addr);
	}

	if (ret)
		return ret;

	tr->cur_image = im;
	return 0;
}

static const struct bpf_trampoline_ops trampoline_ops = {
	.register_fentry   = register_fentry,
	.unregister_fentry = unregister_fentry,
	.modify_fentry     = modify_fentry,
};

static struct bpf_tramp_nodes *
bpf_trampoline_get_progs(const struct bpf_trampoline *tr, int *total, bool *ip_arg)
{
	struct bpf_tramp_node *node, **nodes;
	struct bpf_tramp_nodes *tnodes;
	int kind;

	*total = 0;
	tnodes = kzalloc_objs(*tnodes, BPF_TRAMP_MAX);
	if (!tnodes)
		return ERR_PTR(-ENOMEM);

	for (kind = 0; kind < BPF_TRAMP_MAX; kind++) {
		tnodes[kind].nr_nodes = tr->progs_cnt[kind];
		*total += tr->progs_cnt[kind];
		nodes = tnodes[kind].nodes;

		hlist_for_each_entry(node, &tr->progs_hlist[kind], tramp_hlist) {
			*ip_arg |= node->link->prog->call_get_func_ip;
			*nodes++ = node;
		}
	}
	return tnodes;
}

static void bpf_tramp_image_free(struct bpf_tramp_image *im)
{
	bpf_image_ksym_del(&im->ksym);
	arch_free_bpf_trampoline(im->image, im->size);
	bpf_jit_uncharge_modmem(im->size);
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
		int err = bpf_arch_text_poke(im->ip_after_call, BPF_MOD_NOP,
					     BPF_MOD_JUMP, NULL,
					     im->ip_epilogue);
		WARN_ON(err);
		if (IS_ENABLED(CONFIG_TASKS_RCU))
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

static struct bpf_tramp_image *bpf_tramp_image_alloc(u64 key, int size)
{
	struct bpf_tramp_image *im;
	struct bpf_ksym *ksym;
	void *image;
	int err = -ENOMEM;

	im = kzalloc_obj(*im);
	if (!im)
		goto out;

	err = bpf_jit_charge_modmem(size);
	if (err)
		goto out_free_im;
	im->size = size;

	err = -ENOMEM;
	im->image = image = arch_alloc_bpf_trampoline(size);
	if (!image)
		goto out_uncharge;

	err = percpu_ref_init(&im->pcref, __bpf_tramp_image_release, 0, GFP_KERNEL);
	if (err)
		goto out_free_image;

	ksym = &im->ksym;
	INIT_LIST_HEAD_RCU(&ksym->lnode);
	snprintf(ksym->name, KSYM_NAME_LEN, "bpf_trampoline_%llu", key);
	bpf_image_ksym_init(image, size, ksym);
	bpf_image_ksym_add(ksym);
	return im;

out_free_image:
	arch_free_bpf_trampoline(im->image, im->size);
out_uncharge:
	bpf_jit_uncharge_modmem(size);
out_free_im:
	kfree(im);
out:
	return ERR_PTR(err);
}

static int bpf_trampoline_update(struct bpf_trampoline *tr, bool lock_direct_mutex,
				 const struct bpf_trampoline_ops *ops, void *data)
{
	struct bpf_tramp_image *im;
	struct bpf_tramp_nodes *tnodes;
	u32 orig_flags = tr->flags;
	bool ip_arg = false;
	int err, total, size;

	tnodes = bpf_trampoline_get_progs(tr, &total, &ip_arg);
	if (IS_ERR(tnodes))
		return PTR_ERR(tnodes);

	if (total == 0) {
		err = ops->unregister_fentry(tr, orig_flags, data);
		goto out;
	}

	/* clear all bits except SHARE_IPMODIFY and TAIL_CALL_CTX */
	tr->flags &= (BPF_TRAMP_F_SHARE_IPMODIFY | BPF_TRAMP_F_TAIL_CALL_CTX);

	if (tnodes[BPF_TRAMP_FEXIT].nr_nodes ||
	    tnodes[BPF_TRAMP_MODIFY_RETURN].nr_nodes) {
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
	if (tr->flags & BPF_TRAMP_F_CALL_ORIG) {
		if (tr->flags & BPF_TRAMP_F_SHARE_IPMODIFY) {
			/* The BPF_TRAMP_F_SKIP_FRAME can be cleared in the
			 * first try, reset it in the second try.
			 */
			tr->flags |= BPF_TRAMP_F_ORIG_STACK | BPF_TRAMP_F_SKIP_FRAME;
		} else if (IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_JMP)) {
			/* Use "jmp" instead of "call" for the trampoline
			 * in the origin call case, and we don't need to
			 * skip the frame.
			 */
			tr->flags &= ~BPF_TRAMP_F_SKIP_FRAME;
		}
	}
#endif

	size = arch_bpf_trampoline_size(&tr->func.model, tr->flags,
					tnodes, tr->func.addr);
	if (size < 0) {
		err = size;
		goto out;
	}

	if (size > PAGE_SIZE) {
		err = -E2BIG;
		goto out;
	}

	im = bpf_tramp_image_alloc(tr->key, size);
	if (IS_ERR(im)) {
		err = PTR_ERR(im);
		goto out;
	}

	err = arch_prepare_bpf_trampoline(im, im->image, im->image + size,
					  &tr->func.model, tr->flags, tnodes,
					  tr->func.addr);
	if (err < 0)
		goto out_free;

	err = arch_protect_bpf_trampoline(im->image, im->size);
	if (err)
		goto out_free;

	if (tr->cur_image)
		/* progs already running at this address */
		err = ops->modify_fentry(tr, orig_flags, im, lock_direct_mutex, data);
	else
		/* first time registering */
		err = ops->register_fentry(tr, im, data);

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	if (err == -EAGAIN) {
		/* -EAGAIN from bpf_tramp_ftrace_ops_func. Now
		 * BPF_TRAMP_F_SHARE_IPMODIFY is set, we can generate the
		 * trampoline again, and retry register.
		 */
		bpf_tramp_image_free(im);
		goto again;
	}
#endif

out_free:
	if (err)
		bpf_tramp_image_free(im);
out:
	/* If any error happens, restore previous flags */
	if (err)
		tr->flags = orig_flags;
	kfree(tnodes);
	return err;
}

static enum bpf_tramp_prog_type bpf_attach_type_to_tramp(struct bpf_prog *prog)
{
	switch (prog->expected_attach_type) {
	case BPF_TRACE_FENTRY:
	case BPF_TRACE_FENTRY_MULTI:
		return BPF_TRAMP_FENTRY;
	case BPF_MODIFY_RETURN:
		return BPF_TRAMP_MODIFY_RETURN;
	case BPF_TRACE_FEXIT:
	case BPF_TRACE_FEXIT_MULTI:
		return BPF_TRAMP_FEXIT;
	case BPF_TRACE_FSESSION:
	case BPF_TRACE_FSESSION_MULTI:
		return BPF_TRAMP_FSESSION;
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

static int bpf_freplace_check_tgt_prog(struct bpf_prog *tgt_prog)
{
	struct bpf_prog_aux *aux = tgt_prog->aux;

	guard(mutex)(&aux->ext_mutex);
	if (aux->prog_array_member_cnt)
		/* Program extensions can not extend target prog when the target
		 * prog has been updated to any prog_array map as tail callee.
		 * It's to prevent a potential infinite loop like:
		 * tgt prog entry -> tgt prog subprog -> freplace prog entry
		 * --tailcall-> tgt prog entry.
		 */
		return -EBUSY;

	aux->is_extended = true;
	return 0;
}

static struct bpf_tramp_node *fsession_exit(struct bpf_tramp_node *node)
{
	if (node->link->type == BPF_LINK_TYPE_TRACING) {
		struct bpf_tracing_link *link;

		link = container_of(node->link, struct bpf_tracing_link, link.link);
		return &link->fexit;
	} else if (node->link->type == BPF_LINK_TYPE_TRACING_MULTI) {
		struct bpf_tracing_multi_link *link;
		struct bpf_tracing_multi_node *mnode;

		link = container_of(node->link, struct bpf_tracing_multi_link, link);
		mnode = container_of(node, struct bpf_tracing_multi_node, node);
		return &link->fexits[mnode - link->nodes];
	}
	return NULL;
}

static int bpf_trampoline_add_prog(struct bpf_trampoline *tr,
				   struct bpf_tramp_node *node,
				   int cnt)
{
	enum bpf_tramp_prog_type kind;
	struct bpf_tramp_node *node_existing, *fexit;
	struct hlist_head *prog_list;

	kind = bpf_attach_type_to_tramp(node->link->prog);
	if (kind == BPF_TRAMP_FSESSION) {
		prog_list = &tr->progs_hlist[BPF_TRAMP_FENTRY];
		cnt++;
	} else {
		prog_list = &tr->progs_hlist[kind];
	}
	if (cnt >= BPF_MAX_TRAMP_LINKS)
		return -E2BIG;
	if (!hlist_unhashed(&node->tramp_hlist))
		/* prog already linked */
		return -EBUSY;
	hlist_for_each_entry(node_existing, prog_list, tramp_hlist) {
		if (node_existing->link->prog != node->link->prog)
			continue;
		/* prog already linked */
		return -EBUSY;
	}

	hlist_add_head(&node->tramp_hlist, prog_list);
	if (kind == BPF_TRAMP_FSESSION) {
		tr->progs_cnt[BPF_TRAMP_FENTRY]++;
		fexit = fsession_exit(node);
		if (WARN_ON_ONCE(!fexit))
			return -EINVAL;
		hlist_add_head(&fexit->tramp_hlist, &tr->progs_hlist[BPF_TRAMP_FEXIT]);
		tr->progs_cnt[BPF_TRAMP_FEXIT]++;
	} else {
		tr->progs_cnt[kind]++;
	}
	return 0;
}

static void bpf_trampoline_remove_prog(struct bpf_trampoline *tr,
				       struct bpf_tramp_node *node)
{
	enum bpf_tramp_prog_type kind;
	struct bpf_tramp_node *fexit;

	kind = bpf_attach_type_to_tramp(node->link->prog);
	if (kind == BPF_TRAMP_FSESSION) {
		fexit = fsession_exit(node);
		if (WARN_ON_ONCE(!fexit))
			return;
		hlist_del_init(&fexit->tramp_hlist);
		tr->progs_cnt[BPF_TRAMP_FEXIT]--;
		kind = BPF_TRAMP_FENTRY;
	}
	hlist_del_init(&node->tramp_hlist);
	tr->progs_cnt[kind]--;
}

static int __bpf_trampoline_link_prog(struct bpf_tramp_node *node,
				      struct bpf_trampoline *tr,
				      struct bpf_prog *tgt_prog,
				      const struct bpf_trampoline_ops *ops,
				      void *data)
{
	enum bpf_tramp_prog_type kind;
	int err = 0;
	int cnt = 0, i;

	kind = bpf_attach_type_to_tramp(node->link->prog);
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
		err = bpf_freplace_check_tgt_prog(tgt_prog);
		if (err)
			return err;
		tr->extension_prog = node->link->prog;
		return bpf_arch_text_poke(tr->func.addr, BPF_MOD_NOP,
					  BPF_MOD_JUMP, NULL,
					  node->link->prog->bpf_func);
	}
	err = bpf_trampoline_add_prog(tr, node, cnt);
	if (err)
		return err;
	err = bpf_trampoline_update(tr, true /* lock_direct_mutex */, ops, data);
	if (err)
		bpf_trampoline_remove_prog(tr, node);
	return err;
}

int bpf_trampoline_link_prog(struct bpf_tramp_node *node,
			     struct bpf_trampoline *tr,
			     struct bpf_prog *tgt_prog)
{
	int err;

	trampoline_lock(tr);
	err = __bpf_trampoline_link_prog(node, tr, tgt_prog, &trampoline_ops, NULL);
	trampoline_unlock(tr);
	return err;
}

static int __bpf_trampoline_unlink_prog(struct bpf_tramp_node *node,
					struct bpf_trampoline *tr,
					struct bpf_prog *tgt_prog,
					const struct bpf_trampoline_ops *ops,
					void *data)
{
	enum bpf_tramp_prog_type kind;
	int err;

	kind = bpf_attach_type_to_tramp(node->link->prog);
	if (kind == BPF_TRAMP_REPLACE) {
		WARN_ON_ONCE(!tr->extension_prog);
		err = bpf_arch_text_poke(tr->func.addr, BPF_MOD_JUMP,
					 BPF_MOD_NOP,
					 tr->extension_prog->bpf_func, NULL);
		tr->extension_prog = NULL;
		guard(mutex)(&tgt_prog->aux->ext_mutex);
		tgt_prog->aux->is_extended = false;
		return err;
	}
	bpf_trampoline_remove_prog(tr, node);
	return bpf_trampoline_update(tr, true /* lock_direct_mutex */, ops, data);
}

/* bpf_trampoline_unlink_prog() should never fail. */
int bpf_trampoline_unlink_prog(struct bpf_tramp_node *node,
			       struct bpf_trampoline *tr,
			       struct bpf_prog *tgt_prog)
{
	int err;

	trampoline_lock(tr);
	err = __bpf_trampoline_unlink_prog(node, tr, tgt_prog, &trampoline_ops, NULL);
	trampoline_unlock(tr);
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

	WARN_ON_ONCE(bpf_trampoline_unlink_prog(&shim_link->link.node, shim_link->trampoline, NULL));
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
						     int cgroup_atype,
						     enum bpf_attach_type attach_type)
{
	struct bpf_shim_tramp_link *shim_link = NULL;
	struct bpf_prog *p;

	shim_link = kzalloc_obj(*shim_link, GFP_USER);
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
	bpf_tramp_link_init(&shim_link->link, BPF_LINK_TYPE_UNSPEC,
		      &bpf_shim_tramp_link_lops, p, attach_type, 0);
	bpf_cgroup_atype_get(p->aux->attach_btf_id, cgroup_atype);

	return shim_link;
}

static struct bpf_shim_tramp_link *cgroup_shim_find(struct bpf_trampoline *tr,
						    bpf_func_t bpf_func)
{
	struct bpf_tramp_node *node;
	int kind;

	for (kind = 0; kind < BPF_TRAMP_MAX; kind++) {
		hlist_for_each_entry(node, &tr->progs_hlist[kind], tramp_hlist) {
			struct bpf_prog *p = node->link->prog;

			if (p->bpf_func == bpf_func)
				return container_of(node, struct bpf_shim_tramp_link, link.node);
		}
	}

	return NULL;
}

int bpf_trampoline_link_cgroup_shim(struct bpf_prog *prog,
				    int cgroup_atype,
				    enum bpf_attach_type attach_type)
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

	trampoline_lock(tr);

	shim_link = cgroup_shim_find(tr, bpf_func);
	if (shim_link && !IS_ERR(bpf_link_inc_not_zero(&shim_link->link.link))) {
		/* Reusing existing shim attached by the other program. */
		trampoline_unlock(tr);
		bpf_trampoline_put(tr); /* bpf_trampoline_get above */
		return 0;
	}

	/* Allocate and install new shim. */

	shim_link = cgroup_shim_alloc(prog, bpf_func, cgroup_atype, attach_type);
	if (!shim_link) {
		err = -ENOMEM;
		goto err;
	}

	err = __bpf_trampoline_link_prog(&shim_link->link.node, tr, NULL, &trampoline_ops, NULL);
	if (err)
		goto err;

	shim_link->trampoline = tr;
	/* note, we're still holding tr refcnt from above */

	trampoline_unlock(tr);

	return 0;
err:
	trampoline_unlock(tr);

	if (shim_link)
		bpf_link_put(&shim_link->link.link);

	/* have to release tr while _not_ holding pool mutex for trampoline */
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
	tr = bpf_trampoline_lookup(key, 0);
	if (WARN_ON_ONCE(!tr))
		return;

	trampoline_lock(tr);
	shim_link = cgroup_shim_find(tr, bpf_func);
	trampoline_unlock(tr);

	if (shim_link)
		bpf_link_put(&shim_link->link.link);

	bpf_trampoline_put(tr); /* bpf_trampoline_lookup above */
}
#endif

struct bpf_trampoline *bpf_trampoline_get(u64 key,
					  struct bpf_attach_target_info *tgt_info)
{
	struct bpf_trampoline *tr;

	tr = bpf_trampoline_lookup(key, tgt_info->tgt_addr);
	if (!tr)
		return NULL;

	trampoline_lock(tr);
	if (tr->func.addr)
		goto out;

	memcpy(&tr->func.model, &tgt_info->fmodel, sizeof(tgt_info->fmodel));
	tr->func.addr = (void *)tgt_info->tgt_addr;
out:
	trampoline_unlock(tr);
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

	for (i = 0; i < BPF_TRAMP_MAX; i++)
		if (WARN_ON_ONCE(!hlist_empty(&tr->progs_hlist[i])))
			goto out;

	/* This code will be executed even when the last bpf_tramp_image
	 * is alive. All progs are detached from the trampoline and the
	 * trampoline image is patched with jmp into epilogue to skip
	 * fexit progs. The fentry-only trampoline will be freed via
	 * multiple rcu callbacks.
	 */
	hlist_del(&tr->hlist_key);
	hlist_del(&tr->hlist_ip);
	direct_ops_free(tr);
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
	rcu_read_lock_dont_migrate();

	run_ctx->saved_run_ctx = bpf_set_run_ctx(&run_ctx->run_ctx);

	if (unlikely(!bpf_prog_get_recursion_context(prog))) {
		bpf_prog_inc_misses_counter(prog);
		if (prog->aux->recursion_detected)
			prog->aux->recursion_detected(prog);
		return 0;
	}
	return bpf_prog_start_time();
}

static void notrace __update_prog_stats(struct bpf_prog *prog, u64 start)
{
	struct bpf_prog_stats *stats;
	unsigned long flags;
	u64 duration;

	/*
	 * static_key could be enabled in __bpf_prog_enter* and disabled in
	 * __bpf_prog_exit*. And vice versa. Check that 'start' is valid.
	 */
	if (start <= NO_START_TIME)
		return;

	duration = sched_clock() - start;
	stats = this_cpu_ptr(prog->stats);
	flags = u64_stats_update_begin_irqsave(&stats->syncp);
	u64_stats_inc(&stats->cnt);
	u64_stats_add(&stats->nsecs, duration);
	u64_stats_update_end_irqrestore(&stats->syncp, flags);
}

static __always_inline void notrace update_prog_stats(struct bpf_prog *prog,
						      u64 start)
{
	if (static_branch_unlikely(&bpf_stats_enabled_key))
		__update_prog_stats(prog, start);
}

static void notrace __bpf_prog_exit_recur(struct bpf_prog *prog, u64 start,
					  struct bpf_tramp_run_ctx *run_ctx)
	__releases(RCU)
{
	bpf_reset_run_ctx(run_ctx->saved_run_ctx);

	update_prog_stats(prog, start);
	bpf_prog_put_recursion_context(prog);
	rcu_read_unlock_migrate();
}

static u64 notrace __bpf_prog_enter_lsm_cgroup(struct bpf_prog *prog,
					       struct bpf_tramp_run_ctx *run_ctx)
	__acquires(RCU)
{
	/* Runtime stats are exported via actual BPF_LSM_CGROUP
	 * programs, not the shims.
	 */
	rcu_read_lock_dont_migrate();

	run_ctx->saved_run_ctx = bpf_set_run_ctx(&run_ctx->run_ctx);

	return NO_START_TIME;
}

static void notrace __bpf_prog_exit_lsm_cgroup(struct bpf_prog *prog, u64 start,
					       struct bpf_tramp_run_ctx *run_ctx)
	__releases(RCU)
{
	bpf_reset_run_ctx(run_ctx->saved_run_ctx);

	rcu_read_unlock_migrate();
}

u64 notrace __bpf_prog_enter_sleepable_recur(struct bpf_prog *prog,
					     struct bpf_tramp_run_ctx *run_ctx)
{
	rcu_read_lock_trace();
	migrate_disable();
	might_fault();

	run_ctx->saved_run_ctx = bpf_set_run_ctx(&run_ctx->run_ctx);

	if (unlikely(!bpf_prog_get_recursion_context(prog))) {
		bpf_prog_inc_misses_counter(prog);
		if (prog->aux->recursion_detected)
			prog->aux->recursion_detected(prog);
		return 0;
	}
	return bpf_prog_start_time();
}

void notrace __bpf_prog_exit_sleepable_recur(struct bpf_prog *prog, u64 start,
					     struct bpf_tramp_run_ctx *run_ctx)
{
	bpf_reset_run_ctx(run_ctx->saved_run_ctx);

	update_prog_stats(prog, start);
	bpf_prog_put_recursion_context(prog);
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
	rcu_read_lock_dont_migrate();

	run_ctx->saved_run_ctx = bpf_set_run_ctx(&run_ctx->run_ctx);

	return bpf_prog_start_time();
}

static void notrace __bpf_prog_exit(struct bpf_prog *prog, u64 start,
				    struct bpf_tramp_run_ctx *run_ctx)
	__releases(RCU)
{
	bpf_reset_run_ctx(run_ctx->saved_run_ctx);

	update_prog_stats(prog, start);
	rcu_read_unlock_migrate();
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
	bool sleepable = prog->sleepable;

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
	bool sleepable = prog->sleepable;

	if (bpf_prog_check_recur(prog))
		return sleepable ? __bpf_prog_exit_sleepable_recur :
			__bpf_prog_exit_recur;

	if (resolve_prog_type(prog) == BPF_PROG_TYPE_LSM &&
	    prog->expected_attach_type == BPF_LSM_CGROUP)
		return __bpf_prog_exit_lsm_cgroup;

	return sleepable ? __bpf_prog_exit_sleepable : __bpf_prog_exit;
}

int __weak
arch_prepare_bpf_trampoline(struct bpf_tramp_image *im, void *image, void *image_end,
			    const struct btf_func_model *m, u32 flags,
			    struct bpf_tramp_nodes *tnodes,
			    void *func_addr)
{
	return -ENOTSUPP;
}

void * __weak arch_alloc_bpf_trampoline(unsigned int size)
{
	void *image;

	if (WARN_ON_ONCE(size > PAGE_SIZE))
		return NULL;
	image = bpf_jit_alloc_exec(PAGE_SIZE);
	if (image)
		set_vm_flush_reset_perms(image);
	return image;
}

void __weak arch_free_bpf_trampoline(void *image, unsigned int size)
{
	WARN_ON_ONCE(size > PAGE_SIZE);
	/* bpf_jit_free_exec doesn't need "size", but
	 * bpf_prog_pack_free() needs it.
	 */
	bpf_jit_free_exec(image);
}

int __weak arch_protect_bpf_trampoline(void *image, unsigned int size)
{
	WARN_ON_ONCE(size > PAGE_SIZE);
	return set_memory_rox((long)image, 1);
}

int __weak arch_bpf_trampoline_size(const struct btf_func_model *m, u32 flags,
				    struct bpf_tramp_nodes *tnodes, void *func_addr)
{
	return -ENOTSUPP;
}

#if defined(CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS) && \
    defined(CONFIG_HAVE_SINGLE_FTRACE_DIRECT_OPS) && \
    defined(CONFIG_BPF_SYSCALL)

static void trampoline_lock_all(void)
{
	int i;

	for (i = 0; i < TRAMPOLINE_LOCKS_TABLE_SIZE; i++)
		mutex_lock(&trampoline_locks[i].mutex);
}

static void trampoline_unlock_all(void)
{
	int i;

	for (i = 0; i < TRAMPOLINE_LOCKS_TABLE_SIZE; i++)
		mutex_unlock(&trampoline_locks[i].mutex);
}

static void remove_tracing_multi_data(struct bpf_tracing_multi_data *data)
{
	ftrace_hash_remove(data->reg);
	ftrace_hash_remove(data->unreg);
	ftrace_hash_remove(data->modify);
}

static void clear_tracing_multi_data(struct bpf_tracing_multi_data *data)
{
	remove_tracing_multi_data(data);

	free_ftrace_hash(data->reg);
	free_ftrace_hash(data->unreg);
	free_ftrace_hash(data->modify);
}

static int init_tracing_multi_data(struct bpf_tracing_multi_data *data)
{
	data->reg    = alloc_ftrace_hash(FTRACE_HASH_DEFAULT_BITS);
	data->unreg  = alloc_ftrace_hash(FTRACE_HASH_DEFAULT_BITS);
	data->modify = alloc_ftrace_hash(FTRACE_HASH_DEFAULT_BITS);

	if (!data->reg || !data->unreg || !data->modify) {
		clear_tracing_multi_data(data);
		return -ENOMEM;
	}
	return 0;
}

static void ftrace_hash_add(struct ftrace_hash *hash, struct ftrace_func_entry *entry,
			    unsigned long ip, unsigned long direct)
{
	entry->ip = ip;
	entry->direct = direct;
	add_ftrace_hash_entry(hash, entry);
}

static int register_fentry_multi(struct bpf_trampoline *tr, struct bpf_tramp_image *im, void *ptr)
{
	unsigned long addr = (unsigned long) im->image;
	unsigned long ip = ftrace_location(tr->ip);
	struct bpf_tracing_multi_data *data = ptr;

	if (bpf_trampoline_use_jmp(tr->flags))
		addr = ftrace_jmp_set(addr);

	ftrace_hash_add(data->reg, data->entry, ip, addr);
	tr->cur_image = im;
	return 0;
}

static int unregister_fentry_multi(struct bpf_trampoline *tr, u32 orig_flags, void *ptr)
{
	unsigned long addr = (unsigned long) tr->cur_image->image;
	unsigned long ip = ftrace_location(tr->ip);
	struct bpf_tracing_multi_data *data = ptr;

	if (bpf_trampoline_use_jmp(tr->flags))
		addr = ftrace_jmp_set(addr);

	ftrace_hash_add(data->unreg, data->entry, ip, addr);
	tr->cur_image = NULL;
	return 0;
}

static int modify_fentry_multi(struct bpf_trampoline *tr, u32 orig_flags, struct bpf_tramp_image *im,
			       bool lock_direct_mutex, void *ptr)
{
	unsigned long addr = (unsigned long) im->image;
	unsigned long ip = ftrace_location(tr->ip);
	struct bpf_tracing_multi_data *data = ptr;

	if (bpf_trampoline_use_jmp(tr->flags))
		addr = ftrace_jmp_set(addr);

	ftrace_hash_add(data->modify, data->entry, ip, addr);
	tr->cur_image = im;
	return 0;
}

static const struct bpf_trampoline_ops trampoline_multi_ops = {
	.register_fentry   = register_fentry_multi,
	.unregister_fentry = unregister_fentry_multi,
	.modify_fentry     = modify_fentry_multi,
};

static void bpf_trampoline_multi_attach_init(struct bpf_trampoline *tr)
{
	tr->multi_attach.old_image = tr->cur_image;
	tr->multi_attach.old_flags = tr->flags;
}

static void bpf_trampoline_multi_attach_free(struct bpf_trampoline *tr)
{
	if (tr->multi_attach.old_image)
		bpf_tramp_image_put(tr->multi_attach.old_image);

	tr->multi_attach.old_image = NULL;
	tr->multi_attach.old_flags = 0;
}

static void bpf_trampoline_multi_attach_rollback(struct bpf_trampoline *tr)
{
	if (tr->cur_image)
		bpf_tramp_image_put(tr->cur_image);
	tr->cur_image = tr->multi_attach.old_image;
	tr->flags = tr->multi_attach.old_flags;

	tr->multi_attach.old_image = NULL;
	tr->multi_attach.old_flags = 0;
}

#define for_each_mnode_cnt(mnode, link, cnt) \
	for (i = 0, mnode = &link->nodes[i]; i < cnt; i++, mnode = &link->nodes[i])

#define for_each_mnode(mnode, link) \
	for_each_mnode_cnt(mnode, link, link->nodes_cnt)

int bpf_trampoline_multi_attach(struct bpf_prog *prog, u32 *ids,
				struct bpf_tracing_multi_link *link)
{
	struct bpf_tracing_multi_data *data = &link->data;
	struct bpf_attach_target_info tgt_info = {};
	struct btf *btf = prog->aux->attach_btf;
	struct bpf_tracing_multi_node *mnode;
	struct bpf_trampoline *tr;
	int i, err, rollback_cnt;
	u64 key;

	for_each_mnode(mnode, link) {
		rollback_cnt = i;

		err = bpf_check_attach_btf_id_multi(btf, prog, ids[i], &tgt_info);
		if (err)
			goto rollback_put;

		key = bpf_trampoline_compute_key(NULL, btf, ids[i]);

		tr = bpf_trampoline_get(key, &tgt_info);
		if (!tr) {
			err = -ENOMEM;
			goto rollback_put;
		}

		mnode->trampoline = tr;
		mnode->node.link = &link->link;
		mnode->node.cookie = link->cookies ? link->cookies[i] : 0;

		if (prog->expected_attach_type == BPF_TRACE_FSESSION_MULTI) {
			link->fexits[i].link = &link->link;
			link->fexits[i].cookie = link->cookies ? link->cookies[i] : 0;
		}

		cond_resched();
	}

	err = init_tracing_multi_data(data);
	if (err) {
		rollback_cnt = link->nodes_cnt;
		goto rollback_put;
	}

	trampoline_lock_all();

	for_each_mnode(mnode, link) {
		bpf_trampoline_multi_attach_init(mnode->trampoline);

		data->entry = &mnode->entry;
		err = __bpf_trampoline_link_prog(&mnode->node, mnode->trampoline, NULL,
						 &trampoline_multi_ops, data);
		if (err) {
			rollback_cnt = i;
			goto rollback_unlink;
		}
	}

	rollback_cnt = link->nodes_cnt;
	if (ftrace_hash_count(data->reg)) {
		err = update_ftrace_direct_add(&direct_ops, data->reg);
		if (err)
			goto rollback_unlink;
	}

	if (ftrace_hash_count(data->modify)) {
		err = update_ftrace_direct_mod(&direct_ops, data->modify, true);
		if (err) {
			if (ftrace_hash_count(data->reg))
				WARN_ON_ONCE(update_ftrace_direct_del(&direct_ops, data->reg));
			goto rollback_unlink;
		}
	}

	for_each_mnode(mnode, link)
		bpf_trampoline_multi_attach_free(mnode->trampoline);

	trampoline_unlock_all();

	remove_tracing_multi_data(data);
	return 0;

rollback_unlink:
	for_each_mnode_cnt(mnode, link, rollback_cnt) {
		bpf_trampoline_remove_prog(mnode->trampoline, &mnode->node);
		bpf_trampoline_multi_attach_rollback(mnode->trampoline);
	}

	trampoline_unlock_all();

	clear_tracing_multi_data(data);
	rollback_cnt = link->nodes_cnt;

rollback_put:
	for_each_mnode_cnt(mnode, link, rollback_cnt)
		bpf_trampoline_put(mnode->trampoline);

	return err;
}

int bpf_trampoline_multi_detach(struct bpf_prog *prog, struct bpf_tracing_multi_link *link)
{
	struct bpf_tracing_multi_data *data = &link->data;
	struct bpf_tracing_multi_node *mnode;
	int i;

	trampoline_lock_all();

	for_each_mnode(mnode, link) {
		data->entry = &mnode->entry;
		bpf_trampoline_multi_attach_init(mnode->trampoline);
		WARN_ON_ONCE(__bpf_trampoline_unlink_prog(&mnode->node, mnode->trampoline,
					NULL, &trampoline_multi_ops, data));
	}

	if (ftrace_hash_count(data->unreg))
		WARN_ON_ONCE(update_ftrace_direct_del(&direct_ops, data->unreg));
	if (ftrace_hash_count(data->modify))
		WARN_ON_ONCE(update_ftrace_direct_mod(&direct_ops, data->modify, true));

	for_each_mnode(mnode, link)
		bpf_trampoline_multi_attach_free(mnode->trampoline);

	trampoline_unlock_all();

	for_each_mnode(mnode, link)
		bpf_trampoline_put(mnode->trampoline);

	clear_tracing_multi_data(data);
	return 0;
}

#undef for_each_mnode_cnt
#undef for_each_mnode

#endif /* CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS &&
	  CONFIG_HAVE_SINGLE_FTRACE_DIRECT_OPS &&
	  CONFIG_BPF_SYSCALL */

static int __init init_trampolines(void)
{
	int i;

	for (i = 0; i < TRAMPOLINE_TABLE_SIZE; i++)
		INIT_HLIST_HEAD(&trampoline_key_table[i]);
	for (i = 0; i < TRAMPOLINE_TABLE_SIZE; i++)
		INIT_HLIST_HEAD(&trampoline_ip_table[i]);
	for (i = 0; i < TRAMPOLINE_LOCKS_TABLE_SIZE; i++)
		__mutex_init(&trampoline_locks[i].mutex, "trampoline_lock", &trampoline_locks[i].key);
	return 0;
}
late_initcall(init_trampolines);
