#ifndef __KVM_HOST_H
#define __KVM_HOST_H

/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <linux/types.h>
#include <linux/hardirq.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/preempt.h>
#include <linux/msi.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/ratelimit.h>
#include <linux/err.h>
#include <linux/irqflags.h>
#include <linux/context_tracking.h>
#include <linux/irqbypass.h>
#include <linux/swait.h>
#include <linux/refcount.h>
#include <asm/signal.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>

#include <linux/kvm_types.h>

#include <asm/kvm_host.h>

#ifndef KVM_MAX_VCPU_ID
#define KVM_MAX_VCPU_ID KVM_MAX_VCPUS
#endif

/*
 * The bit 16 ~ bit 31 of kvm_memory_region::flags are internally used
 * in kvm, other bits are visible for userspace which are defined in
 * include/linux/kvm_h.
 */
#define KVM_MEMSLOT_INVALID	(1UL << 16)

/* Two fragments for cross MMIO pages. */
#define KVM_MAX_MMIO_FRAGMENTS	2

#ifndef KVM_ADDRESS_SPACE_NUM
#define KVM_ADDRESS_SPACE_NUM	1
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

/*
 * error pfns indicate that the gfn is in slot but faild to
 * translate it to pfn on host.
 */
static inline bool is_error_pfn(kvm_pfn_t pfn)
{
	return !!(pfn & KVM_PFN_ERR_MASK);
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

#define KVM_ERR_PTR_BAD_PAGE	(ERR_PTR(-ENOENT))

static inline bool is_error_page(struct page *page)
{
	return IS_ERR(page);
}

#define KVM_REQUEST_MASK           GENMASK(7,0)
#define KVM_REQUEST_NO_WAKEUP      BIT(8)
#define KVM_REQUEST_WAIT           BIT(9)
/*
 * Architecture-independent vcpu->requests bit members
 * Bits 4-7 are reserved for more arch-independent bits.
 */
#define KVM_REQ_TLB_FLUSH         (0 | KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_MMU_RELOAD        (1 | KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_PENDING_TIMER     2
#define KVM_REQ_UNHALT            3
#define KVM_REQUEST_ARCH_BASE     8

#define KVM_ARCH_REQ_FLAGS(nr, flags) ({ \
	BUILD_BUG_ON((unsigned)(nr) >= 32 - KVM_REQUEST_ARCH_BASE); \
	(unsigned)(((nr) + KVM_REQUEST_ARCH_BASE) | (flags)); \
})
#define KVM_ARCH_REQ(nr)           KVM_ARCH_REQ_FLAGS(nr, 0)

#define KVM_USERSPACE_IRQ_SOURCE_ID		0
#define KVM_IRQFD_RESAMPLE_IRQ_SOURCE_ID	1

extern struct kmem_cache *kvm_vcpu_cache;

extern spinlock_t kvm_lock;
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
void kvm_io_bus_unregister_dev(struct kvm *kvm, enum kvm_bus bus_idx,
			       struct kvm_io_device *dev);
struct kvm_io_device *kvm_io_bus_get_dev(struct kvm *kvm, enum kvm_bus bus_idx,
					 gpa_t addr);

#ifdef CONFIG_KVM_ASYNC_PF
struct kvm_async_pf {
	struct work_struct work;
	struct list_head link;
	struct list_head queue;
	struct kvm_vcpu *vcpu;
	struct mm_struct *mm;
	gva_t gva;
	unsigned long addr;
	struct kvm_arch_async_pf arch;
	bool   wakeup_all;
};

void kvm_clear_async_pf_completion_queue(struct kvm_vcpu *vcpu);
void kvm_check_async_pf_completion(struct kvm_vcpu *vcpu);
int kvm_setup_async_pf(struct kvm_vcpu *vcpu, gva_t gva, unsigned long hva,
		       struct kvm_arch_async_pf *arch);
int kvm_async_pf_wakeup_all(struct kvm_vcpu *vcpu);
#endif

