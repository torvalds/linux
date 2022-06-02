/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/kvm_util_base.h
 *
 * Copyright (C) 2018, Google LLC.
 */
#ifndef SELFTEST_KVM_UTIL_BASE_H
#define SELFTEST_KVM_UTIL_BASE_H

#include "test_util.h"

#include <linux/compiler.h>
#include "linux/hashtable.h"
#include "linux/list.h"
#include <linux/kernel.h>
#include <linux/kvm.h>
#include "linux/rbtree.h"

#include <sys/ioctl.h>

#include "sparsebit.h"

#define KVM_DEV_PATH "/dev/kvm"
#define KVM_MAX_VCPUS 512

#define NSEC_PER_SEC 1000000000L

typedef uint64_t vm_paddr_t; /* Virtual Machine (Guest) physical address */
typedef uint64_t vm_vaddr_t; /* Virtual Machine (Guest) virtual address */

struct userspace_mem_region {
	struct kvm_userspace_memory_region region;
	struct sparsebit *unused_phy_pages;
	int fd;
	off_t offset;
	void *host_mem;
	void *host_alias;
	void *mmap_start;
	void *mmap_alias;
	size_t mmap_size;
	struct rb_node gpa_node;
	struct rb_node hva_node;
	struct hlist_node slot_node;
};

struct vcpu {
	struct list_head list;
	uint32_t id;
	int fd;
	struct kvm_vm *vm;
	struct kvm_run *state;
	struct kvm_dirty_gfn *dirty_gfns;
	uint32_t fetch_index;
	uint32_t dirty_gfns_count;
};

struct userspace_mem_regions {
	struct rb_root gpa_tree;
	struct rb_root hva_tree;
	DECLARE_HASHTABLE(slot_hash, 9);
};

struct kvm_vm {
	int mode;
	unsigned long type;
	int kvm_fd;
	int fd;
	unsigned int pgtable_levels;
	unsigned int page_size;
	unsigned int page_shift;
	unsigned int pa_bits;
	unsigned int va_bits;
	uint64_t max_gfn;
	struct list_head vcpus;
	struct userspace_mem_regions regions;
	struct sparsebit *vpages_valid;
	struct sparsebit *vpages_mapped;
	bool has_irqchip;
	bool pgd_created;
	vm_paddr_t pgd;
	vm_vaddr_t gdt;
	vm_vaddr_t tss;
	vm_vaddr_t idt;
	vm_vaddr_t handlers;
	uint32_t dirty_ring_size;
};


#define kvm_for_each_vcpu(vm, i, vcpu)			\
	for ((i) = 0; (i) <= (vm)->last_vcpu_id; (i)++)	\
		if (!((vcpu) = vm->vcpus[i]))		\
			continue;			\
		else

struct vcpu *vcpu_get(struct kvm_vm *vm, uint32_t vcpuid);

/*
 * Virtual Translation Tables Dump
 *
 * Input Args:
 *   stream - Output FILE stream
 *   vm     - Virtual Machine
 *   indent - Left margin indent amount
 *
 * Output Args: None
 *
 * Return: None
 *
 * Dumps to the FILE stream given by @stream, the contents of all the
 * virtual translation tables for the VM given by @vm.
 */
void virt_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent);

struct userspace_mem_region *
memslot2region(struct kvm_vm *vm, uint32_t memslot);

/* Minimum allocated guest virtual and physical addresses */
#define KVM_UTIL_MIN_VADDR		0x2000
#define KVM_GUEST_PAGE_TABLE_MIN_PADDR	0x180000

#define DEFAULT_GUEST_PHY_PAGES		512
#define DEFAULT_GUEST_STACK_VADDR_MIN	0xab6000
#define DEFAULT_STACK_PGS		5

