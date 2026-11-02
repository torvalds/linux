/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_KVM_UTIL_ARCH_H
#define SELFTEST_KVM_UTIL_ARCH_H

#include <stdint.h>

#include "kvm_util_types.h"

struct kvm_mmu_arch {};

/* Page table fragment cache for guest page tables < page size */
struct vm_pt_frag_cache {
	gpa_t page;
	size_t page_nr_used;
};

struct kvm_vm_arch {
	gpa_t prtb; /* process table */
	struct vm_pt_frag_cache pt_frag_cache[2]; /* 256B and 4KB PT caches */
};

#endif  /* SELFTEST_KVM_UTIL_ARCH_H */
