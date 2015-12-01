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
		vgic_mmio_read_raz, vgic_mmio_write_wi, 8,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_CONFIG,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 2,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GIC_DIST_SOFTINT,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GIC_DIST_SGI_PENDING_CLEAR,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 16,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
	REGISTER_DESC_WITH_LENGTH(GIC_DIST_SGI_PENDING_SET,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 16,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
};

unsigned int vgic_v2_init_dist_iodev(struct vgic_io_device *dev)
{
	dev->regions = vgic_v2_dist_registers;
	dev->nr_regions = ARRAY_SIZE(vgic_v2_dist_registers);

	kvm_iodevice_init(&dev->dev, &kvm_io_gic_ops);

	return SZ_4K;
}