enum vm_guest_mode {
	VM_MODE_P52V48_4K,
	VM_MODE_P52V48_64K,
	VM_MODE_P48V48_4K,
	VM_MODE_P48V48_16K,
	VM_MODE_P48V48_64K,
	VM_MODE_P40V48_4K,
	VM_MODE_P40V48_16K,
	VM_MODE_P40V48_64K,
	VM_MODE_PXXV48_4K,	/* For 48bits VA but ANY bits PA */
	VM_MODE_P47V64_4K,
	VM_MODE_P44V64_4K,
	VM_MODE_P36V48_4K,
	VM_MODE_P36V48_16K,
	VM_MODE_P36V48_64K,
	VM_MODE_P36V47_16K,
	NUM_VM_MODES,
};

#if defined(__aarch64__)

extern enum vm_guest_mode vm_mode_default;

#define VM_MODE_DEFAULT			vm_mode_default
#define MIN_PAGE_SHIFT			12U
#define ptes_per_page(page_size)	((page_size) / 8)

#elif defined(__x86_64__)

#define VM_MODE_DEFAULT			VM_MODE_PXXV48_4K
#define MIN_PAGE_SHIFT			12U
#define ptes_per_page(page_size)	((page_size) / 8)

#elif defined(__s390x__)

#define VM_MODE_DEFAULT			VM_MODE_P44V64_4K
#define MIN_PAGE_SHIFT			12U
#define ptes_per_page(page_size)	((page_size) / 16)

#elif defined(__riscv)

#if __riscv_xlen == 32
#error "RISC-V 32-bit kvm selftests not supported"
#endif

#define VM_MODE_DEFAULT			VM_MODE_P40V48_4K
#define MIN_PAGE_SHIFT			12U
#define ptes_per_page(page_size)	((page_size) / 8)

#endif

#define MIN_PAGE_SIZE		(1U << MIN_PAGE_SHIFT)
#define PTES_PER_MIN_PAGE	ptes_per_page(MIN_PAGE_SIZE)

struct vm_guest_mode_params {
	unsigned int pa_bits;
	unsigned int va_bits;
	unsigned int page_size;
	unsigned int page_shift;
};
extern const struct vm_guest_mode_params vm_guest_mode_params[];

int open_path_or_exit(const char *path, int flags);
int open_kvm_dev_path_or_exit(void);
int kvm_check_cap(long cap);

#define __KVM_SYSCALL_ERROR(_name, _ret) \
	"%s failed, rc: %i errno: %i (%s)", (_name), (_ret), errno, strerror(errno)

#define __KVM_IOCTL_ERROR(_name, _ret)	__KVM_SYSCALL_ERROR(_name, _ret)
#define KVM_IOCTL_ERROR(_ioctl, _ret) __KVM_IOCTL_ERROR(#_ioctl, _ret)

#define __kvm_ioctl(kvm_fd, cmd, arg) \
	ioctl(kvm_fd, cmd, arg)

static inline void _kvm_ioctl(int kvm_fd, unsigned long cmd, const char *name,
			      void *arg)
{
	int ret = __kvm_ioctl(kvm_fd, cmd, arg);

	TEST_ASSERT(!ret, __KVM_IOCTL_ERROR(name, ret));
}

