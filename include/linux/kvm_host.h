/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_HOST_H
#define __KVM_HOST_H


#include <linux/types.h>
#include <linux/hardirq.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/sched/stat.h>
#include <linux/bug.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/preempt.h>
#include <linux/msi.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/rcupdate.h>
#include <linux/ratelimit.h>
#include <linux/err.h>
#include <linux/irqflags.h>
#include <linux/context_tracking.h>
#include <linux/irqbypass.h>
#include <linux/rcuwait.h>
#include <linux/refcount.h>
#include <linux/nospec.h>
#include <linux/notifier.h>
#include <linux/ftrace.h>
#include <linux/hashtable.h>
#include <linux/instrumentation.h>
#include <linux/interval_tree.h>
#include <linux/rbtree.h>
#include <linux/xarray.h>
#include <asm/signal.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>

#include <linux/kvm_types.h>

#include <asm/kvm_host.h>
#include <linux/kvm_dirty_ring.h>

#ifndef KVM_MAX_VCPU_IDS
#define KVM_MAX_VCPU_IDS KVM_MAX_VCPUS
#endif

/*
 * The bit 16 ~ bit 31 of kvm_userspace_memory_region::flags are internally
 * used in kvm, other bits are visible for userspace which are defined in
 * include/linux/kvm_h.
 */
#define KVM_MEMSLOT_INVALID	(1UL << 16)

/*
 * Bit 63 of the memslot generation number is an "update in-progress flag",
 * e.g. is temporarily set for the duration of kvm_swap_active_memslots().
 * This flag effectively creates a unique generation number that is used to
 * mark cached memslot data, e.g. MMIO accesses, as potentially being stale,
 * i.e. may (or may not) have come from the previous memslots generation.
 *
 * This is necessary because the actual memslots update is not atomic with
 * respect to the generation number update.  Updating the generation number
 * first would allow a vCPU to cache a spte from the old memslots using the
 * new generation number, and updating the generation number after switching
 * to the new memslots would allow cache hits using the old generation number
 * to reference the defunct memslots.
 *
 * This mechanism is used to prevent getting hits in KVM's caches while a
 * memslot update is in-progress, and to prevent cache hits *after* updating
 * the actual generation number against accesses that were inserted into the
 * cache *before* the memslots were updated.
 */
#define KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS	BIT_ULL(63)

/* Two fragments for cross MMIO pages. */
#define KVM_MAX_MMIO_FRAGMENTS	2

#ifndef KVM_MAX_NR_ADDRESS_SPACES
#define KVM_MAX_NR_ADDRESS_SPACES	1
#endif

/*
 * For the normal pfn, the highest 12 bits should be zero,
 * so we can mask bit 62 ~ bit 52  to indicate the error pfn,
 * mask bit 63 to indicate the noslot pfn.
 */
#define KVM_PFN_ERR_MASK	(0x7ffULL << 52)
#define KVM_PFN_ERR_NOSLOT_MASK	(0xfffULL << 52)
#define KVM_PFN_NOSLOT		(0x1ULL << 63)

#define KVM_PFN_ERR_FAULT	(KVM_PFN_ERR_MASK)
#define KVM_PFN_ERR_HWPOISON	(KVM_PFN_ERR_MASK + 1)
#define KVM_PFN_ERR_RO_FAULT	(KVM_PFN_ERR_MASK + 2)
#define KVM_PFN_ERR_SIGPENDING	(KVM_PFN_ERR_MASK + 3)
#define KVM_PFN_ERR_NEEDS_IO	(KVM_PFN_ERR_MASK + 4)

/*
 * error pfns indicate that the gfn is in slot but faild to
 * translate it to pfn on host.
 */
static inline bool is_error_pfn(kvm_pfn_t pfn)
{
	return !!(pfn & KVM_PFN_ERR_MASK);
}

/*
 * KVM_PFN_ERR_SIGPENDING indicates that fetching the PFN was interrupted
 * by a pending signal.  Note, the signal may or may not be fatal.
 */
static inline bool is_sigpending_pfn(kvm_pfn_t pfn)
{
	return pfn == KVM_PFN_ERR_SIGPENDING;
}

/*
 * error_noslot pfns indicate that the gfn can not be
 * translated to pfn - it is not in slot or failed to
 * translate it to pfn.
 */
static inline bool is_error_noslot_pfn(kvm_pfn_t pfn)
{
	return !!(pfn & KVM_PFN_ERR_NOSLOT_MASK);
}

/* noslot pfn indicates that the gfn is not in slot. */
static inline bool is_noslot_pfn(kvm_pfn_t pfn)
{
	return pfn == KVM_PFN_NOSLOT;
}

/*
 * architectures with KVM_HVA_ERR_BAD other than PAGE_OFFSET (e.g. s390)
 * provide own defines and kvm_is_error_hva
 */
#ifndef KVM_HVA_ERR_BAD

#define KVM_HVA_ERR_BAD		(PAGE_OFFSET)
#define KVM_HVA_ERR_RO_BAD	(PAGE_OFFSET + PAGE_SIZE)

static inline bool kvm_is_error_hva(unsigned long addr)
{
	return addr >= PAGE_OFFSET;
}

#endif

static inline bool kvm_is_error_gpa(gpa_t gpa)
{
	return gpa == INVALID_GPA;
}

#define KVM_REQUEST_MASK           GENMASK(7,0)
#define KVM_REQUEST_NO_WAKEUP      BIT(8)
#define KVM_REQUEST_WAIT           BIT(9)
#define KVM_REQUEST_NO_ACTION      BIT(10)
/*
 * Architecture-independent vcpu->requests bit members
 * Bits 3-7 are reserved for more arch-independent bits.
 */
#define KVM_REQ_TLB_FLUSH		(0 | KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_VM_DEAD			(1 | KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_UNBLOCK			2
#define KVM_REQ_DIRTY_RING_SOFT_FULL	3
#define KVM_REQUEST_ARCH_BASE		8

/*
 * KVM_REQ_OUTSIDE_GUEST_MODE exists is purely as way to force the vCPU to
 * OUTSIDE_GUEST_MODE.  KVM_REQ_OUTSIDE_GUEST_MODE differs from a vCPU "kick"
 * in that it ensures the vCPU has reached OUTSIDE_GUEST_MODE before continuing
 * on.  A kick only guarantees that the vCPU is on its way out, e.g. a previous
 * kick may have set vcpu->mode to EXITING_GUEST_MODE, and so there's no
 * guarantee the vCPU received an IPI and has actually exited guest mode.
 */
#define KVM_REQ_OUTSIDE_GUEST_MODE	(KVM_REQUEST_NO_ACTION | KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)

#define KVM_ARCH_REQ_FLAGS(nr, flags) ({ \
	BUILD_BUG_ON((unsigned)(nr) >= (sizeof_field(struct kvm_vcpu, requests) * 8) - KVM_REQUEST_ARCH_BASE); \
	(unsigned)(((nr) + KVM_REQUEST_ARCH_BASE) | (flags)); \
})
#define KVM_ARCH_REQ(nr)           KVM_ARCH_REQ_FLAGS(nr, 0)

bool kvm_make_vcpus_request_mask(struct kvm *kvm, unsigned int req,
				 unsigned long *vcpu_bitmap);
bool kvm_make_all_cpus_request(struct kvm *kvm, unsigned int req);

#define KVM_USERSPACE_IRQ_SOURCE_ID		0
#define KVM_IRQFD_RESAMPLE_IRQ_SOURCE_ID	1

extern struct mutex kvm_lock;
extern struct list_head vm_list;

struct kvm_io_range {
	gpa_t addr;
	int len;
	struct kvm_io_device *dev;
};

#define NR_IOBUS_DEVS 1000

struct kvm_io_bus {
	int dev_count;
	int ioeventfd_count;
	struct kvm_io_range range[];
};

enum kvm_bus {
	KVM_MMIO_BUS,
	KVM_PIO_BUS,
	KVM_VIRTIO_CCW_NOTIFY_BUS,
	KVM_FAST_MMIO_BUS,
	KVM_IOCSR_BUS,
	KVM_NR_BUSES
};

int kvm_io_bus_write(struct kvm_vcpu *vcpu, enum kvm_bus bus_idx, gpa_t addr,
		     int len, const void *val);
int kvm_io_bus_write_cookie(struct kvm_vcpu *vcpu, enum kvm_bus bus_idx,
			    gpa_t addr, int len, const void *val, long cookie);
int kvm_io_bus_read(struct kvm_vcpu *vcpu, enum kvm_bus bus_idx, gpa_t addr,
		    int len, void *val);
int kvm_io_bus_register_dev(struct kvm *kvm, enum kvm_bus bus_idx, gpa_t addr,
			    int len, struct kvm_io_device *dev);
int kvm_io_bus_unregister_dev(struct kvm *kvm, enum kvm_bus bus_idx,
			      struct kvm_io_device *dev);
struct kvm_io_device *kvm_io_bus_get_dev(struct kvm *kvm, enum kvm_bus bus_idx,
					 gpa_t addr);

#ifdef CONFIG_KVM_ASYNC_PF
struct kvm_async_pf {
	struct work_struct work;
	struct list_head link;
	struct list_head queue;
	struct kvm_vcpu *vcpu;
	gpa_t cr2_or_gpa;
	unsigned long addr;
	struct kvm_arch_async_pf arch;
	bool   wakeup_all;
	bool notpresent_injected;
};

void kvm_clear_async_pf_completion_queue(struct kvm_vcpu *vcpu);
void kvm_check_async_pf_completion(struct kvm_vcpu *vcpu);
bool kvm_setup_async_pf(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
			unsigned long hva, struct kvm_arch_async_pf *arch);
int kvm_async_pf_wakeup_all(struct kvm_vcpu *vcpu);
#endif

#ifdef CONFIG_KVM_GENERIC_MMU_NOTIFIER
union kvm_mmu_notifier_arg {
	unsigned long attributes;
};

enum kvm_gfn_range_filter {
	KVM_FILTER_SHARED		= BIT(0),
	KVM_FILTER_PRIVATE		= BIT(1),
};

struct kvm_gfn_range {
	struct kvm_memory_slot *slot;
	gfn_t start;
	gfn_t end;
	union kvm_mmu_notifier_arg arg;
	enum kvm_gfn_range_filter attr_filter;
	bool may_block;
	bool lockless;
};
bool kvm_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range);
bool kvm_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range);
bool kvm_test_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range);
#endif

enum {
	OUTSIDE_GUEST_MODE,
	IN_GUEST_MODE,
	EXITING_GUEST_MODE,
	READING_SHADOW_PAGE_TABLES,
};

struct kvm_host_map {
	/*
	 * Only valid if the 'pfn' is managed by the host kernel (i.e. There is
	 * a 'struct page' for it. When using mem= kernel parameter some memory
	 * can be used as guest memory but they are not managed by host
	 * kernel).
	 */
	struct page *pinned_page;
	struct page *page;
	void *hva;
	kvm_pfn_t pfn;
	kvm_pfn_t gfn;
	bool writable;
};

/*
 * Used to check if the mapping is valid or not. Never use 'kvm_host_map'
 * directly to check for that.
 */
static inline bool kvm_vcpu_mapped(struct kvm_host_map *map)
{
	return !!map->hva;
}

static inline bool kvm_vcpu_can_poll(ktime_t cur, ktime_t stop)
{
	return single_task_running() && !need_resched() && ktime_before(cur, stop);
}

/*
 * Sometimes a large or cross-page mmio needs to be broken up into separate
 * exits for userspace servicing.
 */
struct kvm_mmio_fragment {
	gpa_t gpa;
	void *data;
	unsigned len;
};

struct kvm_vcpu {
	struct kvm *kvm;
#ifdef CONFIG_PREEMPT_NOTIFIERS
	struct preempt_notifier preempt_notifier;
#endif
	int cpu;
	int vcpu_id; /* id given by userspace at creation */
	int vcpu_idx; /* index into kvm->vcpu_array */
	int ____srcu_idx; /* Don't use this directly.  You've been warned. */
#ifdef CONFIG_PROVE_RCU
	int srcu_depth;
#endif
	int mode;
	u64 requests;
	unsigned long guest_debug;

