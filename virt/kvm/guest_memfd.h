/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_GUEST_MEMFD_H__
#define __KVM_GUEST_MEMFD_H__

#include <linux/kvm_types.h>

#ifdef CONFIG_KVM_GUEST_MEMFD
int kvm_gmem_init(struct module *module);
void kvm_gmem_exit(void);
int kvm_gmem_create(struct kvm *kvm, struct kvm_create_guest_memfd *args);
int kvm_gmem_bind(struct kvm *kvm, struct kvm_memory_slot *slot,
		  unsigned int fd, uoff_t offset);
void kvm_gmem_unbind(struct kvm_memory_slot *slot);
#else
static inline int kvm_gmem_init(struct module *module)
{
	return 0;
}
static inline void kvm_gmem_exit(void) {};
static inline int kvm_gmem_bind(struct kvm *kvm,
					 struct kvm_memory_slot *slot,
					 unsigned int fd, uoff_t offset)
{
	WARN_ON_ONCE(1);
	return -EIO;
}

static inline void kvm_gmem_unbind(struct kvm_memory_slot *slot)
{
	WARN_ON_ONCE(1);
}
#endif /* CONFIG_KVM_GUEST_MEMFD */

#endif /* __KVM_GUEST_MEMFD_H__ */
