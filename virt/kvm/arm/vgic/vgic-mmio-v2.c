/*
 * VGICv2 MMIO handling functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/irqchip/arm-gic.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <kvm/iodev.h>
#include <kvm/arm_vgic.h>

#include "vgic.h"
#include "vgic-mmio.h"

static unsigned long vgic_mmio_read_v2_misc(struct kvm_vcpu *vcpu,
					    gpa_t addr, unsigned int len)
{
	u32 value;

	switch (addr & 0x0c) {
	case GIC_DIST_CTRL:
		value = vcpu->kvm->arch.vgic.enabled ? GICD_ENABLE : 0;
		break;
	case GIC_DIST_CTR:
		value = vcpu->kvm->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS;
		value = (value >> 5) - 1;
		value |= (atomic_read(&vcpu->kvm->online_vcpus) - 1) << 5;
		break;
	case GIC_DIST_IIDR:
		value = (PRODUCT_ID_KVM << 24) | (IMPLEMENTER_ARM << 0);
		break;
	default:
		return 0;
	}

	return value;
}

static void vgic_mmio_write_v2_misc(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len,
				    unsigned long val)
{
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;
	bool was_enabled = dist->enabled;

	switch (addr & 0x0c) {
	case GIC_DIST_CTRL:
		dist->enabled = val & GICD_ENABLE;
		if (!was_enabled && dist->enabled)
			vgic_kick_vcpus(vcpu->kvm);
		break;
	case GIC_DIST_CTR:
	case GIC_DIST_IIDR:
		/* Nothing to do */
		return;
	}
}

static void vgic_mmio_write_sgir(struct kvm_vcpu *source_vcpu,
				 gpa_t addr, unsigned int len,
				 unsigned long val)
{
	int nr_vcpus = atomic_read(&source_vcpu->kvm->online_vcpus);
	int intid = val & 0xf;
	int targets = (val >> 16) & 0xff;
	int mode = (val >> 24) & 0x03;
	int c;
	struct kvm_vcpu *vcpu;

	switch (mode) {
	case 0x0:		/* as specified by targets */
		break;
	case 0x1:
		targets = (1U << nr_vcpus) - 1;			/* all, ... */
		targets &= ~(1U << source_vcpu->vcpu_id);	/* but self */
		break;
	case 0x2:		/* this very vCPU only */
		targets = (1U << source_vcpu->vcpu_id);
		break;
	case 0x3:		/* reserved */
		return;
	}

	kvm_for_each_vcpu(c, vcpu, source_vcpu->kvm) {
		struct vgic_irq *irq;

		if (!(targets & (1U << c)))
			continue;

		irq = vgic_get_irq(source_vcpu->kvm, vcpu, intid);

		spin_lock(&irq->irq_lock);
		irq->pending = true;
		irq->source |= 1U << source_vcpu->vcpu_id;

		vgic_queue_irq_unlock(source_vcpu->kvm, irq);
	}
}

static unsigned long vgic_mmio_read_target(struct kvm_vcpu *vcpu,
					   gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 8);
	int i;
	u64 val = 0;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		val |= (u64)irq->targets << (i * 8);
	}

	return val;
}

static void vgic_mmio_write_target(struct kvm_vcpu *vcpu,
				   gpa_t addr, unsigned int len,
				   unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 8);
	int i;

	/* GICD_ITARGETSR[0-7] are read-only */
	if (intid < VGIC_NR_PRIVATE_IRQS)
		return;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, NULL, intid + i);
		int target;

		spin_lock(&irq->irq_lock);

		irq->targets = (val >> (i * 8)) & 0xff;
		target = irq->targets ? __ffs(irq->targets) : 0;
		irq->target_vcpu = kvm_get_vcpu(vcpu->kvm, target);

		spin_unlock(&irq->irq_lock);
	}
}

