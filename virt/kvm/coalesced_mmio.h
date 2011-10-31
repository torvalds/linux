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

#ifdef CONFIG_KVM_MMIO

#include <linux/list.h>

struct kvm_coalesced_mmio_dev {
	struct list_head list;
	struct kvm_io_device dev;
	struct kvm *kvm;
	struct kvm_coalesced_mmio_zone zone;
};

int kvm_coalesced_mmio_init(struct kvm *kvm);
void kvm_coalesced_mmio_free(struct kvm *kvm);
int kvm_vm_ioctl_register_coalesced_mmio(struct kvm *kvm,
                                       struct kvm_coalesced_mmio_zone *zone);
int kvm_vm_ioctl_unregister_coalesced_mmio(struct kvm *kvm,
                                         struct kvm_coalesced_mmio_zone *zone);

#else

static inline int kvm_coalesced_mmio_init(struct kvm *kvm) { return 0; }
static inline void kvm_coalesced_mmio_free(struct kvm *kvm) { }

#endif

#endif
