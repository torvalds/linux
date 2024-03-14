/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_KVM_UTIL_ARCH_H
#define SELFTEST_KVM_UTIL_ARCH_H

#include <stdbool.h>
#include <stdint.h>

#include "kvm_util_types.h"
#include "test_util.h"

extern bool is_forced_emulation_enabled;

struct kvm_vm_arch {
	vm_vaddr_t gdt;
	vm_vaddr_t tss;
	vm_vaddr_t idt;

	uint64_t c_bit;
	uint64_t s_bit;
	int sev_fd;
	bool is_pt_protected;
};

static inline bool __vm_arch_has_protected_memory(struct kvm_vm_arch *arch)
{
	return arch->c_bit || arch->s_bit;
}

#define vm_arch_has_protected_memory(vm) \
	__vm_arch_has_protected_memory(&(vm)->arch)

#define vcpu_arch_put_guest(mem, __val)							\
do {											\
	const typeof(mem) val = (__val);						\
											\
	if (!is_forced_emulation_enabled || guest_random_bool(&guest_rng)) {		\
		(mem) = val;								\
	} else if (guest_random_bool(&guest_rng)) {					\
		__asm__ __volatile__(KVM_FEP "mov %1, %0"				\
				     : "+m" (mem)					\
				     : "r" (val) : "memory");				\
	} else {									\
		uint64_t __old = READ_ONCE(mem);					\
											\
		__asm__ __volatile__(KVM_FEP LOCK_PREFIX "cmpxchg %[new], %[ptr]"	\
				     : [ptr] "+m" (mem), [old] "+a" (__old)		\
				     : [new]"r" (val) : "memory", "cc");		\
	}										\
} while (0)

#endif  // SELFTEST_KVM_UTIL_ARCH_H
