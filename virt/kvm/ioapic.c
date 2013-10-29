/*
 *  Copyright (C) 2001  MandrakeSoft S.A.
 *  Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 *    MandrakeSoft S.A.
 *    43, rue d'Aboukir
 *    75002 Paris - France
 *    http://www.linux-mandrake.com/
 *    http://www.mandrakesoft.com/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *  Yunhong Jiang <yunhong.jiang@intel.com>
 *  Yaozu (Eddie) Dong <eddie.dong@intel.com>
 *  Based on Xen 3.1 code.
 */

#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/smp.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/current.h>
#include <trace/events/kvm.h>

#include "ioapic.h"
#include "lapic.h"
#include "irq.h"

#if 0
#define ioapic_debug(fmt,arg...) printk(KERN_WARNING fmt,##arg)
#else
#define ioapic_debug(fmt, arg...)
#endif
static int ioapic_deliver(struct kvm_ioapic *vioapic, int irq);

static unsigned long ioapic_read_indirect(struct kvm_ioapic *ioapic,
					  unsigned long addr,
					  unsigned long length)
{
	unsigned long result = 0;

	switch (ioapic->ioregsel) {
	case IOAPIC_REG_VERSION:
		result = ((((IOAPIC_NUM_PINS - 1) & 0xff) << 16)
			  | (IOAPIC_VERSION_ID & 0xff));
		break;

	case IOAPIC_REG_APIC_ID:
	case IOAPIC_REG_ARB_ID:
		result = ((ioapic->id & 0xf) << 24);
		break;

	default:
		{
			u32 redir_index = (ioapic->ioregsel - 0x10) >> 1;
			u64 redir_content;

			if (redir_index < IOAPIC_NUM_PINS)
				redir_content =
					ioapic->redirtbl[redir_index].bits;
			else
				redir_content = ~0ULL;

			result = (ioapic->ioregsel & 0x1) ?
			    (redir_content >> 32) & 0xffffffff :
			    redir_content & 0xffffffff;
			break;
		}
	}

	return result;
}

static int ioapic_service(struct kvm_ioapic *ioapic, unsigned int idx)
{
	union kvm_ioapic_redirect_entry *pent;
	int injected = -1;

	pent = &ioapic->redirtbl[idx];

	if (!pent->fields.mask) {
		injected = ioapic_deliver(ioapic, idx);
		if (injected && pent->fields.trig_mode == IOAPIC_LEVEL_TRIG)
			pent->fields.remote_irr = 1;
	}

	return injected;
}

static void update_handled_vectors(struct kvm_ioapic *ioapic)
{
	DECLARE_BITMAP(handled_vectors, 256);
	int i;

	memset(handled_vectors, 0, sizeof(handled_vectors));
	for (i = 0; i < IOAPIC_NUM_PINS; ++i)
		__set_bit(ioapic->redirtbl[i].fields.vector, handled_vectors);
	memcpy(ioapic->handled_vectors, handled_vectors,
	       sizeof(handled_vectors));
	smp_wmb();
}

static void ioapic_write_indirect(struct kvm_ioapic *ioapic, u32 val)
{
	unsigned index;
	bool mask_before, mask_after;
	union kvm_ioapic_redirect_entry *e;

	switch (ioapic->ioregsel) {
	case IOAPIC_REG_VERSION:
		/* Writes are ignored. */
		break;

	case IOAPIC_REG_APIC_ID:
		ioapic->id = (val >> 24) & 0xf;
		break;

	case IOAPIC_REG_ARB_ID:
		break;

	default:
		index = (ioapic->ioregsel - 0x10) >> 1;

		ioapic_debug("change redir index %x val %x\n", index, val);
		if (index >= IOAPIC_NUM_PINS)
			return;
		e = &ioapic->redirtbl[index];
		mask_before = e->fields.mask;
		if (ioapic->ioregsel & 1) {
			e->bits &= 0xffffffff;
			e->bits |= (u64) val << 32;
		} else {
			e->bits &= ~0xffffffffULL;
			e->bits |= (u32) val;
			e->fields.remote_irr = 0;
		}
		update_handled_vectors(ioapic);
		mask_after = e->fields.mask;
		if (mask_before != mask_after)
			kvm_fire_mask_notifiers(ioapic->kvm, KVM_IRQCHIP_IOAPIC, index, mask_after);
		if (e->fields.trig_mode == IOAPIC_LEVEL_TRIG
		    && ioapic->irr & (1 << index))
			ioapic_service(ioapic, index);
		break;
	}
}

