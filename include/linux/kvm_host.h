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
#include <linux/mm.h>
#include <linux/preempt.h>
#include <linux/marker.h>
#include <asm/signal.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>

#include <linux/kvm_types.h>

#include <asm/kvm_host.h>

/*
 * vcpu->requests bit members
 */
#define KVM_REQ_TLB_FLUSH          0
#define KVM_REQ_MIGRATE_TIMER      1
#define KVM_REQ_REPORT_TPR_ACCESS  2
#define KVM_REQ_MMU_RELOAD         3
#define KVM_REQ_TRIPLE_FAULT       4
#define KVM_REQ_PENDING_TIMER      5

struct kvm_vcpu;
extern struct kmem_cache *kvm_vcpu_cache;

/*
 * It would be nice to use something smarter than a linear search, TBD...
 * Thankfully we dont expect many devices to register (famous last words :),
 * so until then it will suffice.  At least its abstracted so we can change
 * in one place.
 */
struct kvm_io_bus {
	int                   dev_count;
#define NR_IOBUS_DEVS 6
	struct kvm_io_device *devs[NR_IOBUS_DEVS];
};

void kvm_io_bus_init(struct kvm_io_bus *bus);
void kvm_io_bus_destroy(struct kvm_io_bus *bus);
struct kvm_io_device *kvm_io_bus_find_dev(struct kvm_io_bus *bus, gpa_t addr);
void kvm_io_bus_register_dev(struct kvm_io_bus *bus,
			     struct kvm_io_device *dev);

struct kvm_vcpu {
	struct kvm *kvm;
#ifdef CONFIG_PREEMPT_NOTIFIERS
	struct preempt_notifier preempt_notifier;
#endif
	int vcpu_id;
	struct mutex mutex;
	int   cpu;
	struct kvm_run *run;
	int guest_mode;
	unsigned long requests;
	struct kvm_guest_debug guest_debug;
	int fpu_active;
	int guest_fpu_loaded;
	wait_queue_head_t wq;
	int sigset_active;
	sigset_t sigset;
	struct kvm_vcpu_stat stat;

#ifdef CONFIG_HAS_IOMEM
	int mmio_needed;
	int mmio_read_completed;
	int mmio_is_write;
	int mmio_size;
	unsigned char mmio_data[8];
	gpa_t mmio_phys_addr;
#endif

	struct kvm_vcpu_arch arch;
};

struct kvm_memory_slot {
	gfn_t base_gfn;
	unsigned long npages;
	unsigned long flags;
	unsigned long *rmap;
	unsigned long *dirty_bitmap;
	struct {
		unsigned long rmap_pde;
		int write_count;
	} *lpage_info;
	unsigned long userspace_addr;
	int user_alloc;
};

struct kvm {
	struct mutex lock; /* protects the vcpus array and APIC accesses */
	spinlock_t mmu_lock;
	struct rw_semaphore slots_lock;
	struct mm_struct *mm; /* userspace tied to this vm */
	int nmemslots;
	struct kvm_memory_slot memslots[KVM_MEMORY_SLOTS +
					KVM_PRIVATE_MEM_SLOTS];
	struct kvm_vcpu *vcpus[KVM_MAX_VCPUS];
	struct list_head vm_list;
	struct kvm_io_bus mmio_bus;
	struct kvm_io_bus pio_bus;
	struct kvm_vm_stat stat;
	struct kvm_arch arch;
	atomic_t users_count;
};

/* The guest did something we don't support. */
#define pr_unimpl(vcpu, fmt, ...)					\
 do {									\
	if (printk_ratelimit())						\
		printk(KERN_ERR "kvm: %i: cpu%i " fmt,			\
		       current->tgid, (vcpu)->vcpu_id , ## __VA_ARGS__); \
 } while (0)

#define kvm_printf(kvm, fmt ...) printk(KERN_DEBUG fmt)
#define vcpu_printf(vcpu, fmt...) kvm_printf(vcpu->kvm, fmt)

int kvm_vcpu_init(struct kvm_vcpu *vcpu, struct kvm *kvm, unsigned id);
void kvm_vcpu_uninit(struct kvm_vcpu *vcpu);

void vcpu_load(struct kvm_vcpu *vcpu);
void vcpu_put(struct kvm_vcpu *vcpu);

void decache_vcpus_on_cpu(int cpu);


int kvm_init(void *opaque, unsigned int vcpu_size,
		  struct module *module);
void kvm_exit(void);

void kvm_get_kvm(struct kvm *kvm);
void kvm_put_kvm(struct kvm *kvm);

#define HPA_MSB ((sizeof(hpa_t) * 8) - 1)
#define HPA_ERR_MASK ((hpa_t)1 << HPA_MSB)
static inline int is_error_hpa(hpa_t hpa) { return hpa >> HPA_MSB; }
struct page *gva_to_page(struct kvm_vcpu *vcpu, gva_t gva);

extern struct page *bad_page;
extern pfn_t bad_pfn;

int is_error_page(struct page *page);
int is_error_pfn(pfn_t pfn);
int kvm_is_error_hva(unsigned long addr);
int kvm_set_memory_region(struct kvm *kvm,
			  struct kvm_userspace_memory_region *mem,
			  int user_alloc);
int __kvm_set_memory_region(struct kvm *kvm,
			    struct kvm_userspace_memory_region *mem,
			    int user_alloc);
int kvm_arch_set_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem,
				struct kvm_memory_slot old,
				int user_alloc);