#define kvm_ioctl(kvm_fd, cmd, arg) \
	_kvm_ioctl(kvm_fd, cmd, #cmd, arg)

int __vm_ioctl(struct kvm_vm *vm, unsigned long cmd, void *arg);
void _vm_ioctl(struct kvm_vm *vm, unsigned long cmd, const char *name, void *arg);
#define vm_ioctl(vm, cmd, arg) _vm_ioctl(vm, cmd, #cmd, arg)

int __vcpu_ioctl(struct kvm_vm *vm, uint32_t vcpuid, unsigned long cmd,
		 void *arg);
void _vcpu_ioctl(struct kvm_vm *vm, uint32_t vcpuid, unsigned long cmd,
		 const char *name, void *arg);
#define vcpu_ioctl(vm, vcpuid, cmd, arg) \
	_vcpu_ioctl(vm, vcpuid, cmd, #cmd, arg)

/*
 * Looks up and returns the value corresponding to the capability
 * (KVM_CAP_*) given by cap.
 */
static inline int vm_check_cap(struct kvm_vm *vm, long cap)
{
	int ret =  __vm_ioctl(vm, KVM_CHECK_EXTENSION, (void *)cap);

	TEST_ASSERT(ret >= 0, KVM_IOCTL_ERROR(KVM_CHECK_EXTENSION, ret));
	return ret;
}

static inline int __vm_enable_cap(struct kvm_vm *vm, uint32_t cap, uint64_t arg0)
{
	struct kvm_enable_cap enable_cap = { .cap = cap, .args = { arg0 } };

	return __vm_ioctl(vm, KVM_ENABLE_CAP, &enable_cap);
}
static inline void vm_enable_cap(struct kvm_vm *vm, uint32_t cap, uint64_t arg0)
{
	struct kvm_enable_cap enable_cap = { .cap = cap, .args = { arg0 } };

	vm_ioctl(vm, KVM_ENABLE_CAP, &enable_cap);
}

void vm_enable_dirty_ring(struct kvm_vm *vm, uint32_t ring_size);
const char *vm_guest_mode_string(uint32_t i);

struct kvm_vm *__vm_create(enum vm_guest_mode mode, uint64_t phy_pages);
struct kvm_vm *vm_create(uint64_t phy_pages);
void kvm_vm_free(struct kvm_vm *vmp);
void kvm_vm_restart(struct kvm_vm *vmp);
void kvm_vm_release(struct kvm_vm *vmp);
int kvm_memcmp_hva_gva(void *hva, struct kvm_vm *vm, const vm_vaddr_t gva,
		       size_t len);
void kvm_vm_elf_load(struct kvm_vm *vm, const char *filename);
int kvm_memfd_alloc(size_t size, bool hugepages);

void vm_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent);

static inline void kvm_vm_get_dirty_log(struct kvm_vm *vm, int slot, void *log)
{
	struct kvm_dirty_log args = { .dirty_bitmap = log, .slot = slot };

	vm_ioctl(vm, KVM_GET_DIRTY_LOG, &args);
}

static inline void kvm_vm_clear_dirty_log(struct kvm_vm *vm, int slot, void *log,
					  uint64_t first_page, uint32_t num_pages)
{
	struct kvm_clear_dirty_log args = {
		.dirty_bitmap = log,
		.slot = slot,
		.first_page = first_page,
		.num_pages = num_pages
	};

	vm_ioctl(vm, KVM_CLEAR_DIRTY_LOG, &args);
}

static inline uint32_t kvm_vm_reset_dirty_ring(struct kvm_vm *vm)
{
	return __vm_ioctl(vm, KVM_RESET_DIRTY_RINGS, NULL);
}

static inline int vm_get_stats_fd(struct kvm_vm *vm)
{
	int fd = __vm_ioctl(vm, KVM_GET_STATS_FD, NULL);

	TEST_ASSERT(fd >= 0, KVM_IOCTL_ERROR(KVM_GET_STATS_FD, fd));
	return fd;
}

/*
 * VM VCPU Dump
 *
 * Input Args:
 *   stream - Output FILE stream
 *   vm     - Virtual Machine
 *   vcpuid - VCPU ID
 *   indent - Left margin indent amount
 *
 * Output Args: None
 *
 * Return: None
 *
 * Dumps the current state of the VCPU specified by @vcpuid, within the VM
 * given by @vm, to the FILE stream given by @stream.
 */
void vcpu_dump(FILE *stream, struct kvm_vm *vm, uint32_t vcpuid,
	       uint8_t indent);

void vm_create_irqchip(struct kvm_vm *vm);

void vm_set_user_memory_region(struct kvm_vm *vm, uint32_t slot, uint32_t flags,
			       uint64_t gpa, uint64_t size, void *hva);
int __vm_set_user_memory_region(struct kvm_vm *vm, uint32_t slot, uint32_t flags,
				uint64_t gpa, uint64_t size, void *hva);
void vm_userspace_mem_region_add(struct kvm_vm *vm,
	enum vm_mem_backing_src_type src_type,
	uint64_t guest_paddr, uint32_t slot, uint64_t npages,
	uint32_t flags);

void vm_mem_region_set_flags(struct kvm_vm *vm, uint32_t slot, uint32_t flags);
void vm_mem_region_move(struct kvm_vm *vm, uint32_t slot, uint64_t new_gpa);
void vm_mem_region_delete(struct kvm_vm *vm, uint32_t slot);
void vm_vcpu_add(struct kvm_vm *vm, uint32_t vcpuid);
vm_vaddr_t vm_vaddr_alloc(struct kvm_vm *vm, size_t sz, vm_vaddr_t vaddr_min);
vm_vaddr_t vm_vaddr_alloc_pages(struct kvm_vm *vm, int nr_pages);
vm_vaddr_t vm_vaddr_alloc_page(struct kvm_vm *vm);

void virt_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
	      unsigned int npages);
