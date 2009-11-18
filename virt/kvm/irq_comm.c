/*
 * irq_comm.c: Common API for in kernel interrupt controller
 * Copyright (c) 2007, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 * Authors:
 *   Yaozu (Eddie) Dong <Eddie.dong@intel.com>
 *
 */

#include <linux/kvm_host.h>
#include <trace/events/kvm.h>

#include <asm/msidef.h>
#ifdef CONFIG_IA64
#include <asm/iosapic.h>
#endif

#include "irq.h"

#include "ioapic.h"

static int kvm_set_pic_irq(struct kvm_kernel_irq_routing_entry *e,
			   struct kvm *kvm, int level)
{
#ifdef CONFIG_X86
	return kvm_pic_set_irq(pic_irqchip(kvm), e->irqchip.pin, level);
#else
	return -1;
#endif
}

static int kvm_set_ioapic_irq(struct kvm_kernel_irq_routing_entry *e,
			      struct kvm *kvm, int level)
{
	return kvm_ioapic_set_irq(kvm->arch.vioapic, e->irqchip.pin, level);
}

inline static bool kvm_is_dm_lowest_prio(struct kvm_lapic_irq *irq)
{
#ifdef CONFIG_IA64
	return irq->delivery_mode ==
		(IOSAPIC_LOWEST_PRIORITY << IOSAPIC_DELIVERY_SHIFT);
#else
	return irq->delivery_mode == APIC_DM_LOWEST;
#endif
}

int kvm_irq_delivery_to_apic(struct kvm *kvm, struct kvm_lapic *src,
		struct kvm_lapic_irq *irq)
{
	int i, r = -1;
	struct kvm_vcpu *vcpu, *lowest = NULL;

	WARN_ON(!mutex_is_locked(&kvm->irq_lock));

	if (irq->dest_mode == 0 && irq->dest_id == 0xff &&
			kvm_is_dm_lowest_prio(irq))
		printk(KERN_INFO "kvm: apic: phys broadcast and lowest prio\n");

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!kvm_apic_present(vcpu))
			continue;

		if (!kvm_apic_match_dest(vcpu, src, irq->shorthand,
					irq->dest_id, irq->dest_mode))
			continue;

		if (!kvm_is_dm_lowest_prio(irq)) {
			if (r < 0)
				r = 0;
			r += kvm_apic_set_irq(vcpu, irq);
		} else {
			if (!lowest)
				lowest = vcpu;
			else if (kvm_apic_compare_prio(vcpu, lowest) < 0)
				lowest = vcpu;
		}
	}

	if (lowest)
		r = kvm_apic_set_irq(lowest, irq);

	return r;
}

static int kvm_set_msi(struct kvm_kernel_irq_routing_entry *e,
		       struct kvm *kvm, int level)
{
	struct kvm_lapic_irq irq;

	trace_kvm_msi_set_irq(e->msi.address_lo, e->msi.data);

	irq.dest_id = (e->msi.address_lo &
			MSI_ADDR_DEST_ID_MASK) >> MSI_ADDR_DEST_ID_SHIFT;
	irq.vector = (e->msi.data &
			MSI_DATA_VECTOR_MASK) >> MSI_DATA_VECTOR_SHIFT;
	irq.dest_mode = (1 << MSI_ADDR_DEST_MODE_SHIFT) & e->msi.address_lo;
	irq.trig_mode = (1 << MSI_DATA_TRIGGER_SHIFT) & e->msi.data;
	irq.delivery_mode = e->msi.data & 0x700;
	irq.level = 1;
	irq.shorthand = 0;

	/* TODO Deal with RH bit of MSI message address */
	return kvm_irq_delivery_to_apic(kvm, NULL, &irq);
}

/* This should be called with the kvm->irq_lock mutex held
 * Return value:
 *  < 0   Interrupt was ignored (masked or not delivered for other reasons)
 *  = 0   Interrupt was coalesced (previous irq is still pending)
 *  > 0   Number of CPUs interrupt was delivered to
 */
