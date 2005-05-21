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
 */
#include <linux/kprobes.h>
#include <linux/spinlock.h>
#include <linux/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/cacheflush.h>
#include <asm/errno.h>
#include <asm/kdebug.h>

#define KPROBE_HASH_BITS 6
#define KPROBE_TABLE_SIZE (1 << KPROBE_HASH_BITS)

static struct hlist_head kprobe_table[KPROBE_TABLE_SIZE];

unsigned int kprobe_cpu = NR_CPUS;
static DEFINE_SPINLOCK(kprobe_lock);
static struct kprobe *curr_kprobe;

/* Locks kprobe: irqs must be disabled */
void lock_kprobes(void)
{
	spin_lock(&kprobe_lock);
	kprobe_cpu = smp_processor_id();
}

void unlock_kprobes(void)
{
	kprobe_cpu = NR_CPUS;
	spin_unlock(&kprobe_lock);
}

/* You have to be holding the kprobe_lock */
struct kprobe *get_kprobe(void *addr)
{
	struct hlist_head *head;
	struct hlist_node *node;

	head = &kprobe_table[hash_ptr(addr, KPROBE_HASH_BITS)];
	hlist_for_each(node, head) {
		struct kprobe *p = hlist_entry(node, struct kprobe, hlist);
		if (p->addr == addr)
			return p;
	}
	return NULL;
}

/*
 * Aggregate handlers for multiple kprobes support - these handlers
 * take care of invoking the individual kprobe handlers on p->list
 */
int aggr_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe *kp;

	list_for_each_entry(kp, &p->list, list) {
		if (kp->pre_handler) {
			curr_kprobe = kp;
			kp->pre_handler(kp, regs);
			curr_kprobe = NULL;
		}
	}
	return 0;
}

void aggr_post_handler(struct kprobe *p, struct pt_regs *regs,
		unsigned long flags)
{
	struct kprobe *kp;

	list_for_each_entry(kp, &p->list, list) {
		if (kp->post_handler) {
			curr_kprobe = kp;
			kp->post_handler(kp, regs, flags);
			curr_kprobe = NULL;
		}
	}
	return;
}

int aggr_fault_handler(struct kprobe *p, struct pt_regs *regs, int trapnr)
{
	/*
	 * if we faulted "during" the execution of a user specified
	 * probe handler, invoke just that probe's fault handler
	 */
	if (curr_kprobe && curr_kprobe->fault_handler) {
		if (curr_kprobe->fault_handler(curr_kprobe, regs, trapnr))
			return 1;
	}
	return 0;
}

/*
 * Fill in the required fields of the "manager kprobe". Replace the
 * earlier kprobe in the hlist with the manager kprobe
 */
static inline void add_aggr_kprobe(struct kprobe *ap, struct kprobe *p)
{
	ap->addr = p->addr;
	ap->opcode = p->opcode;
	memcpy(&ap->ainsn, &p->ainsn, sizeof(struct arch_specific_insn));

	ap->pre_handler = aggr_pre_handler;
	ap->post_handler = aggr_post_handler;
	ap->fault_handler = aggr_fault_handler;

	INIT_LIST_HEAD(&ap->list);
	list_add(&p->list, &ap->list);

	INIT_HLIST_NODE(&ap->hlist);
	hlist_del(&p->hlist);
	hlist_add_head(&ap->hlist,
		&kprobe_table[hash_ptr(ap->addr, KPROBE_HASH_BITS)]);
}

/*
 * This is the second or subsequent kprobe at the address - handle
 * the intricacies
 * TODO: Move kcalloc outside the spinlock
 */
static int register_aggr_kprobe(struct kprobe *old_p, struct kprobe *p)
{
	int ret = 0;
	struct kprobe *ap;

	if (old_p->break_handler || p->break_handler) {
		ret = -EEXIST;	/* kprobe and jprobe can't (yet) coexist */
	} else if (old_p->pre_handler == aggr_pre_handler) {
		list_add(&p->list, &old_p->list);
	} else {
		ap = kcalloc(1, sizeof(struct kprobe), GFP_ATOMIC);
		if (!ap)
			return -ENOMEM;
		add_aggr_kprobe(ap, old_p);
		list_add(&p->list, &ap->list);
	}
	return ret;
}

