/*
 *  Kernel Probes (KProbes)
 *  kernel/kprobes.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
#include <linux/module.h>
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

#include <asm-generic/sections.h>
#include <asm/cacheflush.h>
#include <asm/errno.h>
#include <asm/uaccess.h>

#define KPROBE_HASH_BITS 6
#define KPROBE_TABLE_SIZE (1 << KPROBE_HASH_BITS)


/*
 * Some oddball architectures like 64bit powerpc have function descriptors
 * so this must be overridable.
 */
#ifndef kprobe_lookup_name
#define kprobe_lookup_name(name, addr) \
	addr = ((kprobe_opcode_t *)(kallsyms_lookup_name(name)))
#endif

static int kprobes_initialized;
static struct hlist_head kprobe_table[KPROBE_TABLE_SIZE];
static struct hlist_head kretprobe_inst_table[KPROBE_TABLE_SIZE];

/* NOTE: change this value only with kprobe_mutex held */
static bool kprobes_all_disarmed;

/* This protects kprobe_table and optimizing_list */
static DEFINE_MUTEX(kprobe_mutex);
static DEFINE_PER_CPU(struct kprobe *, kprobe_instance) = NULL;
static struct {
	spinlock_t lock ____cacheline_aligned_in_smp;
} kretprobe_table_locks[KPROBE_TABLE_SIZE];

static spinlock_t *kretprobe_table_lock_ptr(unsigned long hash)
{
	return &(kretprobe_table_locks[hash].lock);
}

/*
 * Normally, functions that we'd want to prohibit kprobes in, are marked
 * __kprobes. But, there are cases where such functions already belong to
 * a different section (__sched for preempt_schedule)
 *
 * For such cases, we now have a blacklist
 */
static struct kprobe_blackpoint kprobe_blacklist[] = {
	{"preempt_schedule",},
	{"native_get_debugreg",},
	{"irq_entries_start",},
	{"common_interrupt",},
	{"mcount",},	/* mcount can be called from everywhere */
	{NULL}    /* Terminator */
};

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
	int nused;
	int ngarbage;
	char slot_used[];
};

#define KPROBE_INSN_PAGE_SIZE(slots)			\
	(offsetof(struct kprobe_insn_page, slot_used) +	\
	 (sizeof(char) * (slots)))

struct kprobe_insn_cache {
	struct list_head pages;	/* list of kprobe_insn_page */
	size_t insn_size;	/* size of instruction slot */
	int nr_garbage;
};

static int slots_per_page(struct kprobe_insn_cache *c)
{
	return PAGE_SIZE/(c->insn_size * sizeof(kprobe_opcode_t));
}

enum kprobe_slot_state {
	SLOT_CLEAN = 0,
	SLOT_DIRTY = 1,
	SLOT_USED = 2,
};

static DEFINE_MUTEX(kprobe_insn_mutex);	/* Protects kprobe_insn_slots */
static struct kprobe_insn_cache kprobe_insn_slots = {
	.pages = LIST_HEAD_INIT(kprobe_insn_slots.pages),
	.insn_size = MAX_INSN_SIZE,
	.nr_garbage = 0,
};
static int __kprobes collect_garbage_slots(struct kprobe_insn_cache *c);

/**
 * __get_insn_slot() - Find a slot on an executable page for an instruction.
 * We allocate an executable page if there's no room on existing ones.
 */
static kprobe_opcode_t __kprobes *__get_insn_slot(struct kprobe_insn_cache *c)
{
	struct kprobe_insn_page *kip;

 retry:
	list_for_each_entry(kip, &c->pages, list) {
		if (kip->nused < slots_per_page(c)) {
			int i;
			for (i = 0; i < slots_per_page(c); i++) {
				if (kip->slot_used[i] == SLOT_CLEAN) {
					kip->slot_used[i] = SLOT_USED;
					kip->nused++;
					return kip->insns + (i * c->insn_size);
				}
			}
			/* kip->nused is broken. Fix it. */
			kip->nused = slots_per_page(c);
			WARN_ON(1);
		}
	}

	/* If there are any garbage slots, collect it and try again. */
	if (c->nr_garbage && collect_garbage_slots(c) == 0)
		goto retry;

	/* All out of space.  Need to allocate a new page. */
	kip = kmalloc(KPROBE_INSN_PAGE_SIZE(slots_per_page(c)), GFP_KERNEL);
	if (!kip)
		return NULL;

	/*
	 * Use module_alloc so this page is within +/- 2GB of where the
	 * kernel image and loaded module images reside. This is required
	 * so x86_64 can correctly handle the %rip-relative fixups.
	 */
	kip->insns = module_alloc(PAGE_SIZE);
	if (!kip->insns) {
		kfree(kip);
		return NULL;
	}
	INIT_LIST_HEAD(&kip->list);
	memset(kip->slot_used, SLOT_CLEAN, slots_per_page(c));
	kip->slot_used[0] = SLOT_USED;
	kip->nused = 1;
	kip->ngarbage = 0;
	list_add(&kip->list, &c->pages);
	return kip->insns;
}


kprobe_opcode_t __kprobes *get_insn_slot(void)
{
	kprobe_opcode_t *ret = NULL;

	mutex_lock(&kprobe_insn_mutex);
	ret = __get_insn_slot(&kprobe_insn_slots);
	mutex_unlock(&kprobe_insn_mutex);

	return ret;
}

/* Return 1 if all garbages are collected, otherwise 0. */
static int __kprobes collect_one_slot(struct kprobe_insn_page *kip, int idx)
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
			list_del(&kip->list);
			module_free(NULL, kip->insns);
			kfree(kip);
		}
		return 1;
	}
	return 0;
}

static int __kprobes collect_garbage_slots(struct kprobe_insn_cache *c)
{
	struct kprobe_insn_page *kip, *next;

	/* Ensure no-one is interrupted on the garbages */
	synchronize_sched();

	list_for_each_entry_safe(kip, next, &c->pages, list) {
		int i;
		if (kip->ngarbage == 0)
			continue;
		kip->ngarbage = 0;	/* we will collect all garbages */
		for (i = 0; i < slots_per_page(c); i++) {
			if (kip->slot_used[i] == SLOT_DIRTY &&
			    collect_one_slot(kip, i))
				break;
		}
	}
	c->nr_garbage = 0;
	return 0;
}

static void __kprobes __free_insn_slot(struct kprobe_insn_cache *c,
				       kprobe_opcode_t *slot, int dirty)
{
	struct kprobe_insn_page *kip;

	list_for_each_entry(kip, &c->pages, list) {
		long idx = ((long)slot - (long)kip->insns) /
				(c->insn_size * sizeof(kprobe_opcode_t));
		if (idx >= 0 && idx < slots_per_page(c)) {
			WARN_ON(kip->slot_used[idx] != SLOT_USED);
			if (dirty) {
				kip->slot_used[idx] = SLOT_DIRTY;
				kip->ngarbage++;
				if (++c->nr_garbage > slots_per_page(c))
					collect_garbage_slots(c);
			} else
				collect_one_slot(kip, idx);
			return;
		}
	}
	/* Could not free this slot. */
	WARN_ON(1);
}