	struct mutex mutex;
	struct kvm_run *run;

#ifndef __KVM_HAVE_ARCH_WQP
	struct rcuwait wait;
#endif
	struct pid *pid;
	rwlock_t pid_lock;
	int sigset_active;
	sigset_t sigset;
	unsigned int halt_poll_ns;
	bool valid_wakeup;

#ifdef CONFIG_HAS_IOMEM
	int mmio_needed;
	int mmio_read_completed;
	int mmio_is_write;
	int mmio_cur_fragment;
	int mmio_nr_fragments;
	struct kvm_mmio_fragment mmio_fragments[KVM_MAX_MMIO_FRAGMENTS];
#endif

#ifdef CONFIG_KVM_ASYNC_PF
	struct {
		u32 queued;
		struct list_head queue;
		struct list_head done;
		spinlock_t lock;
	} async_pf;
#endif

#ifdef CONFIG_HAVE_KVM_CPU_RELAX_INTERCEPT
	/*
	 * Cpu relax intercept or pause loop exit optimization
	 * in_spin_loop: set when a vcpu does a pause loop exit
	 *  or cpu relax intercepted.
	 * dy_eligible: indicates whether vcpu is eligible for directed yield.
	 */
	struct {
		bool in_spin_loop;
		bool dy_eligible;
	} spin_loop;
#endif
	bool wants_to_run;
	bool preempted;
	bool ready;
	bool scheduled_out;
	struct kvm_vcpu_arch arch;
	struct kvm_vcpu_stat stat;
	char stats_id[KVM_STATS_NAME_SIZE];
	struct kvm_dirty_ring dirty_ring;

	/*
	 * The most recently used memslot by this vCPU and the slots generation
	 * for which it is valid.
	 * No wraparound protection is needed since generations won't overflow in
	 * thousands of years, even assuming 1M memslot operations per second.
	 */
	struct kvm_memory_slot *last_used_slot;
	u64 last_used_slot_gen;
};

/*
 * Start accounting time towards a guest.
 * Must be called before entering guest context.
 */
static __always_inline void guest_timing_enter_irqoff(void)
{
	/*
	 * This is running in ioctl context so its safe to assume that it's the
	 * stime pending cputime to flush.
	 */
	instrumentation_begin();
	vtime_account_guest_enter();
	instrumentation_end();
}

/*
 * Enter guest context and enter an RCU extended quiescent state.
 *
 * Between guest_context_enter_irqoff() and guest_context_exit_irqoff() it is
 * unsafe to use any code which may directly or indirectly use RCU, tracing
 * (including IRQ flag tracing), or lockdep. All code in this period must be
 * non-instrumentable.
 */
static __always_inline void guest_context_enter_irqoff(void)
{
	/*
	 * KVM does not hold any references to rcu protected data when it
	 * switches CPU into a guest mode. In fact switching to a guest mode
	 * is very similar to exiting to userspace from rcu point of view. In
	 * addition CPU may stay in a guest mode for quite a long time (up to
	 * one time slice). Lets treat guest mode as quiescent state, just like
	 * we do with user-mode execution.
	 */
	if (!context_tracking_guest_enter()) {
		instrumentation_begin();
		rcu_virt_note_context_switch();
		instrumentation_end();
	}
}

/*
 * Deprecated. Architectures should move to guest_timing_enter_irqoff() and
 * guest_state_enter_irqoff().
 */
static __always_inline void guest_enter_irqoff(void)
{
	guest_timing_enter_irqoff();
	guest_context_enter_irqoff();
}

/**
 * guest_state_enter_irqoff - Fixup state when entering a guest
 *
 * Entry to a guest will enable interrupts, but the kernel state is interrupts
 * disabled when this is invoked. Also tell RCU about it.
 *
 * 1) Trace interrupts on state
 * 2) Invoke context tracking if enabled to adjust RCU state
 * 3) Tell lockdep that interrupts are enabled
 *
 * Invoked from architecture specific code before entering a guest.
 * Must be called with interrupts disabled and the caller must be
 * non-instrumentable.
 * The caller has to invoke guest_timing_enter_irqoff() before this.
 *
 * Note: this is analogous to exit_to_user_mode().
 */
static __always_inline void guest_state_enter_irqoff(void)
{
	instrumentation_begin();
	trace_hardirqs_on_prepare();
	lockdep_hardirqs_on_prepare();
	instrumentation_end();

	guest_context_enter_irqoff();
	lockdep_hardirqs_on(CALLER_ADDR0);
}

/*
 * Exit guest context and exit an RCU extended quiescent state.
 *
 * Between guest_context_enter_irqoff() and guest_context_exit_irqoff() it is
 * unsafe to use any code which may directly or indirectly use RCU, tracing
 * (including IRQ flag tracing), or lockdep. All code in this period must be
 * non-instrumentable.
 */
static __always_inline void guest_context_exit_irqoff(void)
{
	/*
	 * Guest mode is treated as a quiescent state, see
	 * guest_context_enter_irqoff() for more details.
	 */
	if (!context_tracking_guest_exit()) {
		instrumentation_begin();
		rcu_virt_note_context_switch();
		instrumentation_end();
	}
}

/*
 * Stop accounting time towards a guest.
 * Must be called after exiting guest context.
 */
static __always_inline void guest_timing_exit_irqoff(void)
{
	instrumentation_begin();
	/* Flush the guest cputime we spent on the guest */
	vtime_account_guest_exit();
	instrumentation_end();
}

/*
 * Deprecated. Architectures should move to guest_state_exit_irqoff() and
 * guest_timing_exit_irqoff().
 */
static __always_inline void guest_exit_irqoff(void)
{
	guest_context_exit_irqoff();
	guest_timing_exit_irqoff();
}

static inline void guest_exit(void)
{
	unsigned long flags;

	local_irq_save(flags);
	guest_exit_irqoff();
	local_irq_restore(flags);
}

/**
 * guest_state_exit_irqoff - Establish state when returning from guest mode
 *
 * Entry from a guest disables interrupts, but guest mode is traced as
 * interrupts enabled. Also with NO_HZ_FULL RCU might be idle.
 *
 * 1) Tell lockdep that interrupts are disabled
 * 2) Invoke context tracking if enabled to reactivate RCU
 * 3) Trace interrupts off state
 *
 * Invoked from architecture specific code after exiting a guest.
 * Must be invoked with interrupts disabled and the caller must be
 * non-instrumentable.
 * The caller has to invoke guest_timing_exit_irqoff() after this.
 *
 * Note: this is analogous to enter_from_user_mode().
 */
static __always_inline void guest_state_exit_irqoff(void)
{
	lockdep_hardirqs_off(CALLER_ADDR0);
	guest_context_exit_irqoff();

	instrumentation_begin();
	trace_hardirqs_off_finish();
	instrumentation_end();
}

static inline int kvm_vcpu_exiting_guest_mode(struct kvm_vcpu *vcpu)
{
	/*
	 * The memory barrier ensures a previous write to vcpu->requests cannot
	 * be reordered with the read of vcpu->mode.  It pairs with the general
	 * memory barrier following the write of vcpu->mode in VCPU RUN.
	 */
	smp_mb__before_atomic();
	return cmpxchg(&vcpu->mode, IN_GUEST_MODE, EXITING_GUEST_MODE);
}

/*
 * Some of the bitops functions do not support too long bitmaps.
 * This number must be determined not to exceed such limits.
 */
#define KVM_MEM_MAX_NR_PAGES ((1UL << 31) - 1)

/*
 * Since at idle each memslot belongs to two memslot sets it has to contain
 * two embedded nodes for each data structure that it forms a part of.
 *
 * Two memslot sets (one active and one inactive) are necessary so the VM
 * continues to run on one memslot set while the other is being modified.
 *
 * These two memslot sets normally point to the same set of memslots.
 * They can, however, be desynchronized when performing a memslot management
 * operation by replacing the memslot to be modified by its copy.
 * After the operation is complete, both memslot sets once again point to
 * the same, common set of memslot data.
 *
 * The memslots themselves are independent of each other so they can be
 * individually added or deleted.
 */
struct kvm_memory_slot {
	struct hlist_node id_node[2];
	struct interval_tree_node hva_node[2];
	struct rb_node gfn_node[2];
	gfn_t base_gfn;
	unsigned long npages;
	unsigned long *dirty_bitmap;
	struct kvm_arch_memory_slot arch;
	unsigned long userspace_addr;
	u32 flags;
	short id;
	u16 as_id;

#ifdef CONFIG_KVM_PRIVATE_MEM
	struct {
		/*
		 * Writes protected by kvm->slots_lock.  Acquiring a
		 * reference via kvm_gmem_get_file() is protected by
		 * either kvm->slots_lock or kvm->srcu.
		 */
		struct file *file;
		pgoff_t pgoff;
	} gmem;
#endif
};

static inline bool kvm_slot_can_be_private(const struct kvm_memory_slot *slot)
{
	return slot && (slot->flags & KVM_MEM_GUEST_MEMFD);
}

static inline bool kvm_slot_dirty_track_enabled(const struct kvm_memory_slot *slot)
{
	return slot->flags & KVM_MEM_LOG_DIRTY_PAGES;
}

static inline unsigned long kvm_dirty_bitmap_bytes(struct kvm_memory_slot *memslot)
{
	return ALIGN(memslot->npages, BITS_PER_LONG) / 8;
}

static inline unsigned long *kvm_second_dirty_bitmap(struct kvm_memory_slot *memslot)
{
	unsigned long len = kvm_dirty_bitmap_bytes(memslot);

	return memslot->dirty_bitmap + len / sizeof(*memslot->dirty_bitmap);
}

#ifndef KVM_DIRTY_LOG_MANUAL_CAPS
#define KVM_DIRTY_LOG_MANUAL_CAPS KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE
#endif

struct kvm_s390_adapter_int {
	u64 ind_addr;
	u64 summary_addr;
	u64 ind_offset;
	u32 summary_offset;
	u32 adapter_id;
};

struct kvm_hv_sint {
	u32 vcpu;
	u32 sint;
};

struct kvm_xen_evtchn {
	u32 port;
	u32 vcpu_id;
	int vcpu_idx;
	u32 priority;
};

struct kvm_kernel_irq_routing_entry {
	u32 gsi;
	u32 type;
	int (*set)(struct kvm_kernel_irq_routing_entry *e,
		   struct kvm *kvm, int irq_source_id, int level,
		   bool line_status);
	union {
		struct {
			unsigned irqchip;
			unsigned pin;
		} irqchip;
		struct {
			u32 address_lo;
			u32 address_hi;
			u32 data;
			u32 flags;
			u32 devid;
		} msi;
		struct kvm_s390_adapter_int adapter;
		struct kvm_hv_sint hv_sint;
		struct kvm_xen_evtchn xen_evtchn;
	};
	struct hlist_node link;
};

#ifdef CONFIG_HAVE_KVM_IRQ_ROUTING
struct kvm_irq_routing_table {
	int chip[KVM_NR_IRQCHIPS][KVM_IRQCHIP_NUM_PINS];
	u32 nr_rt_entries;
	/*
	 * Array indexed by gsi. Each entry contains list of irq chips
	 * the gsi is connected to.
	 */
	struct hlist_head map[] __counted_by(nr_rt_entries);
};
#endif

bool kvm_arch_irqchip_in_kernel(struct kvm *kvm);

#ifndef KVM_INTERNAL_MEM_SLOTS
#define KVM_INTERNAL_MEM_SLOTS 0
#endif

#define KVM_MEM_SLOTS_NUM SHRT_MAX
#define KVM_USER_MEM_SLOTS (KVM_MEM_SLOTS_NUM - KVM_INTERNAL_MEM_SLOTS)

#if KVM_MAX_NR_ADDRESS_SPACES == 1
static inline int kvm_arch_nr_memslot_as_ids(struct kvm *kvm)
{
	return KVM_MAX_NR_ADDRESS_SPACES;
}

static inline int kvm_arch_vcpu_memslots_id(struct kvm_vcpu *vcpu)
{
	return 0;
}
#endif

/*
 * Arch code must define kvm_arch_has_private_mem if support for private memory
 * is enabled.
 */
#if !defined(kvm_arch_has_private_mem) && !IS_ENABLED(CONFIG_KVM_PRIVATE_MEM)
static inline bool kvm_arch_has_private_mem(struct kvm *kvm)
{
	return false;
}
#endif