void *addr_gpa2hva(struct kvm_vm *vm, vm_paddr_t gpa);
void *addr_gva2hva(struct kvm_vm *vm, vm_vaddr_t gva);
vm_paddr_t addr_hva2gpa(struct kvm_vm *vm, void *hva);
void *addr_gpa2alias(struct kvm_vm *vm, vm_paddr_t gpa);

/*
 * Address Guest Virtual to Guest Physical
 *
 * Input Args:
 *   vm - Virtual Machine
 *   gva - VM virtual address
 *
 * Output Args: None
 *
 * Return:
 *   Equivalent VM physical address
 *
 * Returns the VM physical address of the translated VM virtual
 * address given by @gva.
 */
vm_paddr_t addr_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva);

struct kvm_run *vcpu_state(struct kvm_vm *vm, uint32_t vcpuid);
void vcpu_run(struct kvm_vm *vm, uint32_t vcpuid);
int _vcpu_run(struct kvm_vm *vm, uint32_t vcpuid);

static inline int __vcpu_run(struct kvm_vm *vm, uint32_t vcpuid)
{
	return __vcpu_ioctl(vm, vcpuid, KVM_RUN, NULL);
}

void vcpu_run_complete_io(struct kvm_vm *vm, uint32_t vcpuid);
struct kvm_reg_list *vcpu_get_reg_list(struct kvm_vm *vm, uint32_t vcpuid);

static inline void vcpu_enable_cap(struct kvm_vm *vm, uint32_t vcpu_id,
				   uint32_t cap, uint64_t arg0)
{
	struct kvm_enable_cap enable_cap = { .cap = cap, .args = { arg0 } };

	vcpu_ioctl(vm, vcpu_id, KVM_ENABLE_CAP, &enable_cap);
}

static inline void vcpu_set_guest_debug(struct kvm_vm *vm, uint32_t vcpuid,
					struct kvm_guest_debug *debug)
{
	vcpu_ioctl(vm, vcpuid, KVM_SET_GUEST_DEBUG, debug);
}

static inline void vcpu_set_mp_state(struct kvm_vm *vm, uint32_t vcpuid,
				     struct kvm_mp_state *mp_state)
{
	vcpu_ioctl(vm, vcpuid, KVM_SET_MP_STATE, mp_state);
}

static inline void vcpu_regs_get(struct kvm_vm *vm, uint32_t vcpuid,
				 struct kvm_regs *regs)
{
	vcpu_ioctl(vm, vcpuid, KVM_GET_REGS, regs);
}

