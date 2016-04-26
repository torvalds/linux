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

static const struct vgic_register_region vgic_v2_dist_registers[] = {
	REGISTER_DESC_WITH_LENGTH(GIC_DIST_CTRL,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 12,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_IGROUP,
		vgic_mmio_read_rao, vgic_mmio_write_wi, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ENABLE_SET,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ENABLE_CLEAR,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_PENDING_SET,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_PENDING_CLEAR,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ACTIVE_SET,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ACTIVE_CLEAR,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_PRI,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 8,
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