enum {
	OUTSIDE_GUEST_MODE,
	IN_GUEST_MODE,
	EXITING_GUEST_MODE,
	READING_SHADOW_PAGE_TABLES,
};

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
	int vcpu_id;
	int srcu_idx;
	int mode;
	unsigned long requests;
	unsigned long guest_debug;

	int pre_pcpu;
	struct list_head blocked_vcpu_list;

	struct mutex mutex;
	struct kvm_run *run;

	int guest_fpu_loaded, guest_xcr0_loaded;
	struct swait_queue_head wq;
	struct pid __rcu *pid;
	int sigset_active;
	sigset_t sigset;
	struct kvm_vcpu_stat stat;
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
	bool preempted;
	struct kvm_vcpu_arch arch;
	struct dentry *debugfs_dentry;
};

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

struct kvm_memory_slot {
	gfn_t base_gfn;
	unsigned long npages;
	unsigned long *dirty_bitmap;
	struct kvm_arch_memory_slot arch;
	unsigned long userspace_addr;
	u32 flags;
	short id;
};

static inline unsigned long kvm_dirty_bitmap_bytes(struct kvm_memory_slot *memslot)
{
	return ALIGN(memslot->npages, BITS_PER_LONG) / 8;
}

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
	struct hlist_head map[0];
};
#endif

#ifndef KVM_PRIVATE_MEM_SLOTS
#define KVM_PRIVATE_MEM_SLOTS 0
#endif

#ifndef KVM_MEM_SLOTS_NUM
#define KVM_MEM_SLOTS_NUM (KVM_USER_MEM_SLOTS + KVM_PRIVATE_MEM_SLOTS)
#endif

#ifndef __KVM_VCPU_MULTIPLE_ADDRESS_SPACE
static inline int kvm_arch_vcpu_memslots_id(struct kvm_vcpu *vcpu)
{
	return 0;
}
#endif

/*
 * Note:
 * memslots are not sorted by id anymore, please use id_to_memslot()
 * to get the memslot by its id.
 */
struct kvm_memslots {
	u64 generation;
	struct kvm_memory_slot memslots[KVM_MEM_SLOTS_NUM];
	/* The mapping table from slot id to the index in memslots[]. */
	short id_to_index[KVM_MEM_SLOTS_NUM];
	atomic_t lru_slot;
	int used_slots;
};

struct kvm {
	spinlock_t mmu_lock;
	struct mutex slots_lock;
	struct mm_struct *mm; /* userspace tied to this vm */
	struct kvm_memslots __rcu *memslots[KVM_ADDRESS_SPACE_NUM];
	struct kvm_vcpu *vcpus[KVM_MAX_VCPUS];

