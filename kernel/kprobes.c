// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Kernel Probes (KProbes)
 *  kernel/kprobes.c
 *
 * Copyright (C) IBM Corporation, 2002, 2004
 *
 * 2002-Oct	Created by Vamsi Krishna S <vamsi_krishna@in.ibm.com> Kernel
 *		Probes initial implementation (includes suggestions from
 *		Rusty Russell).
 * 2004-Aug	Updated by Prasanna S Panchamukhi <prasanna@in.ibm.com> with
 *		hlists and exceptions notifier as suggested by Andi Kleen.
 * 2004-July	Suparna Bhattacharya <suparna@in.ibm.com> added jumper probes
 *		interface to access function arguments.
 * 2004-Sep	Prasanna S Panchamukhi <prasanna@in.ibm.com> Changed Kprobes
 *		exceptions notifier to be first on the priority list.
 * 2005-May	Hien Nguyen <hien@us.ibm.com>, Jim Keniston
 *		<jkenisto@us.ibm.com> and Prasanna S Panchamukhi
 *		<prasanna@in.ibm.com> added function-return probes.
 */
#include <linux/kprobes.h>
#include <linux/hash.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/export.h>
#include <linux/moduleloader.h>
#include <linux/kallsyms.h>
#include <linux/freezer.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/sysctl.h>
#include <linux/kdebug.h>
#include <linux/memory.h>
#include <linux/ftrace.h>
#include <linux/cpu.h>
#include <linux/jump_label.h>
#include <linux/static_call.h>
#include <linux/perf_event.h>

#include <asm/sections.h>
#include <asm/cacheflush.h>
#include <asm/errno.h>
#include <linux/uaccess.h>

#define KPROBE_HASH_BITS 6
#define KPROBE_TABLE_SIZE (1 << KPROBE_HASH_BITS)


static int kprobes_initialized;
/* kprobe_table can be accessed by
 * - Normal hlist traversal and RCU add/del under kprobe_mutex is held.
 * Or
 * - RCU hlist traversal under disabling preempt (breakpoint handlers)
 */
static struct hlist_head kprobe_table[KPROBE_TABLE_SIZE];

/* NOTE: change this value only with kprobe_mutex held */
static bool kprobes_all_disarmed;

/* This protects kprobe_table and optimizing_list */
static DEFINE_MUTEX(kprobe_mutex);
static DEFINE_PER_CPU(struct kprobe *, kprobe_instance) = NULL;

kprobe_opcode_t * __weak kprobe_lookup_name(const char *name,
					unsigned int __unused)
{
	return ((kprobe_opcode_t *)(kallsyms_lookup_name(name)));
}

/* Blacklist -- list of struct kprobe_blacklist_entry */
static LIST_HEAD(kprobe_blacklist);

#ifdef __ARCH_WANT_KPROBES_INSN_SLOT
/*
 * kprobe->ainsn.insn points to the copy of the instruction to be
 * single-stepped. x86_64, POWER4 and above have no-exec support and
 * stepping on the instruction on a vmalloced/kmalloced/data page
 * is a recipe for disaster
 */
struct kprobe_insn_page {
	struct list_head list;
	kprobe_opcode_t *insns;		/* Page of instruction slots */
	struct kprobe_insn_cache *cache;
	int nused;
	int ngarbage;
	char slot_used[];
};

#define KPROBE_INSN_PAGE_SIZE(slots)			\
	(offsetof(struct kprobe_insn_page, slot_used) +	\
	 (sizeof(char) * (slots)))

static int slots_per_page(struct kprobe_insn_cache *c)
{
	return PAGE_SIZE/(c->insn_size * sizeof(kprobe_opcode_t));
}

enum kprobe_slot_state {
	SLOT_CLEAN = 0,
	SLOT_DIRTY = 1,
	SLOT_USED = 2,
};

void __weak *alloc_insn_page(void)
{
	return module_alloc(PAGE_SIZE);
}

static void free_insn_page(void *page)
{
	module_memfree(page);
}

struct kprobe_insn_cache kprobe_insn_slots = {
	.mutex = __MUTEX_INITIALIZER(kprobe_insn_slots.mutex),
	.alloc = alloc_insn_page,
	.free = free_insn_page,
	.sym = KPROBE_INSN_PAGE_SYM,
	.pages = LIST_HEAD_INIT(kprobe_insn_slots.pages),
	.insn_size = MAX_INSN_SIZE,
	.nr_garbage = 0,
};
static int collect_garbage_slots(struct kprobe_insn_cache *c);

/**
 * __get_insn_slot() - Find a slot on an executable page for an instruction.
 * We allocate an executable page if there's no room on existing ones.
 */
kprobe_opcode_t *__get_insn_slot(struct kprobe_insn_cache *c)
{
	struct kprobe_insn_page *kip;
	kprobe_opcode_t *slot = NULL;

	/* Since the slot array is not protected by rcu, we need a mutex */
	mutex_lock(&c->mutex);
 retry:
	rcu_read_lock();
	list_for_each_entry_rcu(kip, &c->pages, list) {
		if (kip->nused < slots_per_page(c)) {
			int i;
			for (i = 0; i < slots_per_page(c); i++) {
				if (kip->slot_used[i] == SLOT_CLEAN) {
					kip->slot_used[i] = SLOT_USED;
					kip->nused++;
					slot = kip->insns + (i * c->insn_size);
					rcu_read_unlock();
					goto out;
				}
			}
			/* kip->nused is broken. Fix it. */
			kip->nused = slots_per_page(c);
			WARN_ON(1);
		}
	}
	rcu_read_unlock();

	/* If there are any garbage slots, collect it and try again. */
	if (c->nr_garbage && collect_garbage_slots(c) == 0)
		goto retry;

	/* All out of space.  Need to allocate a new page. */
	kip = kmalloc(KPROBE_INSN_PAGE_SIZE(slots_per_page(c)), GFP_KERNEL);
	if (!kip)
		goto out;

	/*
	 * Use module_alloc so this page is within +/- 2GB of where the
	 * kernel image and loaded module images reside. This is required
	 * so x86_64 can correctly handle the %rip-relative fixups.
	 */
	kip->insns = c->alloc();
	if (!kip->insns) {
		kfree(kip);
		goto out;
	}
	INIT_LIST_HEAD(&kip->list);
	memset(kip->slot_used, SLOT_CLEAN, slots_per_page(c));
	kip->slot_used[0] = SLOT_USED;
	kip->nused = 1;
	kip->ngarbage = 0;
	kip->cache = c;
	list_add_rcu(&kip->list, &c->pages);
	slot = kip->insns;

	/* Record the perf ksymbol register event after adding the page */
	perf_event_ksymbol(PERF_RECORD_KSYMBOL_TYPE_OOL, (unsigned long)kip->insns,
			   PAGE_SIZE, false, c->sym);
out:
	mutex_unlock(&c->mutex);
	return slot;
}

/* Return 1 if all garbages are collected, otherwise 0. */
static int collect_one_slot(struct kprobe_insn_page *kip, int idx)
{
	kip->slot_used[idx] = SLOT_CLEAN;
	kip->nused--;
	if (kip->nused == 0) {
		/*
		 * Page is no longer in use.  Free it unless
		 * it's the last one.  We keep the last one
		 * so as not to have to set it up again the
		 * next time somebody inserts a probe.
		 */
		if (!list_is_singular(&kip->list)) {
			/*
			 * Record perf ksymbol unregister event before removing
			 * the page.
			 */
			perf_event_ksymbol(PERF_RECORD_KSYMBOL_TYPE_OOL,
					   (unsigned long)kip->insns, PAGE_SIZE, true,
					   kip->cache->sym);
			list_del_rcu(&kip->list);
			synchronize_rcu();
			kip->cache->free(kip->insns);
			kfree(kip);
		}
		return 1;
	}
	return 0;
}

static int collect_garbage_slots(struct kprobe_insn_cache *c)
{
	struct kprobe_insn_page *kip, *next;

	/* Ensure no-one is interrupted on the garbages */
	synchronize_rcu();

	list_for_each_entry_safe(kip, next, &c->pages, list) {
		int i;
		if (kip->ngarbage == 0)
			continue;
		kip->ngarbage = 0;	/* we will collect all garbages */
		for (i = 0; i < slots_per_page(c); i++) {
			if (kip->slot_used[i] == SLOT_DIRTY && collect_one_slot(kip, i))
				break;
		}
	}
	c->nr_garbage = 0;
	return 0;
}

void __free_insn_slot(struct kprobe_insn_cache *c,
		      kprobe_opcode_t *slot, int dirty)
{
	struct kprobe_insn_page *kip;
	long idx;

	mutex_lock(&c->mutex);
	rcu_read_lock();
	list_for_each_entry_rcu(kip, &c->pages, list) {
		idx = ((long)slot - (long)kip->insns) /
			(c->insn_size * sizeof(kprobe_opcode_t));
		if (idx >= 0 && idx < slots_per_page(c))
			goto out;
	}
	/* Could not find this slot. */
	WARN_ON(1);
	kip = NULL;
out:
	rcu_read_unlock();
	/* Mark and sweep: this may sleep */
	if (kip) {
		/* Check double free */
		WARN_ON(kip->slot_used[idx] != SLOT_USED);
		if (dirty) {
			kip->slot_used[idx] = SLOT_DIRTY;
			kip->ngarbage++;
			if (++c->nr_garbage > slots_per_page(c))
				collect_garbage_slots(c);
		} else {
			collect_one_slot(kip, idx);
		}
	}
	mutex_unlock(&c->mutex);
}

/*
 * Check given address is on the page of kprobe instruction slots.
 * This will be used for checking whether the address on a stack
 * is on a text area or not.
 */
bool __is_insn_slot_addr(struct kprobe_insn_cache *c, unsigned long addr)
{
	struct kprobe_insn_page *kip;
	bool ret = false;

	rcu_read_lock();
	list_for_each_entry_rcu(kip, &c->pages, list) {
		if (addr >= (unsigned long)kip->insns &&
		    addr < (unsigned long)kip->insns + PAGE_SIZE) {
			ret = true;
			break;
		}
	}
	rcu_read_unlock();

	return ret;
}

int kprobe_cache_get_kallsym(struct kprobe_insn_cache *c, unsigned int *symnum,
			     unsigned long *value, char *type, char *sym)
{
	struct kprobe_insn_page *kip;
	int ret = -ERANGE;

	rcu_read_lock();
	list_for_each_entry_rcu(kip, &c->pages, list) {
		if ((*symnum)--)
			continue;
		strlcpy(sym, c->sym, KSYM_NAME_LEN);
		*type = 't';
		*value = (unsigned long)kip->insns;
		ret = 0;
		break;
	}
	rcu_read_unlock();

	return ret;
}

