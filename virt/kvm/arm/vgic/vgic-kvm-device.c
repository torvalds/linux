/*
 * VGIC: KVM DEVICE API
 *
 * Copyright (C) 2015 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
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
#include <linux/kvm_host.h>
#include <kvm/arm_vgic.h>
#include <linux/uaccess.h>
#include <asm/kvm_mmu.h>
#include "vgic.h"

/* common helpers */

static int vgic_check_ioaddr(struct kvm *kvm, phys_addr_t *ioaddr,
			     phys_addr_t addr, phys_addr_t alignment)
{
	if (addr & ~KVM_PHYS_MASK)
		return -E2BIG;

	if (!IS_ALIGNED(addr, alignment))
		return -EINVAL;

	if (!IS_VGIC_ADDR_UNDEF(*ioaddr))
		return -EEXIST;

	return 0;
}

/**
 * kvm_vgic_addr - set or get vgic VM base addresses
 * @kvm:   pointer to the vm struct
 * @type:  the VGIC addr type, one of KVM_VGIC_V[23]_ADDR_TYPE_XXX
 * @addr:  pointer to address value
 * @write: if true set the address in the VM address space, if false read the
 *          address
 *
 * Set or get the vgic base addresses for the distributor and the virtual CPU
 * interface in the VM physical address space.  These addresses are properties
 * of the emulated core/SoC and therefore user space initially knows this
 * information.
 * Check them for sanity (alignment, double assignment). We can't check for
 * overlapping regions in case of a virtual GICv3 here, since we don't know
 * the number of VCPUs yet, so we defer this check to map_resources().
 */
int kvm_vgic_addr(struct kvm *kvm, unsigned long type, u64 *addr, bool write)
{
	int r = 0;
	struct vgic_dist *vgic = &kvm->arch.vgic;
	int type_needed;
	phys_addr_t *addr_ptr, alignment;

	mutex_lock(&kvm->lock);
	switch (type) {
	case KVM_VGIC_V2_ADDR_TYPE_DIST:
		type_needed = KVM_DEV_TYPE_ARM_VGIC_V2;
		addr_ptr = &vgic->vgic_dist_base;
		alignment = SZ_4K;
		break;
	case KVM_VGIC_V2_ADDR_TYPE_CPU:
		type_needed = KVM_DEV_TYPE_ARM_VGIC_V2;
		addr_ptr = &vgic->vgic_cpu_base;
		alignment = SZ_4K;
		break;
#ifdef CONFIG_KVM_ARM_VGIC_V3
	case KVM_VGIC_V3_ADDR_TYPE_DIST:
		type_needed = KVM_DEV_TYPE_ARM_VGIC_V3;
		addr_ptr = &vgic->vgic_dist_base;
		alignment = SZ_64K;
		break;
	case KVM_VGIC_V3_ADDR_TYPE_REDIST:
		type_needed = KVM_DEV_TYPE_ARM_VGIC_V3;
		addr_ptr = &vgic->vgic_redist_base;
		alignment = SZ_64K;
		break;
#endif
	default:
		r = -ENODEV;
		goto out;
	}

	if (vgic->vgic_model != type_needed) {
		r = -ENODEV;
		goto out;
	}

	if (write) {
		r = vgic_check_ioaddr(kvm, addr_ptr, *addr, alignment);
		if (!r)
			*addr_ptr = *addr;
	} else {
		*addr = *addr_ptr;
	}

out:
	mutex_unlock(&kvm->lock);
	return r;
}

static int vgic_set_common_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	int r;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR: {
		u64 __user *uaddr = (u64 __user *)(long)attr->addr;
		u64 addr;
		unsigned long type = (unsigned long)attr->attr;

		if (copy_from_user(&addr, uaddr, sizeof(addr)))
			return -EFAULT;

		r = kvm_vgic_addr(dev->kvm, type, &addr, true);
		return (r == -ENODEV) ? -ENXIO : r;
	}
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;
		u32 val;
		int ret = 0;

		if (get_user(val, uaddr))
			return -EFAULT;

		/*
		 * We require:
		 * - at least 32 SPIs on top of the 16 SGIs and 16 PPIs
		 * - at most 1024 interrupts
		 * - a multiple of 32 interrupts
		 */
		if (val < (VGIC_NR_PRIVATE_IRQS + 32) ||
		    val > VGIC_MAX_RESERVED ||
		    (val & 31))
			return -EINVAL;

		mutex_lock(&dev->kvm->lock);

		if (vgic_ready(dev->kvm) || dev->kvm->arch.vgic.nr_spis)
			ret = -EBUSY;
		else
			dev->kvm->arch.vgic.nr_spis =
				val - VGIC_NR_PRIVATE_IRQS;

		mutex_unlock(&dev->kvm->lock);

		return ret;
	}
	case KVM_DEV_ARM_VGIC_GRP_CTRL: {
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			mutex_lock(&dev->kvm->lock);
			r = vgic_init(dev->kvm);
			mutex_unlock(&dev->kvm->lock);
			return r;
		}
		break;
	}
	}

	return -ENXIO;
}