static unsigned long vgic_mmio_read_sgipend(struct kvm_vcpu *vcpu,
					    gpa_t addr, unsigned int len)
{
	u32 intid = addr & 0x0f;
	int i;
	u64 val = 0;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		val |= (u64)irq->source << (i * 8);
	}
	return val;
}

static void vgic_mmio_write_sgipendc(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	u32 intid = addr & 0x0f;
	int i;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		spin_lock(&irq->irq_lock);

		irq->source &= ~((val >> (i * 8)) & 0xff);
		if (!irq->source)
			irq->pending = false;

		spin_unlock(&irq->irq_lock);
	}
}

static void vgic_mmio_write_sgipends(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	u32 intid = addr & 0x0f;
	int i;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		spin_lock(&irq->irq_lock);

		irq->source |= (val >> (i * 8)) & 0xff;

		if (irq->source) {
			irq->pending = true;
			vgic_queue_irq_unlock(vcpu->kvm, irq);
		} else {
			spin_unlock(&irq->irq_lock);
		}
	}
}

static const struct vgic_register_region vgic_v2_dist_registers[] = {
	REGISTER_DESC_WITH_LENGTH(GIC_DIST_CTRL,
		vgic_mmio_read_v2_misc, vgic_mmio_write_v2_misc, 12,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_IGROUP,
		vgic_mmio_read_rao, vgic_mmio_write_wi, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ENABLE_SET,
		vgic_mmio_read_enable, vgic_mmio_write_senable, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ENABLE_CLEAR,
		vgic_mmio_read_enable, vgic_mmio_write_cenable, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_PENDING_SET,
		vgic_mmio_read_pending, vgic_mmio_write_spending, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_PENDING_CLEAR,
		vgic_mmio_read_pending, vgic_mmio_write_cpending, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ACTIVE_SET,
		vgic_mmio_read_active, vgic_mmio_write_sactive, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ACTIVE_CLEAR,
		vgic_mmio_read_active, vgic_mmio_write_cactive, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_PRI,
		vgic_mmio_read_priority, vgic_mmio_write_priority, 8,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_TARGET,
		vgic_mmio_read_target, vgic_mmio_write_target, 8,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_CONFIG,
		vgic_mmio_read_config, vgic_mmio_write_config, 2,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GIC_DIST_SOFTINT,
		vgic_mmio_read_raz, vgic_mmio_write_sgir, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GIC_DIST_SGI_PENDING_CLEAR,
		vgic_mmio_read_sgipend, vgic_mmio_write_sgipendc, 16,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
	REGISTER_DESC_WITH_LENGTH(GIC_DIST_SGI_PENDING_SET,
		vgic_mmio_read_sgipend, vgic_mmio_write_sgipends, 16,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
};

unsigned int vgic_v2_init_dist_iodev(struct vgic_io_device *dev)
{
	dev->regions = vgic_v2_dist_registers;
	dev->nr_regions = ARRAY_SIZE(vgic_v2_dist_registers);

	kvm_iodevice_init(&dev->dev, &kvm_io_gic_ops);

	return SZ_4K;
}

int vgic_v2_has_attr_regs(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	int nr_irqs = dev->kvm->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS;
	const struct vgic_register_region *regions;
	gpa_t addr;
	int nr_regions, i, len;

	addr = attr->attr & KVM_DEV_ARM_VGIC_OFFSET_MASK;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
		regions = vgic_v2_dist_registers;
		nr_regions = ARRAY_SIZE(vgic_v2_dist_registers);
		break;
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS:
		return -ENXIO;		/* TODO: describe CPU i/f regs also */
	default:
		return -ENXIO;
	}

	/* We only support aligned 32-bit accesses. */
	if (addr & 3)
		return -ENXIO;

	for (i = 0; i < nr_regions; i++) {
		if (regions[i].bits_per_irq)
			len = (regions[i].bits_per_irq * nr_irqs) / 8;
		else
			len = regions[i].len;

		if (regions[i].reg_offset <= addr &&
		    regions[i].reg_offset + len > addr)
			return 0;
	}

	return -ENXIO;
}
