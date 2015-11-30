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

/* common helpers */

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
	return -ENXIO;
}

static int vgic_v2_get_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	return -ENXIO;
}

static int vgic_v2_has_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
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
	return -ENXIO;
}

static int vgic_v3_get_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	return -ENXIO;
}

static int vgic_v3_has_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
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