/* kprobe removal house-keeping routines */
static inline void cleanup_kprobe(struct kprobe *p, unsigned long flags)
{
	*p->addr = p->opcode;
	hlist_del(&p->hlist);
	flush_icache_range((unsigned long) p->addr,
		   (unsigned long) p->addr + sizeof(kprobe_opcode_t));
	spin_unlock_irqrestore(&kprobe_lock, flags);
	arch_remove_kprobe(p);
}

static inline void cleanup_aggr_kprobe(struct kprobe *old_p,
		struct kprobe *p, unsigned long flags)
{
	list_del(&p->list);
	if (list_empty(&old_p->list)) {
		cleanup_kprobe(old_p, flags);
		kfree(old_p);
	} else
		spin_unlock_irqrestore(&kprobe_lock, flags);
}

int register_kprobe(struct kprobe *p)
{
	int ret = 0;
	unsigned long flags = 0;
	struct kprobe *old_p;

	if ((ret = arch_prepare_kprobe(p)) != 0) {
		goto rm_kprobe;
	}
	spin_lock_irqsave(&kprobe_lock, flags);
	old_p = get_kprobe(p->addr);
	if (old_p) {
		ret = register_aggr_kprobe(old_p, p);
		goto out;
	}

	arch_copy_kprobe(p);
	INIT_HLIST_NODE(&p->hlist);
	hlist_add_head(&p->hlist,
		       &kprobe_table[hash_ptr(p->addr, KPROBE_HASH_BITS)]);

	p->opcode = *p->addr;
	*p->addr = BREAKPOINT_INSTRUCTION;
	flush_icache_range((unsigned long) p->addr,
			   (unsigned long) p->addr + sizeof(kprobe_opcode_t));
out:
	spin_unlock_irqrestore(&kprobe_lock, flags);
rm_kprobe:
	if (ret == -EEXIST)
		arch_remove_kprobe(p);
	return ret;
}

void unregister_kprobe(struct kprobe *p)
{
	unsigned long flags;
	struct kprobe *old_p;

	spin_lock_irqsave(&kprobe_lock, flags);
	old_p = get_kprobe(p->addr);
	if (old_p) {
		if (old_p->pre_handler == aggr_pre_handler)
			cleanup_aggr_kprobe(old_p, p, flags);
		else
			cleanup_kprobe(p, flags);
	} else
		spin_unlock_irqrestore(&kprobe_lock, flags);
}

static struct notifier_block kprobe_exceptions_nb = {
	.notifier_call = kprobe_exceptions_notify,
	.priority = 0x7fffffff /* we need to notified first */
};

int register_jprobe(struct jprobe *jp)
{
	/* Todo: Verify probepoint is a function entry point */
	jp->kp.pre_handler = setjmp_pre_handler;
	jp->kp.break_handler = longjmp_break_handler;

	return register_kprobe(&jp->kp);
}

void unregister_jprobe(struct jprobe *jp)
{
	unregister_kprobe(&jp->kp);
}

static int __init init_kprobes(void)
{
	int i, err = 0;

	/* FIXME allocate the probe table, currently defined statically */
	/* initialize all list heads */
	for (i = 0; i < KPROBE_TABLE_SIZE; i++)
		INIT_HLIST_HEAD(&kprobe_table[i]);

	err = register_die_notifier(&kprobe_exceptions_nb);
	return err;
}

__initcall(init_kprobes);

EXPORT_SYMBOL_GPL(register_kprobe);
EXPORT_SYMBOL_GPL(unregister_kprobe);
EXPORT_SYMBOL_GPL(register_jprobe);
EXPORT_SYMBOL_GPL(unregister_jprobe);
EXPORT_SYMBOL_GPL(jprobe_return);