void __kprobes free_insn_slot(kprobe_opcode_t * slot, int dirty)
{
	mutex_lock(&kprobe_insn_mutex);
	__free_insn_slot(&kprobe_insn_slots, slot, dirty);
	mutex_unlock(&kprobe_insn_mutex);
}
#ifdef CONFIG_OPTPROBES
/* For optimized_kprobe buffer */
static DEFINE_MUTEX(kprobe_optinsn_mutex); /* Protects kprobe_optinsn_slots */
static struct kprobe_insn_cache kprobe_optinsn_slots = {
	.pages = LIST_HEAD_INIT(kprobe_optinsn_slots.pages),
	/* .insn_size is initialized later */
	.nr_garbage = 0,
};
/* Get a slot for optimized_kprobe buffer */
kprobe_opcode_t __kprobes *get_optinsn_slot(void)
{
	kprobe_opcode_t *ret = NULL;

	mutex_lock(&kprobe_optinsn_mutex);
	ret = __get_insn_slot(&kprobe_optinsn_slots);
	mutex_unlock(&kprobe_optinsn_mutex);

	return ret;
}

void __kprobes free_optinsn_slot(kprobe_opcode_t * slot, int dirty)
{
	mutex_lock(&kprobe_optinsn_mutex);
	__free_insn_slot(&kprobe_optinsn_slots, slot, dirty);
	mutex_unlock(&kprobe_optinsn_mutex);
}
#endif
#endif

/* We have preemption disabled.. so it is safe to use __ versions */
static inline void set_kprobe_instance(struct kprobe *kp)
{
	__get_cpu_var(kprobe_instance) = kp;
}

static inline void reset_kprobe_instance(void)
{
	__get_cpu_var(kprobe_instance) = NULL;
}

/*
 * This routine is called either:
 * 	- under the kprobe_mutex - during kprobe_[un]register()
 * 				OR
 * 	- with preemption disabled - from arch/xxx/kernel/kprobes.c
 */
struct kprobe __kprobes *get_kprobe(void *addr)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct kprobe *p;

	head = &kprobe_table[hash_ptr(addr, KPROBE_HASH_BITS)];
	hlist_for_each_entry_rcu(p, node, head, hlist) {
		if (p->addr == addr)
			return p;
	}

	return NULL;
}

static int __kprobes aggr_pre_handler(struct kprobe *p, struct pt_regs *regs);

/* Return true if the kprobe is an aggregator */
static inline int kprobe_aggrprobe(struct kprobe *p)
{
	return p->pre_handler == aggr_pre_handler;
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
void __kprobes opt_pre_handler(struct kprobe *p, struct pt_regs *regs)
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

/*
 * Return an optimized kprobe whose optimizing code replaces
 * instructions including addr (exclude breakpoint).
 */
static struct kprobe *__kprobes get_optimized_kprobe(unsigned long addr)
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

static void kprobe_optimizer(struct work_struct *work);
static DECLARE_DELAYED_WORK(optimizing_work, kprobe_optimizer);
#define OPTIMIZE_DELAY 5

/* Kprobe jump optimizer */
static __kprobes void kprobe_optimizer(struct work_struct *work)
{
	struct optimized_kprobe *op, *tmp;

	/* Lock modules while optimizing kprobes */
	mutex_lock(&module_mutex);
	mutex_lock(&kprobe_mutex);
	if (kprobes_all_disarmed || !kprobes_allow_optimization)
		goto end;

	/*
	 * Wait for quiesence period to ensure all running interrupts
	 * are done. Because optprobe may modify multiple instructions
	 * there is a chance that Nth instruction is interrupted. In that
	 * case, running interrupt can return to 2nd-Nth byte of jump
	 * instruction. This wait is for avoiding it.
	 */
	synchronize_sched();

	/*
	 * The optimization/unoptimization refers online_cpus via
	 * stop_machine() and cpu-hotplug modifies online_cpus.
	 * And same time, text_mutex will be held in cpu-hotplug and here.
	 * This combination can cause a deadlock (cpu-hotplug try to lock
	 * text_mutex but stop_machine can not be done because online_cpus
	 * has been changed)
	 * To avoid this deadlock, we need to call get_online_cpus()
	 * for preventing cpu-hotplug outside of text_mutex locking.
	 */
	get_online_cpus();
	mutex_lock(&text_mutex);
	list_for_each_entry_safe(op, tmp, &optimizing_list, list) {
		WARN_ON(kprobe_disabled(&op->kp));
		if (arch_optimize_kprobe(op) < 0)
			op->kp.flags &= ~KPROBE_FLAG_OPTIMIZED;
		list_del_init(&op->list);
	}
	mutex_unlock(&text_mutex);
	put_online_cpus();
end:
	mutex_unlock(&kprobe_mutex);
	mutex_unlock(&module_mutex);
}

/* Optimize kprobe if p is ready to be optimized */
static __kprobes void optimize_kprobe(struct kprobe *p)
{
	struct optimized_kprobe *op;

	/* Check if the kprobe is disabled or not ready for optimization. */
	if (!kprobe_optready(p) || !kprobes_allow_optimization ||
	    (kprobe_disabled(p) || kprobes_all_disarmed))
		return;

	/* Both of break_handler and post_handler are not supported. */
	if (p->break_handler || p->post_handler)
		return;

	op = container_of(p, struct optimized_kprobe, kp);

	/* Check there is no other kprobes at the optimized instructions */
	if (arch_check_optimized_kprobe(op) < 0)
		return;

	/* Check if it is already optimized. */
	if (op->kp.flags & KPROBE_FLAG_OPTIMIZED)
		return;

	op->kp.flags |= KPROBE_FLAG_OPTIMIZED;
	list_add(&op->list, &optimizing_list);
	if (!delayed_work_pending(&optimizing_work))
		schedule_delayed_work(&optimizing_work, OPTIMIZE_DELAY);
}

/* Unoptimize a kprobe if p is optimized */
static __kprobes void unoptimize_kprobe(struct kprobe *p)
{
	struct optimized_kprobe *op;

	if ((p->flags & KPROBE_FLAG_OPTIMIZED) && kprobe_aggrprobe(p)) {
		op = container_of(p, struct optimized_kprobe, kp);
		if (!list_empty(&op->list))
			/* Dequeue from the optimization queue */
			list_del_init(&op->list);
		else
			/* Replace jump with break */
			arch_unoptimize_kprobe(op);
		op->kp.flags &= ~KPROBE_FLAG_OPTIMIZED;
	}
}

/* Remove optimized instructions */
static void __kprobes kill_optimized_kprobe(struct kprobe *p)
{
	struct optimized_kprobe *op;

	op = container_of(p, struct optimized_kprobe, kp);
	if (!list_empty(&op->list)) {
		/* Dequeue from the optimization queue */
		list_del_init(&op->list);
		op->kp.flags &= ~KPROBE_FLAG_OPTIMIZED;
	}
	/* Don't unoptimize, because the target code will be freed. */
	arch_remove_optimized_kprobe(op);
}

/* Try to prepare optimized instructions */
static __kprobes void prepare_optimized_kprobe(struct kprobe *p)
{
	struct optimized_kprobe *op;

	op = container_of(p, struct optimized_kprobe, kp);
	arch_prepare_optimized_kprobe(op);
}

/* Free optimized instructions and optimized_kprobe */
static __kprobes void free_aggr_kprobe(struct kprobe *p)
{
	struct optimized_kprobe *op;

	op = container_of(p, struct optimized_kprobe, kp);
	arch_remove_optimized_kprobe(op);
	kfree(op);
}

/* Allocate new optimized_kprobe and try to prepare optimized instructions */
static __kprobes struct kprobe *alloc_aggr_kprobe(struct kprobe *p)
{
	struct optimized_kprobe *op;

	op = kzalloc(sizeof(struct optimized_kprobe), GFP_KERNEL);
	if (!op)
		return NULL;

	INIT_LIST_HEAD(&op->list);
	op->kp.addr = p->addr;
	arch_prepare_optimized_kprobe(op);

	return &op->kp;
}

static void __kprobes init_aggr_kprobe(struct kprobe *ap, struct kprobe *p);

/*
 * Prepare an optimized_kprobe and optimize it
 * NOTE: p must be a normal registered kprobe
 */
static __kprobes void try_to_optimize_kprobe(struct kprobe *p)
{
	struct kprobe *ap;
	struct optimized_kprobe *op;

	ap = alloc_aggr_kprobe(p);
	if (!ap)
		return;

	op = container_of(ap, struct optimized_kprobe, kp);
	if (!arch_prepared_optinsn(&op->optinsn)) {
		/* If failed to setup optimizing, fallback to kprobe */
		free_aggr_kprobe(ap);
		return;
	}

	init_aggr_kprobe(ap, p);
	optimize_kprobe(ap);
}

#ifdef CONFIG_SYSCTL
/* This should be called with kprobe_mutex locked */
static void __kprobes optimize_all_kprobes(void)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct kprobe *p;
	unsigned int i;

	/* If optimization is already allowed, just return */
	if (kprobes_allow_optimization)
		return;

	kprobes_allow_optimization = true;
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		hlist_for_each_entry_rcu(p, node, head, hlist)
			if (!kprobe_disabled(p))
				optimize_kprobe(p);
	}
	printk(KERN_INFO "Kprobes globally optimized\n");
}

