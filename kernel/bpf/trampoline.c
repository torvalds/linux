// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019 Facebook */
#include <linux/hash.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/ftrace.h>

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

void *bpf_jit_alloc_exec_page(void)
{
	void *image;

	image = bpf_jit_alloc_exec(PAGE_SIZE);
	if (!image)
		return NULL;

	set_vm_flush_reset_perms(image);
	/* Keep image as writeable. The alternative is to keep flipping ro/rw
	 * everytime new program is attached or detached.
	 */
	set_memory_x((long)image, 1);
	return image;
}

struct bpf_trampoline *bpf_trampoline_lookup(u64 key)
{
	struct bpf_trampoline *tr;
	struct hlist_head *head;
	void *image;
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

	/* is_root was checked earlier. No need for bpf_jit_charge_modmem() */
	image = bpf_jit_alloc_exec_page();
	if (!image) {
		kfree(tr);
		tr = NULL;
		goto out;
	}

	tr->key = key;
	INIT_HLIST_NODE(&tr->hlist);
	hlist_add_head(&tr->hlist, head);
	refcount_set(&tr->refcnt, 1);
	mutex_init(&tr->mutex);
	for (i = 0; i < BPF_TRAMP_MAX; i++)
		INIT_HLIST_HEAD(&tr->progs_hlist[i]);
	tr->image = image;
out:
	mutex_unlock(&trampoline_mutex);
	return tr;
}

static int is_ftrace_location(void *ip)
{
	long addr;

	addr = ftrace_location((long)ip);
	if (!addr)
		return 0;
	if (WARN_ON_ONCE(addr != (long)ip))
		return -EFAULT;
	return 1;
}

static int unregister_fentry(struct bpf_trampoline *tr, void *old_addr)
{
	void *ip = tr->func.addr;
	int ret;

	if (tr->func.ftrace_managed)
		ret = unregister_ftrace_direct((long)ip, (long)old_addr);
	else
		ret = bpf_arch_text_poke(ip, BPF_MOD_CALL, old_addr, NULL);
	return ret;
}

static int modify_fentry(struct bpf_trampoline *tr, void *old_addr, void *new_addr)
{
	void *ip = tr->func.addr;
	int ret;

	if (tr->func.ftrace_managed)
		ret = modify_ftrace_direct((long)ip, (long)old_addr, (long)new_addr);
	else
		ret = bpf_arch_text_poke(ip, BPF_MOD_CALL, old_addr, new_addr);
	return ret;
}

/* first time registering */
static int register_fentry(struct bpf_trampoline *tr, void *new_addr)
{
	void *ip = tr->func.addr;
	int ret;

	ret = is_ftrace_location(ip);
	if (ret < 0)
		return ret;
	tr->func.ftrace_managed = ret;

	if (tr->func.ftrace_managed)
		ret = register_ftrace_direct((long)ip, (long)new_addr);
	else
		ret = bpf_arch_text_poke(ip, BPF_MOD_CALL, NULL, new_addr);
	return ret;
}

/* Each call __bpf_prog_enter + call bpf_func + call __bpf_prog_exit is ~50
 * bytes on x86.  Pick a number to fit into PAGE_SIZE / 2
 */
#define BPF_MAX_TRAMP_PROGS 40

static int bpf_trampoline_update(struct bpf_trampoline *tr)
{
	void *old_image = tr->image + ((tr->selector + 1) & 1) * PAGE_SIZE/2;
	void *new_image = tr->image + (tr->selector & 1) * PAGE_SIZE/2;
	struct bpf_prog *progs_to_run[BPF_MAX_TRAMP_PROGS];
	int fentry_cnt = tr->progs_cnt[BPF_TRAMP_FENTRY];
	int fexit_cnt = tr->progs_cnt[BPF_TRAMP_FEXIT];
	struct bpf_prog **progs, **fentry, **fexit;
	u32 flags = BPF_TRAMP_F_RESTORE_REGS;
	struct bpf_prog_aux *aux;
	int err;

	if (fentry_cnt + fexit_cnt == 0) {
		err = unregister_fentry(tr, old_image);
		tr->selector = 0;
		goto out;
	}

	/* populate fentry progs */
	fentry = progs = progs_to_run;
	hlist_for_each_entry(aux, &tr->progs_hlist[BPF_TRAMP_FENTRY], tramp_hlist)
		*progs++ = aux->prog;

	/* populate fexit progs */
	fexit = progs;
	hlist_for_each_entry(aux, &tr->progs_hlist[BPF_TRAMP_FEXIT], tramp_hlist)
		*progs++ = aux->prog;

	if (fexit_cnt)
		flags = BPF_TRAMP_F_CALL_ORIG | BPF_TRAMP_F_SKIP_FRAME;

	/* Though the second half of trampoline page is unused a task could be
	 * preempted in the middle of the first half of trampoline and two
	 * updates to trampoline would change the code from underneath the
	 * preempted task. Hence wait for tasks to voluntarily schedule or go
	 * to userspace.
	 */
	synchronize_rcu_tasks();

	err = arch_prepare_bpf_trampoline(new_image, new_image + PAGE_SIZE / 2,
					  &tr->func.model, flags,
					  fentry, fentry_cnt,
					  fexit, fexit_cnt,
					  tr->func.addr);
	if (err < 0)
		goto out;

	if (tr->selector)
		/* progs already running at this address */
		err = modify_fentry(tr, old_image, new_image);
	else
		/* first time registering */
		err = register_fentry(tr, new_image);
	if (err)
		goto out;
	tr->selector++;
out:
	return err;
}