#ifdef CONFIG_OPTPROBES
void __weak *alloc_optinsn_page(void)
{
	return alloc_insn_page();
}

void __weak free_optinsn_page(void *page)
{
	free_insn_page(page);
}

/* For optimized_kprobe buffer */
struct kprobe_insn_cache kprobe_optinsn_slots = {
	.mutex = __MUTEX_INITIALIZER(kprobe_optinsn_slots.mutex),
	.alloc = alloc_optinsn_page,
	.free = free_optinsn_page,
	.sym = KPROBE_OPTINSN_PAGE_SYM,
	.pages = LIST_HEAD_INIT(kprobe_optinsn_slots.pages),
	/* .insn_size is initialized later */
	.nr_garbage = 0,
};
#endif
#endif

/* We have preemption disabled.. so it is safe to use __ versions */
static inline void set_kprobe_instance(struct kprobe *kp)
{
	__this_cpu_write(kprobe_instance, kp);
}

static inline void reset_kprobe_instance(void)
{
	__this_cpu_write(kprobe_instance, NULL);
}

/*
 * This routine is called either:
 * 	- under the kprobe_mutex - during kprobe_[un]register()
 * 				OR
 * 	- with preemption disabled - from arch/xxx/kernel/kprobes.c
 */
struct kprobe *get_kprobe(void *addr)
{
	struct hlist_head *head;
	struct kprobe *p;

	head = &kprobe_table[hash_ptr(addr, KPROBE_HASH_BITS)];
	hlist_for_each_entry_rcu(p, head, hlist,
				 lockdep_is_held(&kprobe_mutex)) {
		if (p->addr == addr)
			return p;
	}

	return NULL;
}
NOKPROBE_SYMBOL(get_kprobe);

static int aggr_pre_handler(struct kprobe *p, struct pt_regs *regs);

/* Return true if the kprobe is an aggregator */
static inline int kprobe_aggrprobe(struct kprobe *p)
{
	return p->pre_handler == aggr_pre_handler;
}

/* Return true(!0) if the kprobe is unused */
static inline int kprobe_unused(struct kprobe *p)
{
	return kprobe_aggrprobe(p) && kprobe_disabled(p) &&
	       list_empty(&p->list);
}

/*
 * Keep all fields in the kprobe consistent
 */
static inline void copy_kprobe(struct kprobe *ap, struct kprobe *p)
{
	memcpy(&p->opcode, &ap->opcode, sizeof(kprobe_opcode_t));
	memcpy(&p->ainsn, &ap->ainsn, sizeof(struct arch_specific_insn));
}

#ifdef CONFIG_OPTPROBES
/* NOTE: change this value only with kprobe_mutex held */
static bool kprobes_allow_optimization;

/*
 * Call all pre_handler on the list, but ignores its return value.
 * This must be called from arch-dep optimized caller.
 */
void opt_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe *kp;

	list_for_each_entry_rcu(kp, &p->list, list) {
		if (kp->pre_handler && likely(!kprobe_disabled(kp))) {
			set_kprobe_instance(kp);
			kp->pre_handler(kp, regs);
		}
		reset_kprobe_instance();
	}
}
NOKPROBE_SYMBOL(opt_pre_handler);

/* Free optimized instructions and optimized_kprobe */
static void free_aggr_kprobe(struct kprobe *p)
{
	struct optimized_kprobe *op;

	op = container_of(p, struct optimized_kprobe, kp);
	arch_remove_optimized_kprobe(op);
	arch_remove_kprobe(p);
	kfree(op);
}

/* Return true(!0) if the kprobe is ready for optimization. */
static inline int kprobe_optready(struct kprobe *p)
{
	struct optimized_kprobe *op;

	if (kprobe_aggrprobe(p)) {
		op = container_of(p, struct optimized_kprobe, kp);
		return arch_prepared_optinsn(&op->optinsn);
	}

	return 0;
}

/* Return true(!0) if the kprobe is disarmed. Note: p must be on hash list */
static inline int kprobe_disarmed(struct kprobe *p)
{
	struct optimized_kprobe *op;

	/* If kprobe is not aggr/opt probe, just return kprobe is disabled */
	if (!kprobe_aggrprobe(p))
		return kprobe_disabled(p);

	op = container_of(p, struct optimized_kprobe, kp);

	return kprobe_disabled(p) && list_empty(&op->list);
}

/* Return true(!0) if the probe is queued on (un)optimizing lists */
static int kprobe_queued(struct kprobe *p)
{
	struct optimized_kprobe *op;

	if (kprobe_aggrprobe(p)) {
		op = container_of(p, struct optimized_kprobe, kp);
		if (!list_empty(&op->list))
			return 1;
	}
	return 0;
}

/*
 * Return an optimized kprobe whose optimizing code replaces
 * instructions including addr (exclude breakpoint).
 */
static struct kprobe *get_optimized_kprobe(unsigned long addr)
{
	int i;
	struct kprobe *p = NULL;
	struct optimized_kprobe *op;

	/* Don't check i == 0, since that is a breakpoint case. */
	for (i = 1; !p && i < MAX_OPTIMIZED_LENGTH; i++)
		p = get_kprobe((void *)(addr - i));

	if (p && kprobe_optready(p)) {
		op = container_of(p, struct optimized_kprobe, kp);
		if (arch_within_optimized_kprobe(op, addr))
			return p;
	}

	return NULL;
}

/* Optimization staging list, protected by kprobe_mutex */
static LIST_HEAD(optimizing_list);
static LIST_HEAD(unoptimizing_list);
static LIST_HEAD(freeing_list);

static void kprobe_optimizer(struct work_struct *work);
static DECLARE_DELAYED_WORK(optimizing_work, kprobe_optimizer);
#define OPTIMIZE_DELAY 5

/*
 * Optimize (replace a breakpoint with a jump) kprobes listed on
 * optimizing_list.
 */
static void do_optimize_kprobes(void)
{
	lockdep_assert_held(&text_mutex);
	/*
	 * The optimization/unoptimization refers online_cpus via
	 * stop_machine() and cpu-hotplug modifies online_cpus.
	 * And same time, text_mutex will be held in cpu-hotplug and here.
	 * This combination can cause a deadlock (cpu-hotplug try to lock
	 * text_mutex but stop_machine can not be done because online_cpus
	 * has been changed)
	 * To avoid this deadlock, caller must have locked cpu hotplug
	 * for preventing cpu-hotplug outside of text_mutex locking.
	 */
	lockdep_assert_cpus_held();

	/* Optimization never be done when disarmed */
	if (kprobes_all_disarmed || !kprobes_allow_optimization ||
	    list_empty(&optimizing_list))
		return;

	arch_optimize_kprobes(&optimizing_list);
}

/*
 * Unoptimize (replace a jump with a breakpoint and remove the breakpoint
 * if need) kprobes listed on unoptimizing_list.
 */
static void do_unoptimize_kprobes(void)
{
	struct optimized_kprobe *op, *tmp;

	lockdep_assert_held(&text_mutex);
	/* See comment in do_optimize_kprobes() */
	lockdep_assert_cpus_held();

	/* Unoptimization must be done anytime */
	if (list_empty(&unoptimizing_list))
		return;

	arch_unoptimize_kprobes(&unoptimizing_list, &freeing_list);
	/* Loop free_list for disarming */
	list_for_each_entry_safe(op, tmp, &freeing_list, list) {
		/* Switching from detour code to origin */
		op->kp.flags &= ~KPROBE_FLAG_OPTIMIZED;
		/* Disarm probes if marked disabled */
		if (kprobe_disabled(&op->kp))
			arch_disarm_kprobe(&op->kp);
		if (kprobe_unused(&op->kp)) {
			/*
			 * Remove unused probes from hash list. After waiting
			 * for synchronization, these probes are reclaimed.
			 * (reclaiming is done by do_free_cleaned_kprobes.)
			 */
			hlist_del_rcu(&op->kp.hlist);
		} else
			list_del_init(&op->list);
	}
}

/* Reclaim all kprobes on the free_list */
static void do_free_cleaned_kprobes(void)
{
	struct optimized_kprobe *op, *tmp;

	list_for_each_entry_safe(op, tmp, &freeing_list, list) {
		list_del_init(&op->list);
		if (WARN_ON_ONCE(!kprobe_unused(&op->kp))) {
			/*
			 * This must not happen, but if there is a kprobe
			 * still in use, keep it on kprobes hash list.
			 */
			continue;
		}
		free_aggr_kprobe(&op->kp);
	}
}

/* Start optimizer after OPTIMIZE_DELAY passed */
static void kick_kprobe_optimizer(void)
{
	schedule_delayed_work(&optimizing_work, OPTIMIZE_DELAY);
}

/* Kprobe jump optimizer */
static void kprobe_optimizer(struct work_struct *work)
{
	mutex_lock(&kprobe_mutex);
	cpus_read_lock();
	mutex_lock(&text_mutex);

	/*
	 * Step 1: Unoptimize kprobes and collect cleaned (unused and disarmed)
	 * kprobes before waiting for quiesence period.
	 */
	do_unoptimize_kprobes();

	/*
	 * Step 2: Wait for quiesence period to ensure all potentially
	 * preempted tasks to have normally scheduled. Because optprobe
	 * may modify multiple instructions, there is a chance that Nth
	 * instruction is preempted. In that case, such tasks can return
	 * to 2nd-Nth byte of jump instruction. This wait is for avoiding it.
	 * Note that on non-preemptive kernel, this is transparently converted
	 * to synchronoze_sched() to wait for all interrupts to have completed.
	 */
	synchronize_rcu_tasks();

	/* Step 3: Optimize kprobes after quiesence period */
	do_optimize_kprobes();

	/* Step 4: Free cleaned kprobes after quiesence period */
	do_free_cleaned_kprobes();

	mutex_unlock(&text_mutex);
	cpus_read_unlock();

	/* Step 5: Kick optimizer again if needed */
	if (!list_empty(&optimizing_list) || !list_empty(&unoptimizing_list))
		kick_kprobe_optimizer();

	mutex_unlock(&kprobe_mutex);
}

/* Wait for completing optimization and unoptimization */
void wait_for_kprobe_optimizer(void)
{
	mutex_lock(&kprobe_mutex);

	while (!list_empty(&optimizing_list) || !list_empty(&unoptimizing_list)) {
		mutex_unlock(&kprobe_mutex);

		/* this will also make optimizing_work execute immmediately */
		flush_delayed_work(&optimizing_work);
		/* @optimizing_work might not have been queued yet, relax */
		cpu_relax();

		mutex_lock(&kprobe_mutex);
	}

	mutex_unlock(&kprobe_mutex);
}

