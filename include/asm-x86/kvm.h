#ifndef __LINUX_KVM_X86_H
#define __LINUX_KVM_X86_H

/*
 * KVM x86 specific structures and definitions
 *
 */

#include <asm/types.h>
#include <linux/ioctl.h>

struct kvm_memory_alias {
	__u32 slot;  /* this has a different namespace than memory slots */
	__u32 flags;
	__u64 guest_phys_addr;
	__u64 memory_size;
	__u64 target_phys_addr;
};

#endif