	/*
	 * created_vcpus is protected by kvm->lock, and is incremented
	 * at the beginning of KVM_CREATE_VCPU.  online_vcpus is only
	 * incremented after storing the kvm_vcpu pointer in vcpus,
	 * and is accessed atomically.
	 */
	atomic_t online_vcpus;
	int created_vcpus;
	int last_boosted_vcpu;
	struct list_head vm_list;
	struct mutex lock;
	struct kvm_io_bus __rcu *buses[KVM_NR_BUSES];
#ifdef CONFIG_HAVE_KVM_EVENTFD
	struct {
		spinlock_t        lock;
		struct list_head  items;
		struct list_head  resampler_list;
		struct mutex      resampler_lock;
	} irqfds;
	struct list_head ioeventfds;
#endif
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
#endif
#ifdef CONFIG_HAVE_KVM_IRQFD
	struct hlist_head irq_ack_notifier_list;
#endif

#if defined(CONFIG_MMU_NOTIFIER) && defined(KVM_ARCH_WANT_MMU_NOTIFIER)
	struct mmu_notifier mmu_notifier;
	unsigned long mmu_notifier_seq;
	long mmu_notifier_count;
#endif
	long tlbs_dirty;
	struct list_head devices;
	struct dentry *debugfs_dentry;
	struct kvm_stat_data **debugfs_stat_data;
	struct srcu_struct srcu;
	struct srcu_struct irq_srcu;
	pid_t userspace_pid;
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

static inline struct kvm_io_bus *kvm_get_bus(struct kvm *kvm, enum kvm_bus idx)
{
	return srcu_dereference_check(kvm->buses[idx], &kvm->srcu,
				      lockdep_is_held(&kvm->slots_lock));
}

static inline struct kvm_vcpu *kvm_get_vcpu(struct kvm *kvm, int i)
{
	/* Pairs with smp_wmb() in kvm_vm_ioctl_create_vcpu, in case
	 * the caller has read kvm->online_vcpus before (as is the case
	 * for kvm_for_each_vcpu, for example).
	 */
	smp_rmb();
	return kvm->vcpus[i];
}

#define kvm_for_each_vcpu(idx, vcpup, kvm) \
	for (idx = 0; \
	     idx < atomic_read(&kvm->online_vcpus) && \
	     (vcpup = kvm_get_vcpu(kvm, idx)) != NULL; \
	     idx++)

static inline struct kvm_vcpu *kvm_get_vcpu_by_id(struct kvm *kvm, int id)
{
	struct kvm_vcpu *vcpu = NULL;
	int i;

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

static inline int kvm_vcpu_get_idx(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu *tmp;
	int idx;

	kvm_for_each_vcpu(idx, tmp, vcpu->kvm)
		if (tmp == vcpu)
			return idx;
	BUG();
}

#define kvm_for_each_memslot(memslot, slots)	\
	for (memslot = &slots->memslots[0];	\
	      memslot < slots->memslots + KVM_MEM_SLOTS_NUM && memslot->npages;\
		memslot++)

int kvm_vcpu_init(struct kvm_vcpu *vcpu, struct kvm *kvm, unsigned id);
void kvm_vcpu_uninit(struct kvm_vcpu *vcpu);

int __must_check vcpu_load(struct kvm_vcpu *vcpu);
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

#ifdef CONFIG_HAVE_KVM_IRQFD
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
int kvm_init(void *opaque, unsigned vcpu_size, unsigned vcpu_align,
		  struct module *module);
void kvm_exit(void);

void kvm_get_kvm(struct kvm *kvm);
void kvm_put_kvm(struct kvm *kvm);

static inline struct kvm_memslots *__kvm_memslots(struct kvm *kvm, int as_id)
{
	return srcu_dereference_check(kvm->memslots[as_id], &kvm->srcu,
			lockdep_is_held(&kvm->slots_lock));
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

static inline struct kvm_memory_slot *
id_to_memslot(struct kvm_memslots *slots, int id)
{
	int index = slots->id_to_index[id];
	struct kvm_memory_slot *slot;

	slot = &slots->memslots[index];

	WARN_ON(slot->id != id);
	return slot;
}

/*
 * KVM_SET_USER_MEMORY_REGION ioctl allows the following operations:
 * - create a new memory slot
 * - delete an existing memory slot
 * - modify an existing memory slot
 *   -- move it in the guest physical memory space
 *   -- just change its flags
 *
 * Since flags can be changed by some of these operations, the following
 * differentiation is the best we can do for __kvm_set_memory_region():
 */
enum kvm_mr_change {
	KVM_MR_CREATE,
	KVM_MR_DELETE,
	KVM_MR_MOVE,
	KVM_MR_FLAGS_ONLY,
};

int kvm_set_memory_region(struct kvm *kvm,
			  const struct kvm_userspace_memory_region *mem);
int __kvm_set_memory_region(struct kvm *kvm,
			    const struct kvm_userspace_memory_region *mem);
void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *free,
			   struct kvm_memory_slot *dont);
int kvm_arch_create_memslot(struct kvm *kvm, struct kvm_memory_slot *slot,
			    unsigned long npages);
void kvm_arch_memslots_updated(struct kvm *kvm, struct kvm_memslots *slots);
int kvm_arch_prepare_memory_region(struct kvm *kvm,
				struct kvm_memory_slot *memslot,
				const struct kvm_userspace_memory_region *mem,
				enum kvm_mr_change change);
void kvm_arch_commit_memory_region(struct kvm *kvm,
				const struct kvm_userspace_memory_region *mem,
				const struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change);
bool kvm_largepages_enabled(void);
void kvm_disable_largepages(void);
/* flush all memory translations */
void kvm_arch_flush_shadow_all(struct kvm *kvm);
/* flush memory translations pointing to 'slot' */
void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot);

int gfn_to_page_many_atomic(struct kvm_memory_slot *slot, gfn_t gfn,
			    struct page **pages, int nr_pages);

struct page *gfn_to_page(struct kvm *kvm, gfn_t gfn);
unsigned long gfn_to_hva(struct kvm *kvm, gfn_t gfn);
unsigned long gfn_to_hva_prot(struct kvm *kvm, gfn_t gfn, bool *writable);
unsigned long gfn_to_hva_memslot(struct kvm_memory_slot *slot, gfn_t gfn);
unsigned long gfn_to_hva_memslot_prot(struct kvm_memory_slot *slot, gfn_t gfn,
				      bool *writable);
void kvm_release_page_clean(struct page *page);
void kvm_release_page_dirty(struct page *page);
void kvm_set_page_accessed(struct page *page);

kvm_pfn_t gfn_to_pfn_atomic(struct kvm *kvm, gfn_t gfn);
kvm_pfn_t gfn_to_pfn(struct kvm *kvm, gfn_t gfn);
kvm_pfn_t gfn_to_pfn_prot(struct kvm *kvm, gfn_t gfn, bool write_fault,
		      bool *writable);
kvm_pfn_t gfn_to_pfn_memslot(struct kvm_memory_slot *slot, gfn_t gfn);
kvm_pfn_t gfn_to_pfn_memslot_atomic(struct kvm_memory_slot *slot, gfn_t gfn);
kvm_pfn_t __gfn_to_pfn_memslot(struct kvm_memory_slot *slot, gfn_t gfn,
			       bool atomic, bool *async, bool write_fault,
			       bool *writable);

void kvm_release_pfn_clean(kvm_pfn_t pfn);
void kvm_set_pfn_dirty(kvm_pfn_t pfn);
void kvm_set_pfn_accessed(kvm_pfn_t pfn);
void kvm_get_pfn(kvm_pfn_t pfn);

int kvm_read_guest_page(struct kvm *kvm, gfn_t gfn, void *data, int offset,
			int len);
int kvm_read_guest_atomic(struct kvm *kvm, gpa_t gpa, void *data,
			  unsigned long len);
int kvm_read_guest(struct kvm *kvm, gpa_t gpa, void *data, unsigned long len);
int kvm_read_guest_cached(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
			   void *data, unsigned long len);
int kvm_write_guest_page(struct kvm *kvm, gfn_t gfn, const void *data,
			 int offset, int len);
int kvm_write_guest(struct kvm *kvm, gpa_t gpa, const void *data,
		    unsigned long len);
int kvm_write_guest_cached(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
			   void *data, unsigned long len);
int kvm_write_guest_offset_cached(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
			   void *data, int offset, unsigned long len);
int kvm_gfn_to_hva_cache_init(struct kvm *kvm, struct gfn_to_hva_cache *ghc,
			      gpa_t gpa, unsigned long len);
int kvm_clear_guest_page(struct kvm *kvm, gfn_t gfn, int offset, int len);
int kvm_clear_guest(struct kvm *kvm, gpa_t gpa, unsigned long len);
struct kvm_memory_slot *gfn_to_memslot(struct kvm *kvm, gfn_t gfn);
bool kvm_is_visible_gfn(struct kvm *kvm, gfn_t gfn);
unsigned long kvm_host_page_size(struct kvm *kvm, gfn_t gfn);
void mark_page_dirty(struct kvm *kvm, gfn_t gfn);

struct kvm_memslots *kvm_vcpu_memslots(struct kvm_vcpu *vcpu);
struct kvm_memory_slot *kvm_vcpu_gfn_to_memslot(struct kvm_vcpu *vcpu, gfn_t gfn);
kvm_pfn_t kvm_vcpu_gfn_to_pfn_atomic(struct kvm_vcpu *vcpu, gfn_t gfn);
kvm_pfn_t kvm_vcpu_gfn_to_pfn(struct kvm_vcpu *vcpu, gfn_t gfn);
struct page *kvm_vcpu_gfn_to_page(struct kvm_vcpu *vcpu, gfn_t gfn);
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

void kvm_vcpu_block(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu);
bool kvm_vcpu_wake_up(struct kvm_vcpu *vcpu);
void kvm_vcpu_kick(struct kvm_vcpu *vcpu);
int kvm_vcpu_yield_to(struct kvm_vcpu *target);
void kvm_vcpu_on_spin(struct kvm_vcpu *vcpu);
void kvm_load_guest_fpu(struct kvm_vcpu *vcpu);
void kvm_put_guest_fpu(struct kvm_vcpu *vcpu);

void kvm_flush_remote_tlbs(struct kvm *kvm);
void kvm_reload_remote_mmus(struct kvm *kvm);
bool kvm_make_all_cpus_request(struct kvm *kvm, unsigned int req);

long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg);
long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg);
int kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf);

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext);

