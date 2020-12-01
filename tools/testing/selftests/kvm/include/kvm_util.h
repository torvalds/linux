/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/kvm_util.h
 *
 * Copyright (C) 2018, Google LLC.
 */
#ifndef SELFTEST_KVM_UTIL_H
#define SELFTEST_KVM_UTIL_H

#include "test_util.h"

#include "asm/kvm.h"
#include "linux/list.h"
#include "linux/kvm.h"
#include <sys/ioctl.h>

#include "sparsebit.h"


/*
 * Callers of kvm_util only have an incomplete/opaque description of the
 * structure kvm_util is using to maintain the state of a VM.
 */
struct kvm_vm;

typedef uint64_t vm_paddr_t; /* Virtual Machine (Guest) physical address */
typedef uint64_t vm_vaddr_t; /* Virtual Machine (Guest) virtual address */

/* Minimum allocated guest virtual and physical addresses */
#define KVM_UTIL_MIN_VADDR		0x2000

#define DEFAULT_GUEST_PHY_PAGES		512
#define DEFAULT_GUEST_STACK_VADDR_MIN	0xab6000
#define DEFAULT_STACK_PGS		5

enum vm_guest_mode {
	VM_MODE_P52V48_4K,
	VM_MODE_P52V48_64K,
	VM_MODE_P48V48_4K,
	VM_MODE_P48V48_64K,
	VM_MODE_P40V48_4K,
	VM_MODE_P40V48_64K,
	VM_MODE_PXXV48_4K,	/* For 48bits VA but ANY bits PA */
	NUM_VM_MODES,
};

#if defined(__aarch64__)
#define VM_MODE_DEFAULT VM_MODE_P40V48_4K
#elif defined(__x86_64__)
#define VM_MODE_DEFAULT VM_MODE_PXXV48_4K
#else
#define VM_MODE_DEFAULT VM_MODE_P52V48_4K
#endif

#define vm_guest_mode_string(m) vm_guest_mode_string[m]
extern const char * const vm_guest_mode_string[];

enum vm_mem_backing_src_type {
	VM_MEM_SRC_ANONYMOUS,
	VM_MEM_SRC_ANONYMOUS_THP,
	VM_MEM_SRC_ANONYMOUS_HUGETLB,
};

int kvm_check_cap(long cap);
int vm_enable_cap(struct kvm_vm *vm, struct kvm_enable_cap *cap);
int vcpu_enable_cap(struct kvm_vm *vm, uint32_t vcpu_id,
		    struct kvm_enable_cap *cap);
void vm_enable_dirty_ring(struct kvm_vm *vm, uint32_t ring_size);

struct kvm_vm *vm_create(enum vm_guest_mode mode, uint64_t phy_pages, int perm);
void kvm_vm_free(struct kvm_vm *vmp);
void kvm_vm_restart(struct kvm_vm *vmp, int perm);
void kvm_vm_release(struct kvm_vm *vmp);
void kvm_vm_get_dirty_log(struct kvm_vm *vm, int slot, void *log);
void kvm_vm_clear_dirty_log(struct kvm_vm *vm, int slot, void *log,
			    uint64_t first_page, uint32_t num_pages);

int kvm_memcmp_hva_gva(void *hva, struct kvm_vm *vm, const vm_vaddr_t gva,
		       size_t len);

void kvm_vm_elf_load(struct kvm_vm *vm, const char *filename,
		     uint32_t data_memslot, uint32_t pgd_memslot);

void vm_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent);

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

void vm_userspace_mem_region_add(struct kvm_vm *vm,
	enum vm_mem_backing_src_type src_type,
	uint64_t guest_paddr, uint32_t slot, uint64_t npages,
	uint32_t flags);

void vcpu_ioctl(struct kvm_vm *vm, uint32_t vcpuid, unsigned long ioctl,
		void *arg);
int _vcpu_ioctl(struct kvm_vm *vm, uint32_t vcpuid, unsigned long ioctl,
		void *arg);
void vm_ioctl(struct kvm_vm *vm, unsigned long ioctl, void *arg);
void vm_mem_region_set_flags(struct kvm_vm *vm, uint32_t slot, uint32_t flags);
void vm_mem_region_move(struct kvm_vm *vm, uint32_t slot, uint64_t new_gpa);
void vm_mem_region_delete(struct kvm_vm *vm, uint32_t slot);
void vm_vcpu_add(struct kvm_vm *vm, uint32_t vcpuid);
vm_vaddr_t vm_vaddr_alloc(struct kvm_vm *vm, size_t sz, vm_vaddr_t vaddr_min,
			  uint32_t data_memslot, uint32_t pgd_memslot);
void virt_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
	      unsigned int npages, uint32_t pgd_memslot);
void *addr_gpa2hva(struct kvm_vm *vm, vm_paddr_t gpa);
void *addr_gva2hva(struct kvm_vm *vm, vm_vaddr_t gva);
vm_paddr_t addr_hva2gpa(struct kvm_vm *vm, void *hva);

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
void vcpu_run_complete_io(struct kvm_vm *vm, uint32_t vcpuid);
void vcpu_set_guest_debug(struct kvm_vm *vm, uint32_t vcpuid,
			  struct kvm_guest_debug *debug);