#ifndef kvm_arch_has_readonly_mem
static inline bool kvm_arch_has_readonly_mem(struct kvm *kvm)
{
	return IS_ENABLED(CONFIG_HAVE_KVM_READONLY_MEM);
}
#endif

struct kvm_memslots {
	u64 generation;
	atomic_long_t last_used_slot;
	struct rb_root_cached hva_tree;
	struct rb_root gfn_tree;
	/*
	 * The mapping table from slot id to memslot.
	 *
	 * 7-bit bucket count matches the size of the old id to index array for
	 * 512 slots, while giving good performance with this slot count.
	 * Higher bucket counts bring only small performance improvements but
	 * always result in higher memory usage (even for lower memslot counts).
	 */
	DECLARE_HASHTABLE(id_hash, 7);
	int node_idx;
};

struct kvm {
#ifdef KVM_HAVE_MMU_RWLOCK
	rwlock_t mmu_lock;
#else
	spinlock_t mmu_lock;
#endif /* KVM_HAVE_MMU_RWLOCK */

	struct mutex slots_lock;

	/*
	 * Protects the arch-specific fields of struct kvm_memory_slots in
	 * use by the VM. To be used under the slots_lock (above) or in a
	 * kvm->srcu critical section where acquiring the slots_lock would
	 * lead to deadlock with the synchronize_srcu in
	 * kvm_swap_active_memslots().
	 */
	struct mutex slots_arch_lock;
	struct mm_struct *mm; /* userspace tied to this vm */
	unsigned long nr_memslot_pages;
	/* The two memslot sets - active and inactive (per address space) */
	struct kvm_memslots __memslots[KVM_MAX_NR_ADDRESS_SPACES][2];
	/* The current active memslot set for each address space */
	struct kvm_memslots __rcu *memslots[KVM_MAX_NR_ADDRESS_SPACES];
	struct xarray vcpu_array;
	/*
	 * Protected by slots_lock, but can be read outside if an
	 * incorrect answer is acceptable.
	 */
	atomic_t nr_memslots_dirty_logging;

	/* Used to wait for completion of MMU notifiers.  */
	spinlock_t mn_invalidate_lock;
	unsigned long mn_active_invalidate_count;
	struct rcuwait mn_memslots_update_rcuwait;

	/* For management / invalidation of gfn_to_pfn_caches */
	spinlock_t gpc_lock;
	struct list_head gpc_list;

	/*
	 * created_vcpus is protected by kvm->lock, and is incremented
	 * at the beginning of KVM_CREATE_VCPU.  online_vcpus is only
	 * incremented after storing the kvm_vcpu pointer in vcpus,
	 * and is accessed atomically.
	 */
	atomic_t online_vcpus;
	int max_vcpus;
	int created_vcpus;
	int last_boosted_vcpu;
	struct list_head vm_list;
	struct mutex lock;
	struct kvm_io_bus __rcu *buses[KVM_NR_BUSES];
#ifdef CONFIG_HAVE_KVM_IRQCHIP
	struct {
		spinlock_t        lock;
		struct list_head  items;
		/* resampler_list update side is protected by resampler_lock. */
		struct list_head  resampler_list;
		struct mutex      resampler_lock;
	} irqfds;
#endif
	struct list_head ioeventfds;
	struct kvm_vm_stat stat;
	struct kvm_arch arch;
	refcount_t users_count;
#ifdef CONFIG_KVM_MMIO
	struct kvm_coalesced_mmio_ring *coalesced_mmio_ring;
	spinlock_t ring_lock;
	struct list_head coalesced_zones;
#endif

	struct mutex irq_lock;
#ifdef CONFIG_HAVE_KVM_IRQCHIP
	/*
	 * Update side is protected by irq_lock.
	 */
	struct kvm_irq_routing_table __rcu *irq_routing;

	struct hlist_head irq_ack_notifier_list;
#endif

#ifdef CONFIG_KVM_GENERIC_MMU_NOTIFIER
	struct mmu_notifier mmu_notifier;
	unsigned long mmu_invalidate_seq;
	long mmu_invalidate_in_progress;
	gfn_t mmu_invalidate_range_start;
	gfn_t mmu_invalidate_range_end;
#endif
	struct list_head devices;
	u64 manual_dirty_log_protect;
	struct dentry *debugfs_dentry;
	struct kvm_stat_data **debugfs_stat_data;
	struct srcu_struct srcu;
	struct srcu_struct irq_srcu;
	pid_t userspace_pid;
	bool override_halt_poll_ns;
	unsigned int max_halt_poll_ns;
	u32 dirty_ring_size;
	bool dirty_ring_with_bitmap;
	bool vm_bugged;
	bool vm_dead;

#ifdef CONFIG_HAVE_KVM_PM_NOTIFIER
	struct notifier_block pm_notifier;
#endif
#ifdef CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES
	/* Protected by slots_locks (for writes) and RCU (for reads) */
	struct xarray mem_attr_array;
#endif
	char stats_id[KVM_STATS_NAME_SIZE];
};

