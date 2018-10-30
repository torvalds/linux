// SPDX-License-Identifier: GPL-2.0
/*
 * KVM coalesced MMIO
 *
 * Copyright (c) 2008 Bull S.A.S.
 * Copyright 2009 Red Hat, Inc. and/or its affiliates.
 *
 *  Author: Laurent Vivier <Laurent.Vivier@bull.net>
 *
 */

#include <kvm/iodev.h>

#include <linux/kvm_host.h>
#include <linux/slab.h>
#include <linux/kvm.h>

#include "coalesced_mmio.h"

static inline struct kvm_coalesced_mmio_dev *to_mmio(struct kvm_io_device *dev)
{
	return container_of(dev, struct kvm_coalesced_mmio_dev, dev);
}

static int coalesced_mmio_in_range(struct kvm_coalesced_mmio_dev *dev,
				   gpa_t addr, int len)
{
	/* is it in a batchable area ?
	 * (addr,len) is fully included in
	 * (zone->addr, zone->size)
	 */
	if (len < 0)
		return 0;
	if (addr + len < addr)
		return 0;
	if (addr < dev->zone.addr)
		return 0;
	if (addr + len > dev->zone.addr + dev->zone.size)
		return 0;
	return 1;
}

static int coalesced_mmio_has_room(struct kvm_coalesced_mmio_dev *dev)
{
	struct kvm_coalesced_mmio_ring *ring;
	unsigned avail;

	/* Are we able to batch it ? */

	/* last is the first free entry
	 * check if we don't meet the first used entry
	 * there is always one unused entry in the buffer
	 */
	ring = dev->kvm->coalesced_mmio_ring;
	avail = (ring->first - ring->last - 1) % KVM_COALESCED_MMIO_MAX;
	if (avail == 0) {
		/* full */
		return 0;
	}

	return 1;
}

static int coalesced_mmio_write(struct kvm_vcpu *vcpu,
				struct kvm_io_device *this, gpa_t addr,
				int len, const void *val)
{
	struct kvm_coalesced_mmio_dev *dev = to_mmio(this);
	struct kvm_coalesced_mmio_ring *ring = dev->kvm->coalesced_mmio_ring;

	if (!coalesced_mmio_in_range(dev, addr, len))
		return -EOPNOTSUPP;

	spin_lock(&dev->kvm->ring_lock);

	if (!coalesced_mmio_has_room(dev)) {
		spin_unlock(&dev->kvm->ring_lock);
		return -EOPNOTSUPP;
	}

	/* copy data in first free entry of the ring */

	ring->coalesced_mmio[ring->last].phys_addr = addr;
	ring->coalesced_mmio[ring->last].len = len;
	memcpy(ring->coalesced_mmio[ring->last].data, val, len);
	ring->coalesced_mmio[ring->last].pio = dev->zone.pio;
	smp_wmb();
	ring->last = (ring->last + 1) % KVM_COALESCED_MMIO_MAX;
	spin_unlock(&dev->kvm->ring_lock);
	return 0;
}

static void coalesced_mmio_destructor(struct kvm_io_device *this)
{
	struct kvm_coalesced_mmio_dev *dev = to_mmio(this);

	list_del(&dev->list);

	kfree(dev);
}

static const struct kvm_io_device_ops coalesced_mmio_ops = {
	.write      = coalesced_mmio_write,
	.destructor = coalesced_mmio_destructor,
};

int kvm_coalesced_mmio_init(struct kvm *kvm)
{
	struct page *page;
	int ret;

	ret = -ENOMEM;
	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page)
		goto out_err;

	ret = 0;
	kvm->coalesced_mmio_ring = page_address(page);

	/*
	 * We're using this spinlock to sync access to the coalesced ring.
	 * The list doesn't need it's own lock since device registration and
	 * unregistration should only happen when kvm->slots_lock is held.
	 */
	spin_lock_init(&kvm->ring_lock);
	INIT_LIST_HEAD(&kvm->coalesced_zones);

out_err:
	return ret;
}

void kvm_coalesced_mmio_free(struct kvm *kvm)
{
	if (kvm->coalesced_mmio_ring)
		free_page((unsigned long)kvm->coalesced_mmio_ring);
}

int kvm_vm_ioctl_register_coalesced_mmio(struct kvm *kvm,
					 struct kvm_coalesced_mmio_zone *zone)
{
	int ret;
	struct kvm_coalesced_mmio_dev *dev;

	if (zone->pio != 1 && zone->pio != 0)
		return -EINVAL;

	dev = kzalloc(sizeof(struct kvm_coalesced_mmio_dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	kvm_iodevice_init(&dev->dev, &coalesced_mmio_ops);
	dev->kvm = kvm;
	dev->zone = *zone;

	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm,
				zone->pio ? KVM_PIO_BUS : KVM_MMIO_BUS,
				zone->addr, zone->size, &dev->dev);
	if (ret < 0)
		goto out_free_dev;
	list_add_tail(&dev->list, &kvm->coalesced_zones);
	mutex_unlock(&kvm->slots_lock);

	return 0;

out_free_dev:
	mutex_unlock(&kvm->slots_lock);
	kfree(dev);

	return ret;
}

int kvm_vm_ioctl_unregister_coalesced_mmio(struct kvm *kvm,
					   struct kvm_coalesced_mmio_zone *zone)
{
	struct kvm_coalesced_mmio_dev *dev, *tmp;

	mutex_lock(&kvm->slots_lock);

	list_for_each_entry_safe(dev, tmp, &kvm->coalesced_zones, list)
		if (coalesced_mmio_in_range(dev, zone->addr, zone->size)) {
			kvm_io_bus_unregister_dev(kvm,
				zone->pio ? KVM_PIO_BUS : KVM_MMIO_BUS, &dev->dev);
			kvm_iodevice_destructor(&dev->dev);
		}

	mutex_unlock(&kvm->slots_lock);

	return 0;
}