int kvm_get_dirty_log(struct kvm *kvm,
			struct kvm_dirty_log *log, int *is_dirty);

int kvm_get_dirty_log_protect(struct kvm *kvm,
			struct kvm_dirty_log *log, bool *is_dirty);

void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *kvm,
					struct kvm_memory_slot *slot,
					gfn_t gfn_offset,
					unsigned long mask);

int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
				struct kvm_dirty_log *log);

int kvm_vm_ioctl_irq_line(struct kvm *kvm, struct kvm_irq_level *irq_level,
			bool line_status);
long kvm_arch_vm_ioctl(struct file *filp,
		       unsigned int ioctl, unsigned long arg);

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
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run);

int kvm_arch_init(void *opaque);
void kvm_arch_exit(void);

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu);

void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu);

void kvm_arch_vcpu_free(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu);
struct kvm_vcpu *kvm_arch_vcpu_create(struct kvm *kvm, unsigned int id);
int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu);

bool kvm_arch_has_vcpu_debugfs(void);
int kvm_arch_create_vcpu_debugfs(struct kvm_vcpu *vcpu);

int kvm_arch_hardware_enable(void);
void kvm_arch_hardware_disable(void);
int kvm_arch_hardware_setup(void);
void kvm_arch_hardware_unsetup(void);
void kvm_arch_check_processor_compat(void *rtn);
int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu);
int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu);

