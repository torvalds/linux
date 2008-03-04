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
#include <linux/kdebug.h>

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

static struct hlist_head kprobe_table[KPROBE_TABLE_SIZE];
static struct hlist_head kretprobe_inst_table[KPROBE_TABLE_SIZE];

/* NOTE: change this value only with kprobe_mutex held */
static bool kprobe_enabled;

DEFINE_MUTEX(kprobe_mutex);		/* Protects kprobe_table */
DEFINE_SPINLOCK(kretprobe_lock);	/* Protects kretprobe_inst_table */
static DEFINE_PER_CPU(struct kprobe *, kprobe_instance) = NULL;

#ifdef __ARCH_WANT_KPROBES_INSN_SLOT
/*
 * kprobe->ainsn.insn points to the copy of the instruction to be
 * single-stepped. x86_64, POWER4 and above have no-exec support and
 * stepping on the instruction on a vmalloced/kmalloced/data page
 * is a recipe for disaster
 */
#define INSNS_PER_PAGE	(PAGE_SIZE/(MAX_INSN_SIZE * sizeof(kprobe_opcode_t)))

struct kprobe_insn_page {
	struct hlist_node hlist;
	kprobe_opcode_t *insns;		/* Page of instruction slots */
	char slot_used[INSNS_PER_PAGE];
	int nused;
	int ngarbage;
};

enum kprobe_slot_state {
	SLOT_CLEAN = 0,
	SLOT_DIRTY = 1,
	SLOT_USED = 2,
};

static struct hlist_head kprobe_insn_pages;
static int kprobe_garbage_slots;
static int collect_garbage_slots(void);

static int __kprobes check_safety(void)
{
	int ret = 0;
#if defined(CONFIG_PREEMPT) && defined(CONFIG_PM)
	ret = freeze_processes();
	if (ret == 0) {
		struct task_struct *p, *q;
		do_each_thread(p, q) {
			if (p != current && p->state == TASK_RUNNING &&
			    p->pid != 0) {
				printk("Check failed: %s is running\n",p->comm);
				ret = -1;
				goto loop_end;
			}
		} while_each_thread(p, q);
	}
loop_end:
	thaw_processes();
#else
	synchronize_sched();
#endif
	return ret;
}

/**
 * get_insn_slot() - Find a slot on an executable page for an instruction.
 * We allocate an executable page if there's no room on existing ones.
 */
kprobe_opcode_t __kprobes *get_insn_slot(void)
{
	struct kprobe_insn_page *kip;
	struct hlist_node *pos;

 retry:
	hlist_for_each_entry(kip, pos, &kprobe_insn_pages, hlist) {
		if (kip->nused < INSNS_PER_PAGE) {
			int i;
			for (i = 0; i < INSNS_PER_PAGE; i++) {
				if (kip->slot_used[i] == SLOT_CLEAN) {
					kip->slot_used[i] = SLOT_USED;
					kip->nused++;
					return kip->insns + (i * MAX_INSN_SIZE);
				}
			}
			/* Surprise!  No unused slots.  Fix kip->nused. */
			kip->nused = INSNS_PER_PAGE;
		}
	}

	/* If there are any garbage slots, collect it and try again. */
	if (kprobe_garbage_slots && collect_garbage_slots() == 0) {
		goto retry;
	}
	/* All out of space.  Need to allocate a new page. Use slot 0. */
	kip = kmalloc(sizeof(struct kprobe_insn_page), GFP_KERNEL);
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
	INIT_HLIST_NODE(&kip->hlist);
	hlist_add_head(&kip->hlist, &kprobe_insn_pages);
	memset(kip->slot_used, SLOT_CLEAN, INSNS_PER_PAGE);
	kip->slot_used[0] = SLOT_USED;
	kip->nused = 1;
	kip->ngarbage = 0;
	return kip->insns;
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
		hlist_del(&kip->hlist);
		if (hlist_empty(&kprobe_insn_pages)) {
			INIT_HLIST_NODE(&kip->hlist);
			hlist_add_head(&kip->hlist,
				       &kprobe_insn_pages);
		} else {
			module_free(NULL, kip->insns);
			kfree(kip);
		}
		return 1;
	}
	return 0;
}