/* This should be called with kprobe_mutex locked */
static void __kprobes unoptimize_all_kprobes(void)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct kprobe *p;
	unsigned int i;

	/* If optimization is already prohibited, just return */
	if (!kprobes_allow_optimization)
		return;

	kprobes_allow_optimization = false;
	printk(KERN_INFO "Kprobes globally unoptimized\n");
	get_online_cpus();	/* For avoiding text_mutex deadlock */
	mutex_lock(&text_mutex);
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		hlist_for_each_entry_rcu(p, node, head, hlist) {
			if (!kprobe_disabled(p))
				unoptimize_kprobe(p);
		}
	}

	mutex_unlock(&text_mutex);
	put_online_cpus();
	/* Allow all currently running kprobes to complete */
	synchronize_sched();
}

int sysctl_kprobes_optimization;
int proc_kprobes_optimization_handler(struct ctl_table *table, int write,
				      void __user *buffer, size_t *length,
				      loff_t *ppos)
{
	int ret;

	mutex_lock(&kprobe_mutex);
	sysctl_kprobes_optimization = kprobes_allow_optimization ? 1 : 0;
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);

	if (sysctl_kprobes_optimization)
		optimize_all_kprobes();
	else
		unoptimize_all_kprobes();
	mutex_unlock(&kprobe_mutex);

	return ret;
}
#endif /* CONFIG_SYSCTL */

static void __kprobes __arm_kprobe(struct kprobe *p)
{
	struct kprobe *_p;

	/* Check collision with other optimized kprobes */
	_p = get_optimized_kprobe((unsigned long)p->addr);
	if (unlikely(_p))
		unoptimize_kprobe(_p); /* Fallback to unoptimized kprobe */

	arch_arm_kprobe(p);
	optimize_kprobe(p);	/* Try to optimize (add kprobe to a list) */
}

static void __kprobes __disarm_kprobe(struct kprobe *p)
{
	struct kprobe *_p;

	unoptimize_kprobe(p);	/* Try to unoptimize */
	arch_disarm_kprobe(p);

	/* If another kprobe was blocked, optimize it. */
	_p = get_optimized_kprobe((unsigned long)p->addr);
	if (unlikely(_p))
		optimize_kprobe(_p);
}

#else /* !CONFIG_OPTPROBES */

#define optimize_kprobe(p)			do {} while (0)
#define unoptimize_kprobe(p)			do {} while (0)
#define kill_optimized_kprobe(p)		do {} while (0)
#define prepare_optimized_kprobe(p)		do {} while (0)
#define try_to_optimize_kprobe(p)		do {} while (0)
#define __arm_kprobe(p)				arch_arm_kprobe(p)
#define __disarm_kprobe(p)			arch_disarm_kprobe(p)

static __kprobes void free_aggr_kprobe(struct kprobe *p)
{
	kfree(p);
}

static __kprobes struct kprobe *alloc_aggr_kprobe(struct kprobe *p)
{
	return kzalloc(sizeof(struct kprobe), GFP_KERNEL);
}
#endif /* CONFIG_OPTPROBES */

/* Arm a kprobe with text_mutex */
static void __kprobes arm_kprobe(struct kprobe *kp)
{
	/*
	 * Here, since __arm_kprobe() doesn't use stop_machine(),
	 * this doesn't cause deadlock on text_mutex. So, we don't
	 * need get_online_cpus().
	 */
	mutex_lock(&text_mutex);
	__arm_kprobe(kp);
	mutex_unlock(&text_mutex);
}

/* Disarm a kprobe with text_mutex */
static void __kprobes disarm_kprobe(struct kprobe *kp)
{
	get_online_cpus();	/* For avoiding text_mutex deadlock */
	mutex_lock(&text_mutex);
	__disarm_kprobe(kp);
	mutex_unlock(&text_mutex);
	put_online_cpus();
}