static int vgic_get_common_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	int r = -ENXIO;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR: {
		u64 __user *uaddr = (u64 __user *)(long)attr->addr;
		u64 addr;
		unsigned long type = (unsigned long)attr->attr;

		r = kvm_vgic_addr(dev->kvm, type, &addr, false);
		if (r)
			return (r == -ENODEV) ? -ENXIO : r;

		if (copy_to_user(uaddr, &addr, sizeof(addr)))
			return -EFAULT;
		break;
	}
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;

		r = put_user(dev->kvm->arch.vgic.nr_spis +
			     VGIC_NR_PRIVATE_IRQS, uaddr);
		break;
	}
	}

	return r;
}

static int vgic_create(struct kvm_device *dev, u32 type)
{
	return kvm_vgic_create(dev->kvm, type);
}

static void vgic_destroy(struct kvm_device *dev)
{
	kfree(dev);
}

void kvm_register_vgic_device(unsigned long type)
{
	switch (type) {
	case KVM_DEV_TYPE_ARM_VGIC_V2:
		kvm_register_device_ops(&kvm_arm_vgic_v2_ops,
					KVM_DEV_TYPE_ARM_VGIC_V2);
		break;
#ifdef CONFIG_KVM_ARM_VGIC_V3
	case KVM_DEV_TYPE_ARM_VGIC_V3:
		kvm_register_device_ops(&kvm_arm_vgic_v3_ops,
					KVM_DEV_TYPE_ARM_VGIC_V3);
		break;
#endif
	}
}

/* V2 ops */

static int vgic_v2_set_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	int ret;

	ret = vgic_set_common_attr(dev, attr);
	return ret;

}

static int vgic_v2_get_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	int ret;

	ret = vgic_get_common_attr(dev, attr);
	return ret;
}

static int vgic_v2_has_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR:
		switch (attr->attr) {
		case KVM_VGIC_V2_ADDR_TYPE_DIST:
		case KVM_VGIC_V2_ADDR_TYPE_CPU:
			return 0;
		}
		break;
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS:
		return 0;
	case KVM_DEV_ARM_VGIC_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			return 0;
		}
	}
	return -ENXIO;
}

struct kvm_device_ops kvm_arm_vgic_v2_ops = {
	.name = "kvm-arm-vgic-v2",
	.create = vgic_create,
	.destroy = vgic_destroy,
	.set_attr = vgic_v2_set_attr,
	.get_attr = vgic_v2_get_attr,
	.has_attr = vgic_v2_has_attr,
};

/* V3 ops */

#ifdef CONFIG_KVM_ARM_VGIC_V3

static int vgic_v3_set_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	return vgic_set_common_attr(dev, attr);
}

static int vgic_v3_get_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	return vgic_get_common_attr(dev, attr);
}

static int vgic_v3_has_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR:
		switch (attr->attr) {
		case KVM_VGIC_V3_ADDR_TYPE_DIST:
		case KVM_VGIC_V3_ADDR_TYPE_REDIST:
			return 0;
		}
		break;
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS:
		return 0;
	case KVM_DEV_ARM_VGIC_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			return 0;
		}
	}
	return -ENXIO;
}

struct kvm_device_ops kvm_arm_vgic_v3_ops = {
	.name = "kvm-arm-vgic-v3",
	.create = vgic_create,
	.destroy = vgic_destroy,
	.set_attr = vgic_v3_set_attr,
	.get_attr = vgic_v3_get_attr,
	.has_attr = vgic_v3_has_attr,
};

#endif /* CONFIG_KVM_ARM_VGIC_V3 */

