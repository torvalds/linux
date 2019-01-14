/*
 * tools/testing/selftests/kvm/include/kvm_util.h
 *
 * Copyright (C) 2018, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 */
#ifndef SELFTEST_KVM_UTIL_H
#define SELFTEST_KVM_UTIL_H 1

#include "test_util.h"

#include "asm/kvm.h"
#include "linux/kvm.h"
#include <sys/ioctl.h>

#include "sparsebit.h"

/*
 * Memslots can't cover the gfn starting at this gpa otherwise vCPUs can't be
 * created. Only applies to VMs using EPT.
 */
#define KVM_DEFAULT_IDENTITY_MAP_ADDRESS 0xfffbc000ul


/* Callers of kvm_util only have an incomplete/opaque description of the
 * structure kvm_util is using to maintain the state of a VM.
 */
struct kvm_vm;

typedef uint64_t vm_paddr_t; /* Virtual Machine (Guest) physical address */
typedef uint64_t vm_vaddr_t; /* Virtual Machine (Guest) virtual address */

/* Minimum allocated guest virtual and physical addresses */
#define KVM_UTIL_MIN_VADDR 0x2000

#define DEFAULT_GUEST_PHY_PAGES		512
#define DEFAULT_GUEST_STACK_VADDR_MIN	0xab6000
#define DEFAULT_STACK_PGS               5

enum vm_guest_mode {
	VM_MODE_FLAT48PG,
};

enum vm_mem_backing_src_type {
	VM_MEM_SRC_ANONYMOUS,
	VM_MEM_SRC_ANONYMOUS_THP,
	VM_MEM_SRC_ANONYMOUS_HUGETLB,
};

int kvm_check_cap(long cap);
int vm_enable_cap(struct kvm_vm *vm, struct kvm_enable_cap *cap);

struct kvm_vm *vm_create(enum vm_guest_mode mode, uint64_t phy_pages, int perm);
void kvm_vm_free(struct kvm_vm *vmp);
void kvm_vm_restart(struct kvm_vm *vmp, int perm);
void kvm_vm_release(struct kvm_vm *vmp);
void kvm_vm_get_dirty_log(struct kvm_vm *vm, int slot, void *log);

int kvm_memcmp_hva_gva(void *hva,
	struct kvm_vm *vm, const vm_vaddr_t gva, size_t len);

void kvm_vm_elf_load(struct kvm_vm *vm, const char *filename,
	uint32_t data_memslot, uint32_t pgd_memslot);

void vm_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent);
void vcpu_dump(FILE *stream, struct kvm_vm *vm,
	uint32_t vcpuid, uint8_t indent);

void vm_create_irqchip(struct kvm_vm *vm);

void vm_userspace_mem_region_add(struct kvm_vm *vm,
	enum vm_mem_backing_src_type src_type,
	uint64_t guest_paddr, uint32_t slot, uint64_t npages,
	uint32_t flags);

void vcpu_ioctl(struct kvm_vm *vm,
	uint32_t vcpuid, unsigned long ioctl, void *arg);
void vm_ioctl(struct kvm_vm *vm, unsigned long ioctl, void *arg);
void vm_mem_region_set_flags(struct kvm_vm *vm, uint32_t slot, uint32_t flags);
void vm_vcpu_add(struct kvm_vm *vm, uint32_t vcpuid, int pgd_memslot, int gdt_memslot);
vm_vaddr_t vm_vaddr_alloc(struct kvm_vm *vm, size_t sz, vm_vaddr_t vaddr_min,
	uint32_t data_memslot, uint32_t pgd_memslot);
void virt_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
	      size_t size, uint32_t pgd_memslot);
void *addr_gpa2hva(struct kvm_vm *vm, vm_paddr_t gpa);
void *addr_gva2hva(struct kvm_vm *vm, vm_vaddr_t gva);
vm_paddr_t addr_hva2gpa(struct kvm_vm *vm, void *hva);
vm_paddr_t addr_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva);

struct kvm_run *vcpu_state(struct kvm_vm *vm, uint32_t vcpuid);
void vcpu_run(struct kvm_vm *vm, uint32_t vcpuid);
int _vcpu_run(struct kvm_vm *vm, uint32_t vcpuid);
void vcpu_set_mp_state(struct kvm_vm *vm, uint32_t vcpuid,
	struct kvm_mp_state *mp_state);
