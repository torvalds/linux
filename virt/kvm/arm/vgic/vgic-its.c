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
#include <linux/uaccess.h>

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

	if (its->initialized)
		return 0;

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

	if (!ret)
		its->initialized = true;

	return ret;
}

static int vgic_its_create(struct kvm_device *dev, u32 type)
{
	struct vgic_its *its;

	if (type != KVM_DEV_TYPE_ARM_VGIC_ITS)
		return -ENODEV;

	its = kzalloc(sizeof(struct vgic_its), GFP_KERNEL);
	if (!its)
		return -ENOMEM;

	its->vgic_its_base = VGIC_ADDR_UNDEF;

	dev->kvm->arch.vgic.has_its = true;
	its->initialized = false;
	its->enabled = false;

	dev->private = its;

	return 0;
}

static void vgic_its_destroy(struct kvm_device *kvm_dev)
{
	struct vgic_its *its = kvm_dev->private;

	kfree(its);
}

static int vgic_its_has_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR:
		switch (attr->attr) {
		case KVM_VGIC_ITS_ADDR_TYPE:
			return 0;
		}
		break;
	case KVM_DEV_ARM_VGIC_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			return 0;
		}
		break;
	}
	return -ENXIO;
}

static int vgic_its_set_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	struct vgic_its *its = dev->private;
	int ret;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR: {
		u64 __user *uaddr = (u64 __user *)(long)attr->addr;
		unsigned long type = (unsigned long)attr->attr;
		u64 addr;

		if (type != KVM_VGIC_ITS_ADDR_TYPE)
			return -ENODEV;

		if (its->initialized)
			return -EBUSY;

		if (copy_from_user(&addr, uaddr, sizeof(addr)))
			return -EFAULT;

		ret = vgic_check_ioaddr(dev->kvm, &its->vgic_its_base,
					addr, SZ_64K);
		if (ret)
			return ret;

		its->vgic_its_base = addr;

		return 0;
	}
	case KVM_DEV_ARM_VGIC_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			return vgic_its_init_its(dev->kvm, its);
		}
		break;
	}
	return -ENXIO;
}

static int vgic_its_get_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR: {
		struct vgic_its *its = dev->private;
		u64 addr = its->vgic_its_base;
		u64 __user *uaddr = (u64 __user *)(long)attr->addr;
		unsigned long type = (unsigned long)attr->attr;

		if (type != KVM_VGIC_ITS_ADDR_TYPE)
			return -ENODEV;

		if (copy_to_user(uaddr, &addr, sizeof(addr)))
			return -EFAULT;
		break;
	default:
		return -ENXIO;
	}
	}

	return 0;
}

static struct kvm_device_ops kvm_arm_vgic_its_ops = {
	.name = "kvm-arm-vgic-its",
	.create = vgic_its_create,
	.destroy = vgic_its_destroy,
	.set_attr = vgic_its_set_attr,
	.get_attr = vgic_its_get_attr,
	.has_attr = vgic_its_has_attr,
};

int kvm_vgic_register_its_device(void)
{
	return kvm_register_device_ops(&kvm_arm_vgic_its_ops,
				       KVM_DEV_TYPE_ARM_VGIC_ITS);
}