static int ioapic_deliver(struct kvm_ioapic *ioapic, int irq)
{
	union kvm_ioapic_redirect_entry *entry = &ioapic->redirtbl[irq];
	struct kvm_lapic_irq irqe;

	ioapic_debug("dest=%x dest_mode=%x delivery_mode=%x "
		     "vector=%x trig_mode=%x\n",
		     entry->fields.dest_id, entry->fields.dest_mode,
		     entry->fields.delivery_mode, entry->fields.vector,
		     entry->fields.trig_mode);

	irqe.dest_id = entry->fields.dest_id;
	irqe.vector = entry->fields.vector;
	irqe.dest_mode = entry->fields.dest_mode;
	irqe.trig_mode = entry->fields.trig_mode;
	irqe.delivery_mode = entry->fields.delivery_mode << 8;
	irqe.level = 1;
	irqe.shorthand = 0;

#ifdef CONFIG_X86
	/* Always delivery PIT interrupt to vcpu 0 */
	if (irq == 0) {
		irqe.dest_mode = 0; /* Physical mode. */
		/* need to read apic_id from apic regiest since
		 * it can be rewritten */
		irqe.dest_id = ioapic->kvm->bsp_vcpu_id;
	}
#endif
	return kvm_irq_delivery_to_apic(ioapic->kvm, NULL, &irqe);
}

int kvm_ioapic_set_irq(struct kvm_ioapic *ioapic, int irq, int level)
{
	u32 old_irr;
	u32 mask = 1 << irq;
	union kvm_ioapic_redirect_entry entry;
	int ret = 1;

	spin_lock(&ioapic->lock);
	old_irr = ioapic->irr;
	if (irq >= 0 && irq < IOAPIC_NUM_PINS) {
		entry = ioapic->redirtbl[irq];
		level ^= entry.fields.polarity;
		if (!level)
			ioapic->irr &= ~mask;
		else {
			int edge = (entry.fields.trig_mode == IOAPIC_EDGE_TRIG);
			ioapic->irr |= mask;
			if ((edge && old_irr != ioapic->irr) ||
			    (!edge && !entry.fields.remote_irr))
				ret = ioapic_service(ioapic, irq);
			else
				ret = 0; /* report coalesced interrupt */
		}
		trace_kvm_ioapic_set_irq(entry.bits, irq, ret == 0);
	}
	spin_unlock(&ioapic->lock);

	return ret;
}

static void __kvm_ioapic_update_eoi(struct kvm_ioapic *ioapic, int vector,
				     int trigger_mode)
{
	int i;

	for (i = 0; i < IOAPIC_NUM_PINS; i++) {
		union kvm_ioapic_redirect_entry *ent = &ioapic->redirtbl[i];

		if (ent->fields.vector != vector)
			continue;

		/*
		 * We are dropping lock while calling ack notifiers because ack
		 * notifier callbacks for assigned devices call into IOAPIC
		 * recursively. Since remote_irr is cleared only after call
		 * to notifiers if the same vector will be delivered while lock
		 * is dropped it will be put into irr and will be delivered
		 * after ack notifier returns.
		 */
		spin_unlock(&ioapic->lock);
		kvm_notify_acked_irq(ioapic->kvm, KVM_IRQCHIP_IOAPIC, i);
		spin_lock(&ioapic->lock);

		if (trigger_mode != IOAPIC_LEVEL_TRIG)
			continue;

		ASSERT(ent->fields.trig_mode == IOAPIC_LEVEL_TRIG);
		ent->fields.remote_irr = 0;
		if (!ent->fields.mask && (ioapic->irr & (1 << i)))
			ioapic_service(ioapic, i);
	}
}

void kvm_ioapic_update_eoi(struct kvm *kvm, int vector, int trigger_mode)
{
	struct kvm_ioapic *ioapic = kvm->arch.vioapic;

	smp_rmb();
	if (!test_bit(vector, ioapic->handled_vectors))
		return;
	spin_lock(&ioapic->lock);
	__kvm_ioapic_update_eoi(ioapic, vector, trigger_mode);
	spin_unlock(&ioapic->lock);
}

static inline struct kvm_ioapic *to_ioapic(struct kvm_io_device *dev)
{
	return container_of(dev, struct kvm_ioapic, dev);
}

static inline int ioapic_in_range(struct kvm_ioapic *ioapic, gpa_t addr)
{
	return ((addr >= ioapic->base_address &&
		 (addr < ioapic->base_address + IOAPIC_MEM_LENGTH)));
}