static bool optprobe_queued_unopt(struct optimized_kprobe *op)
{
	struct optimized_kprobe *_op;

	list_for_each_entry(_op, &unoptimizing_list, list) {
		if (op == _op)
			return true;
	}

	return false;
}

/* Optimize kprobe if p is ready to be optimized */
static void optimize_kprobe(struct kprobe *p)
{
	struct optimized_kprobe *op;

	/* Check if the kprobe is disabled or not ready for optimization. */
	if (!kprobe_optready(p) || !kprobes_allow_optimization ||
	    (kprobe_disabled(p) || kprobes_all_disarmed))
		return;

	/* kprobes with post_handler can not be optimized */
	if (p->post_handler)
		return;

	op = container_of(p, struct optimized_kprobe, kp);

	/* Check there is no other kprobes at the optimized instructions */
	if (arch_check_optimized_kprobe(op) < 0)
		return;

	/* Check if it is already optimized. */
	if (op->kp.flags & KPROBE_FLAG_OPTIMIZED) {
		if (optprobe_queued_unopt(op)) {
			/* This is under unoptimizing. Just dequeue the probe */
			list_del_init(&op->list);
		}
		return;
	}
	op->kp.flags |= KPROBE_FLAG_OPTIMIZED;

	/* On unoptimizing/optimizing_list, op must have OPTIMIZED flag */
	if (WARN_ON_ONCE(!list_empty(&op->list)))
		return;

	list_add(&op->list, &optimizing_list);
	kick_kprobe_optimizer();
}

/* Short cut to direct unoptimizing */
static void force_unoptimize_kprobe(struct optimized_kprobe *op)
{
	lockdep_assert_cpus_held();
	arch_unoptimize_kprobe(op);
	op->kp.flags &= ~KPROBE_FLAG_OPTIMIZED;
}

/* Unoptimize a kprobe if p is optimized */
static void unoptimize_kprobe(struct kprobe *p, bool force)
{
	struct optimized_kprobe *op;

	if (!kprobe_aggrprobe(p) || kprobe_disarmed(p))
		return; /* This is not an optprobe nor optimized */

	op = container_of(p, struct optimized_kprobe, kp);
	if (!kprobe_optimized(p))
		return;

	if (!list_empty(&op->list)) {
		if (optprobe_queued_unopt(op)) {
			/* Queued in unoptimizing queue */
			if (force) {
				/*
				 * Forcibly unoptimize the kprobe here, and queue it
				 * in the freeing list for release afterwards.
				 */
				force_unoptimize_kprobe(op);
				list_move(&op->list, &freeing_list);
			}
		} else {
			/* Dequeue from the optimizing queue */
			list_del_init(&op->list);
			op->kp.flags &= ~KPROBE_FLAG_OPTIMIZED;
		}
		return;
	}

	/* Optimized kprobe case */
	if (force) {
		/* Forcibly update the code: this is a special case */
		force_unoptimize_kprobe(op);
	} else {
		list_add(&op->list, &unoptimizing_list);
		kick_kprobe_optimizer();
	}
}

/* Cancel unoptimizing for reusing */
static int reuse_unused_kprobe(struct kprobe *ap)
{
	struct optimized_kprobe *op;

	/*
	 * Unused kprobe MUST be on the way of delayed unoptimizing (means
	 * there is still a relative jump) and disabled.
	 */
	op = container_of(ap, struct optimized_kprobe, kp);
	WARN_ON_ONCE(list_empty(&op->list));
	/* Enable the probe again */
	ap->flags &= ~KPROBE_FLAG_DISABLED;
	/* Optimize it again (remove from op->list) */
	if (!kprobe_optready(ap))
		return -EINVAL;

	optimize_kprobe(ap);
	return 0;
}

/* Remove optimized instructions */
static void kill_optimized_kprobe(struct kprobe *p)
{
	struct optimized_kprobe *op;

	op = container_of(p, struct optimized_kprobe, kp);
	if (!list_empty(&op->list))
		/* Dequeue from the (un)optimization queue */
		list_del_init(&op->list);
	op->kp.flags &= ~KPROBE_FLAG_OPTIMIZED;

	if (kprobe_unused(p)) {
		/* Enqueue if it is unused */
		list_add(&op->list, &freeing_list);
		/*
		 * Remove unused probes from the hash list. After waiting
		 * for synchronization, this probe is reclaimed.
		 * (reclaiming is done by do_free_cleaned_kprobes().)
		 */
		hlist_del_rcu(&op->kp.hlist);
	}

	/* Don't touch the code, because it is already freed. */
	arch_remove_optimized_kprobe(op);
}

static inline
void __prepare_optimized_kprobe(struct optimized_kprobe *op, struct kprobe *p)
{
	if (!kprobe_ftrace(p))
		arch_prepare_optimized_kprobe(op, p);
}

/* Try to prepare optimized instructions */
static void prepare_optimized_kprobe(struct kprobe *p)
{
	struct optimized_kprobe *op;

	op = container_of(p, struct optimized_kprobe, kp);
	__prepare_optimized_kprobe(op, p);
}

/* Allocate new optimized_kprobe and try to prepare optimized instructions */
static struct kprobe *alloc_aggr_kprobe(struct kprobe *p)
{
	struct optimized_kprobe *op;

	op = kzalloc(sizeof(struct optimized_kprobe), GFP_KERNEL);
	if (!op)
		return NULL;

	INIT_LIST_HEAD(&op->list);
	op->kp.addr = p->addr;
	__prepare_optimized_kprobe(op, p);

	return &op->kp;
}

static void init_aggr_kprobe(struct kprobe *ap, struct kprobe *p);

/*
 * Prepare an optimized_kprobe and optimize it
 * NOTE: p must be a normal registered kprobe
 */
static void try_to_optimize_kprobe(struct kprobe *p)
{
	struct kprobe *ap;
	struct optimized_kprobe *op;

	/* Impossible to optimize ftrace-based kprobe */
	if (kprobe_ftrace(p))
		return;

	/* For preparing optimization, jump_label_text_reserved() is called */
	cpus_read_lock();
	jump_label_lock();
	mutex_lock(&text_mutex);

	ap = alloc_aggr_kprobe(p);
	if (!ap)
		goto out;

	op = container_of(ap, struct optimized_kprobe, kp);
	if (!arch_prepared_optinsn(&op->optinsn)) {
		/* If failed to setup optimizing, fallback to kprobe */
		arch_remove_optimized_kprobe(op);
		kfree(op);
		goto out;
	}

	init_aggr_kprobe(ap, p);
	optimize_kprobe(ap);	/* This just kicks optimizer thread */

out:
	mutex_unlock(&text_mutex);
	jump_label_unlock();
	cpus_read_unlock();
}

static void optimize_all_kprobes(void)
{
	struct hlist_head *head;
	struct kprobe *p;
	unsigned int i;

	mutex_lock(&kprobe_mutex);
	/* If optimization is already allowed, just return */
	if (kprobes_allow_optimization)
		goto out;

	cpus_read_lock();
	kprobes_allow_optimization = true;
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		hlist_for_each_entry(p, head, hlist)
			if (!kprobe_disabled(p))
				optimize_kprobe(p);
	}
	cpus_read_unlock();
	printk(KERN_INFO "Kprobes globally optimized\n");
out:
	mutex_unlock(&kprobe_mutex);
}

#ifdef CONFIG_SYSCTL
static void unoptimize_all_kprobes(void)
{
	struct hlist_head *head;
	struct kprobe *p;
	unsigned int i;

	mutex_lock(&kprobe_mutex);
	/* If optimization is already prohibited, just return */
	if (!kprobes_allow_optimization) {
		mutex_unlock(&kprobe_mutex);
		return;
	}

	cpus_read_lock();
	kprobes_allow_optimization = false;
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		hlist_for_each_entry(p, head, hlist) {
			if (!kprobe_disabled(p))
				unoptimize_kprobe(p, false);
		}
	}
	cpus_read_unlock();
	mutex_unlock(&kprobe_mutex);

	/* Wait for unoptimizing completion */
	wait_for_kprobe_optimizer();
	printk(KERN_INFO "Kprobes globally unoptimized\n");
}

static DEFINE_MUTEX(kprobe_sysctl_mutex);
int sysctl_kprobes_optimization;
int proc_kprobes_optimization_handler(struct ctl_table *table, int write,
				      void *buffer, size_t *length,
				      loff_t *ppos)
{
	int ret;

	mutex_lock(&kprobe_sysctl_mutex);
	sysctl_kprobes_optimization = kprobes_allow_optimization ? 1 : 0;
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);

	if (sysctl_kprobes_optimization)
		optimize_all_kprobes();
	else
		unoptimize_all_kprobes();
	mutex_unlock(&kprobe_sysctl_mutex);

	return ret;
}
#endif /* CONFIG_SYSCTL */

/* Put a breakpoint for a probe. Must be called with text_mutex locked */
static void __arm_kprobe(struct kprobe *p)
{
	struct kprobe *_p;

	/* Check collision with other optimized kprobes */
	_p = get_optimized_kprobe((unsigned long)p->addr);
	if (unlikely(_p))
		/* Fallback to unoptimized kprobe */
		unoptimize_kprobe(_p, true);

	arch_arm_kprobe(p);
	optimize_kprobe(p);	/* Try to optimize (add kprobe to a list) */
}

/* Remove the breakpoint of a probe. Must be called with text_mutex locked */
static void __disarm_kprobe(struct kprobe *p, bool reopt)
{
	struct kprobe *_p;

	/* Try to unoptimize */
	unoptimize_kprobe(p, kprobes_all_disarmed);

	if (!kprobe_queued(p)) {
		arch_disarm_kprobe(p);
		/* If another kprobe was blocked, optimize it. */
		_p = get_optimized_kprobe((unsigned long)p->addr);
		if (unlikely(_p) && reopt)
			optimize_kprobe(_p);
	}
	/* TODO: reoptimize others after unoptimized this probe */
}

#else /* !CONFIG_OPTPROBES */

#define optimize_kprobe(p)			do {} while (0)
#define unoptimize_kprobe(p, f)			do {} while (0)
#define kill_optimized_kprobe(p)		do {} while (0)
#define prepare_optimized_kprobe(p)		do {} while (0)
#define try_to_optimize_kprobe(p)		do {} while (0)
#define __arm_kprobe(p)				arch_arm_kprobe(p)
#define __disarm_kprobe(p, o)			arch_disarm_kprobe(p)
#define kprobe_disarmed(p)			kprobe_disabled(p)
#define wait_for_kprobe_optimizer()		do {} while (0)