#define kvm_err(fmt, ...) \
	pr_err("kvm [%i]: " fmt, task_pid_nr(current), ## __VA_ARGS__)
#define kvm_info(fmt, ...) \
	pr_info("kvm [%i]: " fmt, task_pid_nr(current), ## __VA_ARGS__)
#define kvm_debug(fmt, ...) \
	pr_debug("kvm [%i]: " fmt, task_pid_nr(current), ## __VA_ARGS__)
#define kvm_debug_ratelimited(fmt, ...) \
	pr_debug_ratelimited("kvm [%i]: " fmt, task_pid_nr(current), \
			     ## __VA_ARGS__)
#define kvm_pr_unimpl(fmt, ...) \
	pr_err_ratelimited("kvm [%i]: " fmt, \
			   task_tgid_nr(current), ## __VA_ARGS__)

/* The guest did something we don't support. */
#define vcpu_unimpl(vcpu, fmt, ...)					\
	kvm_pr_unimpl("vcpu%i, guest rIP: 0x%lx " fmt,			\
			(vcpu)->vcpu_id, kvm_rip_read(vcpu), ## __VA_ARGS__)

#define vcpu_debug(vcpu, fmt, ...)					\
	kvm_debug("vcpu%i " fmt, (vcpu)->vcpu_id, ## __VA_ARGS__)
#define vcpu_debug_ratelimited(vcpu, fmt, ...)				\
	kvm_debug_ratelimited("vcpu%i " fmt, (vcpu)->vcpu_id,           \
			      ## __VA_ARGS__)
#define vcpu_err(vcpu, fmt, ...)					\
	kvm_err("vcpu%i " fmt, (vcpu)->vcpu_id, ## __VA_ARGS__)

static inline void kvm_vm_dead(struct kvm *kvm)
{
	kvm->vm_dead = true;
	kvm_make_all_cpus_request(kvm, KVM_REQ_VM_DEAD);
}

static inline void kvm_vm_bugged(struct kvm *kvm)
{
	kvm->vm_bugged = true;
	kvm_vm_dead(kvm);
}


#define KVM_BUG(cond, kvm, fmt...)				\
({								\
	bool __ret = !!(cond);					\
								\
	if (WARN_ONCE(__ret && !(kvm)->vm_bugged, fmt))		\
		kvm_vm_bugged(kvm);				\
	unlikely(__ret);					\
})

#define KVM_BUG_ON(cond, kvm)					\
({								\
	bool __ret = !!(cond);					\
								\
	if (WARN_ON_ONCE(__ret && !(kvm)->vm_bugged))		\
		kvm_vm_bugged(kvm);				\
	unlikely(__ret);					\
})

/*
 * Note, "data corruption" refers to corruption of host kernel data structures,
 * not guest data.  Guest data corruption, suspected or confirmed, that is tied
 * and contained to a single VM should *never* BUG() and potentially panic the
 * host, i.e. use this variant of KVM_BUG() if and only if a KVM data structure
 * is corrupted and that corruption can have a cascading effect to other parts
 * of the hosts and/or to other VMs.
 */
#define KVM_BUG_ON_DATA_CORRUPTION(cond, kvm)			\
({								\
	bool __ret = !!(cond);					\
								\
	if (IS_ENABLED(CONFIG_BUG_ON_DATA_CORRUPTION))		\
		BUG_ON(__ret);					\
	else if (WARN_ON_ONCE(__ret && !(kvm)->vm_bugged))	\
		kvm_vm_bugged(kvm);				\
	unlikely(__ret);					\
})

static inline void kvm_vcpu_srcu_read_lock(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_PROVE_RCU
	WARN_ONCE(vcpu->srcu_depth++,
		  "KVM: Illegal vCPU srcu_idx LOCK, depth=%d", vcpu->srcu_depth - 1);
#endif
	vcpu->____srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
}

static inline void kvm_vcpu_srcu_read_unlock(struct kvm_vcpu *vcpu)
{
	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->____srcu_idx);

#ifdef CONFIG_PROVE_RCU
	WARN_ONCE(--vcpu->srcu_depth,
		  "KVM: Illegal vCPU srcu_idx UNLOCK, depth=%d", vcpu->srcu_depth);
#endif
}

static inline bool kvm_dirty_log_manual_protect_and_init_set(struct kvm *kvm)
{
	return !!(kvm->manual_dirty_log_protect & KVM_DIRTY_LOG_INITIALLY_SET);
}

static inline struct kvm_io_bus *kvm_get_bus(struct kvm *kvm, enum kvm_bus idx)
{
	return srcu_dereference_check(kvm->buses[idx], &kvm->srcu,
				      lockdep_is_held(&kvm->slots_lock) ||
				      !refcount_read(&kvm->users_count));
}

static inline struct kvm_vcpu *kvm_get_vcpu(struct kvm *kvm, int i)
{
	int num_vcpus = atomic_read(&kvm->online_vcpus);

	/*
	 * Explicitly verify the target vCPU is online, as the anti-speculation
	 * logic only limits the CPU's ability to speculate, e.g. given a "bad"
	 * index, clamping the index to 0 would return vCPU0, not NULL.
	 */
	if (i >= num_vcpus)
		return NULL;

	i = array_index_nospec(i, num_vcpus);

	/* Pairs with smp_wmb() in kvm_vm_ioctl_create_vcpu.  */
	smp_rmb();
	return xa_load(&kvm->vcpu_array, i);
}

#define kvm_for_each_vcpu(idx, vcpup, kvm)				\
	if (atomic_read(&kvm->online_vcpus))				\
		xa_for_each_range(&kvm->vcpu_array, idx, vcpup, 0,	\
				  (atomic_read(&kvm->online_vcpus) - 1))

static inline struct kvm_vcpu *kvm_get_vcpu_by_id(struct kvm *kvm, int id)
{
	struct kvm_vcpu *vcpu = NULL;
	unsigned long i;

	if (id < 0)
		return NULL;
	if (id < KVM_MAX_VCPUS)
		vcpu = kvm_get_vcpu(kvm, id);
	if (vcpu && vcpu->vcpu_id == id)
		return vcpu;
	kvm_for_each_vcpu(i, vcpu, kvm)
		if (vcpu->vcpu_id == id)
			return vcpu;
	return NULL;
}

void kvm_destroy_vcpus(struct kvm *kvm);

void vcpu_load(struct kvm_vcpu *vcpu);
void vcpu_put(struct kvm_vcpu *vcpu);

#ifdef __KVM_HAVE_IOAPIC
void kvm_arch_post_irq_ack_notifier_list_update(struct kvm *kvm);
void kvm_arch_post_irq_routing_update(struct kvm *kvm);
#else
static inline void kvm_arch_post_irq_ack_notifier_list_update(struct kvm *kvm)
{
}
static inline void kvm_arch_post_irq_routing_update(struct kvm *kvm)
{
}
#endif

#ifdef CONFIG_HAVE_KVM_IRQCHIP
int kvm_irqfd_init(void);
void kvm_irqfd_exit(void);
#else
static inline int kvm_irqfd_init(void)
{
	return 0;
}

static inline void kvm_irqfd_exit(void)
{
}
#endif
int kvm_init(unsigned vcpu_size, unsigned vcpu_align, struct module *module);
void kvm_exit(void);

void kvm_get_kvm(struct kvm *kvm);
bool kvm_get_kvm_safe(struct kvm *kvm);
void kvm_put_kvm(struct kvm *kvm);
bool file_is_kvm(struct file *file);
void kvm_put_kvm_no_destroy(struct kvm *kvm);

static inline struct kvm_memslots *__kvm_memslots(struct kvm *kvm, int as_id)
{
	as_id = array_index_nospec(as_id, KVM_MAX_NR_ADDRESS_SPACES);
	return srcu_dereference_check(kvm->memslots[as_id], &kvm->srcu,
			lockdep_is_held(&kvm->slots_lock) ||
			!refcount_read(&kvm->users_count));
}

static inline struct kvm_memslots *kvm_memslots(struct kvm *kvm)
{
	return __kvm_memslots(kvm, 0);
}

static inline struct kvm_memslots *kvm_vcpu_memslots(struct kvm_vcpu *vcpu)
{
	int as_id = kvm_arch_vcpu_memslots_id(vcpu);

	return __kvm_memslots(vcpu->kvm, as_id);
}

static inline bool kvm_memslots_empty(struct kvm_memslots *slots)
{
	return RB_EMPTY_ROOT(&slots->gfn_tree);
}

bool kvm_are_all_memslots_empty(struct kvm *kvm);

#define kvm_for_each_memslot(memslot, bkt, slots)			      \
	hash_for_each(slots->id_hash, bkt, memslot, id_node[slots->node_idx]) \
		if (WARN_ON_ONCE(!memslot->npages)) {			      \
		} else

static inline
struct kvm_memory_slot *id_to_memslot(struct kvm_memslots *slots, int id)
{
	struct kvm_memory_slot *slot;
	int idx = slots->node_idx;

	hash_for_each_possible(slots->id_hash, slot, id_node[idx], id) {
		if (slot->id == id)
			return slot;
	}

	return NULL;
}

/* Iterator used for walking memslots that overlap a gfn range. */
struct kvm_memslot_iter {
	struct kvm_memslots *slots;
	struct rb_node *node;
	struct kvm_memory_slot *slot;
};

static inline void kvm_memslot_iter_next(struct kvm_memslot_iter *iter)
{
	iter->node = rb_next(iter->node);
	if (!iter->node)
		return;

	iter->slot = container_of(iter->node, struct kvm_memory_slot, gfn_node[iter->slots->node_idx]);
}

static inline void kvm_memslot_iter_start(struct kvm_memslot_iter *iter,
					  struct kvm_memslots *slots,
					  gfn_t start)
{
	int idx = slots->node_idx;
	struct rb_node *tmp;
	struct kvm_memory_slot *slot;

	iter->slots = slots;

	/*
	 * Find the so called "upper bound" of a key - the first node that has
	 * its key strictly greater than the searched one (the start gfn in our case).
	 */
	iter->node = NULL;
	for (tmp = slots->gfn_tree.rb_node; tmp; ) {
		slot = container_of(tmp, struct kvm_memory_slot, gfn_node[idx]);
		if (start < slot->base_gfn) {
			iter->node = tmp;
			tmp = tmp->rb_left;
		} else {
			tmp = tmp->rb_right;
		}
	}

	/*
	 * Find the slot with the lowest gfn that can possibly intersect with
	 * the range, so we'll ideally have slot start <= range start
	 */
	if (iter->node) {
		/*
		 * A NULL previous node means that the very first slot
		 * already has a higher start gfn.
		 * In this case slot start > range start.
		 */
		tmp = rb_prev(iter->node);
		if (tmp)
			iter->node = tmp;
	} else {
		/* a NULL node below means no slots */
		iter->node = rb_last(&slots->gfn_tree);
	}

	if (iter->node) {
		iter->slot = container_of(iter->node, struct kvm_memory_slot, gfn_node[idx]);

		/*
		 * It is possible in the slot start < range start case that the
		 * found slot ends before or at range start (slot end <= range start)
		 * and so it does not overlap the requested range.
		 *
		 * In such non-overlapping case the next slot (if it exists) will
		 * already have slot start > range start, otherwise the logic above
		 * would have found it instead of the current slot.
		 */
		if (iter->slot->base_gfn + iter->slot->npages <= start)
			kvm_memslot_iter_next(iter);
	}
}

static inline bool kvm_memslot_iter_is_valid(struct kvm_memslot_iter *iter, gfn_t end)
{
	if (!iter->node)
		return false;

	/*
	 * If this slot starts beyond or at the end of the range so does
	 * every next one
	 */
	return iter->slot->base_gfn < end;
}

/* Iterate over each memslot at least partially intersecting [start, end) range */
#define kvm_for_each_memslot_in_gfn_range(iter, slots, start, end)	\
	for (kvm_memslot_iter_start(iter, slots, start);		\
	     kvm_memslot_iter_is_valid(iter, end);			\
	     kvm_memslot_iter_next(iter))

struct kvm_memory_slot *gfn_to_memslot(struct kvm *kvm, gfn_t gfn);
struct kvm_memslots *kvm_vcpu_memslots(struct kvm_vcpu *vcpu);
struct kvm_memory_slot *kvm_vcpu_gfn_to_memslot(struct kvm_vcpu *vcpu, gfn_t gfn);

/*
 * KVM_SET_USER_MEMORY_REGION ioctl allows the following operations:
 * - create a new memory slot
 * - delete an existing memory slot
 * - modify an existing memory slot
 *   -- move it in the guest physical memory space
 *   -- just change its flags
 *
 * Since flags can be changed by some of these operations, the following
 * differentiation is the best we can do for kvm_set_memory_region():
 */
enum kvm_mr_change {
	KVM_MR_CREATE,
	KVM_MR_DELETE,
	KVM_MR_MOVE,
	KVM_MR_FLAGS_ONLY,
};

int kvm_set_internal_memslot(struct kvm *kvm,
			     const struct kvm_userspace_memory_region2 *mem);
void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *slot);
void kvm_arch_memslots_updated(struct kvm *kvm, u64 gen);
int kvm_arch_prepare_memory_region(struct kvm *kvm,
				const struct kvm_memory_slot *old,
				struct kvm_memory_slot *new,
				enum kvm_mr_change change);
void kvm_arch_commit_memory_region(struct kvm *kvm,
				struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change);
/* flush all memory translations */
void kvm_arch_flush_shadow_all(struct kvm *kvm);
/* flush memory translations pointing to 'slot' */
void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot);

int kvm_prefetch_pages(struct kvm_memory_slot *slot, gfn_t gfn,
		       struct page **pages, int nr_pages);

struct page *__gfn_to_page(struct kvm *kvm, gfn_t gfn, bool write);
static inline struct page *gfn_to_page(struct kvm *kvm, gfn_t gfn)
{
	return __gfn_to_page(kvm, gfn, true);
}

unsigned long gfn_to_hva(struct kvm *kvm, gfn_t gfn);
unsigned long gfn_to_hva_prot(struct kvm *kvm, gfn_t gfn, bool *writable);
unsigned long gfn_to_hva_memslot(struct kvm_memory_slot *slot, gfn_t gfn);
unsigned long gfn_to_hva_memslot_prot(struct kvm_memory_slot *slot, gfn_t gfn,
				      bool *writable);

static inline void kvm_release_page_unused(struct page *page)
{
	if (!page)
		return;

	put_page(page);
}

void kvm_release_page_clean(struct page *page);
void kvm_release_page_dirty(struct page *page);

static inline void kvm_release_faultin_page(struct kvm *kvm, struct page *page,
					    bool unused, bool dirty)
{
	lockdep_assert_once(lockdep_is_held(&kvm->mmu_lock) || unused);

	if (!page)
		return;

	/*
	 * If the page that KVM got from the *primary MMU* is writable, and KVM
	 * installed or reused a SPTE, mark the page/folio dirty.  Note, this
	 * may mark a folio dirty even if KVM created a read-only SPTE, e.g. if
	 * the GFN is write-protected.  Folios can't be safely marked dirty
	 * outside of mmu_lock as doing so could race with writeback on the
	 * folio.  As a result, KVM can't mark folios dirty in the fast page
	 * fault handler, and so KVM must (somewhat) speculatively mark the
	 * folio dirty if KVM could locklessly make the SPTE writable.
	 */
	if (unused)
		kvm_release_page_unused(page);
	else if (dirty)
		kvm_release_page_dirty(page);
	else
		kvm_release_page_clean(page);
}

kvm_pfn_t __kvm_faultin_pfn(const struct kvm_memory_slot *slot, gfn_t gfn,
			    unsigned int foll, bool *writable,
			    struct page **refcounted_page);

static inline kvm_pfn_t kvm_faultin_pfn(struct kvm_vcpu *vcpu, gfn_t gfn,
					bool write, bool *writable,
					struct page **refcounted_page)
{
	return __kvm_faultin_pfn(kvm_vcpu_gfn_to_memslot(vcpu, gfn), gfn,
				 write ? FOLL_WRITE : 0, writable, refcounted_page);
}

int kvm_read_guest_page(struct kvm *kvm, gfn_t gfn, void *data, int offset,
			int len);
int kvm_read_guest(struct kvm *kvm, gpa_t gpa, void *data, unsigned long len);
int kvm_read_guest_cached(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
			   void *data, unsigned long len);
int kvm_read_guest_offset_cached(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
				 void *data, unsigned int offset,
				 unsigned long len);
int kvm_write_guest_page(struct kvm *kvm, gfn_t gfn, const void *data,
			 int offset, int len);
int kvm_write_guest(struct kvm *kvm, gpa_t gpa, const void *data,
		    unsigned long len);
int kvm_write_guest_cached(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
			   void *data, unsigned long len);
int kvm_write_guest_offset_cached(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
				  void *data, unsigned int offset,
				  unsigned long len);
int kvm_gfn_to_hva_cache_init(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
			      gpa_t gpa, unsigned long len);

#define __kvm_get_guest(kvm, gfn, offset, v)				\
({									\
	unsigned long __addr = gfn_to_hva(kvm, gfn);			\
	typeof(v) __user *__uaddr = (typeof(__uaddr))(__addr + offset);	\
	int __ret = -EFAULT;						\
									\
	if (!kvm_is_error_hva(__addr))					\
		__ret = get_user(v, __uaddr);				\
	__ret;								\
})

#define kvm_get_guest(kvm, gpa, v)					\
({									\
	gpa_t __gpa = gpa;						\
	struct kvm *__kvm = kvm;					\
									\
	__kvm_get_guest(__kvm, __gpa >> PAGE_SHIFT,			\
			offset_in_page(__gpa), v);			\
})

#define __kvm_put_guest(kvm, gfn, offset, v)				\
({									\
	unsigned long __addr = gfn_to_hva(kvm, gfn);			\
	typeof(v) __user *__uaddr = (typeof(__uaddr))(__addr + offset);	\
	int __ret = -EFAULT;						\
									\
	if (!kvm_is_error_hva(__addr))					\
		__ret = put_user(v, __uaddr);				\
	if (!__ret)							\
		mark_page_dirty(kvm, gfn);				\
	__ret;								\
})

#define kvm_put_guest(kvm, gpa, v)					\
({									\
	gpa_t __gpa = gpa;						\
	struct kvm *__kvm = kvm;					\
									\
	__kvm_put_guest(__kvm, __gpa >> PAGE_SHIFT,			\
			offset_in_page(__gpa), v);			\
})

int kvm_clear_guest(struct kvm *kvm, gpa_t gpa, unsigned long len);
bool kvm_is_visible_gfn(struct kvm *kvm, gfn_t gfn);
bool kvm_vcpu_is_visible_gfn(struct kvm_vcpu *vcpu, gfn_t gfn);
unsigned long kvm_host_page_size(struct kvm_vcpu *vcpu, gfn_t gfn);
void mark_page_dirty_in_slot(struct kvm *kvm, const struct kvm_memory_slot *memslot, gfn_t gfn);
void mark_page_dirty(struct kvm *kvm, gfn_t gfn);

int __kvm_vcpu_map(struct kvm_vcpu *vcpu, gpa_t gpa, struct kvm_host_map *map,
		   bool writable);
void kvm_vcpu_unmap(struct kvm_vcpu *vcpu, struct kvm_host_map *map);

static inline int kvm_vcpu_map(struct kvm_vcpu *vcpu, gpa_t gpa,
			       struct kvm_host_map *map)
{
	return __kvm_vcpu_map(vcpu, gpa, map, true);
}

static inline int kvm_vcpu_map_readonly(struct kvm_vcpu *vcpu, gpa_t gpa,
					struct kvm_host_map *map)
{
	return __kvm_vcpu_map(vcpu, gpa, map, false);
}

unsigned long kvm_vcpu_gfn_to_hva(struct kvm_vcpu *vcpu, gfn_t gfn);
unsigned long kvm_vcpu_gfn_to_hva_prot(struct kvm_vcpu *vcpu, gfn_t gfn, bool *writable);
int kvm_vcpu_read_guest_page(struct kvm_vcpu *vcpu, gfn_t gfn, void *data, int offset,
			     int len);
int kvm_vcpu_read_guest_atomic(struct kvm_vcpu *vcpu, gpa_t gpa, void *data,
			       unsigned long len);
int kvm_vcpu_read_guest(struct kvm_vcpu *vcpu, gpa_t gpa, void *data,
			unsigned long len);
int kvm_vcpu_write_guest_page(struct kvm_vcpu *vcpu, gfn_t gfn, const void *data,
			      int offset, int len);
int kvm_vcpu_write_guest(struct kvm_vcpu *vcpu, gpa_t gpa, const void *data,
			 unsigned long len);
void kvm_vcpu_mark_page_dirty(struct kvm_vcpu *vcpu, gfn_t gfn);

/**
 * kvm_gpc_init - initialize gfn_to_pfn_cache.
 *
 * @gpc:	   struct gfn_to_pfn_cache object.
 * @kvm:	   pointer to kvm instance.
 *
 * This sets up a gfn_to_pfn_cache by initializing locks and assigning the
 * immutable attributes.  Note, the cache must be zero-allocated (or zeroed by
 * the caller before init).
 */
void kvm_gpc_init(struct gfn_to_pfn_cache *gpc, struct kvm *kvm);

/**
 * kvm_gpc_activate - prepare a cached kernel mapping and HPA for a given guest
 *                    physical address.
 *
 * @gpc:	   struct gfn_to_pfn_cache object.
 * @gpa:	   guest physical address to map.
 * @len:	   sanity check; the range being access must fit a single page.
 *
 * @return:	   0 for success.
 *		   -EINVAL for a mapping which would cross a page boundary.
 *		   -EFAULT for an untranslatable guest physical address.
 *
 * This primes a gfn_to_pfn_cache and links it into the @gpc->kvm's list for
 * invalidations to be processed.  Callers are required to use kvm_gpc_check()
 * to ensure that the cache is valid before accessing the target page.
 */
int kvm_gpc_activate(struct gfn_to_pfn_cache *gpc, gpa_t gpa, unsigned long len);

/**
 * kvm_gpc_activate_hva - prepare a cached kernel mapping and HPA for a given HVA.
 *
 * @gpc:          struct gfn_to_pfn_cache object.
 * @hva:          userspace virtual address to map.
 * @len:          sanity check; the range being access must fit a single page.
 *
 * @return:       0 for success.
 *                -EINVAL for a mapping which would cross a page boundary.
 *                -EFAULT for an untranslatable guest physical address.
 *
 * The semantics of this function are the same as those of kvm_gpc_activate(). It
 * merely bypasses a layer of address translation.
 */
int kvm_gpc_activate_hva(struct gfn_to_pfn_cache *gpc, unsigned long hva, unsigned long len);

/**
 * kvm_gpc_check - check validity of a gfn_to_pfn_cache.
 *
 * @gpc:	   struct gfn_to_pfn_cache object.
 * @len:	   sanity check; the range being access must fit a single page.
 *
 * @return:	   %true if the cache is still valid and the address matches.
 *		   %false if the cache is not valid.
 *
 * Callers outside IN_GUEST_MODE context should hold a read lock on @gpc->lock
 * while calling this function, and then continue to hold the lock until the
 * access is complete.
 *
 * Callers in IN_GUEST_MODE may do so without locking, although they should
 * still hold a read lock on kvm->scru for the memslot checks.
 */
bool kvm_gpc_check(struct gfn_to_pfn_cache *gpc, unsigned long len);

/**
 * kvm_gpc_refresh - update a previously initialized cache.
 *
 * @gpc:	   struct gfn_to_pfn_cache object.
 * @len:	   sanity check; the range being access must fit a single page.
 *
 * @return:	   0 for success.
 *		   -EINVAL for a mapping which would cross a page boundary.
 *		   -EFAULT for an untranslatable guest physical address.
 *
 * This will attempt to refresh a gfn_to_pfn_cache. Note that a successful
 * return from this function does not mean the page can be immediately
 * accessed because it may have raced with an invalidation. Callers must
 * still lock and check the cache status, as this function does not return
 * with the lock still held to permit access.
 */
int kvm_gpc_refresh(struct gfn_to_pfn_cache *gpc, unsigned long len);

/**
 * kvm_gpc_deactivate - deactivate and unlink a gfn_to_pfn_cache.
 *
 * @gpc:	   struct gfn_to_pfn_cache object.
 *
 * This removes a cache from the VM's list to be processed on MMU notifier
 * invocation.
 */
void kvm_gpc_deactivate(struct gfn_to_pfn_cache *gpc);

static inline bool kvm_gpc_is_gpa_active(struct gfn_to_pfn_cache *gpc)
{
	return gpc->active && !kvm_is_error_gpa(gpc->gpa);
}

static inline bool kvm_gpc_is_hva_active(struct gfn_to_pfn_cache *gpc)
{
	return gpc->active && kvm_is_error_gpa(gpc->gpa);
}

void kvm_sigset_activate(struct kvm_vcpu *vcpu);
void kvm_sigset_deactivate(struct kvm_vcpu *vcpu);

void kvm_vcpu_halt(struct kvm_vcpu *vcpu);
bool kvm_vcpu_block(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu);
bool kvm_vcpu_wake_up(struct kvm_vcpu *vcpu);
void kvm_vcpu_kick(struct kvm_vcpu *vcpu);
int kvm_vcpu_yield_to(struct kvm_vcpu *target);
void kvm_vcpu_on_spin(struct kvm_vcpu *vcpu, bool yield_to_kernel_mode);

void kvm_flush_remote_tlbs(struct kvm *kvm);
void kvm_flush_remote_tlbs_range(struct kvm *kvm, gfn_t gfn, u64 nr_pages);
void kvm_flush_remote_tlbs_memslot(struct kvm *kvm,
				   const struct kvm_memory_slot *memslot);

#ifdef KVM_ARCH_NR_OBJS_PER_MEMORY_CACHE
int kvm_mmu_topup_memory_cache(struct kvm_mmu_memory_cache *mc, int min);
int __kvm_mmu_topup_memory_cache(struct kvm_mmu_memory_cache *mc, int capacity, int min);
int kvm_mmu_memory_cache_nr_free_objects(struct kvm_mmu_memory_cache *mc);
void kvm_mmu_free_memory_cache(struct kvm_mmu_memory_cache *mc);
void *kvm_mmu_memory_cache_alloc(struct kvm_mmu_memory_cache *mc);
#endif

void kvm_mmu_invalidate_begin(struct kvm *kvm);
void kvm_mmu_invalidate_range_add(struct kvm *kvm, gfn_t start, gfn_t end);
void kvm_mmu_invalidate_end(struct kvm *kvm);
bool kvm_mmu_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range);

long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg);
long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg);
vm_fault_t kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf);

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext);

void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *kvm,
					struct kvm_memory_slot *slot,
					gfn_t gfn_offset,
					unsigned long mask);