static int __kprobes collect_garbage_slots(void)
{
	struct kprobe_insn_page *kip;
	struct hlist_node *pos, *next;

	/* Ensure no-one is preepmted on the garbages */
	if (check_safety() != 0)
		return -EAGAIN;

	hlist_for_each_entry_safe(kip, pos, next, &kprobe_insn_pages, hlist) {
		int i;
		if (kip->ngarbage == 0)
			continue;
		kip->ngarbage = 0;	/* we will collect all garbages */
		for (i = 0; i < INSNS_PER_PAGE; i++) {
			if (kip->slot_used[i] == SLOT_DIRTY &&
			    collect_one_slot(kip, i))
				break;
		}
	}
	kprobe_garbage_slots = 0;
	return 0;
}

void __kprobes free_insn_slot(kprobe_opcode_t * slot, int dirty)
{
	struct kprobe_insn_page *kip;
	struct hlist_node *pos;

	hlist_for_each_entry(kip, pos, &kprobe_insn_pages, hlist) {
		if (kip->insns <= slot &&
		    slot < kip->insns + (INSNS_PER_PAGE * MAX_INSN_SIZE)) {
			int i = (slot - kip->insns) / MAX_INSN_SIZE;
			if (dirty) {
				kip->slot_used[i] = SLOT_DIRTY;
				kip->ngarbage++;
			} else {
				collect_one_slot(kip, i);
			}
			break;
		}
	}

	if (dirty && ++kprobe_garbage_slots > INSNS_PER_PAGE)
		collect_garbage_slots();
}
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

/*
 * Aggregate handlers for multiple kprobes support - these handlers
 * take care of invoking the individual kprobe handlers on p->list
 */