static int reuse_unused_kprobe(struct kprobe *ap)
{
	/*
	 * If the optimized kprobe is NOT supported, the aggr kprobe is
	 * released at the same time that the last aggregated kprobe is
	 * unregistered.
	 * Thus there should be no chance to reuse unused kprobe.
	 */
	printk(KERN_ERR "Error: There should be no unused kprobe here.\n");
	return -EINVAL;
}

static void free_aggr_kprobe(struct kprobe *p)
{
	arch_remove_kprobe(p);
	kfree(p);
}

static struct kprobe *alloc_aggr_kprobe(struct kprobe *p)
{
	return kzalloc(sizeof(struct kprobe), GFP_KERNEL);
}
#endif /* CONFIG_OPTPROBES */

#ifdef CONFIG_KPROBES_ON_FTRACE
static struct ftrace_ops kprobe_ftrace_ops __read_mostly = {
	.func = kprobe_ftrace_handler,
	.flags = FTRACE_OPS_FL_SAVE_REGS,
};

static struct ftrace_ops kprobe_ipmodify_ops __read_mostly = {
	.func = kprobe_ftrace_handler,
	.flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_IPMODIFY,
};

static int kprobe_ipmodify_enabled;
static int kprobe_ftrace_enabled;

/* Must ensure p->addr is really on ftrace */
static int prepare_kprobe(struct kprobe *p)
{
	if (!kprobe_ftrace(p))
		return arch_prepare_kprobe(p);

	return arch_prepare_kprobe_ftrace(p);
}

/* Caller must lock kprobe_mutex */
static int __arm_kprobe_ftrace(struct kprobe *p, struct ftrace_ops *ops,
			       int *cnt)
{
	int ret = 0;

	ret = ftrace_set_filter_ip(ops, (unsigned long)p->addr, 0, 0);
	if (ret) {
		pr_debug("Failed to arm kprobe-ftrace at %pS (%d)\n",
			 p->addr, ret);
		return ret;
	}

	if (*cnt == 0) {
		ret = register_ftrace_function(ops);
		if (ret) {
			pr_debug("Failed to init kprobe-ftrace (%d)\n", ret);
			goto err_ftrace;
		}
	}

	(*cnt)++;
	return ret;

err_ftrace:
	/*
	 * At this point, sinec ops is not registered, we should be sefe from
	 * registering empty filter.
	 */
	ftrace_set_filter_ip(ops, (unsigned long)p->addr, 1, 0);
	return ret;
}

static int arm_kprobe_ftrace(struct kprobe *p)
{
	bool ipmodify = (p->post_handler != NULL);

	return __arm_kprobe_ftrace(p,
		ipmodify ? &kprobe_ipmodify_ops : &kprobe_ftrace_ops,
		ipmodify ? &kprobe_ipmodify_enabled : &kprobe_ftrace_enabled);
}

/* Caller must lock kprobe_mutex */
static int __disarm_kprobe_ftrace(struct kprobe *p, struct ftrace_ops *ops,
				  int *cnt)
{
	int ret = 0;

	if (*cnt == 1) {
		ret = unregister_ftrace_function(ops);
		if (WARN(ret < 0, "Failed to unregister kprobe-ftrace (%d)\n", ret))
			return ret;
	}

	(*cnt)--;

	ret = ftrace_set_filter_ip(ops, (unsigned long)p->addr, 1, 0);
	WARN_ONCE(ret < 0, "Failed to disarm kprobe-ftrace at %pS (%d)\n",
		  p->addr, ret);
	return ret;
}

static int disarm_kprobe_ftrace(struct kprobe *p)
{
	bool ipmodify = (p->post_handler != NULL);

	return __disarm_kprobe_ftrace(p,
		ipmodify ? &kprobe_ipmodify_ops : &kprobe_ftrace_ops,
		ipmodify ? &kprobe_ipmodify_enabled : &kprobe_ftrace_enabled);
}
#else	/* !CONFIG_KPROBES_ON_FTRACE */
static inline int prepare_kprobe(struct kprobe *p)
{
	return arch_prepare_kprobe(p);
}

static inline int arm_kprobe_ftrace(struct kprobe *p)
{
	return -ENODEV;
}

static inline int disarm_kprobe_ftrace(struct kprobe *p)
{
	return -ENODEV;
}
#endif

/* Arm a kprobe with text_mutex */
static int arm_kprobe(struct kprobe *kp)
{
	if (unlikely(kprobe_ftrace(kp)))
		return arm_kprobe_ftrace(kp);

	cpus_read_lock();
	mutex_lock(&text_mutex);
	__arm_kprobe(kp);
	mutex_unlock(&text_mutex);
	cpus_read_unlock();

	return 0;
}

/* Disarm a kprobe with text_mutex */
static int disarm_kprobe(struct kprobe *kp, bool reopt)
{
	if (unlikely(kprobe_ftrace(kp)))
		return disarm_kprobe_ftrace(kp);

	cpus_read_lock();
	mutex_lock(&text_mutex);
	__disarm_kprobe(kp, reopt);
	mutex_unlock(&text_mutex);
	cpus_read_unlock();

	return 0;
}

/*
 * Aggregate handlers for multiple kprobes support - these handlers
 * take care of invoking the individual kprobe handlers on p->list
 */
static int aggr_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe *kp;

	list_for_each_entry_rcu(kp, &p->list, list) {
		if (kp->pre_handler && likely(!kprobe_disabled(kp))) {
			set_kprobe_instance(kp);
			if (kp->pre_handler(kp, regs))
				return 1;
		}
		reset_kprobe_instance();
	}
	return 0;
}
NOKPROBE_SYMBOL(aggr_pre_handler);

static void aggr_post_handler(struct kprobe *p, struct pt_regs *regs,
			      unsigned long flags)
{
	struct kprobe *kp;

	list_for_each_entry_rcu(kp, &p->list, list) {
		if (kp->post_handler && likely(!kprobe_disabled(kp))) {
			set_kprobe_instance(kp);
			kp->post_handler(kp, regs, flags);
			reset_kprobe_instance();
		}
	}
}
NOKPROBE_SYMBOL(aggr_post_handler);

/* Walks the list and increments nmissed count for multiprobe case */
void kprobes_inc_nmissed_count(struct kprobe *p)
{
	struct kprobe *kp;
	if (!kprobe_aggrprobe(p)) {
		p->nmissed++;
	} else {
		list_for_each_entry_rcu(kp, &p->list, list)
			kp->nmissed++;
	}
	return;
}
NOKPROBE_SYMBOL(kprobes_inc_nmissed_count);

static void free_rp_inst_rcu(struct rcu_head *head)
{
	struct kretprobe_instance *ri = container_of(head, struct kretprobe_instance, rcu);

	if (refcount_dec_and_test(&ri->rph->ref))
		kfree(ri->rph);
	kfree(ri);
}
NOKPROBE_SYMBOL(free_rp_inst_rcu);

static void recycle_rp_inst(struct kretprobe_instance *ri)
{
	struct kretprobe *rp = get_kretprobe(ri);

	if (likely(rp)) {
		freelist_add(&ri->freelist, &rp->freelist);
	} else
		call_rcu(&ri->rcu, free_rp_inst_rcu);
}
NOKPROBE_SYMBOL(recycle_rp_inst);

static struct kprobe kprobe_busy = {
	.addr = (void *) get_kprobe,
};

void kprobe_busy_begin(void)
{
	struct kprobe_ctlblk *kcb;

	preempt_disable();
	__this_cpu_write(current_kprobe, &kprobe_busy);
	kcb = get_kprobe_ctlblk();
	kcb->kprobe_status = KPROBE_HIT_ACTIVE;
}

void kprobe_busy_end(void)
{
	__this_cpu_write(current_kprobe, NULL);
	preempt_enable();
}

/*
 * This function is called from finish_task_switch when task tk becomes dead,
 * so that we can recycle any function-return probe instances associated
 * with this task. These left over instances represent probed functions
 * that have been called but will never return.
 */
void kprobe_flush_task(struct task_struct *tk)
{
	struct kretprobe_instance *ri;
	struct llist_node *node;

	/* Early boot, not yet initialized. */
	if (unlikely(!kprobes_initialized))
		return;

	kprobe_busy_begin();

	node = __llist_del_all(&tk->kretprobe_instances);
	while (node) {
		ri = container_of(node, struct kretprobe_instance, llist);
		node = node->next;

		recycle_rp_inst(ri);
	}

	kprobe_busy_end();
}
NOKPROBE_SYMBOL(kprobe_flush_task);

static inline void free_rp_inst(struct kretprobe *rp)
{
	struct kretprobe_instance *ri;
	struct freelist_node *node;
	int count = 0;

	node = rp->freelist.head;
	while (node) {
		ri = container_of(node, struct kretprobe_instance, freelist);
		node = node->next;

		kfree(ri);
		count++;
	}

	if (refcount_sub_and_test(count, &rp->rph->ref)) {
		kfree(rp->rph);
		rp->rph = NULL;
	}
}

/* Add the new probe to ap->list */
static int add_new_kprobe(struct kprobe *ap, struct kprobe *p)
{
	if (p->post_handler)
		unoptimize_kprobe(ap, true);	/* Fall back to normal kprobe */

	list_add_rcu(&p->list, &ap->list);
	if (p->post_handler && !ap->post_handler)
		ap->post_handler = aggr_post_handler;

	return 0;
}

/*
 * Fill in the required fields of the "manager kprobe". Replace the
 * earlier kprobe in the hlist with the manager kprobe
 */
static void init_aggr_kprobe(struct kprobe *ap, struct kprobe *p)
{
	/* Copy p's insn slot to ap */
	copy_kprobe(p, ap);
	flush_insn_slot(ap);
	ap->addr = p->addr;
	ap->flags = p->flags & ~KPROBE_FLAG_OPTIMIZED;
	ap->pre_handler = aggr_pre_handler;
	/* We don't care the kprobe which has gone. */
	if (p->post_handler && !kprobe_gone(p))
		ap->post_handler = aggr_post_handler;

	INIT_LIST_HEAD(&ap->list);
	INIT_HLIST_NODE(&ap->hlist);

	list_add_rcu(&p->list, &ap->list);
	hlist_replace_rcu(&p->hlist, &ap->hlist);
}

/*
 * This is the second or subsequent kprobe at the address - handle
 * the intricacies
 */
