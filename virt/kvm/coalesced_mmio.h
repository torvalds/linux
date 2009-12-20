#ifndef __KVM_COALESCED_MMIO_H__
#define __KVM_COALESCED_MMIO_H__

/*
 * KVM coalesced MMIO
 *
 * Copyright (c) 2008 Bull S.A.S.
 *
 *  Author: Laurent Vivier <Laurent.Vivier@bull.net>
 *
 */

#define KVM_COALESCED_MMIO_ZONE_MAX 100

struct kvm_coalesced_mmio_dev {
	struct kvm_io_device dev;
	struct kvm *kvm;
	spinlock_t lock;
	int nb_zones;
	struct kvm_coalesced_mmio_zone zone[KVM_COALESCED_MMIO_ZONE_MAX];
};

int kvm_coalesced_mmio_init(struct kvm *kvm);
int kvm_vm_ioctl_register_coalesced_mmio(struct kvm *kvm,
                                       struct kvm_coalesced_mmio_zone *zone);
int kvm_vm_ioctl_unregister_coalesced_mmio(struct kvm *kvm,
                                         struct kvm_coalesced_mmio_zone *zone);

#endif