static inline void vcpu_regs_set(struct kvm_vm *vm, uint32_t vcpuid,
				 struct kvm_regs *regs)
{
	vcpu_ioctl(vm, vcpuid, KVM_SET_REGS, regs);
}
static inline void vcpu_sregs_get(struct kvm_vm *vm, uint32_t vcpuid,
				  struct kvm_sregs *sregs)
{
	vcpu_ioctl(vm, vcpuid, KVM_GET_SREGS, sregs);

}
static inline void vcpu_sregs_set(struct kvm_vm *vm, uint32_t vcpuid,
				  struct kvm_sregs *sregs)
{
	vcpu_ioctl(vm, vcpuid, KVM_SET_SREGS, sregs);
}
static inline int _vcpu_sregs_set(struct kvm_vm *vm, uint32_t vcpuid,
				  struct kvm_sregs *sregs)
{
	return __vcpu_ioctl(vm, vcpuid, KVM_SET_SREGS, sregs);
}
static inline void vcpu_fpu_get(struct kvm_vm *vm, uint32_t vcpuid,
				struct kvm_fpu *fpu)
{
	vcpu_ioctl(vm, vcpuid, KVM_GET_FPU, fpu);
}
static inline void vcpu_fpu_set(struct kvm_vm *vm, uint32_t vcpuid,
				struct kvm_fpu *fpu)
{
	vcpu_ioctl(vm, vcpuid, KVM_SET_FPU, fpu);
}
static inline void vcpu_get_reg(struct kvm_vm *vm, uint32_t vcpuid,
				struct kvm_one_reg *reg)
{
	vcpu_ioctl(vm, vcpuid, KVM_GET_ONE_REG, reg);
}
static inline void vcpu_set_reg(struct kvm_vm *vm, uint32_t vcpuid,
				struct kvm_one_reg *reg)
{
	vcpu_ioctl(vm, vcpuid, KVM_SET_ONE_REG, reg);
}
#ifdef __KVM_HAVE_VCPU_EVENTS
static inline void vcpu_events_get(struct kvm_vm *vm, uint32_t vcpuid,
				   struct kvm_vcpu_events *events)
{
	vcpu_ioctl(vm, vcpuid, KVM_GET_VCPU_EVENTS, events);
}
static inline void vcpu_events_set(struct kvm_vm *vm, uint32_t vcpuid,
				   struct kvm_vcpu_events *events)
{
	vcpu_ioctl(vm, vcpuid, KVM_SET_VCPU_EVENTS, events);
}
#endif
#ifdef __x86_64__
static inline void vcpu_nested_state_get(struct kvm_vm *vm, uint32_t vcpuid,
					 struct kvm_nested_state *state)
{
	vcpu_ioctl(vm, vcpuid, KVM_GET_NESTED_STATE, state);
}
static inline int __vcpu_nested_state_set(struct kvm_vm *vm, uint32_t vcpuid,
					  struct kvm_nested_state *state)
{
	return __vcpu_ioctl(vm, vcpuid, KVM_SET_NESTED_STATE, state);
}

static inline void vcpu_nested_state_set(struct kvm_vm *vm, uint32_t vcpuid,
					 struct kvm_nested_state *state)
{
	vcpu_ioctl(vm, vcpuid, KVM_SET_NESTED_STATE, state);
}
#endif
static inline int vcpu_get_stats_fd(struct kvm_vm *vm, uint32_t vcpuid)
{
	int fd = __vcpu_ioctl(vm, vcpuid, KVM_GET_STATS_FD, NULL);

	TEST_ASSERT(fd >= 0, KVM_IOCTL_ERROR(KVM_GET_STATS_FD, fd));
	return fd;
}

int __kvm_has_device_attr(int dev_fd, uint32_t group, uint64_t attr);

static inline void kvm_has_device_attr(int dev_fd, uint32_t group, uint64_t attr)
{
	int ret = __kvm_has_device_attr(dev_fd, group, attr);

	TEST_ASSERT(!ret, "KVM_HAS_DEVICE_ATTR failed, rc: %i errno: %i", ret, errno);
}

int __kvm_device_attr_get(int dev_fd, uint32_t group, uint64_t attr, void *val);

static inline void kvm_device_attr_get(int dev_fd, uint32_t group,
				       uint64_t attr, void *val)
{
	int ret = __kvm_device_attr_get(dev_fd, group, attr, val);

	TEST_ASSERT(!ret, KVM_IOCTL_ERROR(KVM_GET_DEVICE_ATTR, ret));
}