static enum bpf_tramp_prog_type bpf_attach_type_to_tramp(enum bpf_attach_type t)
{
	switch (t) {
	case BPF_TRACE_FENTRY:
		return BPF_TRAMP_FENTRY;
	case BPF_TRACE_FEXIT:
		return BPF_TRAMP_FEXIT;
	default:
		return BPF_TRAMP_REPLACE;
	}
}

int bpf_trampoline_link_prog(struct bpf_prog *prog)
{
	enum bpf_tramp_prog_type kind;
	struct bpf_trampoline *tr;
	int err = 0;
	int cnt;

	tr = prog->aux->trampoline;
	kind = bpf_attach_type_to_tramp(prog->expected_attach_type);
	mutex_lock(&tr->mutex);
	if (tr->extension_prog) {
		/* cannot attach fentry/fexit if extension prog is attached.
		 * cannot overwrite extension prog either.
		 */
		err = -EBUSY;
		goto out;
	}
	cnt = tr->progs_cnt[BPF_TRAMP_FENTRY] + tr->progs_cnt[BPF_TRAMP_FEXIT];
	if (kind == BPF_TRAMP_REPLACE) {
		/* Cannot attach extension if fentry/fexit are in use. */
		if (cnt) {
			err = -EBUSY;
			goto out;
		}
		tr->extension_prog = prog;
		err = bpf_arch_text_poke(tr->func.addr, BPF_MOD_JUMP, NULL,
					 prog->bpf_func);
		goto out;
	}
	if (cnt >= BPF_MAX_TRAMP_PROGS) {
		err = -E2BIG;
		goto out;
	}
	if (!hlist_unhashed(&prog->aux->tramp_hlist)) {
		/* prog already linked */
		err = -EBUSY;
		goto out;
	}
	hlist_add_head(&prog->aux->tramp_hlist, &tr->progs_hlist[kind]);
	tr->progs_cnt[kind]++;
	err = bpf_trampoline_update(prog->aux->trampoline);
	if (err) {
		hlist_del(&prog->aux->tramp_hlist);
		tr->progs_cnt[kind]--;
	}
out:
	mutex_unlock(&tr->mutex);
	return err;
}

/* bpf_trampoline_unlink_prog() should never fail. */
int bpf_trampoline_unlink_prog(struct bpf_prog *prog)
{
	enum bpf_tramp_prog_type kind;
	struct bpf_trampoline *tr;
	int err;

	tr = prog->aux->trampoline;
	kind = bpf_attach_type_to_tramp(prog->expected_attach_type);
	mutex_lock(&tr->mutex);
	if (kind == BPF_TRAMP_REPLACE) {
		WARN_ON_ONCE(!tr->extension_prog);
		err = bpf_arch_text_poke(tr->func.addr, BPF_MOD_JUMP,
					 tr->extension_prog->bpf_func, NULL);
		tr->extension_prog = NULL;
		goto out;
	}
	hlist_del(&prog->aux->tramp_hlist);
	tr->progs_cnt[kind]--;
	err = bpf_trampoline_update(prog->aux->trampoline);
out:
	mutex_unlock(&tr->mutex);
	return err;
}

void bpf_trampoline_put(struct bpf_trampoline *tr)
{
	if (!tr)
		return;
	mutex_lock(&trampoline_mutex);
	if (!refcount_dec_and_test(&tr->refcnt))
		goto out;
	WARN_ON_ONCE(mutex_is_locked(&tr->mutex));
	if (WARN_ON_ONCE(!hlist_empty(&tr->progs_hlist[BPF_TRAMP_FENTRY])))
		goto out;
	if (WARN_ON_ONCE(!hlist_empty(&tr->progs_hlist[BPF_TRAMP_FEXIT])))
		goto out;
	/* wait for tasks to get out of trampoline before freeing it */
	synchronize_rcu_tasks();
	bpf_jit_free_exec(tr->image);
	hlist_del(&tr->hlist);
	kfree(tr);
out:
	mutex_unlock(&trampoline_mutex);
}

/* The logic is similar to BPF_PROG_RUN, but with explicit rcu and preempt that
 * are needed for trampoline. The macro is split into
 * call _bpf_prog_enter
 * call prog->bpf_func
 * call __bpf_prog_exit
 */
u64 notrace __bpf_prog_enter(void)
{
	u64 start = 0;

	rcu_read_lock();
	preempt_disable();
	if (static_branch_unlikely(&bpf_stats_enabled_key))
		start = sched_clock();
	return start;
}

void notrace __bpf_prog_exit(struct bpf_prog *prog, u64 start)
{
	struct bpf_prog_stats *stats;

	if (static_branch_unlikely(&bpf_stats_enabled_key) &&
	    /* static_key could be enabled in __bpf_prog_enter
	     * and disabled in __bpf_prog_exit.
	     * And vice versa.
	     * Hence check that 'start' is not zero.
	     */
	    start) {
		stats = this_cpu_ptr(prog->aux->stats);
		u64_stats_update_begin(&stats->syncp);
		stats->cnt++;
		stats->nsecs += sched_clock() - start;
		u64_stats_update_end(&stats->syncp);
	}
	preempt_enable();
	rcu_read_unlock();
}

int __weak
arch_prepare_bpf_trampoline(void *image, void *image_end,
			    const struct btf_func_model *m, u32 flags,
			    struct bpf_prog **fentry_progs, int fentry_cnt,
			    struct bpf_prog **fexit_progs, int fexit_cnt,
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