int kvm_set_irq(struct kvm *kvm, int irq_source_id, int irq, int level)
{
	struct kvm_kernel_irq_routing_entry *e;
	unsigned long *irq_state, sig_level;
	int ret = -1;

	trace_kvm_set_irq(irq, level, irq_source_id);

	WARN_ON(!mutex_is_locked(&kvm->irq_lock));

	if (irq < KVM_IOAPIC_NUM_PINS) {
		irq_state = (unsigned long *)&kvm->arch.irq_states[irq];

		/* Logical OR for level trig interrupt */
		if (level)
			set_bit(irq_source_id, irq_state);
		else
			clear_bit(irq_source_id, irq_state);
		sig_level = !!(*irq_state);
	} else if (!level)
		return ret;
	else /* Deal with MSI/MSI-X */
		sig_level = 1;

	/* Not possible to detect if the guest uses the PIC or the
	 * IOAPIC.  So set the bit in both. The guest will ignore
	 * writes to the unused one.
	 */
	list_for_each_entry(e, &kvm->irq_routing, link)
		if (e->gsi == irq) {
			int r = e->set(e, kvm, sig_level);
			if (r < 0)
				continue;

			ret = r + ((ret < 0) ? 0 : ret);
		}
	return ret;
}

void kvm_notify_acked_irq(struct kvm *kvm, unsigned irqchip, unsigned pin)
{
	struct kvm_kernel_irq_routing_entry *e;
	struct kvm_irq_ack_notifier *kian;
	struct hlist_node *n;
	unsigned gsi = pin;

	trace_kvm_ack_irq(irqchip, pin);

	list_for_each_entry(e, &kvm->irq_routing, link)
		if (e->type == KVM_IRQ_ROUTING_IRQCHIP &&
		    e->irqchip.irqchip == irqchip &&
		    e->irqchip.pin == pin) {
			gsi = e->gsi;
			break;
		}

	hlist_for_each_entry(kian, n, &kvm->arch.irq_ack_notifier_list, link)
		if (kian->gsi == gsi)
			kian->irq_acked(kian);
}

void kvm_register_irq_ack_notifier(struct kvm *kvm,
				   struct kvm_irq_ack_notifier *kian)
{
	mutex_lock(&kvm->irq_lock);
	hlist_add_head(&kian->link, &kvm->arch.irq_ack_notifier_list);
	mutex_unlock(&kvm->irq_lock);
}

void kvm_unregister_irq_ack_notifier(struct kvm *kvm,
				    struct kvm_irq_ack_notifier *kian)
{
	mutex_lock(&kvm->irq_lock);
	hlist_del_init(&kian->link);
	mutex_unlock(&kvm->irq_lock);
}

int kvm_request_irq_source_id(struct kvm *kvm)
{
	unsigned long *bitmap = &kvm->arch.irq_sources_bitmap;
	int irq_source_id;

	mutex_lock(&kvm->irq_lock);
	irq_source_id = find_first_zero_bit(bitmap,
				sizeof(kvm->arch.irq_sources_bitmap));

	if (irq_source_id >= sizeof(kvm->arch.irq_sources_bitmap)) {
		printk(KERN_WARNING "kvm: exhaust allocatable IRQ sources!\n");
		return -EFAULT;
	}

	ASSERT(irq_source_id != KVM_USERSPACE_IRQ_SOURCE_ID);
	set_bit(irq_source_id, bitmap);
	mutex_unlock(&kvm->irq_lock);

	return irq_source_id;
}

void kvm_free_irq_source_id(struct kvm *kvm, int irq_source_id)
{
	int i;

	ASSERT(irq_source_id != KVM_USERSPACE_IRQ_SOURCE_ID);

	mutex_lock(&kvm->irq_lock);
	if (irq_source_id < 0 ||
	    irq_source_id >= sizeof(kvm->arch.irq_sources_bitmap)) {
		printk(KERN_ERR "kvm: IRQ source ID out of range!\n");
		return;
	}
	for (i = 0; i < KVM_IOAPIC_NUM_PINS; i++)
		clear_bit(irq_source_id, &kvm->arch.irq_states[i]);
	clear_bit(irq_source_id, &kvm->arch.irq_sources_bitmap);
	mutex_unlock(&kvm->irq_lock);
}