static int register_aggr_kprobe(struct kprobe *orig_p, struct kprobe *p)
{
	int ret = 0;
	struct kprobe *ap = orig_p;

	cpus_read_lock();

	/* For preparing optimization, jump_label_text_reserved() is called */
	jump_label_lock();
	mutex_lock(&text_mutex);

	if (!kprobe_aggrprobe(orig_p)) {
		/* If orig_p is not an aggr_kprobe, create new aggr_kprobe. */
		ap = alloc_aggr_kprobe(orig_p);
		if (!ap) {
			ret = -ENOMEM;
			goto out;
		}
		init_aggr_kprobe(ap, orig_p);
	} else if (kprobe_unused(ap)) {
		/* This probe is going to die. Rescue it */
		ret = reuse_unused_kprobe(ap);
		if (ret)
			goto out;
	}

	if (kprobe_gone(ap)) {
		/*
		 * Attempting to insert new probe at the same location that
		 * had a probe in the module vaddr area which already
		 * freed. So, the instruction slot has already been
		 * released. We need a new slot for the new probe.
		 */
		ret = arch_prepare_kprobe(ap);
		if (ret)
			/*
			 * Even if fail to allocate new slot, don't need to
			 * free aggr_probe. It will be used next time, or
			 * freed by unregister_kprobe.
			 */
			goto out;

		/* Prepare optimized instructions if possible. */
		prepare_optimized_kprobe(ap);

		/*
		 * Clear gone flag to prevent allocating new slot again, and
		 * set disabled flag because it is not armed yet.
		 */
		ap->flags = (ap->flags & ~KPROBE_FLAG_GONE)
			    | KPROBE_FLAG_DISABLED;
	}

	/* Copy ap's insn slot to p */
	copy_kprobe(ap, p);
	ret = add_new_kprobe(ap, p);

out:
	mutex_unlock(&text_mutex);
	jump_label_unlock();
	cpus_read_unlock();

	if (ret == 0 && kprobe_disabled(ap) && !kprobe_disabled(p)) {
		ap->flags &= ~KPROBE_FLAG_DISABLED;
		if (!kprobes_all_disarmed) {
			/* Arm the breakpoint again. */
			ret = arm_kprobe(ap);
			if (ret) {
				ap->flags |= KPROBE_FLAG_DISABLED;
				list_del_rcu(&p->list);
				synchronize_rcu();
			}
		}
	}
	return ret;
}

bool __weak arch_within_kprobe_blacklist(unsigned long addr)
{
	/* The __kprobes marked functions and entry code must not be probed */
	return addr >= (unsigned long)__kprobes_text_start &&
	       addr < (unsigned long)__kprobes_text_end;
}

static bool __within_kprobe_blacklist(unsigned long addr)
{
	struct kprobe_blacklist_entry *ent;

	if (arch_within_kprobe_blacklist(addr))
		return true;
	/*
	 * If there exists a kprobe_blacklist, verify and
	 * fail any probe registration in the prohibited area
	 */
	list_for_each_entry(ent, &kprobe_blacklist, list) {
		if (addr >= ent->start_addr && addr < ent->end_addr)
			return true;
	}
	return false;
}

bool within_kprobe_blacklist(unsigned long addr)
{
	char symname[KSYM_NAME_LEN], *p;

	if (__within_kprobe_blacklist(addr))
		return true;

	/* Check if the address is on a suffixed-symbol */
	if (!lookup_symbol_name(addr, symname)) {
		p = strchr(symname, '.');
		if (!p)
			return false;
		*p = '\0';
		addr = (unsigned long)kprobe_lookup_name(symname, 0);
		if (addr)
			return __within_kprobe_blacklist(addr);
	}
	return false;
}

/*
 * If we have a symbol_name argument, look it up and add the offset field
 * to it. This way, we can specify a relative address to a symbol.
 * This returns encoded errors if it fails to look up symbol or invalid
 * combination of parameters.
 */
static kprobe_opcode_t *_kprobe_addr(kprobe_opcode_t *addr,
			const char *symbol_name, unsigned int offset)
{
	if ((symbol_name && addr) || (!symbol_name && !addr))
		goto invalid;

	if (symbol_name) {
		addr = kprobe_lookup_name(symbol_name, offset);
		if (!addr)
			return ERR_PTR(-ENOENT);
	}

	addr = (kprobe_opcode_t *)(((char *)addr) + offset);
	if (addr)
		return addr;

invalid:
	return ERR_PTR(-EINVAL);
}

static kprobe_opcode_t *kprobe_addr(struct kprobe *p)
{
	return _kprobe_addr(p->addr, p->symbol_name, p->offset);
}

/* Check passed kprobe is valid and return kprobe in kprobe_table. */
static struct kprobe *__get_valid_kprobe(struct kprobe *p)
{
	struct kprobe *ap, *list_p;

	lockdep_assert_held(&kprobe_mutex);

	ap = get_kprobe(p->addr);
	if (unlikely(!ap))
		return NULL;

	if (p != ap) {
		list_for_each_entry(list_p, &ap->list, list)
			if (list_p == p)
			/* kprobe p is a valid probe */
				goto valid;
		return NULL;
	}
valid:
	return ap;
}

/*
 * Warn and return error if the kprobe is being re-registered since
 * there must be a software bug.
 */
static inline int warn_kprobe_rereg(struct kprobe *p)
{
	int ret = 0;

	mutex_lock(&kprobe_mutex);
	if (WARN_ON_ONCE(__get_valid_kprobe(p)))
		ret = -EINVAL;
	mutex_unlock(&kprobe_mutex);

	return ret;
}

int __weak arch_check_ftrace_location(struct kprobe *p)
{
	unsigned long ftrace_addr;

	ftrace_addr = ftrace_location((unsigned long)p->addr);
	if (ftrace_addr) {
#ifdef CONFIG_KPROBES_ON_FTRACE
		/* Given address is not on the instruction boundary */
		if ((unsigned long)p->addr != ftrace_addr)
			return -EILSEQ;
		p->flags |= KPROBE_FLAG_FTRACE;
#else	/* !CONFIG_KPROBES_ON_FTRACE */
		return -EINVAL;
#endif
	}
	return 0;
}

static int check_kprobe_address_safe(struct kprobe *p,
				     struct module **probed_mod)
{
	int ret;

	ret = arch_check_ftrace_location(p);
	if (ret)
		return ret;
	jump_label_lock();
	preempt_disable();

	/* Ensure it is not in reserved area nor out of text */
	if (!(core_kernel_text((unsigned long) p->addr) ||
	    is_module_text_address((unsigned long) p->addr)) ||
	    in_gate_area_no_mm((unsigned long) p->addr) ||
	    within_kprobe_blacklist((unsigned long) p->addr) ||
	    jump_label_text_reserved(p->addr, p->addr) ||
	    static_call_text_reserved(p->addr, p->addr) ||
	    find_bug((unsigned long)p->addr)) {
		ret = -EINVAL;
		goto out;
	}

	/* Check if are we probing a module */
	*probed_mod = __module_text_address((unsigned long) p->addr);
	if (*probed_mod) {
		/*
		 * We must hold a refcount of the probed module while updating
		 * its code to prohibit unexpected unloading.
		 */
		if (unlikely(!try_module_get(*probed_mod))) {
			ret = -ENOENT;
			goto out;
		}

		/*
		 * If the module freed .init.text, we couldn't insert
		 * kprobes in there.
		 */
		if (within_module_init((unsigned long)p->addr, *probed_mod) &&
		    (*probed_mod)->state != MODULE_STATE_COMING) {
			module_put(*probed_mod);
			*probed_mod = NULL;
			ret = -ENOENT;
		}
	}
out:
	preempt_enable();
	jump_label_unlock();

	return ret;
}

int register_kprobe(struct kprobe *p)
{
	int ret;
	struct kprobe *old_p;
	struct module *probed_mod;
	kprobe_opcode_t *addr;

	/* Adjust probe address from symbol */
	addr = kprobe_addr(p);
	if (IS_ERR(addr))
		return PTR_ERR(addr);
	p->addr = addr;

	ret = warn_kprobe_rereg(p);
	if (ret)
		return ret;

	/* User can pass only KPROBE_FLAG_DISABLED to register_kprobe */
	p->flags &= KPROBE_FLAG_DISABLED;
	p->nmissed = 0;
	INIT_LIST_HEAD(&p->list);

	ret = check_kprobe_address_safe(p, &probed_mod);
	if (ret)
		return ret;

	mutex_lock(&kprobe_mutex);

	old_p = get_kprobe(p->addr);
	if (old_p) {
		/* Since this may unoptimize old_p, locking text_mutex. */
		ret = register_aggr_kprobe(old_p, p);
		goto out;
	}

	cpus_read_lock();
	/* Prevent text modification */
	mutex_lock(&text_mutex);
	ret = prepare_kprobe(p);
	mutex_unlock(&text_mutex);
	cpus_read_unlock();
	if (ret)
		goto out;

	INIT_HLIST_NODE(&p->hlist);
	hlist_add_head_rcu(&p->hlist,
		       &kprobe_table[hash_ptr(p->addr, KPROBE_HASH_BITS)]);

	if (!kprobes_all_disarmed && !kprobe_disabled(p)) {
		ret = arm_kprobe(p);
		if (ret) {
			hlist_del_rcu(&p->hlist);
			synchronize_rcu();
			goto out;
		}
	}

	/* Try to optimize kprobe */
	try_to_optimize_kprobe(p);
out:
	mutex_unlock(&kprobe_mutex);

	if (probed_mod)
		module_put(probed_mod);

	return ret;
}
EXPORT_SYMBOL_GPL(register_kprobe);

/* Check if all probes on the aggrprobe are disabled */
static int aggr_kprobe_disabled(struct kprobe *ap)
{
	struct kprobe *kp;

	lockdep_assert_held(&kprobe_mutex);

	list_for_each_entry(kp, &ap->list, list)
		if (!kprobe_disabled(kp))
			/*
			 * There is an active probe on the list.
			 * We can't disable this ap.
			 */
			return 0;

	return 1;
}

/* Disable one kprobe: Make sure called under kprobe_mutex is locked */
static struct kprobe *__disable_kprobe(struct kprobe *p)
{
	struct kprobe *orig_p;
	int ret;

	/* Get an original kprobe for return */
	orig_p = __get_valid_kprobe(p);
	if (unlikely(orig_p == NULL))
		return ERR_PTR(-EINVAL);

	if (!kprobe_disabled(p)) {
		/* Disable probe if it is a child probe */
		if (p != orig_p)
			p->flags |= KPROBE_FLAG_DISABLED;

		/* Try to disarm and disable this/parent probe */
		if (p == orig_p || aggr_kprobe_disabled(orig_p)) {
			/*
			 * Don't be lazy here.  Even if 'kprobes_all_disarmed'
			 * is false, 'orig_p' might not have been armed yet.
			 * Note arm_all_kprobes() __tries__ to arm all kprobes
			 * on the best effort basis.
			 */
			if (!kprobes_all_disarmed && !kprobe_disabled(orig_p)) {
				ret = disarm_kprobe(orig_p, true);
				if (ret) {
					p->flags &= ~KPROBE_FLAG_DISABLED;
					return ERR_PTR(ret);
				}
			}
			orig_p->flags |= KPROBE_FLAG_DISABLED;
		}
	}