void kvm_arch_sync_dirty_log(struct kvm *kvm, struct kvm_memory_slot *memslot);

#ifndef CONFIG_KVM_GENERIC_DIRTYLOG_READ_PROTECT
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log);
int kvm_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log,
		      int *is_dirty, struct kvm_memory_slot **memslot);
#endif

int kvm_vm_ioctl_irq_line(struct kvm *kvm, struct kvm_irq_level *irq_level,
			bool line_status);
int kvm_vm_ioctl_enable_cap(struct kvm *kvm,
			    struct kvm_enable_cap *cap);
int kvm_arch_vm_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg);
long kvm_arch_vm_compat_ioctl(struct file *filp, unsigned int ioctl,
			      unsigned long arg);

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu);
int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu);

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
				    struct kvm_translation *tr);

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs);
int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs);
int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs);
int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs);
int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state);
int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state);
int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg);
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu);

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu);
int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id);
int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu);

#ifdef CONFIG_HAVE_KVM_PM_NOTIFIER
int kvm_arch_pm_notifier(struct kvm *kvm, unsigned long state);
#endif

#ifdef __KVM_HAVE_ARCH_VCPU_DEBUGFS
void kvm_arch_create_vcpu_debugfs(struct kvm_vcpu *vcpu, struct dentry *debugfs_dentry);
#else
static inline void kvm_create_vcpu_debugfs(struct kvm_vcpu *vcpu) {}
#endif

#ifdef CONFIG_KVM_GENERIC_HARDWARE_ENABLING
/*
 * kvm_arch_{enable,disable}_virtualization() are called on one CPU, under
 * kvm_usage_lock, immediately after/before 0=>1 and 1=>0 transitions of
 * kvm_usage_count, i.e. at the beginning of the generic hardware enabling
 * sequence, and at the end of the generic hardware disabling sequence.
 */
void kvm_arch_enable_virtualization(void);
void kvm_arch_disable_virtualization(void);
/*
 * kvm_arch_{enable,disable}_virtualization_cpu() are called on "every" CPU to
 * do the actual twiddling of hardware bits.  The hooks are called on all
 * online CPUs when KVM enables/disabled virtualization, and on a single CPU
 * when that CPU is onlined/offlined (including for Resume/Suspend).
 */
int kvm_arch_enable_virtualization_cpu(void);
void kvm_arch_disable_virtualization_cpu(void);
#endif
int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu);
bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu);
int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu);
bool kvm_arch_dy_runnable(struct kvm_vcpu *vcpu);
bool kvm_arch_dy_has_pending_interrupt(struct kvm_vcpu *vcpu);
bool kvm_arch_vcpu_preempted_in_kernel(struct kvm_vcpu *vcpu);
void kvm_arch_pre_destroy_vm(struct kvm *kvm);
void kvm_arch_create_vm_debugfs(struct kvm *kvm);

#ifndef __KVM_HAVE_ARCH_VM_ALLOC
/*
 * All architectures that want to use vzalloc currently also
 * need their own kvm_arch_alloc_vm implementation.
 */
static inline struct kvm *kvm_arch_alloc_vm(void)
{
	return kzalloc(sizeof(struct kvm), GFP_KERNEL_ACCOUNT);
}
#endif

static inline void __kvm_arch_free_vm(struct kvm *kvm)
{
	kvfree(kvm);
}

#ifndef __KVM_HAVE_ARCH_VM_FREE
static inline void kvm_arch_free_vm(struct kvm *kvm)
{
	__kvm_arch_free_vm(kvm);
}
#endif

#ifndef __KVM_HAVE_ARCH_FLUSH_REMOTE_TLBS
static inline int kvm_arch_flush_remote_tlbs(struct kvm *kvm)
{
	return -ENOTSUPP;
}
#else
int kvm_arch_flush_remote_tlbs(struct kvm *kvm);
#endif

#ifndef __KVM_HAVE_ARCH_FLUSH_REMOTE_TLBS_RANGE
static inline int kvm_arch_flush_remote_tlbs_range(struct kvm *kvm,
						    gfn_t gfn, u64 nr_pages)
{
	return -EOPNOTSUPP;
}
#else
int kvm_arch_flush_remote_tlbs_range(struct kvm *kvm, gfn_t gfn, u64 nr_pages);
#endif

#ifdef __KVM_HAVE_ARCH_NONCOHERENT_DMA
void kvm_arch_register_noncoherent_dma(struct kvm *kvm);
void kvm_arch_unregister_noncoherent_dma(struct kvm *kvm);
bool kvm_arch_has_noncoherent_dma(struct kvm *kvm);
#else
static inline void kvm_arch_register_noncoherent_dma(struct kvm *kvm)
{
}

static inline void kvm_arch_unregister_noncoherent_dma(struct kvm *kvm)
{
}