int __kvm_device_attr_set(int dev_fd, uint32_t group, uint64_t attr, void *val);

static inline void kvm_device_attr_set(int dev_fd, uint32_t group,
				       uint64_t attr, void *val)
{
	int ret = __kvm_device_attr_set(dev_fd, group, attr, val);

	TEST_ASSERT(!ret, KVM_IOCTL_ERROR(KVM_SET_DEVICE_ATTR, ret));
}

int __vcpu_has_device_attr(struct kvm_vm *vm, uint32_t vcpuid, uint32_t group,
			   uint64_t attr);

static inline void vcpu_has_device_attr(struct kvm_vm *vm, uint32_t vcpuid,
					uint32_t group, uint64_t attr)
{
	int ret = __vcpu_has_device_attr(vm, vcpuid, group, attr);

	TEST_ASSERT(!ret, KVM_IOCTL_ERROR(KVM_HAS_DEVICE_ATTR, ret));
}

int __vcpu_device_attr_get(struct kvm_vm *vm, uint32_t vcpuid, uint32_t group,
			   uint64_t attr, void *val);
void vcpu_device_attr_get(struct kvm_vm *vm, uint32_t vcpuid, uint32_t group,
			  uint64_t attr, void *val);
int __vcpu_device_attr_set(struct kvm_vm *vm, uint32_t vcpuid, uint32_t group,
			   uint64_t attr, void *val);
void vcpu_device_attr_set(struct kvm_vm *vm, uint32_t vcpuid, uint32_t group,
			  uint64_t attr, void *val);
int __kvm_test_create_device(struct kvm_vm *vm, uint64_t type);
int __kvm_create_device(struct kvm_vm *vm, uint64_t type);

static inline int kvm_create_device(struct kvm_vm *vm, uint64_t type)
{
	int fd = __kvm_create_device(vm, type);

	TEST_ASSERT(fd >= 0, KVM_IOCTL_ERROR(KVM_CREATE_DEVICE, fd));
	return fd;
}

void *vcpu_map_dirty_ring(struct kvm_vm *vm, uint32_t vcpuid);

/*
 * VM VCPU Args Set
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *   num - number of arguments
 *   ... - arguments, each of type uint64_t
 *
 * Output Args: None
 *
 * Return: None
 *
 * Sets the first @num function input registers of the VCPU with @vcpuid,
 * per the C calling convention of the architecture, to the values given
 * as variable args. Each of the variable args is expected to be of type
 * uint64_t. The maximum @num can be is specific to the architecture.
 */
void vcpu_args_set(struct kvm_vm *vm, uint32_t vcpuid, unsigned int num, ...);

void kvm_irq_line(struct kvm_vm *vm, uint32_t irq, int level);
int _kvm_irq_line(struct kvm_vm *vm, uint32_t irq, int level);

#define KVM_MAX_IRQ_ROUTES		4096

struct kvm_irq_routing *kvm_gsi_routing_create(void);
void kvm_gsi_routing_irqchip_add(struct kvm_irq_routing *routing,
		uint32_t gsi, uint32_t pin);
int _kvm_gsi_routing_write(struct kvm_vm *vm, struct kvm_irq_routing *routing);
void kvm_gsi_routing_write(struct kvm_vm *vm, struct kvm_irq_routing *routing);

const char *exit_reason_str(unsigned int exit_reason);

void virt_pgd_alloc(struct kvm_vm *vm);

/*
 * VM Virtual Page Map
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vaddr - VM Virtual Address
 *   paddr - VM Physical Address
 *   memslot - Memory region slot for new virtual translation tables
 *
 * Output Args: None
 *
 * Return: None
 *
 * Within @vm, creates a virtual translation for the page starting
 * at @vaddr to the page starting at @paddr.
 */
void virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr);