	return orig_p;
}

/*
 * Unregister a kprobe without a scheduler synchronization.
 */
static int __unregister_kprobe_top(struct kprobe *p)
{
	struct kprobe *ap, *list_p;

	/* Disable kprobe. This will disarm it if needed. */
	ap = __disable_kprobe(p);
	if (IS_ERR(ap))
		return PTR_ERR(ap);

	if (ap == p)
		/*
		 * This probe is an independent(and non-optimized) kprobe
		 * (not an aggrprobe). Remove from the hash list.
		 */
		goto disarmed;

	/* Following process expects this probe is an aggrprobe */
	WARN_ON(!kprobe_aggrprobe(ap));

	if (list_is_singular(&ap->list) && kprobe_disarmed(ap))
		/*
		 * !disarmed could be happen if the probe is under delayed
		 * unoptimizing.
		 */
		goto disarmed;
	else {
		/* If disabling probe has special handlers, update aggrprobe */
		if (p->post_handler && !kprobe_gone(p)) {
			list_for_each_entry(list_p, &ap->list, list) {
				if ((list_p != p) && (list_p->post_handler))
					goto noclean;
			}
			ap->post_handler = NULL;
		}
noclean:
		/*
		 * Remove from the aggrprobe: this path will do nothing in
		 * __unregister_kprobe_bottom().
		 */
		list_del_rcu(&p->list);
		if (!kprobe_disabled(ap) && !kprobes_all_disarmed)
			/*
			 * Try to optimize this probe again, because post
			 * handler may have been changed.
			 */
			optimize_kprobe(ap);
	}
	return 0;

disarmed:
	hlist_del_rcu(&ap->hlist);
	return 0;
}

static void __unregister_kprobe_bottom(struct kprobe *p)
{
	struct kprobe *ap;

	if (list_empty(&p->list))
		/* This is an independent kprobe */
		arch_remove_kprobe(p);
	else if (list_is_singular(&p->list)) {
		/* This is the last child of an aggrprobe */
		ap = list_entry(p->list.next, struct kprobe, list);
		list_del(&p->list);
		free_aggr_kprobe(ap);
	}
	/* Otherwise, do nothing. */
}