/*
 * Aggregate handlers for multiple kprobes support - these handlers
 * take care of invoking the individual kprobe handlers on p->list
 */
static int __kprobes aggr_pre_handler(struct kprobe *p, struct pt_regs *regs)
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

static void __kprobes aggr_post_handler(struct kprobe *p, struct pt_regs *regs,
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

static int __kprobes aggr_fault_handler(struct kprobe *p, struct pt_regs *regs,
					int trapnr)
{
	struct kprobe *cur = __get_cpu_var(kprobe_instance);

	/*
	 * if we faulted "during" the execution of a user specified
	 * probe handler, invoke just that probe's fault handler
	 */
	if (cur && cur->fault_handler) {
		if (cur->fault_handler(cur, regs, trapnr))
			return 1;
	}
	return 0;
}

static int __kprobes aggr_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe *cur = __get_cpu_var(kprobe_instance);
	int ret = 0;

	if (cur && cur->break_handler) {
		if (cur->break_handler(cur, regs))
			ret = 1;
	}
	reset_kprobe_instance();
	return ret;
}

/* Walks the list and increments nmissed count for multiprobe case */
void __kprobes kprobes_inc_nmissed_count(struct kprobe *p)
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

void __kprobes recycle_rp_inst(struct kretprobe_instance *ri,
				struct hlist_head *head)
{
	struct kretprobe *rp = ri->rp;

	/* remove rp inst off the rprobe_inst_table */
	hlist_del(&ri->hlist);
	INIT_HLIST_NODE(&ri->hlist);
	if (likely(rp)) {
		spin_lock(&rp->lock);
		hlist_add_head(&ri->hlist, &rp->free_instances);
		spin_unlock(&rp->lock);
	} else
		/* Unregistering */
		hlist_add_head(&ri->hlist, head);
}

void __kprobes kretprobe_hash_lock(struct task_struct *tsk,
			 struct hlist_head **head, unsigned long *flags)
__acquires(hlist_lock)
{
	unsigned long hash = hash_ptr(tsk, KPROBE_HASH_BITS);
	spinlock_t *hlist_lock;

	*head = &kretprobe_inst_table[hash];
	hlist_lock = kretprobe_table_lock_ptr(hash);
	spin_lock_irqsave(hlist_lock, *flags);
}

static void __kprobes kretprobe_table_lock(unsigned long hash,
	unsigned long *flags)
__acquires(hlist_lock)
{
	spinlock_t *hlist_lock = kretprobe_table_lock_ptr(hash);
	spin_lock_irqsave(hlist_lock, *flags);
}

void __kprobes kretprobe_hash_unlock(struct task_struct *tsk,
	unsigned long *flags)
__releases(hlist_lock)
{
	unsigned long hash = hash_ptr(tsk, KPROBE_HASH_BITS);
	spinlock_t *hlist_lock;

	hlist_lock = kretprobe_table_lock_ptr(hash);
	spin_unlock_irqrestore(hlist_lock, *flags);
}

static void __kprobes kretprobe_table_unlock(unsigned long hash,
       unsigned long *flags)
__releases(hlist_lock)
{
	spinlock_t *hlist_lock = kretprobe_table_lock_ptr(hash);
	spin_unlock_irqrestore(hlist_lock, *flags);
}

/*
 * This function is called from finish_task_switch when task tk becomes dead,
 * so that we can recycle any function-return probe instances associated
 * with this task. These left over instances represent probed functions
 * that have been called but will never return.
 */
void __kprobes kprobe_flush_task(struct task_struct *tk)
{
	struct kretprobe_instance *ri;
	struct hlist_head *head, empty_rp;
	struct hlist_node *node, *tmp;
	unsigned long hash, flags = 0;

	if (unlikely(!kprobes_initialized))
		/* Early boot.  kretprobe_table_locks not yet initialized. */
		return;

	hash = hash_ptr(tk, KPROBE_HASH_BITS);
	head = &kretprobe_inst_table[hash];
	kretprobe_table_lock(hash, &flags);
	hlist_for_each_entry_safe(ri, node, tmp, head, hlist) {
		if (ri->task == tk)
			recycle_rp_inst(ri, &empty_rp);
	}
	kretprobe_table_unlock(hash, &flags);
	INIT_HLIST_HEAD(&empty_rp);
	hlist_for_each_entry_safe(ri, node, tmp, &empty_rp, hlist) {
		hlist_del(&ri->hlist);
		kfree(ri);
	}
}

static inline void free_rp_inst(struct kretprobe *rp)
{
	struct kretprobe_instance *ri;
	struct hlist_node *pos, *next;

	hlist_for_each_entry_safe(ri, pos, next, &rp->free_instances, hlist) {
		hlist_del(&ri->hlist);
		kfree(ri);
	}
}

static void __kprobes cleanup_rp_inst(struct kretprobe *rp)
{
	unsigned long flags, hash;
	struct kretprobe_instance *ri;
	struct hlist_node *pos, *next;
	struct hlist_head *head;

	/* No race here */
	for (hash = 0; hash < KPROBE_TABLE_SIZE; hash++) {
		kretprobe_table_lock(hash, &flags);
		head = &kretprobe_inst_table[hash];
		hlist_for_each_entry_safe(ri, pos, next, head, hlist) {
			if (ri->rp == rp)
				ri->rp = NULL;
		}
		kretprobe_table_unlock(hash, &flags);
	}
	free_rp_inst(rp);
}

/*
* Add the new probe to ap->list. Fail if this is the
* second jprobe at the address - two jprobes can't coexist
*/
static int __kprobes add_new_kprobe(struct kprobe *ap, struct kprobe *p)
{
	BUG_ON(kprobe_gone(ap) || kprobe_gone(p));

	if (p->break_handler || p->post_handler)
		unoptimize_kprobe(ap);	/* Fall back to normal kprobe */

	if (p->break_handler) {
		if (ap->break_handler)
			return -EEXIST;
		list_add_tail_rcu(&p->list, &ap->list);
		ap->break_handler = aggr_break_handler;
	} else
		list_add_rcu(&p->list, &ap->list);
	if (p->post_handler && !ap->post_handler)
		ap->post_handler = aggr_post_handler;

	if (kprobe_disabled(ap) && !kprobe_disabled(p)) {
		ap->flags &= ~KPROBE_FLAG_DISABLED;
		if (!kprobes_all_disarmed)
			/* Arm the breakpoint again. */
			__arm_kprobe(ap);
	}
	return 0;
}

/*
 * Fill in the required fields of the "manager kprobe". Replace the
 * earlier kprobe in the hlist with the manager kprobe
 */
