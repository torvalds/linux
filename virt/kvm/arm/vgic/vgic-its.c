/*
 * GICv3 ITS emulation
 *
 * Copyright (C) 2015,2016 ARM Ltd.
 * Author: Andre Przywara <andre.przywara@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/cpu.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/interrupt.h>

#include <linux/irqchip/arm-gic-v3.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>

#include "vgic.h"
#include "vgic-mmio.h"

#define REGISTER_ITS_DESC(off, rd, wr, length, acc)		\
{								\
	.reg_offset = off,					\
	.len = length,						\
	.access_flags = acc,					\
	.its_read = rd,						\
	.its_write = wr,					\
}

static unsigned long its_mmio_read_raz(struct kvm *kvm, struct vgic_its *its,
				       gpa_t addr, unsigned int len)
{
	return 0;
}

static void its_mmio_write_wi(struct kvm *kvm, struct vgic_its *its,
			      gpa_t addr, unsigned int len, unsigned long val)
{
	/* Ignore */
}

static struct vgic_register_region its_registers[] = {
	REGISTER_ITS_DESC(GITS_CTLR,
		its_mmio_read_raz, its_mmio_write_wi, 4,
		VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_IIDR,
		its_mmio_read_raz, its_mmio_write_wi, 4,
		VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_TYPER,
		its_mmio_read_raz, its_mmio_write_wi, 8,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_CBASER,
		its_mmio_read_raz, its_mmio_write_wi, 8,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_CWRITER,
		its_mmio_read_raz, its_mmio_write_wi, 8,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_CREADR,
		its_mmio_read_raz, its_mmio_write_wi, 8,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_BASER,
		its_mmio_read_raz, its_mmio_write_wi, 0x40,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_IDREGS_BASE,
		its_mmio_read_raz, its_mmio_write_wi, 0x30,
		VGIC_ACCESS_32bit),
};

static int vgic_its_init_its(struct kvm *kvm, struct vgic_its *its)
{
	struct vgic_io_device *iodev = &its->iodev;
	int ret;

	if (IS_VGIC_ADDR_UNDEF(its->vgic_its_base))
		return -ENXIO;

	iodev->regions = its_registers;
	iodev->nr_regions = ARRAY_SIZE(its_registers);
	kvm_iodevice_init(&iodev->dev, &kvm_io_gic_ops);

	iodev->base_addr = its->vgic_its_base;
	iodev->iodev_type = IODEV_ITS;
	iodev->its = its;
	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, iodev->base_addr,
				      KVM_VGIC_V3_ITS_SIZE, &iodev->dev);
	mutex_unlock(&kvm->slots_lock);

	return ret;
}