int register_kprobes(struct kprobe **kps, int num)
{
	int i, ret = 0;

	if (num <= 0)
		return -EINVAL;
	for (i = 0; i < num; i++) {
		ret = register_kprobe(kps[i]);
		if (ret < 0) {
			if (i > 0)
				unregister_kprobes(kps, i);
			break;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(register_kprobes);

void unregister_kprobe(struct kprobe *p)
{
	unregister_kprobes(&p, 1);
}
EXPORT_SYMBOL_GPL(unregister_kprobe);

void unregister_kprobes(struct kprobe **kps, int num)
{
	int i;

	if (num <= 0)
		return;
	mutex_lock(&kprobe_mutex);
	for (i = 0; i < num; i++)
		if (__unregister_kprobe_top(kps[i]) < 0)
			kps[i]->addr = NULL;
	mutex_unlock(&kprobe_mutex);

	synchronize_rcu();
	for (i = 0; i < num; i++)
		if (kps[i]->addr)
			__unregister_kprobe_bottom(kps[i]);
}
EXPORT_SYMBOL_GPL(unregister_kprobes);

int __weak kprobe_exceptions_notify(struct notifier_block *self,
					unsigned long val, void *data)
{
	return NOTIFY_DONE;
}
NOKPROBE_SYMBOL(kprobe_exceptions_notify);

static struct notifier_block kprobe_exceptions_nb = {
	.notifier_call = kprobe_exceptions_notify,
	.priority = 0x7fffffff /* we need to be notified first */
};

unsigned long __weak arch_deref_entry_point(void *entry)
{
	return (unsigned long)entry;
}

#ifdef CONFIG_KRETPROBES

unsigned long __kretprobe_trampoline_handler(struct pt_regs *regs,
					     void *trampoline_address,
					     void *frame_pointer)
{
	kprobe_opcode_t *correct_ret_addr = NULL;
	struct kretprobe_instance *ri = NULL;
	struct llist_node *first, *node;
	struct kretprobe *rp;

	/* Find all nodes for this frame. */
	first = node = current->kretprobe_instances.first;
	while (node) {
		ri = container_of(node, struct kretprobe_instance, llist);

		BUG_ON(ri->fp != frame_pointer);

		if (ri->ret_addr != trampoline_address) {
			correct_ret_addr = ri->ret_addr;
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			goto found;
		}

		node = node->next;
	}
	pr_err("Oops! Kretprobe fails to find correct return address.\n");
	BUG_ON(1);

found:
	/* Unlink all nodes for this frame. */
	current->kretprobe_instances.first = node->next;
	node->next = NULL;

	/* Run them..  */
	while (first) {
		ri = container_of(first, struct kretprobe_instance, llist);
		first = first->next;

		rp = get_kretprobe(ri);
		if (rp && rp->handler) {
			struct kprobe *prev = kprobe_running();

			__this_cpu_write(current_kprobe, &rp->kp);
			ri->ret_addr = correct_ret_addr;
			rp->handler(ri, regs);
			__this_cpu_write(current_kprobe, prev);
		}

		recycle_rp_inst(ri);
	}

	return (unsigned long)correct_ret_addr;
}
NOKPROBE_SYMBOL(__kretprobe_trampoline_handler)

/*
 * This kprobe pre_handler is registered with every kretprobe. When probe
 * hits it will set up the return probe.
 */
static int pre_handler_kretprobe(struct kprobe *p, struct pt_regs *regs)
{
	struct kretprobe *rp = container_of(p, struct kretprobe, kp);
	struct kretprobe_instance *ri;
	struct freelist_node *fn;

	fn = freelist_try_get(&rp->freelist);
	if (!fn) {
		rp->nmissed++;
		return 0;
	}

	ri = container_of(fn, struct kretprobe_instance, freelist);

	if (rp->entry_handler && rp->entry_handler(ri, regs)) {
		freelist_add(&ri->freelist, &rp->freelist);
		return 0;
	}

	arch_prepare_kretprobe(ri, regs);

	__llist_add(&ri->llist, &current->kretprobe_instances);

	return 0;
}
NOKPROBE_SYMBOL(pre_handler_kretprobe);

bool __weak arch_kprobe_on_func_entry(unsigned long offset)
{
	return !offset;
}

/**
 * kprobe_on_func_entry() -- check whether given address is function entry
 * @addr: Target address
 * @sym:  Target symbol name
 * @offset: The offset from the symbol or the address
 *
 * This checks whether the given @addr+@offset or @sym+@offset is on the
 * function entry address or not.
 * This returns 0 if it is the function entry, or -EINVAL if it is not.
 * And also it returns -ENOENT if it fails the symbol or address lookup.
 * Caller must pass @addr or @sym (either one must be NULL), or this
 * returns -EINVAL.
 */
int kprobe_on_func_entry(kprobe_opcode_t *addr, const char *sym, unsigned long offset)
{
	kprobe_opcode_t *kp_addr = _kprobe_addr(addr, sym, offset);

	if (IS_ERR(kp_addr))
		return PTR_ERR(kp_addr);

	if (!kallsyms_lookup_size_offset((unsigned long)kp_addr, NULL, &offset))
		return -ENOENT;

	if (!arch_kprobe_on_func_entry(offset))
		return -EINVAL;

	return 0;
}

int register_kretprobe(struct kretprobe *rp)
{
	int ret;
	struct kretprobe_instance *inst;
	int i;
	void *addr;

	ret = kprobe_on_func_entry(rp->kp.addr, rp->kp.symbol_name, rp->kp.offset);
	if (ret)
		return ret;

	/* If only rp->kp.addr is specified, check reregistering kprobes */
	if (rp->kp.addr && warn_kprobe_rereg(&rp->kp))
		return -EINVAL;

	if (kretprobe_blacklist_size) {
		addr = kprobe_addr(&rp->kp);
		if (IS_ERR(addr))
			return PTR_ERR(addr);

		for (i = 0; kretprobe_blacklist[i].name != NULL; i++) {
			if (kretprobe_blacklist[i].addr == addr)
				return -EINVAL;
		}
	}

	if (rp->data_size > KRETPROBE_MAX_DATA_SIZE)
		return -E2BIG;

	rp->kp.pre_handler = pre_handler_kretprobe;
	rp->kp.post_handler = NULL;

	/* Pre-allocate memory for max kretprobe instances */
	if (rp->maxactive <= 0) {
#ifdef CONFIG_PREEMPTION
		rp->maxactive = max_t(unsigned int, 10, 2*num_possible_cpus());
#else
		rp->maxactive = num_possible_cpus();
#endif
	}
	rp->freelist.head = NULL;
	rp->rph = kzalloc(sizeof(struct kretprobe_holder), GFP_KERNEL);
	if (!rp->rph)
		return -ENOMEM;

	rp->rph->rp = rp;
	for (i = 0; i < rp->maxactive; i++) {
		inst = kzalloc(sizeof(struct kretprobe_instance) +
			       rp->data_size, GFP_KERNEL);
		if (inst == NULL) {
			refcount_set(&rp->rph->ref, i);
			free_rp_inst(rp);
			return -ENOMEM;
		}
		inst->rph = rp->rph;
		freelist_add(&inst->freelist, &rp->freelist);
	}
	refcount_set(&rp->rph->ref, i);

	rp->nmissed = 0;
	/* Establish function entry probe point */
	ret = register_kprobe(&rp->kp);
	if (ret != 0)
		free_rp_inst(rp);
	return ret;
}
EXPORT_SYMBOL_GPL(register_kretprobe);

int register_kretprobes(struct kretprobe **rps, int num)
{
	int ret = 0, i;

	if (num <= 0)
		return -EINVAL;
	for (i = 0; i < num; i++) {
		ret = register_kretprobe(rps[i]);
		if (ret < 0) {
			if (i > 0)
				unregister_kretprobes(rps, i);
			break;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(register_kretprobes);

void unregister_kretprobe(struct kretprobe *rp)
{
	unregister_kretprobes(&rp, 1);
}
EXPORT_SYMBOL_GPL(unregister_kretprobe);

void unregister_kretprobes(struct kretprobe **rps, int num)
{
	int i;

	if (num <= 0)
		return;
	mutex_lock(&kprobe_mutex);
	for (i = 0; i < num; i++) {
		if (__unregister_kprobe_top(&rps[i]->kp) < 0)
			rps[i]->kp.addr = NULL;
		rps[i]->rph->rp = NULL;
	}
	mutex_unlock(&kprobe_mutex);

	synchronize_rcu();
	for (i = 0; i < num; i++) {
		if (rps[i]->kp.addr) {
			__unregister_kprobe_bottom(&rps[i]->kp);
			free_rp_inst(rps[i]);
		}
	}
}
EXPORT_SYMBOL_GPL(unregister_kretprobes);

#else /* CONFIG_KRETPROBES */
int register_kretprobe(struct kretprobe *rp)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(register_kretprobe);

int register_kretprobes(struct kretprobe **rps, int num)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(register_kretprobes);

void unregister_kretprobe(struct kretprobe *rp)
{
}
EXPORT_SYMBOL_GPL(unregister_kretprobe);

void unregister_kretprobes(struct kretprobe **rps, int num)
{
}
EXPORT_SYMBOL_GPL(unregister_kretprobes);

static int pre_handler_kretprobe(struct kprobe *p, struct pt_regs *regs)
{
	return 0;
}
NOKPROBE_SYMBOL(pre_handler_kretprobe);

#endif /* CONFIG_KRETPROBES */

/* Set the kprobe gone and remove its instruction buffer. */
static void kill_kprobe(struct kprobe *p)
{
	struct kprobe *kp;

	lockdep_assert_held(&kprobe_mutex);

	p->flags |= KPROBE_FLAG_GONE;
	if (kprobe_aggrprobe(p)) {
		/*
		 * If this is an aggr_kprobe, we have to list all the
		 * chained probes and mark them GONE.
		 */
		list_for_each_entry(kp, &p->list, list)
			kp->flags |= KPROBE_FLAG_GONE;
		p->post_handler = NULL;
		kill_optimized_kprobe(p);
	}
	/*
	 * Here, we can remove insn_slot safely, because no thread calls
	 * the original probed function (which will be freed soon) any more.
	 */
	arch_remove_kprobe(p);

	/*
	 * The module is going away. We should disarm the kprobe which
	 * is using ftrace, because ftrace framework is still available at
	 * MODULE_STATE_GOING notification.
	 */
	if (kprobe_ftrace(p) && !kprobe_disabled(p) && !kprobes_all_disarmed)
		disarm_kprobe_ftrace(p);
}

/* Disable one kprobe */
int disable_kprobe(struct kprobe *kp)
{
	int ret = 0;
	struct kprobe *p;

	mutex_lock(&kprobe_mutex);

	/* Disable this kprobe */
	p = __disable_kprobe(kp);
	if (IS_ERR(p))
		ret = PTR_ERR(p);

	mutex_unlock(&kprobe_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(disable_kprobe);

/* Enable one kprobe */
int enable_kprobe(struct kprobe *kp)
{
	int ret = 0;
	struct kprobe *p;

	mutex_lock(&kprobe_mutex);

	/* Check whether specified probe is valid. */
	p = __get_valid_kprobe(kp);
	if (unlikely(p == NULL)) {
		ret = -EINVAL;
		goto out;
	}

	if (kprobe_gone(kp)) {
		/* This kprobe has gone, we couldn't enable it. */
		ret = -EINVAL;
		goto out;
	}

	if (p != kp)
		kp->flags &= ~KPROBE_FLAG_DISABLED;

	if (!kprobes_all_disarmed && kprobe_disabled(p)) {
		p->flags &= ~KPROBE_FLAG_DISABLED;
		ret = arm_kprobe(p);
		if (ret) {
			p->flags |= KPROBE_FLAG_DISABLED;
			if (p != kp)
				kp->flags |= KPROBE_FLAG_DISABLED;
		}
	}
out:
	mutex_unlock(&kprobe_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(enable_kprobe);

/* Caller must NOT call this in usual path. This is only for critical case */
void dump_kprobe(struct kprobe *kp)
{
	pr_err("Dumping kprobe:\n");
	pr_err("Name: %s\nOffset: %x\nAddress: %pS\n",
	       kp->symbol_name, kp->offset, kp->addr);
}
NOKPROBE_SYMBOL(dump_kprobe);

int kprobe_add_ksym_blacklist(unsigned long entry)
{
	struct kprobe_blacklist_entry *ent;
	unsigned long offset = 0, size = 0;

	if (!kernel_text_address(entry) ||
	    !kallsyms_lookup_size_offset(entry, &size, &offset))
		return -EINVAL;

	ent = kmalloc(sizeof(*ent), GFP_KERNEL);
	if (!ent)
		return -ENOMEM;
	ent->start_addr = entry;
	ent->end_addr = entry + size;
	INIT_LIST_HEAD(&ent->list);
	list_add_tail(&ent->list, &kprobe_blacklist);

	return (int)size;
}

/* Add all symbols in given area into kprobe blacklist */
int kprobe_add_area_blacklist(unsigned long start, unsigned long end)
{
	unsigned long entry;
	int ret = 0;

	for (entry = start; entry < end; entry += ret) {
		ret = kprobe_add_ksym_blacklist(entry);
		if (ret < 0)
			return ret;
		if (ret == 0)	/* In case of alias symbol */
			ret = 1;
	}
	return 0;
}

/* Remove all symbols in given area from kprobe blacklist */
static void kprobe_remove_area_blacklist(unsigned long start, unsigned long end)
{
	struct kprobe_blacklist_entry *ent, *n;

	list_for_each_entry_safe(ent, n, &kprobe_blacklist, list) {
		if (ent->start_addr < start || ent->start_addr >= end)
			continue;
		list_del(&ent->list);
		kfree(ent);
	}
}

static void kprobe_remove_ksym_blacklist(unsigned long entry)
{
	kprobe_remove_area_blacklist(entry, entry + 1);
}

int __weak arch_kprobe_get_kallsym(unsigned int *symnum, unsigned long *value,
				   char *type, char *sym)
{
	return -ERANGE;
}

int kprobe_get_kallsym(unsigned int symnum, unsigned long *value, char *type,
		       char *sym)
{
#ifdef __ARCH_WANT_KPROBES_INSN_SLOT
	if (!kprobe_cache_get_kallsym(&kprobe_insn_slots, &symnum, value, type, sym))
		return 0;
#ifdef CONFIG_OPTPROBES
	if (!kprobe_cache_get_kallsym(&kprobe_optinsn_slots, &symnum, value, type, sym))
		return 0;
#endif
#endif
	if (!arch_kprobe_get_kallsym(&symnum, value, type, sym))
		return 0;
	return -ERANGE;
}

int __init __weak arch_populate_kprobe_blacklist(void)
{
	return 0;
}

/*
 * Lookup and populate the kprobe_blacklist.
 *
 * Unlike the kretprobe blacklist, we'll need to determine
 * the range of addresses that belong to the said functions,
 * since a kprobe need not necessarily be at the beginning
 * of a function.
 */
static int __init populate_kprobe_blacklist(unsigned long *start,
					     unsigned long *end)
{
	unsigned long entry;
	unsigned long *iter;
	int ret;

	for (iter = start; iter < end; iter++) {
		entry = arch_deref_entry_point((void *)*iter);
		ret = kprobe_add_ksym_blacklist(entry);
		if (ret == -EINVAL)
			continue;
		if (ret < 0)
			return ret;
	}

	/* Symbols in __kprobes_text are blacklisted */
	ret = kprobe_add_area_blacklist((unsigned long)__kprobes_text_start,
					(unsigned long)__kprobes_text_end);
	if (ret)
		return ret;

	/* Symbols in noinstr section are blacklisted */
	ret = kprobe_add_area_blacklist((unsigned long)__noinstr_text_start,
					(unsigned long)__noinstr_text_end);

	return ret ? : arch_populate_kprobe_blacklist();
}

static void add_module_kprobe_blacklist(struct module *mod)
{
	unsigned long start, end;
	int i;

	if (mod->kprobe_blacklist) {
		for (i = 0; i < mod->num_kprobe_blacklist; i++)
			kprobe_add_ksym_blacklist(mod->kprobe_blacklist[i]);
	}

	start = (unsigned long)mod->kprobes_text_start;
	if (start) {
		end = start + mod->kprobes_text_size;
		kprobe_add_area_blacklist(start, end);
	}

	start = (unsigned long)mod->noinstr_text_start;
	if (start) {
		end = start + mod->noinstr_text_size;
		kprobe_add_area_blacklist(start, end);
	}
}

static void remove_module_kprobe_blacklist(struct module *mod)
{
	unsigned long start, end;
	int i;

	if (mod->kprobe_blacklist) {
		for (i = 0; i < mod->num_kprobe_blacklist; i++)
			kprobe_remove_ksym_blacklist(mod->kprobe_blacklist[i]);
	}

	start = (unsigned long)mod->kprobes_text_start;
	if (start) {
		end = start + mod->kprobes_text_size;
		kprobe_remove_area_blacklist(start, end);
	}

	start = (unsigned long)mod->noinstr_text_start;
	if (start) {
		end = start + mod->noinstr_text_size;
		kprobe_remove_area_blacklist(start, end);
	}
}

/* Module notifier call back, checking kprobes on the module */
static int kprobes_module_callback(struct notifier_block *nb,
				   unsigned long val, void *data)
{
	struct module *mod = data;
	struct hlist_head *head;
	struct kprobe *p;
	unsigned int i;
	int checkcore = (val == MODULE_STATE_GOING);

	if (val == MODULE_STATE_COMING) {
		mutex_lock(&kprobe_mutex);
		add_module_kprobe_blacklist(mod);
		mutex_unlock(&kprobe_mutex);
	}
	if (val != MODULE_STATE_GOING && val != MODULE_STATE_LIVE)
		return NOTIFY_DONE;

	/*
	 * When MODULE_STATE_GOING was notified, both of module .text and
	 * .init.text sections would be freed. When MODULE_STATE_LIVE was
	 * notified, only .init.text section would be freed. We need to
	 * disable kprobes which have been inserted in the sections.
	 */
	mutex_lock(&kprobe_mutex);
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		hlist_for_each_entry(p, head, hlist)
			if (within_module_init((unsigned long)p->addr, mod) ||
			    (checkcore &&
			     within_module_core((unsigned long)p->addr, mod))) {
				/*
				 * The vaddr this probe is installed will soon
				 * be vfreed buy not synced to disk. Hence,
				 * disarming the breakpoint isn't needed.
				 *
				 * Note, this will also move any optimized probes
				 * that are pending to be removed from their
				 * corresponding lists to the freeing_list and
				 * will not be touched by the delayed
				 * kprobe_optimizer work handler.
				 */
				kill_kprobe(p);
			}
	}
	if (val == MODULE_STATE_GOING)
		remove_module_kprobe_blacklist(mod);
	mutex_unlock(&kprobe_mutex);
	return NOTIFY_DONE;
}

static struct notifier_block kprobe_module_nb = {
	.notifier_call = kprobes_module_callback,
	.priority = 0
};

/* Markers of _kprobe_blacklist section */
extern unsigned long __start_kprobe_blacklist[];
extern unsigned long __stop_kprobe_blacklist[];

void kprobe_free_init_mem(void)
{
	void *start = (void *)(&__init_begin);
	void *end = (void *)(&__init_end);
	struct hlist_head *head;
	struct kprobe *p;
	int i;

	mutex_lock(&kprobe_mutex);

	/* Kill all kprobes on initmem */
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		hlist_for_each_entry(p, head, hlist) {
			if (start <= (void *)p->addr && (void *)p->addr < end)
				kill_kprobe(p);
		}
	}

	mutex_unlock(&kprobe_mutex);
}

static int __init init_kprobes(void)
{
	int i, err = 0;

	/* FIXME allocate the probe table, currently defined statically */
	/* initialize all list heads */
	for (i = 0; i < KPROBE_TABLE_SIZE; i++)
		INIT_HLIST_HEAD(&kprobe_table[i]);

	err = populate_kprobe_blacklist(__start_kprobe_blacklist,
					__stop_kprobe_blacklist);
	if (err) {
		pr_err("kprobes: failed to populate blacklist: %d\n", err);
		pr_err("Please take care of using kprobes.\n");
	}

	if (kretprobe_blacklist_size) {
		/* lookup the function address from its name */
		for (i = 0; kretprobe_blacklist[i].name != NULL; i++) {
			kretprobe_blacklist[i].addr =
				kprobe_lookup_name(kretprobe_blacklist[i].name, 0);
			if (!kretprobe_blacklist[i].addr)
				printk("kretprobe: lookup failed: %s\n",
				       kretprobe_blacklist[i].name);
		}
	}

	/* By default, kprobes are armed */
	kprobes_all_disarmed = false;

#if defined(CONFIG_OPTPROBES) && defined(__ARCH_WANT_KPROBES_INSN_SLOT)
	/* Init kprobe_optinsn_slots for allocation */
	kprobe_optinsn_slots.insn_size = MAX_OPTINSN_SIZE;
#endif

	err = arch_init_kprobes();
	if (!err)
		err = register_die_notifier(&kprobe_exceptions_nb);
	if (!err)
		err = register_module_notifier(&kprobe_module_nb);

	kprobes_initialized = (err == 0);

	if (!err)
		init_test_probes();
	return err;
}
early_initcall(init_kprobes);

#if defined(CONFIG_OPTPROBES)
static int __init init_optprobes(void)
{
	/*
	 * Enable kprobe optimization - this kicks the optimizer which
	 * depends on synchronize_rcu_tasks() and ksoftirqd, that is
	 * not spawned in early initcall. So delay the optimization.
	 */
	optimize_all_kprobes();

	return 0;
}
subsys_initcall(init_optprobes);
#endif

#ifdef CONFIG_DEBUG_FS
static void report_probe(struct seq_file *pi, struct kprobe *p,
		const char *sym, int offset, char *modname, struct kprobe *pp)
{
	char *kprobe_type;
	void *addr = p->addr;

	if (p->pre_handler == pre_handler_kretprobe)
		kprobe_type = "r";
	else
		kprobe_type = "k";

	if (!kallsyms_show_value(pi->file->f_cred))
		addr = NULL;

	if (sym)
		seq_printf(pi, "%px  %s  %s+0x%x  %s ",
			addr, kprobe_type, sym, offset,
			(modname ? modname : " "));
	else	/* try to use %pS */
		seq_printf(pi, "%px  %s  %pS ",
			addr, kprobe_type, p->addr);

	if (!pp)
		pp = p;
	seq_printf(pi, "%s%s%s%s\n",
		(kprobe_gone(p) ? "[GONE]" : ""),
		((kprobe_disabled(p) && !kprobe_gone(p)) ?  "[DISABLED]" : ""),
		(kprobe_optimized(pp) ? "[OPTIMIZED]" : ""),
		(kprobe_ftrace(pp) ? "[FTRACE]" : ""));
}

static void *kprobe_seq_start(struct seq_file *f, loff_t *pos)
{
	return (*pos < KPROBE_TABLE_SIZE) ? pos : NULL;
}

static void *kprobe_seq_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos >= KPROBE_TABLE_SIZE)
		return NULL;
	return pos;
}

static void kprobe_seq_stop(struct seq_file *f, void *v)
{
	/* Nothing to do */
}

static int show_kprobe_addr(struct seq_file *pi, void *v)
{
	struct hlist_head *head;
	struct kprobe *p, *kp;
	const char *sym = NULL;
	unsigned int i = *(loff_t *) v;
	unsigned long offset = 0;
	char *modname, namebuf[KSYM_NAME_LEN];

	head = &kprobe_table[i];
	preempt_disable();
	hlist_for_each_entry_rcu(p, head, hlist) {
		sym = kallsyms_lookup((unsigned long)p->addr, NULL,
					&offset, &modname, namebuf);
		if (kprobe_aggrprobe(p)) {
			list_for_each_entry_rcu(kp, &p->list, list)
				report_probe(pi, kp, sym, offset, modname, p);
		} else
			report_probe(pi, p, sym, offset, modname, NULL);
	}
	preempt_enable();
	return 0;
}

static const struct seq_operations kprobes_sops = {
	.start = kprobe_seq_start,
	.next  = kprobe_seq_next,
	.stop  = kprobe_seq_stop,
	.show  = show_kprobe_addr
};

DEFINE_SEQ_ATTRIBUTE(kprobes);

/* kprobes/blacklist -- shows which functions can not be probed */
static void *kprobe_blacklist_seq_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&kprobe_mutex);
	return seq_list_start(&kprobe_blacklist, *pos);
}

static void *kprobe_blacklist_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &kprobe_blacklist, pos);
}

static int kprobe_blacklist_seq_show(struct seq_file *m, void *v)
{
	struct kprobe_blacklist_entry *ent =
		list_entry(v, struct kprobe_blacklist_entry, list);

	/*
	 * If /proc/kallsyms is not showing kernel address, we won't
	 * show them here either.
	 */
	if (!kallsyms_show_value(m->file->f_cred))
		seq_printf(m, "0x%px-0x%px\t%ps\n", NULL, NULL,
			   (void *)ent->start_addr);
	else
		seq_printf(m, "0x%px-0x%px\t%ps\n", (void *)ent->start_addr,
			   (void *)ent->end_addr, (void *)ent->start_addr);
	return 0;
}

static void kprobe_blacklist_seq_stop(struct seq_file *f, void *v)
{
	mutex_unlock(&kprobe_mutex);
}

static const struct seq_operations kprobe_blacklist_sops = {
	.start = kprobe_blacklist_seq_start,
	.next  = kprobe_blacklist_seq_next,
	.stop  = kprobe_blacklist_seq_stop,
	.show  = kprobe_blacklist_seq_show,
};
DEFINE_SEQ_ATTRIBUTE(kprobe_blacklist);

static int arm_all_kprobes(void)
{
	struct hlist_head *head;
	struct kprobe *p;
	unsigned int i, total = 0, errors = 0;
	int err, ret = 0;

	mutex_lock(&kprobe_mutex);

	/* If kprobes are armed, just return */
	if (!kprobes_all_disarmed)
		goto already_enabled;

	/*
	 * optimize_kprobe() called by arm_kprobe() checks
	 * kprobes_all_disarmed, so set kprobes_all_disarmed before
	 * arm_kprobe.
	 */
	kprobes_all_disarmed = false;
	/* Arming kprobes doesn't optimize kprobe itself */
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		/* Arm all kprobes on a best-effort basis */
		hlist_for_each_entry(p, head, hlist) {
			if (!kprobe_disabled(p)) {
				err = arm_kprobe(p);
				if (err)  {
					errors++;
					ret = err;
				}
				total++;
			}
		}
	}

	if (errors)
		pr_warn("Kprobes globally enabled, but failed to arm %d out of %d probes\n",
			errors, total);
	else
		pr_info("Kprobes globally enabled\n");

already_enabled:
	mutex_unlock(&kprobe_mutex);
	return ret;
}

static int disarm_all_kprobes(void)
{
	struct hlist_head *head;
	struct kprobe *p;
	unsigned int i, total = 0, errors = 0;
	int err, ret = 0;

	mutex_lock(&kprobe_mutex);

	/* If kprobes are already disarmed, just return */
	if (kprobes_all_disarmed) {
		mutex_unlock(&kprobe_mutex);
		return 0;
	}

	kprobes_all_disarmed = true;

	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		/* Disarm all kprobes on a best-effort basis */
		hlist_for_each_entry(p, head, hlist) {
			if (!arch_trampoline_kprobe(p) && !kprobe_disabled(p)) {
				err = disarm_kprobe(p, false);
				if (err) {
					errors++;
					ret = err;
				}
				total++;
			}
		}
	}

	if (errors)
		pr_warn("Kprobes globally disabled, but failed to disarm %d out of %d probes\n",
			errors, total);
	else
		pr_info("Kprobes globally disabled\n");

	mutex_unlock(&kprobe_mutex);

	/* Wait for disarming all kprobes by optimizer */
	wait_for_kprobe_optimizer();

	return ret;
}