void kvm_register_irq_mask_notifier(struct kvm *kvm, int irq,
				    struct kvm_irq_mask_notifier *kimn)
{
	mutex_lock(&kvm->irq_lock);
	kimn->irq = irq;
	hlist_add_head(&kimn->link, &kvm->mask_notifier_list);
	mutex_unlock(&kvm->irq_lock);
}

void kvm_unregister_irq_mask_notifier(struct kvm *kvm, int irq,
				      struct kvm_irq_mask_notifier *kimn)
{
	mutex_lock(&kvm->irq_lock);
	hlist_del(&kimn->link);
	mutex_unlock(&kvm->irq_lock);
}

void kvm_fire_mask_notifiers(struct kvm *kvm, int irq, bool mask)
{
	struct kvm_irq_mask_notifier *kimn;
	struct hlist_node *n;

	WARN_ON(!mutex_is_locked(&kvm->irq_lock));

	hlist_for_each_entry(kimn, n, &kvm->mask_notifier_list, link)
		if (kimn->irq == irq)
			kimn->func(kimn, mask);
}

static void __kvm_free_irq_routing(struct list_head *irq_routing)
{
	struct kvm_kernel_irq_routing_entry *e, *n;

	list_for_each_entry_safe(e, n, irq_routing, link)
		kfree(e);
}

void kvm_free_irq_routing(struct kvm *kvm)
{
	mutex_lock(&kvm->irq_lock);
	__kvm_free_irq_routing(&kvm->irq_routing);
	mutex_unlock(&kvm->irq_lock);
}

static int setup_routing_entry(struct kvm_kernel_irq_routing_entry *e,
			       const struct kvm_irq_routing_entry *ue)
{
	int r = -EINVAL;
	int delta;

	e->gsi = ue->gsi;
	e->type = ue->type;
	switch (ue->type) {
	case KVM_IRQ_ROUTING_IRQCHIP:
		delta = 0;
		switch (ue->u.irqchip.irqchip) {
		case KVM_IRQCHIP_PIC_MASTER:
			e->set = kvm_set_pic_irq;
			break;
		case KVM_IRQCHIP_PIC_SLAVE:
			e->set = kvm_set_pic_irq;
			delta = 8;
			break;
		case KVM_IRQCHIP_IOAPIC:
			e->set = kvm_set_ioapic_irq;
			break;
		default:
			goto out;
		}
		e->irqchip.irqchip = ue->u.irqchip.irqchip;
		e->irqchip.pin = ue->u.irqchip.pin + delta;
		break;
	case KVM_IRQ_ROUTING_MSI:
		e->set = kvm_set_msi;
		e->msi.address_lo = ue->u.msi.address_lo;
		e->msi.address_hi = ue->u.msi.address_hi;
		e->msi.data = ue->u.msi.data;
		break;
	default:
		goto out;
	}
	r = 0;
out:
	return r;
}


int kvm_set_irq_routing(struct kvm *kvm,
			const struct kvm_irq_routing_entry *ue,
			unsigned nr,
			unsigned flags)
{
	struct list_head irq_list = LIST_HEAD_INIT(irq_list);
	struct list_head tmp = LIST_HEAD_INIT(tmp);
	struct kvm_kernel_irq_routing_entry *e = NULL;
	unsigned i;
	int r;

	for (i = 0; i < nr; ++i) {
		r = -EINVAL;
		if (ue->gsi >= KVM_MAX_IRQ_ROUTES)
			goto out;
		if (ue->flags)
			goto out;
		r = -ENOMEM;
		e = kzalloc(sizeof(*e), GFP_KERNEL);
		if (!e)
			goto out;
		r = setup_routing_entry(e, ue);
		if (r)
			goto out;
		++ue;
		list_add(&e->link, &irq_list);
		e = NULL;
	}

	mutex_lock(&kvm->irq_lock);
	list_splice(&kvm->irq_routing, &tmp);
	INIT_LIST_HEAD(&kvm->irq_routing);
	list_splice(&irq_list, &kvm->irq_routing);
	INIT_LIST_HEAD(&irq_list);
	list_splice(&tmp, &irq_list);
	mutex_unlock(&kvm->irq_lock);

	r = 0;

out:
	kfree(e);
	__kvm_free_irq_routing(&irq_list);
	return r;
}