#ifndef __KVM_HAVE_ARCH_VM_ALLOC
static inline struct kvm *kvm_arch_alloc_vm(void)
{
	return kzalloc(sizeof(struct kvm), GFP_KERNEL);
}

static inline void kvm_arch_free_vm(struct kvm *kvm)
{
	kfree(kvm);
}
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

static inline bool kvm_arch_has_assigned_device(struct kvm *kvm)
{
	return false;
}
#endif

static inline struct swait_queue_head *kvm_arch_vcpu_wq(struct kvm_vcpu *vcpu)
{
#ifdef __KVM_HAVE_ARCH_WQP
	return vcpu->arch.wqp;
#else
	return &vcpu->wq;
#endif
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

int kvm_arch_init_vm(struct kvm *kvm, unsigned long type);
void kvm_arch_destroy_vm(struct kvm *kvm);
void kvm_arch_sync_events(struct kvm *kvm);

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu);
void kvm_vcpu_kick(struct kvm_vcpu *vcpu);

bool kvm_is_reserved_pfn(kvm_pfn_t pfn);

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

/*
 * search_memslots() and __gfn_to_memslot() are here because they are
 * used in non-modular code in arch/powerpc/kvm/book3s_hv_rm_mmu.c.
 * gfn_to_memslot() itself isn't here as an inline because that would
 * bloat other code too much.
 */
