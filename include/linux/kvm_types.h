/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __KVM_TYPES_H__
#define __KVM_TYPES_H__

#include <linux/bits.h>
#include <linux/export.h>
#include <linux/types.h>
#include <asm/kvm_types.h>

#ifdef KVM_SUB_MODULES
#define EXPORT_SYMBOL_FOR_KVM_INTERNAL(symbol) \
	EXPORT_SYMBOL_FOR_MODULES(symbol, __stringify(KVM_SUB_MODULES))
#else
#define EXPORT_SYMBOL_FOR_KVM_INTERNAL(symbol)
#endif

#ifndef __ASSEMBLER__

#include <linux/mutex.h>
#include <linux/spinlock_types.h>

struct kvm;
struct kvm_async_pf;
struct kvm_device_ops;
struct kvm_gfn_range;
struct kvm_interrupt;
struct kvm_irq_routing_table;
struct kvm_memory_slot;
struct kvm_one_reg;
struct kvm_run;
struct kvm_userspace_memory_region;
struct kvm_vcpu;
struct kvm_vcpu_init;
struct kvm_memslots;

enum kvm_mr_change;

/*
 * Address types:
 *
 *  gva - guest virtual address
 *  gpa - guest physical address
 *  gfn - guest frame number
 *  hva - host virtual address
 *  hpa - host physical address
 *  hfn - host frame number
 */

typedef unsigned long  gva_t;
typedef u64            gpa_t;
typedef u64            gfn_t;

#define INVALID_GPA	(~(gpa_t)0)

typedef unsigned long  hva_t;
typedef u64            hpa_t;
typedef u64            hfn_t;

typedef hfn_t kvm_pfn_t;

struct gfn_to_hva_cache {
	u64 generation;
	gpa_t gpa;
	unsigned long hva;
	unsigned long len;
	struct kvm_memory_slot *memslot;
};

struct gfn_to_pfn_cache {
	u64 generation;
	gpa_t gpa;
	unsigned long uhva;
	struct kvm_memory_slot *memslot;
	struct kvm *kvm;
	struct list_head list;
	rwlock_t lock;
	struct mutex refresh_lock;
	void *khva;
	kvm_pfn_t pfn;
	bool active;
	bool valid;
};

#ifdef KVM_ARCH_NR_OBJS_PER_MEMORY_CACHE
/*
 * Memory caches are used to preallocate memory ahead of various MMU flows,
 * e.g. page fault handlers.  Gracefully handling allocation failures deep in
 * MMU flows is problematic, as is triggering reclaim, I/O, etc... while
 * holding MMU locks.  Note, these caches act more like prefetch buffers than
 * classical caches, i.e. objects are not returned to the cache on being freed.
 *
 * The @capacity field and @objects array are lazily initialized when the cache
 * is topped up (__kvm_mmu_topup_memory_cache()).
 */
struct kvm_mmu_memory_cache {
	gfp_t gfp_zero;
	gfp_t gfp_custom;
	u64 init_value;
	struct kmem_cache *kmem_cache;
	int capacity;
	int nobjs;
	void **objects;
};
#endif

#define HALT_POLL_HIST_COUNT			32

struct kvm_vm_stat_generic {
	u64 remote_tlb_flush;
	u64 remote_tlb_flush_requests;
};

struct kvm_vcpu_stat_generic {
	u64 halt_successful_poll;
	u64 halt_attempted_poll;
	u64 halt_poll_invalid;
	u64 halt_wakeup;
	u64 halt_poll_success_ns;
	u64 halt_poll_fail_ns;
	u64 halt_wait_ns;
	u64 halt_poll_success_hist[HALT_POLL_HIST_COUNT];
	u64 halt_poll_fail_hist[HALT_POLL_HIST_COUNT];
	u64 halt_wait_hist[HALT_POLL_HIST_COUNT];
	u64 blocking;
};

#define KVM_STATS_NAME_SIZE	48
#endif /* !__ASSEMBLER__ */

#endif /* __KVM_TYPES_H__ */