#define IOAPIC_ROUTING_ENTRY(irq) \
	{ .gsi = irq, .type = KVM_IRQ_ROUTING_IRQCHIP,	\
	  .u.irqchip.irqchip = KVM_IRQCHIP_IOAPIC, .u.irqchip.pin = (irq) }
#define ROUTING_ENTRY1(irq) IOAPIC_ROUTING_ENTRY(irq)

#ifdef CONFIG_X86
#  define PIC_ROUTING_ENTRY(irq) \
	{ .gsi = irq, .type = KVM_IRQ_ROUTING_IRQCHIP,	\
	  .u.irqchip.irqchip = SELECT_PIC(irq), .u.irqchip.pin = (irq) % 8 }
#  define ROUTING_ENTRY2(irq) \
	IOAPIC_ROUTING_ENTRY(irq), PIC_ROUTING_ENTRY(irq)
#else
#  define ROUTING_ENTRY2(irq) \
	IOAPIC_ROUTING_ENTRY(irq)
#endif

static const struct kvm_irq_routing_entry default_routing[] = {
	ROUTING_ENTRY2(0), ROUTING_ENTRY2(1),
	ROUTING_ENTRY2(2), ROUTING_ENTRY2(3),
	ROUTING_ENTRY2(4), ROUTING_ENTRY2(5),
	ROUTING_ENTRY2(6), ROUTING_ENTRY2(7),
	ROUTING_ENTRY2(8), ROUTING_ENTRY2(9),
	ROUTING_ENTRY2(10), ROUTING_ENTRY2(11),
	ROUTING_ENTRY2(12), ROUTING_ENTRY2(13),
	ROUTING_ENTRY2(14), ROUTING_ENTRY2(15),
	ROUTING_ENTRY1(16), ROUTING_ENTRY1(17),
	ROUTING_ENTRY1(18), ROUTING_ENTRY1(19),
	ROUTING_ENTRY1(20), ROUTING_ENTRY1(21),
	ROUTING_ENTRY1(22), ROUTING_ENTRY1(23),
#ifdef CONFIG_IA64
	ROUTING_ENTRY1(24), ROUTING_ENTRY1(25),
	ROUTING_ENTRY1(26), ROUTING_ENTRY1(27),
	ROUTING_ENTRY1(28), ROUTING_ENTRY1(29),
	ROUTING_ENTRY1(30), ROUTING_ENTRY1(31),
	ROUTING_ENTRY1(32), ROUTING_ENTRY1(33),
	ROUTING_ENTRY1(34), ROUTING_ENTRY1(35),
	ROUTING_ENTRY1(36), ROUTING_ENTRY1(37),
	ROUTING_ENTRY1(38), ROUTING_ENTRY1(39),
	ROUTING_ENTRY1(40), ROUTING_ENTRY1(41),
	ROUTING_ENTRY1(42), ROUTING_ENTRY1(43),
	ROUTING_ENTRY1(44), ROUTING_ENTRY1(45),
	ROUTING_ENTRY1(46), ROUTING_ENTRY1(47),
#endif
};

int kvm_setup_default_irq_routing(struct kvm *kvm)
{
	return kvm_set_irq_routing(kvm, default_routing,
				   ARRAY_SIZE(default_routing), 0);
}