static inline bool kvm_arch_has_noncoherent_dma(struct kvm *kvm)
{
	return false;
}
#endif
#ifdef __KVM_HAVE_ARCH_ASSIGNED_DEVICE
void kvm_arch_start_assignment(struct kvm *kvm);
void kvm_arch_end_assignment(struct kvm *kvm);
bool kvm_arch_has_assigned_device(struct kvm *kvm);
#else
static inline void kvm_arch_start_assignment(struct kvm *kvm)
{
}

static inline void kvm_arch_end_assignment(struct kvm *kvm)
{
}

static __always_inline bool kvm_arch_has_assigned_device(struct kvm *kvm)
{
	return false;
}
#endif

static inline struct rcuwait *kvm_arch_vcpu_get_wait(struct kvm_vcpu *vcpu)
{
#ifdef __KVM_HAVE_ARCH_WQP
	return vcpu->arch.waitp;
#else
	return &vcpu->wait;
#endif
}

/*
 * Wake a vCPU if necessary, but don't do any stats/metadata updates.  Returns
 * true if the vCPU was blocking and was awakened, false otherwise.
 */
static inline bool __kvm_vcpu_wake_up(struct kvm_vcpu *vcpu)
{
	return !!rcuwait_wake_up(kvm_arch_vcpu_get_wait(vcpu));
}

static inline bool kvm_vcpu_is_blocking(struct kvm_vcpu *vcpu)
{
	return rcuwait_active(kvm_arch_vcpu_get_wait(vcpu));
}

#ifdef __KVM_HAVE_ARCH_INTC_INITIALIZED
/*
 * returns true if the virtual interrupt controller is initialized and
 * ready to accept virtual IRQ. On some architectures the virtual interrupt
 * controller is dynamically instantiated and this is not always true.
 */
bool kvm_arch_intc_initialized(struct kvm *kvm);
#else
static inline bool kvm_arch_intc_initialized(struct kvm *kvm)
{
	return true;
}
#endif

#ifdef CONFIG_GUEST_PERF_EVENTS
unsigned long kvm_arch_vcpu_get_ip(struct kvm_vcpu *vcpu);

void kvm_register_perf_callbacks(unsigned int (*pt_intr_handler)(void));
void kvm_unregister_perf_callbacks(void);
#else
static inline void kvm_register_perf_callbacks(void *ign) {}
static inline void kvm_unregister_perf_callbacks(void) {}
#endif /* CONFIG_GUEST_PERF_EVENTS */

int kvm_arch_init_vm(struct kvm *kvm, unsigned long type);
void kvm_arch_destroy_vm(struct kvm *kvm);

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu);

struct kvm_irq_ack_notifier {
	struct hlist_node link;
	unsigned gsi;
	void (*irq_acked)(struct kvm_irq_ack_notifier *kian);
};

int kvm_irq_map_gsi(struct kvm *kvm,
		    struct kvm_kernel_irq_routing_entry *entries, int gsi);
int kvm_irq_map_chip_pin(struct kvm *kvm, unsigned irqchip, unsigned pin);

int kvm_set_irq(struct kvm *kvm, int irq_source_id, u32 irq, int level,
		bool line_status);
int kvm_set_msi(struct kvm_kernel_irq_routing_entry *irq_entry, struct kvm *kvm,
		int irq_source_id, int level, bool line_status);
int kvm_arch_set_irq_inatomic(struct kvm_kernel_irq_routing_entry *e,
			       struct kvm *kvm, int irq_source_id,
			       int level, bool line_status);
bool kvm_irq_has_notifier(struct kvm *kvm, unsigned irqchip, unsigned pin);
void kvm_notify_acked_gsi(struct kvm *kvm, int gsi);
void kvm_notify_acked_irq(struct kvm *kvm, unsigned irqchip, unsigned pin);
void kvm_register_irq_ack_notifier(struct kvm *kvm,
				   struct kvm_irq_ack_notifier *kian);
void kvm_unregister_irq_ack_notifier(struct kvm *kvm,
				   struct kvm_irq_ack_notifier *kian);
int kvm_request_irq_source_id(struct kvm *kvm);
void kvm_free_irq_source_id(struct kvm *kvm, int irq_source_id);
bool kvm_arch_irqfd_allowed(struct kvm *kvm, struct kvm_irqfd *args);

/*
 * Returns a pointer to the memslot if it contains gfn.
 * Otherwise returns NULL.
 */
static inline struct kvm_memory_slot *
try_get_memslot(struct kvm_memory_slot *slot, gfn_t gfn)
{
	if (!slot)
		return NULL;

	if (gfn >= slot->base_gfn && gfn < slot->base_gfn + slot->npages)
		return slot;
	else
		return NULL;
}

/*
 * Returns a pointer to the memslot that contains gfn. Otherwise returns NULL.
 *
 * With "approx" set returns the memslot also when the address falls
 * in a hole. In that case one of the memslots bordering the hole is
 * returned.
 */
static inline struct kvm_memory_slot *
search_memslots(struct kvm_memslots *slots, gfn_t gfn, bool approx)
{
	struct kvm_memory_slot *slot;
	struct rb_node *node;
	int idx = slots->node_idx;

	slot = NULL;
	for (node = slots->gfn_tree.rb_node; node; ) {
		slot = container_of(node, struct kvm_memory_slot, gfn_node[idx]);
		if (gfn >= slot->base_gfn) {
			if (gfn < slot->base_gfn + slot->npages)
				return slot;
			node = node->rb_right;
		} else
			node = node->rb_left;
	}

	return approx ? slot : NULL;
}

static inline struct kvm_memory_slot *
____gfn_to_memslot(struct kvm_memslots *slots, gfn_t gfn, bool approx)
{
	struct kvm_memory_slot *slot;

	slot = (struct kvm_memory_slot *)atomic_long_read(&slots->last_used_slot);
	slot = try_get_memslot(slot, gfn);
	if (slot)
		return slot;

	slot = search_memslots(slots, gfn, approx);
	if (slot) {
		atomic_long_set(&slots->last_used_slot, (unsigned long)slot);
		return slot;
	}

	return NULL;
}

/*
 * __gfn_to_memslot() and its descendants are here to allow arch code to inline
 * the lookups in hot paths.  gfn_to_memslot() itself isn't here as an inline
 * because that would bloat other code too much.
 */
static inline struct kvm_memory_slot *
__gfn_to_memslot(struct kvm_memslots *slots, gfn_t gfn)
{
	return ____gfn_to_memslot(slots, gfn, false);
}

static inline unsigned long
__gfn_to_hva_memslot(const struct kvm_memory_slot *slot, gfn_t gfn)
{
	/*
	 * The index was checked originally in search_memslots.  To avoid
	 * that a malicious guest builds a Spectre gadget out of e.g. page
	 * table walks, do not let the processor speculate loads outside
	 * the guest's registered memslots.
	 */
	unsigned long offset = gfn - slot->base_gfn;
	offset = array_index_nospec(offset, slot->npages);
	return slot->userspace_addr + offset * PAGE_SIZE;
}

static inline int memslot_id(struct kvm *kvm, gfn_t gfn)
{
	return gfn_to_memslot(kvm, gfn)->id;
}

static inline gfn_t
hva_to_gfn_memslot(unsigned long hva, struct kvm_memory_slot *slot)
{
	gfn_t gfn_offset = (hva - slot->userspace_addr) >> PAGE_SHIFT;

	return slot->base_gfn + gfn_offset;
}

static inline gpa_t gfn_to_gpa(gfn_t gfn)
{
	return (gpa_t)gfn << PAGE_SHIFT;
}

static inline gfn_t gpa_to_gfn(gpa_t gpa)
{
	return (gfn_t)(gpa >> PAGE_SHIFT);
}

static inline hpa_t pfn_to_hpa(kvm_pfn_t pfn)
{
	return (hpa_t)pfn << PAGE_SHIFT;
}

static inline bool kvm_is_gpa_in_memslot(struct kvm *kvm, gpa_t gpa)
{
	unsigned long hva = gfn_to_hva(kvm, gpa_to_gfn(gpa));

	return !kvm_is_error_hva(hva);
}

static inline void kvm_gpc_mark_dirty_in_slot(struct gfn_to_pfn_cache *gpc)
{
	lockdep_assert_held(&gpc->lock);

	if (!gpc->memslot)
		return;

	mark_page_dirty_in_slot(gpc->kvm, gpc->memslot, gpa_to_gfn(gpc->gpa));
}

enum kvm_stat_kind {
	KVM_STAT_VM,
	KVM_STAT_VCPU,
};

struct kvm_stat_data {
	struct kvm *kvm;
	const struct _kvm_stats_desc *desc;
	enum kvm_stat_kind kind;
};

struct _kvm_stats_desc {
	struct kvm_stats_desc desc;
	char name[KVM_STATS_NAME_SIZE];
};

#define STATS_DESC_COMMON(type, unit, base, exp, sz, bsz)		       \
	.flags = type | unit | base |					       \
		 BUILD_BUG_ON_ZERO(type & ~KVM_STATS_TYPE_MASK) |	       \
		 BUILD_BUG_ON_ZERO(unit & ~KVM_STATS_UNIT_MASK) |	       \
		 BUILD_BUG_ON_ZERO(base & ~KVM_STATS_BASE_MASK),	       \
	.exponent = exp,						       \
	.size = sz,							       \
	.bucket_size = bsz

#define VM_GENERIC_STATS_DESC(stat, type, unit, base, exp, sz, bsz)	       \
	{								       \
		{							       \
			STATS_DESC_COMMON(type, unit, base, exp, sz, bsz),     \
			.offset = offsetof(struct kvm_vm_stat, generic.stat)   \
		},							       \
		.name = #stat,						       \
	}
#define VCPU_GENERIC_STATS_DESC(stat, type, unit, base, exp, sz, bsz)	       \
	{								       \
		{							       \
			STATS_DESC_COMMON(type, unit, base, exp, sz, bsz),     \
			.offset = offsetof(struct kvm_vcpu_stat, generic.stat) \
		},							       \
		.name = #stat,						       \
	}
#define VM_STATS_DESC(stat, type, unit, base, exp, sz, bsz)		       \
	{								       \
		{							       \
			STATS_DESC_COMMON(type, unit, base, exp, sz, bsz),     \
			.offset = offsetof(struct kvm_vm_stat, stat)	       \
		},							       \
		.name = #stat,						       \
	}
#define VCPU_STATS_DESC(stat, type, unit, base, exp, sz, bsz)		       \
	{								       \
		{							       \
			STATS_DESC_COMMON(type, unit, base, exp, sz, bsz),     \
			.offset = offsetof(struct kvm_vcpu_stat, stat)	       \
		},							       \
		.name = #stat,						       \
	}
/* SCOPE: VM, VM_GENERIC, VCPU, VCPU_GENERIC */
#define STATS_DESC(SCOPE, stat, type, unit, base, exp, sz, bsz)		       \
	SCOPE##_STATS_DESC(stat, type, unit, base, exp, sz, bsz)

#define STATS_DESC_CUMULATIVE(SCOPE, name, unit, base, exponent)	       \
	STATS_DESC(SCOPE, name, KVM_STATS_TYPE_CUMULATIVE,		       \
		unit, base, exponent, 1, 0)
#define STATS_DESC_INSTANT(SCOPE, name, unit, base, exponent)		       \
	STATS_DESC(SCOPE, name, KVM_STATS_TYPE_INSTANT,			       \
		unit, base, exponent, 1, 0)
#define STATS_DESC_PEAK(SCOPE, name, unit, base, exponent)		       \
	STATS_DESC(SCOPE, name, KVM_STATS_TYPE_PEAK,			       \
		unit, base, exponent, 1, 0)
#define STATS_DESC_LINEAR_HIST(SCOPE, name, unit, base, exponent, sz, bsz)     \
	STATS_DESC(SCOPE, name, KVM_STATS_TYPE_LINEAR_HIST,		       \
		unit, base, exponent, sz, bsz)
#define STATS_DESC_LOG_HIST(SCOPE, name, unit, base, exponent, sz)	       \
	STATS_DESC(SCOPE, name, KVM_STATS_TYPE_LOG_HIST,		       \
		unit, base, exponent, sz, 0)

/* Cumulative counter, read/write */
#define STATS_DESC_COUNTER(SCOPE, name)					       \
	STATS_DESC_CUMULATIVE(SCOPE, name, KVM_STATS_UNIT_NONE,		       \
		KVM_STATS_BASE_POW10, 0)