gfn_t unalias_gfn(struct kvm *kvm, gfn_t gfn);
struct page *gfn_to_page(struct kvm *kvm, gfn_t gfn);
unsigned long gfn_to_hva(struct kvm *kvm, gfn_t gfn);
void kvm_release_page_clean(struct page *page);
void kvm_release_page_dirty(struct page *page);
void kvm_set_page_dirty(struct page *page);
void kvm_set_page_accessed(struct page *page);

pfn_t gfn_to_pfn(struct kvm *kvm, gfn_t gfn);
void kvm_release_pfn_dirty(pfn_t);
void kvm_release_pfn_clean(pfn_t pfn);
void kvm_set_pfn_dirty(pfn_t pfn);
void kvm_set_pfn_accessed(pfn_t pfn);
void kvm_get_pfn(pfn_t pfn);

int kvm_read_guest_page(struct kvm *kvm, gfn_t gfn, void *data, int offset,
			int len);
int kvm_read_guest_atomic(struct kvm *kvm, gpa_t gpa, void *data,
			  unsigned long len);
int kvm_read_guest(struct kvm *kvm, gpa_t gpa, void *data, unsigned long len);
int kvm_write_guest_page(struct kvm *kvm, gfn_t gfn, const void *data,
			 int offset, int len);
int kvm_write_guest(struct kvm *kvm, gpa_t gpa, const void *data,
		    unsigned long len);
int kvm_clear_guest_page(struct kvm *kvm, gfn_t gfn, int offset, int len);
int kvm_clear_guest(struct kvm *kvm, gpa_t gpa, unsigned long len);
struct kvm_memory_slot *gfn_to_memslot(struct kvm *kvm, gfn_t gfn);
int kvm_is_visible_gfn(struct kvm *kvm, gfn_t gfn);
void mark_page_dirty(struct kvm *kvm, gfn_t gfn);

void kvm_vcpu_block(struct kvm_vcpu *vcpu);
void kvm_resched(struct kvm_vcpu *vcpu);
void kvm_load_guest_fpu(struct kvm_vcpu *vcpu);
void kvm_put_guest_fpu(struct kvm_vcpu *vcpu);
void kvm_flush_remote_tlbs(struct kvm *kvm);
void kvm_reload_remote_mmus(struct kvm *kvm);

long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg);
long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg);
void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu);

int kvm_dev_ioctl_check_extension(long ext);

int kvm_get_dirty_log(struct kvm *kvm,
			struct kvm_dirty_log *log, int *is_dirty);
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
				struct kvm_dirty_log *log);

int kvm_vm_ioctl_set_memory_region(struct kvm *kvm,
				   struct
				   kvm_userspace_memory_region *mem,
				   int user_alloc);
long kvm_arch_vm_ioctl(struct file *filp,
		       unsigned int ioctl, unsigned long arg);
void kvm_arch_destroy_vm(struct kvm *kvm);

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
int kvm_arch_vcpu_ioctl_debug_guest(struct kvm_vcpu *vcpu,
				    struct kvm_debug_guest *dbg);
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run);

int kvm_arch_init(void *opaque);
void kvm_arch_exit(void);

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu);

void kvm_arch_vcpu_free(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu);
struct kvm_vcpu *kvm_arch_vcpu_create(struct kvm *kvm, unsigned int id);
int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu);

int kvm_arch_vcpu_reset(struct kvm_vcpu *vcpu);
void kvm_arch_hardware_enable(void *garbage);
void kvm_arch_hardware_disable(void *garbage);
int kvm_arch_hardware_setup(void);
void kvm_arch_hardware_unsetup(void);
void kvm_arch_check_processor_compat(void *rtn);
int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu);

void kvm_free_physmem(struct kvm *kvm);

struct  kvm *kvm_arch_create_vm(void);
void kvm_arch_destroy_vm(struct kvm *kvm);

int kvm_cpu_get_interrupt(struct kvm_vcpu *v);
int kvm_cpu_has_interrupt(struct kvm_vcpu *v);
int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu);
void kvm_vcpu_kick(struct kvm_vcpu *vcpu);

static inline void kvm_guest_enter(void)
{
	account_system_vtime(current);
	current->flags |= PF_VCPU;
}

static inline void kvm_guest_exit(void)
{
	account_system_vtime(current);
	current->flags &= ~PF_VCPU;
}

static inline int memslot_id(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	return slot - kvm->memslots;
}

static inline gpa_t gfn_to_gpa(gfn_t gfn)
{
	return (gpa_t)gfn << PAGE_SHIFT;
}

static inline void kvm_migrate_timers(struct kvm_vcpu *vcpu)
{
	set_bit(KVM_REQ_MIGRATE_TIMER, &vcpu->requests);
}

enum kvm_stat_kind {
	KVM_STAT_VM,
	KVM_STAT_VCPU,
};

struct kvm_stats_debugfs_item {
	const char *name;
	int offset;
	enum kvm_stat_kind kind;
	struct dentry *dentry;
};
extern struct kvm_stats_debugfs_item debugfs_entries[];
extern struct dentry *kvm_debugfs_dir;

#ifdef CONFIG_KVM_TRACE
int kvm_trace_ioctl(unsigned int ioctl, unsigned long arg);
void kvm_trace_cleanup(void);
#else
static inline
int kvm_trace_ioctl(unsigned int ioctl, unsigned long arg)
{
	return -EINVAL;
}
#define kvm_trace_cleanup() ((void)0)
#endif

#endif