vm_paddr_t vm_phy_page_alloc(struct kvm_vm *vm, vm_paddr_t paddr_min,
			     uint32_t memslot);
vm_paddr_t vm_phy_pages_alloc(struct kvm_vm *vm, size_t num,
			      vm_paddr_t paddr_min, uint32_t memslot);
vm_paddr_t vm_alloc_page_table(struct kvm_vm *vm);

/*
 * Create a VM with reasonable defaults
 *
 * Input Args:
 *   vcpuid - The id of the single VCPU to add to the VM.
 *   extra_mem_pages - The number of extra pages to add (this will
 *                     decide how much extra space we will need to
 *                     setup the page tables using memslot 0)
 *   guest_code - The vCPU's entry point
 *
 * Output Args: None
 *
 * Return:
 *   Pointer to opaque structure that describes the created VM.
 */
struct kvm_vm *vm_create_default(uint32_t vcpuid, uint64_t extra_mem_pages,
				 void *guest_code);

/* Same as vm_create_default, but can be used for more than one vcpu */
struct kvm_vm *vm_create_default_with_vcpus(uint32_t nr_vcpus, uint64_t extra_mem_pages,
					    uint32_t num_percpu_pages, void *guest_code,
					    uint32_t vcpuids[]);

/* Like vm_create_default_with_vcpus, but accepts mode and slot0 memory as a parameter */
struct kvm_vm *vm_create_with_vcpus(enum vm_guest_mode mode, uint32_t nr_vcpus,
				    uint64_t slot0_mem_pages, uint64_t extra_mem_pages,
				    uint32_t num_percpu_pages, void *guest_code,
				    uint32_t vcpuids[]);

/* Create a default VM without any vcpus. */
struct kvm_vm *vm_create_without_vcpus(enum vm_guest_mode mode, uint64_t pages);

/*
 * Adds a vCPU with reasonable defaults (e.g. a stack)
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - The id of the VCPU to add to the VM.
 *   guest_code - The vCPU's entry point
 */
void vm_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid, void *guest_code);

bool vm_is_unrestricted_guest(struct kvm_vm *vm);

unsigned int vm_get_page_size(struct kvm_vm *vm);
unsigned int vm_get_page_shift(struct kvm_vm *vm);
unsigned long vm_compute_max_gfn(struct kvm_vm *vm);
uint64_t vm_get_max_gfn(struct kvm_vm *vm);
int vm_get_kvm_fd(struct kvm_vm *vm);
int vm_get_fd(struct kvm_vm *vm);

unsigned int vm_calc_num_guest_pages(enum vm_guest_mode mode, size_t size);
unsigned int vm_num_host_pages(enum vm_guest_mode mode, unsigned int num_guest_pages);
unsigned int vm_num_guest_pages(enum vm_guest_mode mode, unsigned int num_host_pages);
static inline unsigned int
vm_adjust_num_guest_pages(enum vm_guest_mode mode, unsigned int num_guest_pages)
{
	unsigned int n;
	n = vm_num_guest_pages(mode, vm_num_host_pages(mode, num_guest_pages));
#ifdef __s390x__
	/* s390 requires 1M aligned guest sizes */
	n = (n + 255) & ~255;
#endif
	return n;
}

struct kvm_userspace_memory_region *
kvm_userspace_memory_region_find(struct kvm_vm *vm, uint64_t start,
				 uint64_t end);

#define sync_global_to_guest(vm, g) ({				\
	typeof(g) *_p = addr_gva2hva(vm, (vm_vaddr_t)&(g));	\
	memcpy(_p, &(g), sizeof(g));				\
})

#define sync_global_from_guest(vm, g) ({			\
	typeof(g) *_p = addr_gva2hva(vm, (vm_vaddr_t)&(g));	\
	memcpy(&(g), _p, sizeof(g));				\
})

void assert_on_unhandled_exception(struct kvm_vm *vm, uint32_t vcpuid);

uint32_t guest_get_vcpuid(void);

#endif /* SELFTEST_KVM_UTIL_BASE_H */