/* Instantaneous counter, read only */
#define STATS_DESC_ICOUNTER(SCOPE, name)				       \
	STATS_DESC_INSTANT(SCOPE, name, KVM_STATS_UNIT_NONE,		       \
		KVM_STATS_BASE_POW10, 0)
/* Peak counter, read/write */
#define STATS_DESC_PCOUNTER(SCOPE, name)				       \
	STATS_DESC_PEAK(SCOPE, name, KVM_STATS_UNIT_NONE,		       \
		KVM_STATS_BASE_POW10, 0)

/* Instantaneous boolean value, read only */
#define STATS_DESC_IBOOLEAN(SCOPE, name)				       \
	STATS_DESC_INSTANT(SCOPE, name, KVM_STATS_UNIT_BOOLEAN,		       \
		KVM_STATS_BASE_POW10, 0)
/* Peak (sticky) boolean value, read/write */
#define STATS_DESC_PBOOLEAN(SCOPE, name)				       \
	STATS_DESC_PEAK(SCOPE, name, KVM_STATS_UNIT_BOOLEAN,		       \
		KVM_STATS_BASE_POW10, 0)

/* Cumulative time in nanosecond */
#define STATS_DESC_TIME_NSEC(SCOPE, name)				       \
	STATS_DESC_CUMULATIVE(SCOPE, name, KVM_STATS_UNIT_SECONDS,	       \
		KVM_STATS_BASE_POW10, -9)
/* Linear histogram for time in nanosecond */
#define STATS_DESC_LINHIST_TIME_NSEC(SCOPE, name, sz, bsz)		       \
	STATS_DESC_LINEAR_HIST(SCOPE, name, KVM_STATS_UNIT_SECONDS,	       \
		KVM_STATS_BASE_POW10, -9, sz, bsz)
/* Logarithmic histogram for time in nanosecond */
#define STATS_DESC_LOGHIST_TIME_NSEC(SCOPE, name, sz)			       \
	STATS_DESC_LOG_HIST(SCOPE, name, KVM_STATS_UNIT_SECONDS,	       \
		KVM_STATS_BASE_POW10, -9, sz)

#define KVM_GENERIC_VM_STATS()						       \
	STATS_DESC_COUNTER(VM_GENERIC, remote_tlb_flush),		       \
	STATS_DESC_COUNTER(VM_GENERIC, remote_tlb_flush_requests)

#define KVM_GENERIC_VCPU_STATS()					       \
	STATS_DESC_COUNTER(VCPU_GENERIC, halt_successful_poll),		       \
	STATS_DESC_COUNTER(VCPU_GENERIC, halt_attempted_poll),		       \
	STATS_DESC_COUNTER(VCPU_GENERIC, halt_poll_invalid),		       \
	STATS_DESC_COUNTER(VCPU_GENERIC, halt_wakeup),			       \
	STATS_DESC_TIME_NSEC(VCPU_GENERIC, halt_poll_success_ns),	       \
	STATS_DESC_TIME_NSEC(VCPU_GENERIC, halt_poll_fail_ns),		       \
	STATS_DESC_TIME_NSEC(VCPU_GENERIC, halt_wait_ns),		       \
	STATS_DESC_LOGHIST_TIME_NSEC(VCPU_GENERIC, halt_poll_success_hist,     \
			HALT_POLL_HIST_COUNT),				       \
	STATS_DESC_LOGHIST_TIME_NSEC(VCPU_GENERIC, halt_poll_fail_hist,	       \
			HALT_POLL_HIST_COUNT),				       \
	STATS_DESC_LOGHIST_TIME_NSEC(VCPU_GENERIC, halt_wait_hist,	       \
			HALT_POLL_HIST_COUNT),				       \
	STATS_DESC_IBOOLEAN(VCPU_GENERIC, blocking)

ssize_t kvm_stats_read(char *id, const struct kvm_stats_header *header,
		       const struct _kvm_stats_desc *desc,
		       void *stats, size_t size_stats,
		       char __user *user_buffer, size_t size, loff_t *offset);

/**
 * kvm_stats_linear_hist_update() - Update bucket value for linear histogram
 * statistics data.
 *
 * @data: start address of the stats data
 * @size: the number of bucket of the stats data
 * @value: the new value used to update the linear histogram's bucket
 * @bucket_size: the size (width) of a bucket
 */
static inline void kvm_stats_linear_hist_update(u64 *data, size_t size,
						u64 value, size_t bucket_size)
{
	size_t index = div64_u64(value, bucket_size);

	index = min(index, size - 1);
	++data[index];
}

/**
 * kvm_stats_log_hist_update() - Update bucket value for logarithmic histogram
 * statistics data.
 *
 * @data: start address of the stats data
 * @size: the number of bucket of the stats data
 * @value: the new value used to update the logarithmic histogram's bucket
 */
static inline void kvm_stats_log_hist_update(u64 *data, size_t size, u64 value)
{
	size_t index = fls64(value);

	index = min(index, size - 1);
	++data[index];
}

#define KVM_STATS_LINEAR_HIST_UPDATE(array, value, bsize)		       \
	kvm_stats_linear_hist_update(array, ARRAY_SIZE(array), value, bsize)
#define KVM_STATS_LOG_HIST_UPDATE(array, value)				       \
	kvm_stats_log_hist_update(array, ARRAY_SIZE(array), value)


extern const struct kvm_stats_header kvm_vm_stats_header;
extern const struct _kvm_stats_desc kvm_vm_stats_desc[];
extern const struct kvm_stats_header kvm_vcpu_stats_header;
extern const struct _kvm_stats_desc kvm_vcpu_stats_desc[];

#ifdef CONFIG_KVM_GENERIC_MMU_NOTIFIER
static inline int mmu_invalidate_retry(struct kvm *kvm, unsigned long mmu_seq)
{
	if (unlikely(kvm->mmu_invalidate_in_progress))
		return 1;
	/*
	 * Ensure the read of mmu_invalidate_in_progress happens before
	 * the read of mmu_invalidate_seq.  This interacts with the
	 * smp_wmb() in mmu_notifier_invalidate_range_end to make sure
	 * that the caller either sees the old (non-zero) value of
	 * mmu_invalidate_in_progress or the new (incremented) value of
	 * mmu_invalidate_seq.
	 *
	 * PowerPC Book3s HV KVM calls this under a per-page lock rather
	 * than under kvm->mmu_lock, for scalability, so can't rely on
	 * kvm->mmu_lock to keep things ordered.
	 */
	smp_rmb();
	if (kvm->mmu_invalidate_seq != mmu_seq)
		return 1;
	return 0;
}

static inline int mmu_invalidate_retry_gfn(struct kvm *kvm,
					   unsigned long mmu_seq,
					   gfn_t gfn)
{
	lockdep_assert_held(&kvm->mmu_lock);
	/*
	 * If mmu_invalidate_in_progress is non-zero, then the range maintained
	 * by kvm_mmu_notifier_invalidate_range_start contains all addresses
	 * that might be being invalidated. Note that it may include some false
	 * positives, due to shortcuts when handing concurrent invalidations.
	 */
	if (unlikely(kvm->mmu_invalidate_in_progress)) {
		/*
		 * Dropping mmu_lock after bumping mmu_invalidate_in_progress
		 * but before updating the range is a KVM bug.
		 */
		if (WARN_ON_ONCE(kvm->mmu_invalidate_range_start == INVALID_GPA ||
				 kvm->mmu_invalidate_range_end == INVALID_GPA))
			return 1;

		if (gfn >= kvm->mmu_invalidate_range_start &&
		    gfn < kvm->mmu_invalidate_range_end)
			return 1;
	}

	if (kvm->mmu_invalidate_seq != mmu_seq)
		return 1;
	return 0;
}

/*
 * This lockless version of the range-based retry check *must* be paired with a
 * call to the locked version after acquiring mmu_lock, i.e. this is safe to
 * use only as a pre-check to avoid contending mmu_lock.  This version *will*
 * get false negatives and false positives.
 */
static inline bool mmu_invalidate_retry_gfn_unsafe(struct kvm *kvm,
						   unsigned long mmu_seq,
						   gfn_t gfn)
{
	/*
	 * Use READ_ONCE() to ensure the in-progress flag and sequence counter
	 * are always read from memory, e.g. so that checking for retry in a
	 * loop won't result in an infinite retry loop.  Don't force loads for
	 * start+end, as the key to avoiding infinite retry loops is observing
	 * the 1=>0 transition of in-progress, i.e. getting false negatives
	 * due to stale start+end values is acceptable.
	 */
	if (unlikely(READ_ONCE(kvm->mmu_invalidate_in_progress)) &&
	    gfn >= kvm->mmu_invalidate_range_start &&
	    gfn < kvm->mmu_invalidate_range_end)
		return true;

	return READ_ONCE(kvm->mmu_invalidate_seq) != mmu_seq;
}
#endif

#ifdef CONFIG_HAVE_KVM_IRQ_ROUTING

#define KVM_MAX_IRQ_ROUTES 4096 /* might need extension/rework in the future */

bool kvm_arch_can_set_irq_routing(struct kvm *kvm);
int kvm_set_irq_routing(struct kvm *kvm,
			const struct kvm_irq_routing_entry *entries,
			unsigned nr,
			unsigned flags);
int kvm_init_irq_routing(struct kvm *kvm);
int kvm_set_routing_entry(struct kvm *kvm,
			  struct kvm_kernel_irq_routing_entry *e,
			  const struct kvm_irq_routing_entry *ue);
void kvm_free_irq_routing(struct kvm *kvm);

#else

static inline void kvm_free_irq_routing(struct kvm *kvm) {}

static inline int kvm_init_irq_routing(struct kvm *kvm)
{
	return 0;
}

#endif

int kvm_send_userspace_msi(struct kvm *kvm, struct kvm_msi *msi);

void kvm_eventfd_init(struct kvm *kvm);
int kvm_ioeventfd(struct kvm *kvm, struct kvm_ioeventfd *args);

#ifdef CONFIG_HAVE_KVM_IRQCHIP
int kvm_irqfd(struct kvm *kvm, struct kvm_irqfd *args);
void kvm_irqfd_release(struct kvm *kvm);
bool kvm_notify_irqfd_resampler(struct kvm *kvm,
				unsigned int irqchip,
				unsigned int pin);
void kvm_irq_routing_update(struct kvm *);
#else
static inline int kvm_irqfd(struct kvm *kvm, struct kvm_irqfd *args)
{
	return -EINVAL;
}

static inline void kvm_irqfd_release(struct kvm *kvm) {}

static inline bool kvm_notify_irqfd_resampler(struct kvm *kvm,
					      unsigned int irqchip,
					      unsigned int pin)
{
	return false;
}
#endif /* CONFIG_HAVE_KVM_IRQCHIP */

void kvm_arch_irq_routing_update(struct kvm *kvm);

static inline void __kvm_make_request(int req, struct kvm_vcpu *vcpu)
{
	/*
	 * Ensure the rest of the request is published to kvm_check_request's
	 * caller.  Paired with the smp_mb__after_atomic in kvm_check_request.
	 */
	smp_wmb();
	set_bit(req & KVM_REQUEST_MASK, (void *)&vcpu->requests);
}

static __always_inline void kvm_make_request(int req, struct kvm_vcpu *vcpu)
{
	/*
	 * Request that don't require vCPU action should never be logged in
	 * vcpu->requests.  The vCPU won't clear the request, so it will stay
	 * logged indefinitely and prevent the vCPU from entering the guest.
	 */
	BUILD_BUG_ON(!__builtin_constant_p(req) ||
		     (req & KVM_REQUEST_NO_ACTION));

	__kvm_make_request(req, vcpu);
}

static inline bool kvm_request_pending(struct kvm_vcpu *vcpu)
{
	return READ_ONCE(vcpu->requests);
}

static inline bool kvm_test_request(int req, struct kvm_vcpu *vcpu)
{
	return test_bit(req & KVM_REQUEST_MASK, (void *)&vcpu->requests);
}

static inline void kvm_clear_request(int req, struct kvm_vcpu *vcpu)
{
	clear_bit(req & KVM_REQUEST_MASK, (void *)&vcpu->requests);
}