/*
 * XXX: The debugfs bool file interface doesn't allow for callbacks
 * when the bool state is switched. We can reuse that facility when
 * available
 */
static ssize_t read_enabled_file_bool(struct file *file,
	       char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[3];

	if (!kprobes_all_disarmed)
		buf[0] = '1';
	else
		buf[0] = '0';
	buf[1] = '\n';
	buf[2] = 0x00;
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t write_enabled_file_bool(struct file *file,
	       const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	size_t buf_size;
	int ret = 0;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';
	switch (buf[0]) {
	case 'y':
	case 'Y':
	case '1':
		ret = arm_all_kprobes();
		break;
	case 'n':
	case 'N':
	case '0':
		ret = disarm_all_kprobes();
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;

	return count;
}

static const struct file_operations fops_kp = {
	.read =         read_enabled_file_bool,
	.write =        write_enabled_file_bool,
	.llseek =	default_llseek,
};

static int __init debugfs_kprobe_init(void)
{
	struct dentry *dir;

	dir = debugfs_create_dir("kprobes", NULL);

	debugfs_create_file("list", 0400, dir, NULL, &kprobes_fops);

	debugfs_create_file("enabled", 0600, dir, NULL, &fops_kp);

	debugfs_create_file("blacklist", 0400, dir, NULL,
			    &kprobe_blacklist_fops);

	return 0;
}

late_initcall(debugfs_kprobe_init);
#endif /* CONFIG_DEBUG_FS */