void vcpu_set_mp_state(struct kvm_vm *vm, uint32_t vcpuid,
		       struct kvm_mp_state *mp_state);
struct kvm_reg_list *vcpu_get_reg_list(struct kvm_vm *vm, uint32_t vcpuid);
void vcpu_regs_get(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_regs *regs);
void vcpu_regs_set(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_regs *regs);

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

void vcpu_sregs_get(struct kvm_vm *vm, uint32_t vcpuid,
		    struct kvm_sregs *sregs);
void vcpu_sregs_set(struct kvm_vm *vm, uint32_t vcpuid,
		    struct kvm_sregs *sregs);
int _vcpu_sregs_set(struct kvm_vm *vm, uint32_t vcpuid,
		    struct kvm_sregs *sregs);
void vcpu_fpu_get(struct kvm_vm *vm, uint32_t vcpuid,
		  struct kvm_fpu *fpu);
void vcpu_fpu_set(struct kvm_vm *vm, uint32_t vcpuid,
		  struct kvm_fpu *fpu);
void vcpu_get_reg(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_one_reg *reg);
void vcpu_set_reg(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_one_reg *reg);
#ifdef __KVM_HAVE_VCPU_EVENTS
void vcpu_events_get(struct kvm_vm *vm, uint32_t vcpuid,
		     struct kvm_vcpu_events *events);
void vcpu_events_set(struct kvm_vm *vm, uint32_t vcpuid,
		     struct kvm_vcpu_events *events);
#endif
#ifdef __x86_64__
void vcpu_nested_state_get(struct kvm_vm *vm, uint32_t vcpuid,
			   struct kvm_nested_state *state);
int vcpu_nested_state_set(struct kvm_vm *vm, uint32_t vcpuid,
			  struct kvm_nested_state *state, bool ignore_error);
#endif

const char *exit_reason_str(unsigned int exit_reason);

void virt_pgd_alloc(struct kvm_vm *vm, uint32_t pgd_memslot);

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
void virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
		 uint32_t memslot);

vm_paddr_t vm_phy_page_alloc(struct kvm_vm *vm, vm_paddr_t paddr_min,
			     uint32_t memslot);
vm_paddr_t vm_phy_pages_alloc(struct kvm_vm *vm, size_t num,
			      vm_paddr_t paddr_min, uint32_t memslot);

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
unsigned int vm_get_max_gfn(struct kvm_vm *vm);
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

struct kvm_dirty_log *
allocate_kvm_dirty_log(struct kvm_userspace_memory_region *region);

int vm_create_device(struct kvm_vm *vm, struct kvm_create_device *cd);

#define sync_global_to_guest(vm, g) ({				\
	typeof(g) *_p = addr_gva2hva(vm, (vm_vaddr_t)&(g));	\
	memcpy(_p, &(g), sizeof(g));				\
})

#define sync_global_from_guest(vm, g) ({			\
	typeof(g) *_p = addr_gva2hva(vm, (vm_vaddr_t)&(g));	\
	memcpy(&(g), _p, sizeof(g));				\
})

void assert_on_unhandled_exception(struct kvm_vm *vm, uint32_t vcpuid);

/* Common ucalls */
enum {
	UCALL_NONE,
	UCALL_SYNC,
	UCALL_ABORT,
	UCALL_DONE,
};

#define UCALL_MAX_ARGS 6

struct ucall {
	uint64_t cmd;
	uint64_t args[UCALL_MAX_ARGS];
};

void ucall_init(struct kvm_vm *vm, void *arg);
void ucall_uninit(struct kvm_vm *vm);
void ucall(uint64_t cmd, int nargs, ...);
uint64_t get_ucall(struct kvm_vm *vm, uint32_t vcpu_id, struct ucall *uc);

#define GUEST_SYNC_ARGS(stage, arg1, arg2, arg3, arg4)	\
				ucall(UCALL_SYNC, 6, "hello", stage, arg1, arg2, arg3, arg4)
#define GUEST_SYNC(stage)	ucall(UCALL_SYNC, 2, "hello", stage)
#define GUEST_DONE()		ucall(UCALL_DONE, 0)
#define __GUEST_ASSERT(_condition, _nargs, _args...) do {	\
	if (!(_condition))					\
		ucall(UCALL_ABORT, 2 + _nargs,			\
			"Failed guest assert: "			\
			#_condition, __LINE__, _args);		\
} while (0)

#define GUEST_ASSERT(_condition) \
	__GUEST_ASSERT((_condition), 0, 0)

#define GUEST_ASSERT_1(_condition, arg1) \
	__GUEST_ASSERT((_condition), 1, (arg1))

#define GUEST_ASSERT_2(_condition, arg1, arg2) \
	__GUEST_ASSERT((_condition), 2, (arg1), (arg2))

#define GUEST_ASSERT_3(_condition, arg1, arg2, arg3) \
	__GUEST_ASSERT((_condition), 3, (arg1), (arg2), (arg3))

#define GUEST_ASSERT_4(_condition, arg1, arg2, arg3, arg4) \
	__GUEST_ASSERT((_condition), 4, (arg1), (arg2), (arg3), (arg4))

#endif /* SELFTEST_KVM_UTIL_H */