static int __kprobes aggr_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe *kp;

	list_for_each_entry_rcu(kp, &p->list, list) {
		if (kp->pre_handler) {
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
		if (kp->post_handler) {
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
	if (p->pre_handler != aggr_pre_handler) {
		p->nmissed++;
	} else {
		list_for_each_entry_rcu(kp, &p->list, list)
			kp->nmissed++;
	}
	return;
}

/* Called with kretprobe_lock held */
void __kprobes recycle_rp_inst(struct kretprobe_instance *ri,
				struct hlist_head *head)
{
	/* remove rp inst off the rprobe_inst_table */
	hlist_del(&ri->hlist);
	if (ri->rp) {
		/* remove rp inst off the used list */
		hlist_del(&ri->uflist);
		/* put rp inst back onto the free list */
		INIT_HLIST_NODE(&ri->uflist);
		hlist_add_head(&ri->uflist, &ri->rp->free_instances);
	} else
		/* Unregistering */
		hlist_add_head(&ri->hlist, head);
}

struct hlist_head __kprobes *kretprobe_inst_table_head(struct task_struct *tsk)
{
	return &kretprobe_inst_table[hash_ptr(tsk, KPROBE_HASH_BITS)];
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
	unsigned long flags = 0;

	INIT_HLIST_HEAD(&empty_rp);
	spin_lock_irqsave(&kretprobe_lock, flags);
	head = kretprobe_inst_table_head(tk);
	hlist_for_each_entry_safe(ri, node, tmp, head, hlist) {
		if (ri->task == tk)
			recycle_rp_inst(ri, &empty_rp);
	}
	spin_unlock_irqrestore(&kretprobe_lock, flags);

	hlist_for_each_entry_safe(ri, node, tmp, &empty_rp, hlist) {
		hlist_del(&ri->hlist);
		kfree(ri);
	}
}

static inline void free_rp_inst(struct kretprobe *rp)
{
	struct kretprobe_instance *ri;
	struct hlist_node *pos, *next;

	hlist_for_each_entry_safe(ri, pos, next, &rp->free_instances, uflist) {
		hlist_del(&ri->uflist);
		kfree(ri);
	}
}

/*
 * Keep all fields in the kprobe consistent
 */
static inline void copy_kprobe(struct kprobe *old_p, struct kprobe *p)
{
	memcpy(&p->opcode, &old_p->opcode, sizeof(kprobe_opcode_t));
	memcpy(&p->ainsn, &old_p->ainsn, sizeof(struct arch_specific_insn));
}

/*
* Add the new probe to old_p->list. Fail if this is the
* second jprobe at the address - two jprobes can't coexist
*/
static int __kprobes add_new_kprobe(struct kprobe *old_p, struct kprobe *p)
{
	if (p->break_handler) {
		if (old_p->break_handler)
			return -EEXIST;
		list_add_tail_rcu(&p->list, &old_p->list);
		old_p->break_handler = aggr_break_handler;
	} else
		list_add_rcu(&p->list, &old_p->list);
	if (p->post_handler && !old_p->post_handler)
		old_p->post_handler = aggr_post_handler;
	return 0;
}

/*
 * Fill in the required fields of the "manager kprobe". Replace the
 * earlier kprobe in the hlist with the manager kprobe
 */
static inline void add_aggr_kprobe(struct kprobe *ap, struct kprobe *p)
{
	copy_kprobe(p, ap);
	flush_insn_slot(ap);
	ap->addr = p->addr;
	ap->pre_handler = aggr_pre_handler;
	ap->fault_handler = aggr_fault_handler;
	if (p->post_handler)
		ap->post_handler = aggr_post_handler;
	if (p->break_handler)
		ap->break_handler = aggr_break_handler;

	INIT_LIST_HEAD(&ap->list);
	list_add_rcu(&p->list, &ap->list);

	hlist_replace_rcu(&p->hlist, &ap->hlist);
}

/*
 * This is the second or subsequent kprobe at the address - handle
 * the intricacies
 */
static int __kprobes register_aggr_kprobe(struct kprobe *old_p,
					  struct kprobe *p)
{
	int ret = 0;
	struct kprobe *ap;

	if (old_p->pre_handler == aggr_pre_handler) {
		copy_kprobe(old_p, p);
		ret = add_new_kprobe(old_p, p);
	} else {
		ap = kzalloc(sizeof(struct kprobe), GFP_KERNEL);
		if (!ap)
			return -ENOMEM;
		add_aggr_kprobe(ap, old_p);
		copy_kprobe(ap, p);
		ret = add_new_kprobe(ap, p);
	}
	return ret;
}

static int __kprobes in_kprobes_functions(unsigned long addr)
{
	if (addr >= (unsigned long)__kprobes_text_start &&
	    addr < (unsigned long)__kprobes_text_end)
		return -EINVAL;
	return 0;
}

static int __kprobes __register_kprobe(struct kprobe *p,
	unsigned long called_from)
{
	int ret = 0;
	struct kprobe *old_p;
	struct module *probed_mod;

	/*
	 * If we have a symbol_name argument look it up,
	 * and add it to the address.  That way the addr
	 * field can either be global or relative to a symbol.
	 */
	if (p->symbol_name) {
		if (p->addr)
			return -EINVAL;
		kprobe_lookup_name(p->symbol_name, p->addr);
	}

	if (!p->addr)
		return -EINVAL;
	p->addr = (kprobe_opcode_t *)(((char *)p->addr)+ p->offset);

	if (!kernel_text_address((unsigned long) p->addr) ||
	    in_kprobes_functions((unsigned long) p->addr))
		return -EINVAL;

	p->mod_refcounted = 0;

	/*
	 * Check if are we probing a module.
	 */
	probed_mod = module_text_address((unsigned long) p->addr);
	if (probed_mod) {
		struct module *calling_mod = module_text_address(called_from);
		/*
		 * We must allow modules to probe themself and in this case
		 * avoid incrementing the module refcount, so as to allow
		 * unloading of self probing modules.
		 */
		if (calling_mod && calling_mod != probed_mod) {
			if (unlikely(!try_module_get(probed_mod)))
				return -EINVAL;
			p->mod_refcounted = 1;
		} else
			probed_mod = NULL;
	}

	p->nmissed = 0;
	mutex_lock(&kprobe_mutex);
	old_p = get_kprobe(p->addr);
	if (old_p) {
		ret = register_aggr_kprobe(old_p, p);
		goto out;
	}

	ret = arch_prepare_kprobe(p);
	if (ret)
		goto out;

	INIT_HLIST_NODE(&p->hlist);
	hlist_add_head_rcu(&p->hlist,
		       &kprobe_table[hash_ptr(p->addr, KPROBE_HASH_BITS)]);

	if (kprobe_enabled)
		arch_arm_kprobe(p);

out:
	mutex_unlock(&kprobe_mutex);

	if (ret && probed_mod)
		module_put(probed_mod);
	return ret;
}

int __kprobes register_kprobe(struct kprobe *p)
{
	return __register_kprobe(p, (unsigned long)__builtin_return_address(0));
}

void __kprobes unregister_kprobe(struct kprobe *p)
{
	struct module *mod;
	struct kprobe *old_p, *list_p;
	int cleanup_p;

	mutex_lock(&kprobe_mutex);
	old_p = get_kprobe(p->addr);
	if (unlikely(!old_p)) {
		mutex_unlock(&kprobe_mutex);
		return;
	}
	if (p != old_p) {
		list_for_each_entry_rcu(list_p, &old_p->list, list)
			if (list_p == p)
			/* kprobe p is a valid probe */
				goto valid_p;
		mutex_unlock(&kprobe_mutex);
		return;
	}
valid_p:
	if (old_p == p ||
	    (old_p->pre_handler == aggr_pre_handler &&
	     p->list.next == &old_p->list && p->list.prev == &old_p->list)) {
		/*
		 * Only probe on the hash list. Disarm only if kprobes are
		 * enabled - otherwise, the breakpoint would already have
		 * been removed. We save on flushing icache.
		 */
		if (kprobe_enabled)
			arch_disarm_kprobe(p);
		hlist_del_rcu(&old_p->hlist);
		cleanup_p = 1;
	} else {
		list_del_rcu(&p->list);
		cleanup_p = 0;
	}

	mutex_unlock(&kprobe_mutex);

	synchronize_sched();
	if (p->mod_refcounted) {
		mod = module_text_address((unsigned long)p->addr);
		if (mod)
			module_put(mod);
	}

	if (cleanup_p) {
		if (p != old_p) {
			list_del_rcu(&p->list);
			kfree(old_p);
		}
		arch_remove_kprobe(p);
	} else {
		mutex_lock(&kprobe_mutex);
		if (p->break_handler)
			old_p->break_handler = NULL;
		if (p->post_handler){
			list_for_each_entry_rcu(list_p, &old_p->list, list){
				if (list_p->post_handler){
					cleanup_p = 2;
					break;
				}
			}
			if (cleanup_p == 0)
				old_p->post_handler = NULL;
		}
		mutex_unlock(&kprobe_mutex);
	}
}

static struct notifier_block kprobe_exceptions_nb = {
	.notifier_call = kprobe_exceptions_notify,
	.priority = 0x7fffffff /* we need to be notified first */
};

unsigned long __weak arch_deref_entry_point(void *entry)
{
	return (unsigned long)entry;
}

int __kprobes register_jprobe(struct jprobe *jp)
{
	unsigned long addr = arch_deref_entry_point(jp->entry);

	if (!kernel_text_address(addr))
		return -EINVAL;

	/* Todo: Verify probepoint is a function entry point */
	jp->kp.pre_handler = setjmp_pre_handler;
	jp->kp.break_handler = longjmp_break_handler;

	return __register_kprobe(&jp->kp,
		(unsigned long)__builtin_return_address(0));
}

void __kprobes unregister_jprobe(struct jprobe *jp)
{
	unregister_kprobe(&jp->kp);
}

#ifdef CONFIG_KRETPROBES
/*
 * This kprobe pre_handler is registered with every kretprobe. When probe
 * hits it will set up the return probe.
 */
static int __kprobes pre_handler_kretprobe(struct kprobe *p,
					   struct pt_regs *regs)
{
	struct kretprobe *rp = container_of(p, struct kretprobe, kp);
	unsigned long flags = 0;

	/*TODO: consider to only swap the RA after the last pre_handler fired */
	spin_lock_irqsave(&kretprobe_lock, flags);
	if (!hlist_empty(&rp->free_instances)) {
		struct kretprobe_instance *ri;

		ri = hlist_entry(rp->free_instances.first,
				 struct kretprobe_instance, uflist);
		ri->rp = rp;
		ri->task = current;

		if (rp->entry_handler && rp->entry_handler(ri, regs)) {
			spin_unlock_irqrestore(&kretprobe_lock, flags);
			return 0;
		}

		arch_prepare_kretprobe(ri, regs);

		/* XXX(hch): why is there no hlist_move_head? */
		hlist_del(&ri->uflist);
		hlist_add_head(&ri->uflist, &ri->rp->used_instances);
		hlist_add_head(&ri->hlist, kretprobe_inst_table_head(ri->task));
	} else
		rp->nmissed++;
	spin_unlock_irqrestore(&kretprobe_lock, flags);
	return 0;
}

int __kprobes register_kretprobe(struct kretprobe *rp)
{
	int ret = 0;
	struct kretprobe_instance *inst;
	int i;
	void *addr = rp->kp.addr;

	if (kretprobe_blacklist_size) {
		if (addr == NULL)
			kprobe_lookup_name(rp->kp.symbol_name, addr);
		addr += rp->kp.offset;

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
		rp->maxactive = max(10, 2 * NR_CPUS);
#else
		rp->maxactive = NR_CPUS;
#endif
	}
	INIT_HLIST_HEAD(&rp->used_instances);
	INIT_HLIST_HEAD(&rp->free_instances);
	for (i = 0; i < rp->maxactive; i++) {
		inst = kmalloc(sizeof(struct kretprobe_instance) +
			       rp->data_size, GFP_KERNEL);
		if (inst == NULL) {
			free_rp_inst(rp);
			return -ENOMEM;
		}
		INIT_HLIST_NODE(&inst->uflist);
		hlist_add_head(&inst->uflist, &rp->free_instances);
	}

	rp->nmissed = 0;
	/* Establish function entry probe point */
	if ((ret = __register_kprobe(&rp->kp,
		(unsigned long)__builtin_return_address(0))) != 0)
		free_rp_inst(rp);
	return ret;
}

#else /* CONFIG_KRETPROBES */
int __kprobes register_kretprobe(struct kretprobe *rp)
{
	return -ENOSYS;
}

static int __kprobes pre_handler_kretprobe(struct kprobe *p,
					   struct pt_regs *regs)
{
	return 0;
}
#endif /* CONFIG_KRETPROBES */

void __kprobes unregister_kretprobe(struct kretprobe *rp)
{
	unsigned long flags;
	struct kretprobe_instance *ri;
	struct hlist_node *pos, *next;

	unregister_kprobe(&rp->kp);

	/* No race here */
	spin_lock_irqsave(&kretprobe_lock, flags);
	hlist_for_each_entry_safe(ri, pos, next, &rp->used_instances, uflist) {
		ri->rp = NULL;
		hlist_del(&ri->uflist);
	}
	spin_unlock_irqrestore(&kretprobe_lock, flags);
	free_rp_inst(rp);
}

static int __init init_kprobes(void)
{
	int i, err = 0;

	/* FIXME allocate the probe table, currently defined statically */
	/* initialize all list heads */
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		INIT_HLIST_HEAD(&kprobe_table[i]);
		INIT_HLIST_HEAD(&kretprobe_inst_table[i]);
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

	/* By default, kprobes are enabled */
	kprobe_enabled = true;

	err = arch_init_kprobes();
	if (!err)
		err = register_die_notifier(&kprobe_exceptions_nb);

	if (!err)
		init_test_probes();
	return err;
}

#ifdef CONFIG_DEBUG_FS
static void __kprobes report_probe(struct seq_file *pi, struct kprobe *p,
		const char *sym, int offset,char *modname)
{
	char *kprobe_type;

	if (p->pre_handler == pre_handler_kretprobe)
		kprobe_type = "r";
	else if (p->pre_handler == setjmp_pre_handler)
		kprobe_type = "j";
	else
		kprobe_type = "k";
	if (sym)
		seq_printf(pi, "%p  %s  %s+0x%x  %s\n", p->addr, kprobe_type,
			sym, offset, (modname ? modname : " "));
	else
		seq_printf(pi, "%p  %s  %p\n", p->addr, kprobe_type, p->addr);
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
		if (p->pre_handler == aggr_pre_handler) {
			list_for_each_entry_rcu(kp, &p->list, list)
				report_probe(pi, kp, sym, offset, modname);
		} else
			report_probe(pi, p, sym, offset, modname);
	}
	preempt_enable();
	return 0;
}

static struct seq_operations kprobes_seq_ops = {
	.start = kprobe_seq_start,
	.next  = kprobe_seq_next,
	.stop  = kprobe_seq_stop,
	.show  = show_kprobe_addr
};

static int __kprobes kprobes_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &kprobes_seq_ops);
}