void vcpu_regs_get(struct kvm_vm *vm,
	uint32_t vcpuid, struct kvm_regs *regs);
void vcpu_regs_set(struct kvm_vm *vm,
	uint32_t vcpuid, struct kvm_regs *regs);
void vcpu_args_set(struct kvm_vm *vm, uint32_t vcpuid, unsigned int num, ...);
void vcpu_sregs_get(struct kvm_vm *vm,
	uint32_t vcpuid, struct kvm_sregs *sregs);
void vcpu_sregs_set(struct kvm_vm *vm,
	uint32_t vcpuid, struct kvm_sregs *sregs);
int _vcpu_sregs_set(struct kvm_vm *vm,
	uint32_t vcpuid, struct kvm_sregs *sregs);
void vcpu_events_get(struct kvm_vm *vm, uint32_t vcpuid,
			  struct kvm_vcpu_events *events);
void vcpu_events_set(struct kvm_vm *vm, uint32_t vcpuid,
			  struct kvm_vcpu_events *events);
uint64_t vcpu_get_msr(struct kvm_vm *vm, uint32_t vcpuid, uint64_t msr_index);
void vcpu_set_msr(struct kvm_vm *vm, uint32_t vcpuid, uint64_t msr_index,
	uint64_t msr_value);

const char *exit_reason_str(unsigned int exit_reason);

void virt_pgd_alloc(struct kvm_vm *vm, uint32_t pgd_memslot);
void virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
	uint32_t pgd_memslot);
vm_paddr_t vm_phy_page_alloc(struct kvm_vm *vm,
	vm_paddr_t paddr_min, uint32_t memslot);

struct kvm_cpuid2 *kvm_get_supported_cpuid(void);
void vcpu_set_cpuid(
	struct kvm_vm *vm, uint32_t vcpuid, struct kvm_cpuid2 *cpuid);

struct kvm_cpuid_entry2 *
kvm_get_supported_cpuid_index(uint32_t function, uint32_t index);

static inline struct kvm_cpuid_entry2 *
kvm_get_supported_cpuid_entry(uint32_t function)
{
	return kvm_get_supported_cpuid_index(function, 0);
}

struct kvm_vm *vm_create_default(uint32_t vcpuid, uint64_t extra_mem_size,
				 void *guest_code);
void vm_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid, void *guest_code);

typedef void (*vmx_guest_code_t)(vm_vaddr_t vmxon_vaddr,
				 vm_paddr_t vmxon_paddr,
				 vm_vaddr_t vmcs_vaddr,
				 vm_paddr_t vmcs_paddr);

struct kvm_userspace_memory_region *
kvm_userspace_memory_region_find(struct kvm_vm *vm, uint64_t start,
				 uint64_t end);

struct kvm_dirty_log *
allocate_kvm_dirty_log(struct kvm_userspace_memory_region *region);

int vm_create_device(struct kvm_vm *vm, struct kvm_create_device *cd);

#define GUEST_PORT_SYNC         0x1000
#define GUEST_PORT_ABORT        0x1001
#define GUEST_PORT_DONE         0x1002

static inline void __exit_to_l0(uint16_t port, uint64_t arg0, uint64_t arg1)
{
	__asm__ __volatile__("in %[port], %%al"
			     :
			     : [port]"d"(port), "D"(arg0), "S"(arg1)
			     : "rax");
}

/*
 * Allows to pass three arguments to the host: port is 16bit wide,
 * arg0 & arg1 are 64bit wide
 */
#define GUEST_SYNC_ARGS(_port, _arg0, _arg1) \
	__exit_to_l0(_port, (uint64_t) (_arg0), (uint64_t) (_arg1))

#define GUEST_ASSERT(_condition) do {				\
		if (!(_condition))				\
			GUEST_SYNC_ARGS(GUEST_PORT_ABORT,	\
					"Failed guest assert: "	\
					#_condition, __LINE__);	\
	} while (0)

#define GUEST_SYNC(stage)  GUEST_SYNC_ARGS(GUEST_PORT_SYNC, "hello", stage)

#define GUEST_DONE()  GUEST_SYNC_ARGS(GUEST_PORT_DONE, 0, 0)

struct guest_args {
	uint64_t arg0;
	uint64_t arg1;
	uint16_t port;
} __attribute__ ((packed));

void guest_args_read(struct kvm_vm *vm, uint32_t vcpu_id,
		     struct guest_args *args);

#endif /* SELFTEST_KVM_UTIL_H */