static inline struct kvm_memory_slot *
search_memslots(struct kvm_memslots *slots, gfn_t gfn)
{
	int start = 0, end = slots->used_slots;
	int slot = atomic_read(&slots->lru_slot);
	struct kvm_memory_slot *memslots = slots->memslots;

	if (gfn >= memslots[slot].base_gfn &&
	    gfn < memslots[slot].base_gfn + memslots[slot].npages)
		return &memslots[slot];

	while (start < end) {
		slot = start + (end - start) / 2;

		if (gfn >= memslots[slot].base_gfn)
			end = slot;
		else
			start = slot + 1;
	}

	if (gfn >= memslots[start].base_gfn &&
	    gfn < memslots[start].base_gfn + memslots[start].npages) {
		atomic_set(&slots->lru_slot, start);
		return &memslots[start];
	}

	return NULL;
}

static inline struct kvm_memory_slot *
__gfn_to_memslot(struct kvm_memslots *slots, gfn_t gfn)
{
	return search_memslots(slots, gfn);
}

static inline unsigned long
__gfn_to_hva_memslot(struct kvm_memory_slot *slot, gfn_t gfn)
{
	return slot->userspace_addr + (gfn - slot->base_gfn) * PAGE_SIZE;
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

static inline bool kvm_is_error_gpa(struct kvm *kvm, gpa_t gpa)
{
	unsigned long hva = gfn_to_hva(kvm, gpa_to_gfn(gpa));

	return kvm_is_error_hva(hva);
}

enum kvm_stat_kind {
	KVM_STAT_VM,
	KVM_STAT_VCPU,
};

struct kvm_stat_data {
	int offset;
	struct kvm *kvm;
};

struct kvm_stats_debugfs_item {
	const char *name;
	int offset;
	enum kvm_stat_kind kind;
};
extern struct kvm_stats_debugfs_item debugfs_entries[];
extern struct dentry *kvm_debugfs_dir;

#if defined(CONFIG_MMU_NOTIFIER) && defined(KVM_ARCH_WANT_MMU_NOTIFIER)
static inline int mmu_notifier_retry(struct kvm *kvm, unsigned long mmu_seq)
{
	if (unlikely(kvm->mmu_notifier_count))
		return 1;
	/*
	 * Ensure the read of mmu_notifier_count happens before the read
	 * of mmu_notifier_seq.  This interacts with the smp_wmb() in
	 * mmu_notifier_invalidate_range_end to make sure that the caller
	 * either sees the old (non-zero) value of mmu_notifier_count or
	 * the new (incremented) value of mmu_notifier_seq.
	 * PowerPC Book3s HV KVM calls this under a per-page lock
	 * rather than under kvm->mmu_lock, for scalability, so
	 * can't rely on kvm->mmu_lock to keep things ordered.
	 */
	smp_rmb();
	if (kvm->mmu_notifier_seq != mmu_seq)
		return 1;
	return 0;
}
#endif

#ifdef CONFIG_HAVE_KVM_IRQ_ROUTING

#ifdef CONFIG_S390
#define KVM_MAX_IRQ_ROUTES 4096 //FIXME: we can have more than that...
#elif defined(CONFIG_ARM64)
#define KVM_MAX_IRQ_ROUTES 4096
#else
#define KVM_MAX_IRQ_ROUTES 1024
#endif

bool kvm_arch_can_set_irq_routing(struct kvm *kvm);
int kvm_set_irq_routing(struct kvm *kvm,
			const struct kvm_irq_routing_entry *entries,
			unsigned nr,
			unsigned flags);
int kvm_set_routing_entry(struct kvm *kvm,
			  struct kvm_kernel_irq_routing_entry *e,
			  const struct kvm_irq_routing_entry *ue);
void kvm_free_irq_routing(struct kvm *kvm);

#else

static inline void kvm_free_irq_routing(struct kvm *kvm) {}

#endif

int kvm_send_userspace_msi(struct kvm *kvm, struct kvm_msi *msi);

#ifdef CONFIG_HAVE_KVM_EVENTFD

void kvm_eventfd_init(struct kvm *kvm);
int kvm_ioeventfd(struct kvm *kvm, struct kvm_ioeventfd *args);

#ifdef CONFIG_HAVE_KVM_IRQFD
int kvm_irqfd(struct kvm *kvm, struct kvm_irqfd *args);
void kvm_irqfd_release(struct kvm *kvm);
void kvm_irq_routing_update(struct kvm *);
#else
static inline int kvm_irqfd(struct kvm *kvm, struct kvm_irqfd *args)
{
	return -EINVAL;
}

static inline void kvm_irqfd_release(struct kvm *kvm) {}
#endif

#else

static inline void kvm_eventfd_init(struct kvm *kvm) {}

static inline int kvm_irqfd(struct kvm *kvm, struct kvm_irqfd *args)
{
	return -EINVAL;
}

static inline void kvm_irqfd_release(struct kvm *kvm) {}

#ifdef CONFIG_HAVE_KVM_IRQCHIP
static inline void kvm_irq_routing_update(struct kvm *kvm)
{
}
#endif
void kvm_arch_irq_routing_update(struct kvm *kvm);

static inline int kvm_ioeventfd(struct kvm *kvm, struct kvm_ioeventfd *args)
{
	return -ENOSYS;
}

#endif /* CONFIG_HAVE_KVM_EVENTFD */

static inline void kvm_make_request(int req, struct kvm_vcpu *vcpu)
{
	/*
	 * Ensure the rest of the request is published to kvm_check_request's
	 * caller.  Paired with the smp_mb__after_atomic in kvm_check_request.
	 */
	smp_wmb();
	set_bit(req & KVM_REQUEST_MASK, &vcpu->requests);
}

static inline bool kvm_request_pending(struct kvm_vcpu *vcpu)
{
	return READ_ONCE(vcpu->requests);
}

static inline bool kvm_test_request(int req, struct kvm_vcpu *vcpu)
{
	return test_bit(req & KVM_REQUEST_MASK, &vcpu->requests);
}

static inline void kvm_clear_request(int req, struct kvm_vcpu *vcpu)
{
	clear_bit(req & KVM_REQUEST_MASK, &vcpu->requests);
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

extern bool kvm_rebooting;

extern unsigned int halt_poll_ns;
extern unsigned int halt_poll_ns_grow;
extern unsigned int halt_poll_ns_shrink;

struct kvm_device {
	struct kvm_device_ops *ops;
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

	int (*set_attr)(struct kvm_device *dev, struct kvm_device_attr *attr);
	int (*get_attr)(struct kvm_device *dev, struct kvm_device_attr *attr);
	int (*has_attr)(struct kvm_device *dev, struct kvm_device_attr *attr);
	long (*ioctl)(struct kvm_device *dev, unsigned int ioctl,
		      unsigned long arg);
};

void kvm_device_get(struct kvm_device *dev);
void kvm_device_put(struct kvm_device *dev);
struct kvm_device *kvm_device_from_filp(struct file *filp);
int kvm_register_device_ops(struct kvm_device_ops *ops, u32 type);
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

#ifdef CONFIG_HAVE_KVM_IRQ_BYPASS
bool kvm_arch_has_irq_bypass(void);
int kvm_arch_irq_bypass_add_producer(struct irq_bypass_consumer *,
			   struct irq_bypass_producer *);
void kvm_arch_irq_bypass_del_producer(struct irq_bypass_consumer *,
			   struct irq_bypass_producer *);
void kvm_arch_irq_bypass_stop(struct irq_bypass_consumer *);
void kvm_arch_irq_bypass_start(struct irq_bypass_consumer *);
int kvm_arch_update_irqfd_routing(struct kvm *kvm, unsigned int host_irq,
				  uint32_t guest_irq, bool set);
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

#endif