static inline bool kvm_check_request(int req, struct kvm_vcpu *vcpu)
{
	if (kvm_test_request(req, vcpu)) {
		kvm_clear_request(req, vcpu);

		/*
		 * Ensure the rest of the request is visible to kvm_check_request's
		 * caller.  Paired with the smp_wmb in kvm_make_request.
		 */
		smp_mb__after_atomic();
		return true;
	} else {
		return false;
	}
}

#ifdef CONFIG_KVM_GENERIC_HARDWARE_ENABLING
extern bool kvm_rebooting;
#endif

extern unsigned int halt_poll_ns;
extern unsigned int halt_poll_ns_grow;
extern unsigned int halt_poll_ns_grow_start;
extern unsigned int halt_poll_ns_shrink;

struct kvm_device {
	const struct kvm_device_ops *ops;
	struct kvm *kvm;
	void *private;
	struct list_head vm_node;
};

/* create, destroy, and name are mandatory */
struct kvm_device_ops {
	const char *name;

	/*
	 * create is called holding kvm->lock and any operations not suitable
	 * to do while holding the lock should be deferred to init (see
	 * below).
	 */
	int (*create)(struct kvm_device *dev, u32 type);

	/*
	 * init is called after create if create is successful and is called
	 * outside of holding kvm->lock.
	 */
	void (*init)(struct kvm_device *dev);

	/*
	 * Destroy is responsible for freeing dev.
	 *
	 * Destroy may be called before or after destructors are called
	 * on emulated I/O regions, depending on whether a reference is
	 * held by a vcpu or other kvm component that gets destroyed
	 * after the emulated I/O.
	 */
	void (*destroy)(struct kvm_device *dev);

	/*
	 * Release is an alternative method to free the device. It is
	 * called when the device file descriptor is closed. Once
	 * release is called, the destroy method will not be called
	 * anymore as the device is removed from the device list of
	 * the VM. kvm->lock is held.
	 */
	void (*release)(struct kvm_device *dev);

	int (*set_attr)(struct kvm_device *dev, struct kvm_device_attr *attr);
	int (*get_attr)(struct kvm_device *dev, struct kvm_device_attr *attr);
	int (*has_attr)(struct kvm_device *dev, struct kvm_device_attr *attr);
	long (*ioctl)(struct kvm_device *dev, unsigned int ioctl,
		      unsigned long arg);
	int (*mmap)(struct kvm_device *dev, struct vm_area_struct *vma);
};

struct kvm_device *kvm_device_from_filp(struct file *filp);
int kvm_register_device_ops(const struct kvm_device_ops *ops, u32 type);
void kvm_unregister_device_ops(u32 type);

extern struct kvm_device_ops kvm_mpic_ops;
extern struct kvm_device_ops kvm_arm_vgic_v2_ops;
extern struct kvm_device_ops kvm_arm_vgic_v3_ops;

#ifdef CONFIG_HAVE_KVM_CPU_RELAX_INTERCEPT

static inline void kvm_vcpu_set_in_spin_loop(struct kvm_vcpu *vcpu, bool val)
{
	vcpu->spin_loop.in_spin_loop = val;
}
static inline void kvm_vcpu_set_dy_eligible(struct kvm_vcpu *vcpu, bool val)
{
	vcpu->spin_loop.dy_eligible = val;
}

#else /* !CONFIG_HAVE_KVM_CPU_RELAX_INTERCEPT */

static inline void kvm_vcpu_set_in_spin_loop(struct kvm_vcpu *vcpu, bool val)
{
}

static inline void kvm_vcpu_set_dy_eligible(struct kvm_vcpu *vcpu, bool val)
{
}
#endif /* CONFIG_HAVE_KVM_CPU_RELAX_INTERCEPT */

static inline bool kvm_is_visible_memslot(struct kvm_memory_slot *memslot)
{
	return (memslot && memslot->id < KVM_USER_MEM_SLOTS &&
		!(memslot->flags & KVM_MEMSLOT_INVALID));
}

struct kvm_vcpu *kvm_get_running_vcpu(void);
struct kvm_vcpu * __percpu *kvm_get_running_vcpus(void);

#if IS_ENABLED(CONFIG_HAVE_KVM_IRQ_BYPASS)
bool kvm_arch_has_irq_bypass(void);
int kvm_arch_irq_bypass_add_producer(struct irq_bypass_consumer *,
			   struct irq_bypass_producer *);
void kvm_arch_irq_bypass_del_producer(struct irq_bypass_consumer *,
			   struct irq_bypass_producer *);
void kvm_arch_irq_bypass_stop(struct irq_bypass_consumer *);
void kvm_arch_irq_bypass_start(struct irq_bypass_consumer *);
int kvm_arch_update_irqfd_routing(struct kvm *kvm, unsigned int host_irq,
				  uint32_t guest_irq, bool set);
bool kvm_arch_irqfd_route_changed(struct kvm_kernel_irq_routing_entry *,
				  struct kvm_kernel_irq_routing_entry *);
#endif /* CONFIG_HAVE_KVM_IRQ_BYPASS */

#ifdef CONFIG_HAVE_KVM_INVALID_WAKEUPS
/* If we wakeup during the poll time, was it a sucessful poll? */
static inline bool vcpu_valid_wakeup(struct kvm_vcpu *vcpu)
{
	return vcpu->valid_wakeup;
}

#else
static inline bool vcpu_valid_wakeup(struct kvm_vcpu *vcpu)
{
	return true;
}
#endif /* CONFIG_HAVE_KVM_INVALID_WAKEUPS */

#ifdef CONFIG_HAVE_KVM_NO_POLL
/* Callback that tells if we must not poll */
bool kvm_arch_no_poll(struct kvm_vcpu *vcpu);
#else
static inline bool kvm_arch_no_poll(struct kvm_vcpu *vcpu)
{
	return false;
}
#endif /* CONFIG_HAVE_KVM_NO_POLL */

#ifdef CONFIG_HAVE_KVM_VCPU_ASYNC_IOCTL
long kvm_arch_vcpu_async_ioctl(struct file *filp,
			       unsigned int ioctl, unsigned long arg);
#else
static inline long kvm_arch_vcpu_async_ioctl(struct file *filp,
					     unsigned int ioctl,
					     unsigned long arg)
{
	return -ENOIOCTLCMD;
}
#endif /* CONFIG_HAVE_KVM_VCPU_ASYNC_IOCTL */

void kvm_arch_guest_memory_reclaimed(struct kvm *kvm);

#ifdef CONFIG_HAVE_KVM_VCPU_RUN_PID_CHANGE
int kvm_arch_vcpu_run_pid_change(struct kvm_vcpu *vcpu);
#else
static inline int kvm_arch_vcpu_run_pid_change(struct kvm_vcpu *vcpu)
{
	return 0;
}
#endif /* CONFIG_HAVE_KVM_VCPU_RUN_PID_CHANGE */

#ifdef CONFIG_KVM_XFER_TO_GUEST_WORK
static inline void kvm_handle_signal_exit(struct kvm_vcpu *vcpu)
{
	vcpu->run->exit_reason = KVM_EXIT_INTR;
	vcpu->stat.signal_exits++;
}
#endif /* CONFIG_KVM_XFER_TO_GUEST_WORK */

/*
 * If more than one page is being (un)accounted, @virt must be the address of
 * the first page of a block of pages what were allocated together (i.e
 * accounted together).
 *
 * kvm_account_pgtable_pages() is thread-safe because mod_lruvec_page_state()
 * is thread-safe.
 */
static inline void kvm_account_pgtable_pages(void *virt, int nr)
{
	mod_lruvec_page_state(virt_to_page(virt), NR_SECONDARY_PAGETABLE, nr);
}

/*
 * This defines how many reserved entries we want to keep before we
 * kick the vcpu to the userspace to avoid dirty ring full.  This
 * value can be tuned to higher if e.g. PML is enabled on the host.
 */
#define  KVM_DIRTY_RING_RSVD_ENTRIES  64

/* Max number of entries allowed for each kvm dirty ring */
#define  KVM_DIRTY_RING_MAX_ENTRIES  65536

static inline void kvm_prepare_memory_fault_exit(struct kvm_vcpu *vcpu,
						 gpa_t gpa, gpa_t size,
						 bool is_write, bool is_exec,
						 bool is_private)
{
	vcpu->run->exit_reason = KVM_EXIT_MEMORY_FAULT;
	vcpu->run->memory_fault.gpa = gpa;
	vcpu->run->memory_fault.size = size;

	/* RWX flags are not (yet) defined or communicated to userspace. */
	vcpu->run->memory_fault.flags = 0;
	if (is_private)
		vcpu->run->memory_fault.flags |= KVM_MEMORY_EXIT_FLAG_PRIVATE;
}

#ifdef CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES
static inline unsigned long kvm_get_memory_attributes(struct kvm *kvm, gfn_t gfn)
{
	return xa_to_value(xa_load(&kvm->mem_attr_array, gfn));
}

bool kvm_range_has_memory_attributes(struct kvm *kvm, gfn_t start, gfn_t end,
				     unsigned long mask, unsigned long attrs);
bool kvm_arch_pre_set_memory_attributes(struct kvm *kvm,
					struct kvm_gfn_range *range);
bool kvm_arch_post_set_memory_attributes(struct kvm *kvm,
					 struct kvm_gfn_range *range);

static inline bool kvm_mem_is_private(struct kvm *kvm, gfn_t gfn)
{
	return IS_ENABLED(CONFIG_KVM_PRIVATE_MEM) &&
	       kvm_get_memory_attributes(kvm, gfn) & KVM_MEMORY_ATTRIBUTE_PRIVATE;
}
#else
static inline bool kvm_mem_is_private(struct kvm *kvm, gfn_t gfn)
{
	return false;
}
#endif /* CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES */

#ifdef CONFIG_KVM_PRIVATE_MEM
int kvm_gmem_get_pfn(struct kvm *kvm, struct kvm_memory_slot *slot,
		     gfn_t gfn, kvm_pfn_t *pfn, struct page **page,
		     int *max_order);
#else
static inline int kvm_gmem_get_pfn(struct kvm *kvm,
				   struct kvm_memory_slot *slot, gfn_t gfn,
				   kvm_pfn_t *pfn, struct page **page,
				   int *max_order)
{
	KVM_BUG_ON(1, kvm);
	return -EIO;
}
#endif /* CONFIG_KVM_PRIVATE_MEM */

#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_PREPARE
int kvm_arch_gmem_prepare(struct kvm *kvm, gfn_t gfn, kvm_pfn_t pfn, int max_order);
#endif

#ifdef CONFIG_KVM_GENERIC_PRIVATE_MEM
/**
 * kvm_gmem_populate() - Populate/prepare a GPA range with guest data
 *
 * @kvm: KVM instance
 * @gfn: starting GFN to be populated
 * @src: userspace-provided buffer containing data to copy into GFN range
 *       (passed to @post_populate, and incremented on each iteration
 *       if not NULL)
 * @npages: number of pages to copy from userspace-buffer
 * @post_populate: callback to issue for each gmem page that backs the GPA
 *                 range
 * @opaque: opaque data to pass to @post_populate callback
 *
 * This is primarily intended for cases where a gmem-backed GPA range needs
 * to be initialized with userspace-provided data prior to being mapped into
 * the guest as a private page. This should be called with the slots->lock
 * held so that caller-enforced invariants regarding the expected memory
 * attributes of the GPA range do not race with KVM_SET_MEMORY_ATTRIBUTES.
 *
 * Returns the number of pages that were populated.
 */
typedef int (*kvm_gmem_populate_cb)(struct kvm *kvm, gfn_t gfn, kvm_pfn_t pfn,
				    void __user *src, int order, void *opaque);

long kvm_gmem_populate(struct kvm *kvm, gfn_t gfn, void __user *src, long npages,
		       kvm_gmem_populate_cb post_populate, void *opaque);
#endif

#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_INVALIDATE
void kvm_arch_gmem_invalidate(kvm_pfn_t start, kvm_pfn_t end);
#endif

#ifdef CONFIG_KVM_GENERIC_PRE_FAULT_MEMORY
long kvm_arch_vcpu_pre_fault_memory(struct kvm_vcpu *vcpu,
				    struct kvm_pre_fault_memory *range);
#endif

#endif