static int ioapic_mmio_read(struct kvm_io_device *this, gpa_t addr, int len,
			    void *val)
{
	struct kvm_ioapic *ioapic = to_ioapic(this);
	u32 result;
	if (!ioapic_in_range(ioapic, addr))
		return -EOPNOTSUPP;

	ioapic_debug("addr %lx\n", (unsigned long)addr);
	ASSERT(!(addr & 0xf));	/* check alignment */

	addr &= 0xff;
	spin_lock(&ioapic->lock);
	switch (addr) {
	case IOAPIC_REG_SELECT:
		result = ioapic->ioregsel;
		break;

	case IOAPIC_REG_WINDOW:
		result = ioapic_read_indirect(ioapic, addr, len);
		break;

	default:
		result = 0;
		break;
	}
	spin_unlock(&ioapic->lock);

	switch (len) {
	case 8:
		*(u64 *) val = result;
		break;
	case 1:
	case 2:
	case 4:
		memcpy(val, (char *)&result, len);
		break;
	default:
		printk(KERN_WARNING "ioapic: wrong length %d\n", len);
	}
	return 0;
}

static int ioapic_mmio_write(struct kvm_io_device *this, gpa_t addr, int len,
			     const void *val)
{
	struct kvm_ioapic *ioapic = to_ioapic(this);
	u32 data;
	if (!ioapic_in_range(ioapic, addr))
		return -EOPNOTSUPP;

	ioapic_debug("ioapic_mmio_write addr=%p len=%d val=%p\n",
		     (void*)addr, len, val);
	ASSERT(!(addr & 0xf));	/* check alignment */

	switch (len) {
	case 8:
	case 4:
		data = *(u32 *) val;
		break;
	case 2:
		data = *(u16 *) val;
		break;
	case 1:
		data = *(u8  *) val;
		break;
	default:
		printk(KERN_WARNING "ioapic: Unsupported size %d\n", len);
		return 0;
	}

	addr &= 0xff;
	spin_lock(&ioapic->lock);
	switch (addr) {
	case IOAPIC_REG_SELECT:
		ioapic->ioregsel = data & 0xFF; /* 8-bit register */
		break;

	case IOAPIC_REG_WINDOW:
		ioapic_write_indirect(ioapic, data);
		break;
#ifdef	CONFIG_IA64
	case IOAPIC_REG_EOI:
		__kvm_ioapic_update_eoi(ioapic, data, IOAPIC_LEVEL_TRIG);
		break;
#endif

	default:
		break;
	}
	spin_unlock(&ioapic->lock);
	return 0;
}

void kvm_ioapic_reset(struct kvm_ioapic *ioapic)
{
	int i;

	for (i = 0; i < IOAPIC_NUM_PINS; i++)
		ioapic->redirtbl[i].fields.mask = 1;
	ioapic->base_address = IOAPIC_DEFAULT_BASE_ADDRESS;
	ioapic->ioregsel = 0;
	ioapic->irr = 0;
	ioapic->id = 0;
	update_handled_vectors(ioapic);
}

static const struct kvm_io_device_ops ioapic_mmio_ops = {
	.read     = ioapic_mmio_read,
	.write    = ioapic_mmio_write,
};

int kvm_ioapic_init(struct kvm *kvm)
{
	struct kvm_ioapic *ioapic;
	int ret;

	ioapic = kzalloc(sizeof(struct kvm_ioapic), GFP_KERNEL);
	if (!ioapic)
		return -ENOMEM;
	spin_lock_init(&ioapic->lock);
	kvm->arch.vioapic = ioapic;
	kvm_ioapic_reset(ioapic);
	kvm_iodevice_init(&ioapic->dev, &ioapic_mmio_ops);
	ioapic->kvm = kvm;
	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, ioapic->base_address,
				      IOAPIC_MEM_LENGTH, &ioapic->dev);
	mutex_unlock(&kvm->slots_lock);
	if (ret < 0) {
		kvm->arch.vioapic = NULL;
		kfree(ioapic);
	}

	return ret;
}

void kvm_ioapic_destroy(struct kvm *kvm)
{
	struct kvm_ioapic *ioapic = kvm->arch.vioapic;

	if (ioapic) {
		kvm_io_bus_unregister_dev(kvm, KVM_MMIO_BUS, &ioapic->dev);
		kvm->arch.vioapic = NULL;
		kfree(ioapic);
	}
}

int kvm_get_ioapic(struct kvm *kvm, struct kvm_ioapic_state *state)
{
	struct kvm_ioapic *ioapic = ioapic_irqchip(kvm);
	if (!ioapic)
		return -EINVAL;

	spin_lock(&ioapic->lock);
	memcpy(state, ioapic, sizeof(struct kvm_ioapic_state));
	spin_unlock(&ioapic->lock);
	return 0;
}

int kvm_set_ioapic(struct kvm *kvm, struct kvm_ioapic_state *state)
{
	struct kvm_ioapic *ioapic = ioapic_irqchip(kvm);
	if (!ioapic)
		return -EINVAL;

	spin_lock(&ioapic->lock);
	memcpy(ioapic, state, sizeof(struct kvm_ioapic_state));
	update_handled_vectors(ioapic);
	spin_unlock(&ioapic->lock);
	return 0;
}