static void __kprobes init_aggr_kprobe(struct kprobe *ap, struct kprobe *p)
{
	/* Copy p's insn slot to ap */
	copy_kprobe(p, ap);
	flush_insn_slot(ap);
	ap->addr = p->addr;
	ap->flags = p->flags & ~KPROBE_FLAG_OPTIMIZED;
	ap->pre_handler = aggr_pre_handler;
	ap->fault_handler = aggr_fault_handler;
	/* We don't care the kprobe which has gone. */
	if (p->post_handler && !kprobe_gone(p))
		ap->post_handler = aggr_post_handler;
	if (p->break_handler && !kprobe_gone(p))
		ap->break_handler = aggr_break_handler;

	INIT_LIST_HEAD(&ap->list);
	INIT_HLIST_NODE(&ap->hlist);

	list_add_rcu(&p->list, &ap->list);
	hlist_replace_rcu(&p->hlist, &ap->hlist);
}

/*
 * This is the second or subsequent kprobe at the address - handle
 * the intricacies
 */
static int __kprobes register_aggr_kprobe(struct kprobe *orig_p,
					  struct kprobe *p)
{
	int ret = 0;
	struct kprobe *ap = orig_p;

	if (!kprobe_aggrprobe(orig_p)) {
		/* If orig_p is not an aggr_kprobe, create new aggr_kprobe. */
		ap = alloc_aggr_kprobe(orig_p);
		if (!ap)
			return -ENOMEM;
		init_aggr_kprobe(ap, orig_p);
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
			return ret;

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
	return add_new_kprobe(ap, p);
}

static int __kprobes in_kprobes_functions(unsigned long addr)
{
	struct kprobe_blackpoint *kb;

	if (addr >= (unsigned long)__kprobes_text_start &&
	    addr < (unsigned long)__kprobes_text_end)
		return -EINVAL;
	/*
	 * If there exists a kprobe_blacklist, verify and
	 * fail any probe registration in the prohibited area
	 */
	for (kb = kprobe_blacklist; kb->name != NULL; kb++) {
		if (kb->start_addr) {
			if (addr >= kb->start_addr &&
			    addr < (kb->start_addr + kb->range))
				return -EINVAL;
		}
	}
	return 0;
}

/*
 * If we have a symbol_name argument, look it up and add the offset field
 * to it. This way, we can specify a relative address to a symbol.
 */
static kprobe_opcode_t __kprobes *kprobe_addr(struct kprobe *p)
{
	kprobe_opcode_t *addr = p->addr;
	if (p->symbol_name) {
		if (addr)
			return NULL;
		kprobe_lookup_name(p->symbol_name, addr);
	}

	if (!addr)
		return NULL;
	return (kprobe_opcode_t *)(((char *)addr) + p->offset);
}

/* Check passed kprobe is valid and return kprobe in kprobe_table. */
static struct kprobe * __kprobes __get_valid_kprobe(struct kprobe *p)
{
	struct kprobe *ap, *list_p;

	ap = get_kprobe(p->addr);
	if (unlikely(!ap))
		return NULL;