static struct file_operations debugfs_kprobes_operations = {
	.open           = kprobes_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
};

static void __kprobes enable_all_kprobes(void)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct kprobe *p;
	unsigned int i;

	mutex_lock(&kprobe_mutex);

	/* If kprobes are already enabled, just return */
	if (kprobe_enabled)
		goto already_enabled;

	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		hlist_for_each_entry_rcu(p, node, head, hlist)
			arch_arm_kprobe(p);
	}

	kprobe_enabled = true;
	printk(KERN_INFO "Kprobes globally enabled\n");

already_enabled:
	mutex_unlock(&kprobe_mutex);
	return;
}

static void __kprobes disable_all_kprobes(void)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct kprobe *p;
	unsigned int i;

	mutex_lock(&kprobe_mutex);

	/* If kprobes are already disabled, just return */
	if (!kprobe_enabled)
		goto already_disabled;

	kprobe_enabled = false;
	printk(KERN_INFO "Kprobes globally disabled\n");
	for (i = 0; i < KPROBE_TABLE_SIZE; i++) {
		head = &kprobe_table[i];
		hlist_for_each_entry_rcu(p, node, head, hlist) {
			if (!arch_trampoline_kprobe(p))
				arch_disarm_kprobe(p);
		}
	}

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

	if (kprobe_enabled)
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
		enable_all_kprobes();
		break;
	case 'n':
	case 'N':
	case '0':
		disable_all_kprobes();
		break;
	}

	return count;
}

static struct file_operations fops_kp = {
	.read =         read_enabled_file_bool,
	.write =        write_enabled_file_bool,
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

EXPORT_SYMBOL_GPL(register_kprobe);
EXPORT_SYMBOL_GPL(unregister_kprobe);
EXPORT_SYMBOL_GPL(register_jprobe);
EXPORT_SYMBOL_GPL(unregister_jprobe);
#ifdef CONFIG_KPROBES
EXPORT_SYMBOL_GPL(jprobe_return);
#endif

#ifdef CONFIG_KPROBES
EXPORT_SYMBOL_GPL(register_kretprobe);
EXPORT_SYMBOL_GPL(unregister_kretprobe);
#endif