	if (p != ap) {
		list_for_each_entry_rcu(list_p, &ap->list, list)
			if (list_p == p)
			/* kprobe p is a valid probe */
				goto valid;
		return NULL;
	}
valid:
	return ap;
}

/* Return error if the kprobe is being re-registered */
static inline int check_kprobe_rereg(struct kprobe *p)
{
	int ret = 0;

	mutex_lock(&kprobe_mutex);
	if (__get_valid_kprobe(p))
		ret = -EINVAL;
	mutex_unlock(&kprobe_mutex);

	return ret;
}

int __kprobes register_kprobe(struct kprobe *p)
{
	int ret = 0;
	struct kprobe *old_p;
	struct module *probed_mod;
	kprobe_opcode_t *addr;

	addr = kprobe_addr(p);
	if (!addr)
		return -EINVAL;
	p->addr = addr;

	ret = check_kprobe_rereg(p);
	if (ret)
		return ret;

	jump_label_lock();
	preempt_disable();
	if (!kernel_text_address((unsigned long) p->addr) ||
	    in_kprobes_functions((unsigned long) p->addr) ||
	    ftrace_text_reserved(p->addr, p->addr) ||
	    jump_label_text_reserved(p->addr, p->addr))
		goto fail_with_jump_label;

	/* User can pass only KPROBE_FLAG_DISABLED to register_kprobe */
	p->flags &= KPROBE_FLAG_DISABLED;

	/*
	 * Check if are we probing a module.
	 */
	probed_mod = __module_text_address((unsigned long) p->addr);
	if (probed_mod) {
		/*
		 * We must hold a refcount of the probed module while updating
		 * its code to prohibit unexpected unloading.
		 */
		if (unlikely(!try_module_get(probed_mod)))
			goto fail_with_jump_label;

		/*
		 * If the module freed .init.text, we couldn't insert
		 * kprobes in there.
		 */
		if (within_module_init((unsigned long)p->addr, probed_mod) &&
		    probed_mod->state != MODULE_STATE_COMING) {
			module_put(probed_mod);
			goto fail_with_jump_label;
		}
	}
	preempt_enable();
	jump_label_unlock();

	p->nmissed = 0;
	INIT_LIST_HEAD(&p->list);
	mutex_lock(&kprobe_mutex);

	jump_label_lock(); /* needed to call jump_label_text_reserved() */

	get_online_cpus();	/* For avoiding text_mutex deadlock. */
	mutex_lock(&text_mutex);

	old_p = get_kprobe(p->addr);
	if (old_p) {
		/* Since this may unoptimize old_p, locking text_mutex. */
		ret = register_aggr_kprobe(old_p, p);
		goto out;
	}

	ret = arch_prepare_kprobe(p);
	if (ret)
		goto out;

	INIT_HLIST_NODE(&p->hlist);
	hlist_add_head_rcu(&p->hlist,
		       &kprobe_table[hash_ptr(p->addr, KPROBE_HASH_BITS)]);

	if (!kprobes_all_disarmed && !kprobe_disabled(p))
		__arm_kprobe(p);

	/* Try to optimize kprobe */
	try_to_optimize_kprobe(p);

out:
	mutex_unlock(&text_mutex);
	put_online_cpus();
	jump_label_unlock();
	mutex_unlock(&kprobe_mutex);

	if (probed_mod)
		module_put(probed_mod);

	return ret;

fail_with_jump_label:
	preempt_enable();
	jump_label_unlock();
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(register_kprobe);

/* Check if all probes on the aggrprobe are disabled */
static int __kprobes aggr_kprobe_disabled(struct kprobe *ap)
{
	struct kprobe *kp;

	list_for_each_entry_rcu(kp, &ap->list, list)
		if (!kprobe_disabled(kp))
			/*
			 * There is an active probe on the list.
			 * We can't disable this ap.
			 */
			return 0;

	return 1;
}

/* Disable one kprobe: Make sure called under kprobe_mutex is locked */
static struct kprobe *__kprobes __disable_kprobe(struct kprobe *p)
{
	struct kprobe *orig_p;

	/* Get an original kprobe for return */
	orig_p = __get_valid_kprobe(p);
	if (unlikely(orig_p == NULL))
		return NULL;

	if (!kprobe_disabled(p)) {
		/* Disable probe if it is a child probe */
		if (p != orig_p)
			p->flags |= KPROBE_FLAG_DISABLED;

		/* Try to disarm and disable this/parent probe */
		if (p == orig_p || aggr_kprobe_disabled(orig_p)) {
			disarm_kprobe(orig_p);
			orig_p->flags |= KPROBE_FLAG_DISABLED;
		}
	}

	return orig_p;
}

/*
 * Unregister a kprobe without a scheduler synchronization.
 */
static int __kprobes __unregister_kprobe_top(struct kprobe *p)
{
	struct kprobe *ap, *list_p;

	/* Disable kprobe. This will disarm it if needed. */
	ap = __disable_kprobe(p);
	if (ap == NULL)
		return -EINVAL;

	if (ap == p)
		/*
		 * This probe is an independent(and non-optimized) kprobe
		 * (not an aggrprobe). Remove from the hash list.
		 */
		goto disarmed;

	/* Following process expects this probe is an aggrprobe */
	WARN_ON(!kprobe_aggrprobe(ap));

	if (list_is_singular(&ap->list))
		/* This probe is the last child of aggrprobe */
		goto disarmed;
	else {
		/* If disabling probe has special handlers, update aggrprobe */
		if (p->break_handler && !kprobe_gone(p))
			ap->break_handler = NULL;
		if (p->post_handler && !kprobe_gone(p)) {
			list_for_each_entry_rcu(list_p, &ap->list, list) {
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

static void __kprobes __unregister_kprobe_bottom(struct kprobe *p)
{
	struct kprobe *ap;

	if (list_empty(&p->list))
		arch_remove_kprobe(p);
	else if (list_is_singular(&p->list)) {
		/* "p" is the last child of an aggr_kprobe */
		ap = list_entry(p->list.next, struct kprobe, list);
		list_del(&p->list);
		arch_remove_kprobe(ap);
		free_aggr_kprobe(ap);
	}
}

int __kprobes register_kprobes(struct kprobe **kps, int num)
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

void __kprobes unregister_kprobe(struct kprobe *p)
{
	unregister_kprobes(&p, 1);
}
EXPORT_SYMBOL_GPL(unregister_kprobe);

void __kprobes unregister_kprobes(struct kprobe **kps, int num)
{
	int i;

	if (num <= 0)
		return;
	mutex_lock(&kprobe_mutex);
	for (i = 0; i < num; i++)
		if (__unregister_kprobe_top(kps[i]) < 0)
			kps[i]->addr = NULL;
	mutex_unlock(&kprobe_mutex);

	synchronize_sched();
	for (i = 0; i < num; i++)
		if (kps[i]->addr)
			__unregister_kprobe_bottom(kps[i]);
}
EXPORT_SYMBOL_GPL(unregister_kprobes);

static struct notifier_block kprobe_exceptions_nb = {
	.notifier_call = kprobe_exceptions_notify,
	.priority = 0x7fffffff /* we need to be notified first */
};

unsigned long __weak arch_deref_entry_point(void *entry)
{
	return (unsigned long)entry;
}

int __kprobes register_jprobes(struct jprobe **jps, int num)
{
	struct jprobe *jp;
	int ret = 0, i;

	if (num <= 0)
		return -EINVAL;
	for (i = 0; i < num; i++) {
		unsigned long addr, offset;
		jp = jps[i];
		addr = arch_deref_entry_point(jp->entry);

		/* Verify probepoint is a function entry point */
		if (kallsyms_lookup_size_offset(addr, NULL, &offset) &&
		    offset == 0) {
			jp->kp.pre_handler = setjmp_pre_handler;
			jp->kp.break_handler = longjmp_break_handler;
			ret = register_kprobe(&jp->kp);
		} else
			ret = -EINVAL;

		if (ret < 0) {
			if (i > 0)
				unregister_jprobes(jps, i);
			break;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(register_jprobes);

int __kprobes register_jprobe(struct jprobe *jp)
{
	return register_jprobes(&jp, 1);
}
EXPORT_SYMBOL_GPL(register_jprobe);

void __kprobes unregister_jprobe(struct jprobe *jp)
{
	unregister_jprobes(&jp, 1);
}
EXPORT_SYMBOL_GPL(unregister_jprobe);

void __kprobes unregister_jprobes(struct jprobe **jps, int num)
{
	int i;

	if (num <= 0)
		return;
	mutex_lock(&kprobe_mutex);
	for (i = 0; i < num; i++)
		if (__unregister_kprobe_top(&jps[i]->kp) < 0)
			jps[i]->kp.addr = NULL;
	mutex_unlock(&kprobe_mutex);

	synchronize_sched();
	for (i = 0; i < num; i++) {
		if (jps[i]->kp.addr)
			__unregister_kprobe_bottom(&jps[i]->kp);
	}
}
EXPORT_SYMBOL_GPL(unregister_jprobes);

#ifdef CONFIG_KRETPROBES
/*
 * This kprobe pre_handler is registered with every kretprobe. When probe
 * hits it will set up the return probe.
 */
static int __kprobes pre_handler_kretprobe(struct kprobe *p,
					   struct pt_regs *regs)
{
	struct kretprobe *rp = container_of(p, struct kretprobe, kp);
	unsigned long hash, flags = 0;
	struct kretprobe_instance *ri;

	/*TODO: consider to only swap the RA after the last pre_handler fired */
	hash = hash_ptr(current, KPROBE_HASH_BITS);
	spin_lock_irqsave(&rp->lock, flags);
	if (!hlist_empty(&rp->free_instances)) {
		ri = hlist_entry(rp->free_instances.first,
				struct kretprobe_instance, hlist);
		hlist_del(&ri->hlist);
		spin_unlock_irqrestore(&rp->lock, flags);

		ri->rp = rp;
		ri->task = current;

		if (rp->entry_handler && rp->entry_handler(ri, regs))
			return 0;

		arch_prepare_kretprobe(ri, regs);

		/* XXX(hch): why is there no hlist_move_head? */
		INIT_HLIST_NODE(&ri->hlist);
		kretprobe_table_lock(hash, &flags);
		hlist_add_head(&ri->hlist, &kretprobe_inst_table[hash]);
		kretprobe_table_unlock(hash, &flags);
	} else {
		rp->nmissed++;
		spin_unlock_irqrestore(&rp->lock, flags);
	}
	return 0;
}

int __kprobes register_kretprobe(struct kretprobe *rp)
{
	int ret = 0;
	struct kretprobe_instance *inst;
	int i;
	void *addr;

	if (kretprobe_blacklist_size) {
		addr = kprobe_addr(&rp->kp);
		if (!addr)
			return -EINVAL;

		for (i = 0; kretprobe_blacklist[i].name != NULL; i++) {
			if (kretprobe_blacklist[i].addr == addr)
				return -EINVAL;
		}
	}

	rp->kp.pre_handler = pre_handler_kretprobe;
	rp->kp.post_handler = NULL;
	rp->kp.fault_handler = NULL;
	rp->kp.break_handler = NULL;

	/* Pre-allocate memory for max kretprobe instances */
	if (rp->maxactive <= 0) {
#ifdef CONFIG_PREEMPT
		rp->maxactive = max_t(unsigned int, 10, 2*num_possible_cpus());
#else
		rp->maxactive = num_possible_cpus();
#endif
	}
	spin_lock_init(&rp->lock);
	INIT_HLIST_HEAD(&rp->free_instances);
	for (i = 0; i < rp->maxactive; i++) {
		inst = kmalloc(sizeof(struct kretprobe_instance) +
			       rp->data_size, GFP_KERNEL);
		if (inst == NULL) {
			free_rp_inst(rp);
			return -ENOMEM;
		}
		INIT_HLIST_NODE(&inst->hlist);
		hlist_add_head(&inst->hlist, &rp->free_instances);
	}

	rp->nmissed = 0;
	/* Establish function entry probe point */
	ret = register_kprobe(&rp->kp);
	if (ret != 0)
		free_rp_inst(rp);
	return ret;
}
EXPORT_SYMBOL_GPL(register_kretprobe);

int __kprobes register_kretprobes(struct kretprobe **rps, int num)
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

void __kprobes unregister_kretprobe(struct kretprobe *rp)
{
	unregister_kretprobes(&rp, 1);
}
EXPORT_SYMBOL_GPL(unregister_kretprobe);

void __kprobes unregister_kretprobes(struct kretprobe **rps, int num)
{
	int i;

	if (num <= 0)
		return;
	mutex_lock(&kprobe_mutex);
	for (i = 0; i < num; i++)
		if (__unregister_kprobe_top(&rps[i]->kp) < 0)
			rps[i]->kp.addr = NULL;
	mutex_unlock(&kprobe_mutex);

	synchronize_sched();
	for (i = 0; i < num; i++) {
		if (rps[i]->kp.addr) {
			__unregister_kprobe_bottom(&rps[i]->kp);
			cleanup_rp_inst(rps[i]);
		}
	}
}
EXPORT_SYMBOL_GPL(unregister_kretprobes);

#else /* CONFIG_KRETPROBES */
int __kprobes register_kretprobe(struct kretprobe *rp)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(register_kretprobe);

int __kprobes register_kretprobes(struct kretprobe **rps, int num)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(register_kretprobes);

void __kprobes unregister_kretprobe(struct kretprobe *rp)
{
}
EXPORT_SYMBOL_GPL(unregister_kretprobe);

void __kprobes unregister_kretprobes(struct kretprobe **rps, int num)
{
}
EXPORT_SYMBOL_GPL(unregister_kretprobes);

static int __kprobes pre_handler_kretprobe(struct kprobe *p,
					   struct pt_regs *regs)
{
	return 0;
}

#endif /* CONFIG_KRETPROBES */

/* Set the kprobe gone and remove its instruction buffer. */
static void __kprobes kill_kprobe(struct kprobe *p)
{
	struct kprobe *kp;

	p->flags |= KPROBE_FLAG_GONE;
	if (kprobe_aggrprobe(p)) {
		/*
		 * If this is an aggr_kprobe, we have to list all the
		 * chained probes and mark them GONE.
		 */
		list_for_each_entry_rcu(kp, &p->list, list)
			kp->flags |= KPROBE_FLAG_GONE;
		p->post_handler = NULL;
		p->break_handler = NULL;
		kill_optimized_kprobe(p);
	}
	/*
	 * Here, we can remove insn_slot safely, because no thread calls
	 * the original probed function (which will be freed soon) any more.
	 */
	arch_remove_kprobe(p);
}

/* Disable one kprobe */
int __kprobes disable_kprobe(struct kprobe *kp)
{
	int ret = 0;

	mutex_lock(&kprobe_mutex);

	/* Disable this kprobe */
	if (__disable_kprobe(kp) == NULL)
		ret = -EINVAL;

	mutex_unlock(&kprobe_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(disable_kprobe);

/* Enable one kprobe */
int __kprobes enable_kprobe(struct kprobe *kp)
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
		arm_kprobe(p);
	}
out:
	mutex_unlock(&kprobe_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(enable_kprobe);

void __kprobes dump_kprobe(struct kprobe *kp)
{
	printk(KERN_WARNING "Dumping kprobe:\n");
	printk(KERN_WARNING "Name: %s\nAddress: %p\nOffset: %x\n",
	       kp->symbol_name, kp->addr, kp->offset);
}

/* Module notifier call back, checking kprobes on the module */
static int __kprobes kprobes_module_callback(struct notifier_block *nb,
					     unsigned long val, void *data)
{
	struct module *mod = data;
	struct hlist_head *head;
	struct hlist_node *node;
	struct kprobe *p;
	unsigned int i;
	int checkcore = (val == MODULE_STATE_GOING);

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
		hlist_for_each_entry_rcu(p, node, head, hlist)
			if (within_module_init((unsigned long)p->addr, mod) ||
			    (checkcore &&
			     within_module_core((unsigned long)p->addr, mod))) {
				/*
				 * The vaddr this probe is installed will soon
				 * be vfreed buy not synced to disk. Hence,
				 * disarming the breakpoint isn't needed.
				 */
				kill_kprobe(p);
			}
	}
	mutex_unlock(&kprobe_mutex);
	return NOTIFY_DONE;
}

static struct notifier_block kprobe_module_nb = {
	.notifier_call = kprobes_module_callback,
	.priority = 0
};

static int __init init_kprobes(void)
{
	int i, err = 0;
	unsigned long offset = 0, size = 0;
	char *modname, namebuf[128];
	const char *symbol_name;
	void *addr;
	struct kprobe_blackpoint *kb;

	/* FIXME allocate the probe table, currently defined statically */
	/* initialize all list heads */
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		INIT_HLIST_HEAD(&kprobe_table[i]);
		INIT_HLIST_HEAD(&kretprobe_inst_table[i]);
		spin_lock_init(&(kretprobe_table_locks[i].lock));
	}

	/*
	 * Lookup and populate the kprobe_blacklist.
	 *
	 * Unlike the kretprobe blacklist, we'll need to determine
	 * the range of addresses that belong to the said functions,
	 * since a kprobe need not necessarily be at the beginning
	 * of a function.
	 */
	for (kb = kprobe_blacklist; kb->name != NULL; kb++) {
		kprobe_lookup_name(kb->name, addr);
		if (!addr)
			continue;

		kb->start_addr = (unsigned long)addr;
		symbol_name = kallsyms_lookup(kb->start_addr,
				&size, &offset, &modname, namebuf);
		if (!symbol_name)
			kb->range = 0;
		else
			kb->range = size;
	}

	if (kretprobe_blacklist_size) {
		/* lookup the function address from its name */
		for (i = 0; kretprobe_blacklist[i].name != NULL; i++) {
			kprobe_lookup_name(kretprobe_blacklist[i].name,
					   kretprobe_blacklist[i].addr);
			if (!kretprobe_blacklist[i].addr)
				printk("kretprobe: lookup failed: %s\n",
				       kretprobe_blacklist[i].name);
		}
	}

#if defined(CONFIG_OPTPROBES)
#if defined(__ARCH_WANT_KPROBES_INSN_SLOT)
	/* Init kprobe_optinsn_slots */
	kprobe_optinsn_slots.insn_size = MAX_OPTINSN_SIZE;
#endif
	/* By default, kprobes can be optimized */
	kprobes_allow_optimization = true;
#endif

	/* By default, kprobes are armed */
	kprobes_all_disarmed = false;

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

#ifdef CONFIG_DEBUG_FS
static void __kprobes report_probe(struct seq_file *pi, struct kprobe *p,
		const char *sym, int offset, char *modname, struct kprobe *pp)
{
	char *kprobe_type;

	if (p->pre_handler == pre_handler_kretprobe)
		kprobe_type = "r";
	else if (p->pre_handler == setjmp_pre_handler)
		kprobe_type = "j";
	else
		kprobe_type = "k";

	if (sym)
		seq_printf(pi, "%p  %s  %s+0x%x  %s ",
			p->addr, kprobe_type, sym, offset,
			(modname ? modname : " "));
	else
		seq_printf(pi, "%p  %s  %p ",
			p->addr, kprobe_type, p->addr);

	if (!pp)
		pp = p;
	seq_printf(pi, "%s%s%s\n",
		(kprobe_gone(p) ? "[GONE]" : ""),
		((kprobe_disabled(p) && !kprobe_gone(p)) ?  "[DISABLED]" : ""),
		(kprobe_optimized(pp) ? "[OPTIMIZED]" : ""));
}

static void __kprobes *kprobe_seq_start(struct seq_file *f, loff_t *pos)
{
	return (*pos < KPROBE_TABLE_SIZE) ? pos : NULL;
}

static void __kprobes *kprobe_seq_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos >= KPROBE_TABLE_SIZE)
		return NULL;
	return pos;
}

static void __kprobes kprobe_seq_stop(struct seq_file *f, void *v)
{
	/* Nothing to do */
}

static int __kprobes show_kprobe_addr(struct seq_file *pi, void *v)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct kprobe *p, *kp;
	const char *sym = NULL;
	unsigned int i = *(loff_t *) v;
	unsigned long offset = 0;
	char *modname, namebuf[128];

	head = &kprobe_table[i];
	preempt_disable();
	hlist_for_each_entry_rcu(p, node, head, hlist) {
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

static const struct seq_operations kprobes_seq_ops = {
	.start = kprobe_seq_start,
	.next  = kprobe_seq_next,
	.stop  = kprobe_seq_stop,
	.show  = show_kprobe_addr
};

static int __kprobes kprobes_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &kprobes_seq_ops);
}

static const struct file_operations debugfs_kprobes_operations = {
	.open           = kprobes_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
};

static void __kprobes arm_all_kprobes(void)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct kprobe *p;
	unsigned int i;

	mutex_lock(&kprobe_mutex);

	/* If kprobes are armed, just return */
	if (!kprobes_all_disarmed)
		goto already_enabled;

	/* Arming kprobes doesn't optimize kprobe itself */
	mutex_lock(&text_mutex);
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		hlist_for_each_entry_rcu(p, node, head, hlist)
			if (!kprobe_disabled(p))
				__arm_kprobe(p);
	}
	mutex_unlock(&text_mutex);

	kprobes_all_disarmed = false;
	printk(KERN_INFO "Kprobes globally enabled\n");

already_enabled:
	mutex_unlock(&kprobe_mutex);
	return;
}

static void __kprobes disarm_all_kprobes(void)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct kprobe *p;
	unsigned int i;

	mutex_lock(&kprobe_mutex);

	/* If kprobes are already disarmed, just return */
	if (kprobes_all_disarmed)
		goto already_disabled;

	kprobes_all_disarmed = true;
	printk(KERN_INFO "Kprobes globally disabled\n");

	/*
	 * Here we call get_online_cpus() for avoiding text_mutex deadlock,
	 * because disarming may also unoptimize kprobes.
	 */
	get_online_cpus();
	mutex_lock(&text_mutex);
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		hlist_for_each_entry_rcu(p, node, head, hlist) {
			if (!arch_trampoline_kprobe(p) && !kprobe_disabled(p))
				__disarm_kprobe(p);
		}
	}

	mutex_unlock(&text_mutex);
	put_online_cpus();
	mutex_unlock(&kprobe_mutex);
	/* Allow all currently running kprobes to complete */
	synchronize_sched();
	return;

already_disabled:
	mutex_unlock(&kprobe_mutex);
	return;
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
	int buf_size;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	switch (buf[0]) {
	case 'y':
	case 'Y':
	case '1':
		arm_all_kprobes();
		break;
	case 'n':
	case 'N':
	case '0':
		disarm_all_kprobes();
		break;
	}

	return count;
}

static const struct file_operations fops_kp = {
	.read =         read_enabled_file_bool,
	.write =        write_enabled_file_bool,
	.llseek =	default_llseek,
};

static int __kprobes debugfs_kprobe_init(void)
{
	struct dentry *dir, *file;
	unsigned int value = 1;

	dir = debugfs_create_dir("kprobes", NULL);
	if (!dir)
		return -ENOMEM;

	file = debugfs_create_file("list", 0444, dir, NULL,
				&debugfs_kprobes_operations);
	if (!file) {
		debugfs_remove(dir);
		return -ENOMEM;
	}

	file = debugfs_create_file("enabled", 0600, dir,
					&value, &fops_kp);
	if (!file) {
		debugfs_remove(dir);
		return -ENOMEM;
	}

	return 0;
}

late_initcall(debugfs_kprobe_init);
#endif /* CONFIG_DEBUG_FS */

module_init(init_kprobes);

/* defined in arch/.../kernel/kprobes.c */
EXPORT_SYMBOL_GPL(jprobe_return);
