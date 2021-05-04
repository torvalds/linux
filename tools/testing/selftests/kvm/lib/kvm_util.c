// SPDX-License-Identifier: GPL-2.0-only
/*
 * tools/testing/selftests/kvm/lib/kvm_util.c
 *
 * Copyright (C) 2018, Google LLC.
 */

#define _GNU_SOURCE /* for program_invocation_name */
#include "test_util.h"
#include "kvm_util.h"
#include "kvm_util_internal.h"
#include "processor.h"

#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/kernel.h>

#define KVM_UTIL_PGS_PER_HUGEPG 512
#define KVM_UTIL_MIN_PFN	2

static int vcpu_mmap_sz(void);

/* Aligns x up to the next multiple of size. Size must be a power of 2. */
static void *align(void *x, size_t size)
{
	size_t mask = size - 1;
	TEST_ASSERT(size != 0 && !(size & (size - 1)),
		    "size not a power of 2: %lu", size);
	return (void *) (((size_t) x + mask) & ~mask);
}

/*
 * Capability
 *
 * Input Args:
 *   cap - Capability
 *
 * Output Args: None
 *
 * Return:
 *   On success, the Value corresponding to the capability (KVM_CAP_*)
 *   specified by the value of cap.  On failure a TEST_ASSERT failure
 *   is produced.
 *
 * Looks up and returns the value corresponding to the capability
 * (KVM_CAP_*) given by cap.
 */
int kvm_check_cap(long cap)
{
	int ret;
	int kvm_fd;

	kvm_fd = open(KVM_DEV_PATH, O_RDONLY);
	if (kvm_fd < 0)
		exit(KSFT_SKIP);

	ret = ioctl(kvm_fd, KVM_CHECK_EXTENSION, cap);
	TEST_ASSERT(ret != -1, "KVM_CHECK_EXTENSION IOCTL failed,\n"
		"  rc: %i errno: %i", ret, errno);

	close(kvm_fd);

	return ret;
}

/* VM Enable Capability
 *
 * Input Args:
 *   vm - Virtual Machine
 *   cap - Capability
 *
 * Output Args: None
 *
 * Return: On success, 0. On failure a TEST_ASSERT failure is produced.
 *
 * Enables a capability (KVM_CAP_*) on the VM.
 */
int vm_enable_cap(struct kvm_vm *vm, struct kvm_enable_cap *cap)
{
	int ret;

	ret = ioctl(vm->fd, KVM_ENABLE_CAP, cap);
	TEST_ASSERT(ret == 0, "KVM_ENABLE_CAP IOCTL failed,\n"
		"  rc: %i errno: %i", ret, errno);

	return ret;
}

/* VCPU Enable Capability
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpu_id - VCPU
 *   cap - Capability
 *
 * Output Args: None
 *
 * Return: On success, 0. On failure a TEST_ASSERT failure is produced.
 *
 * Enables a capability (KVM_CAP_*) on the VCPU.
 */
int vcpu_enable_cap(struct kvm_vm *vm, uint32_t vcpu_id,
		    struct kvm_enable_cap *cap)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpu_id);
	int r;

	TEST_ASSERT(vcpu, "cannot find vcpu %d", vcpu_id);

	r = ioctl(vcpu->fd, KVM_ENABLE_CAP, cap);
	TEST_ASSERT(!r, "KVM_ENABLE_CAP vCPU ioctl failed,\n"
			"  rc: %i, errno: %i", r, errno);

	return r;
}

void vm_enable_dirty_ring(struct kvm_vm *vm, uint32_t ring_size)
{
	struct kvm_enable_cap cap = { 0 };

	cap.cap = KVM_CAP_DIRTY_LOG_RING;
	cap.args[0] = ring_size;
	vm_enable_cap(vm, &cap);
	vm->dirty_ring_size = ring_size;
}

static void vm_open(struct kvm_vm *vm, int perm)
{
	vm->kvm_fd = open(KVM_DEV_PATH, perm);
	if (vm->kvm_fd < 0)
		exit(KSFT_SKIP);

	if (!kvm_check_cap(KVM_CAP_IMMEDIATE_EXIT)) {
		print_skip("immediate_exit not available");
		exit(KSFT_SKIP);
	}

	vm->fd = ioctl(vm->kvm_fd, KVM_CREATE_VM, vm->type);
	TEST_ASSERT(vm->fd >= 0, "KVM_CREATE_VM ioctl failed, "
		"rc: %i errno: %i", vm->fd, errno);
}

const char * const vm_guest_mode_string[] = {
	"PA-bits:52,  VA-bits:48,  4K pages",
	"PA-bits:52,  VA-bits:48, 64K pages",
	"PA-bits:48,  VA-bits:48,  4K pages",
	"PA-bits:48,  VA-bits:48, 64K pages",
	"PA-bits:40,  VA-bits:48,  4K pages",
	"PA-bits:40,  VA-bits:48, 64K pages",
	"PA-bits:ANY, VA-bits:48,  4K pages",
};
_Static_assert(sizeof(vm_guest_mode_string)/sizeof(char *) == NUM_VM_MODES,
	       "Missing new mode strings?");

const struct vm_guest_mode_params vm_guest_mode_params[] = {
	{ 52, 48,  0x1000, 12 },
	{ 52, 48, 0x10000, 16 },
	{ 48, 48,  0x1000, 12 },
	{ 48, 48, 0x10000, 16 },
	{ 40, 48,  0x1000, 12 },
	{ 40, 48, 0x10000, 16 },
	{  0,  0,  0x1000, 12 },
};
_Static_assert(sizeof(vm_guest_mode_params)/sizeof(struct vm_guest_mode_params) == NUM_VM_MODES,
	       "Missing new mode params?");

/*
 * VM Create
 *
 * Input Args:
 *   mode - VM Mode (e.g. VM_MODE_P52V48_4K)
 *   phy_pages - Physical memory pages
 *   perm - permission
 *
 * Output Args: None
 *
 * Return:
 *   Pointer to opaque structure that describes the created VM.
 *
 * Creates a VM with the mode specified by mode (e.g. VM_MODE_P52V48_4K).
 * When phy_pages is non-zero, a memory region of phy_pages physical pages
 * is created and mapped starting at guest physical address 0.  The file
 * descriptor to control the created VM is created with the permissions
 * given by perm (e.g. O_RDWR).
 */
struct kvm_vm *vm_create(enum vm_guest_mode mode, uint64_t phy_pages, int perm)
{
	struct kvm_vm *vm;

	pr_debug("%s: mode='%s' pages='%ld' perm='%d'\n", __func__,
		 vm_guest_mode_string(mode), phy_pages, perm);

	vm = calloc(1, sizeof(*vm));
	TEST_ASSERT(vm != NULL, "Insufficient Memory");

	INIT_LIST_HEAD(&vm->vcpus);
	INIT_LIST_HEAD(&vm->userspace_mem_regions);

	vm->mode = mode;
	vm->type = 0;

	vm->pa_bits = vm_guest_mode_params[mode].pa_bits;
	vm->va_bits = vm_guest_mode_params[mode].va_bits;
	vm->page_size = vm_guest_mode_params[mode].page_size;
	vm->page_shift = vm_guest_mode_params[mode].page_shift;

	/* Setup mode specific traits. */
	switch (vm->mode) {
	case VM_MODE_P52V48_4K:
		vm->pgtable_levels = 4;
		break;
	case VM_MODE_P52V48_64K:
		vm->pgtable_levels = 3;
		break;
	case VM_MODE_P48V48_4K:
		vm->pgtable_levels = 4;
		break;
	case VM_MODE_P48V48_64K:
		vm->pgtable_levels = 3;
		break;
	case VM_MODE_P40V48_4K:
		vm->pgtable_levels = 4;
		break;
	case VM_MODE_P40V48_64K:
		vm->pgtable_levels = 3;
		break;
	case VM_MODE_PXXV48_4K:
#ifdef __x86_64__
		kvm_get_cpu_address_width(&vm->pa_bits, &vm->va_bits);
		/*
		 * Ignore KVM support for 5-level paging (vm->va_bits == 57),
		 * it doesn't take effect unless a CR4.LA57 is set, which it
		 * isn't for this VM_MODE.
		 */
		TEST_ASSERT(vm->va_bits == 48 || vm->va_bits == 57,
			    "Linear address width (%d bits) not supported",
			    vm->va_bits);
		pr_debug("Guest physical address width detected: %d\n",
			 vm->pa_bits);
		vm->pgtable_levels = 4;
		vm->va_bits = 48;
#else
		TEST_FAIL("VM_MODE_PXXV48_4K not supported on non-x86 platforms");
#endif
		break;
	default:
		TEST_FAIL("Unknown guest mode, mode: 0x%x", mode);
	}

#ifdef __aarch64__
	if (vm->pa_bits != 40)
		vm->type = KVM_VM_TYPE_ARM_IPA_SIZE(vm->pa_bits);
#endif

	vm_open(vm, perm);

	/* Limit to VA-bit canonical virtual addresses. */
	vm->vpages_valid = sparsebit_alloc();
	sparsebit_set_num(vm->vpages_valid,
		0, (1ULL << (vm->va_bits - 1)) >> vm->page_shift);
	sparsebit_set_num(vm->vpages_valid,
		(~((1ULL << (vm->va_bits - 1)) - 1)) >> vm->page_shift,
		(1ULL << (vm->va_bits - 1)) >> vm->page_shift);

	/* Limit physical addresses to PA-bits. */
	vm->max_gfn = ((1ULL << vm->pa_bits) >> vm->page_shift) - 1;

	/* Allocate and setup memory for guest. */
	vm->vpages_mapped = sparsebit_alloc();
	if (phy_pages != 0)
		vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
					    0, 0, phy_pages, 0);

	return vm;
}

struct kvm_vm *vm_create_with_vcpus(enum vm_guest_mode mode, uint32_t nr_vcpus,
				    uint64_t extra_mem_pages, uint32_t num_percpu_pages,
				    void *guest_code, uint32_t vcpuids[])
{
	/* The maximum page table size for a memory region will be when the
	 * smallest pages are used. Considering each page contains x page
	 * table descriptors, the total extra size for page tables (for extra
	 * N pages) will be: N/x+N/x^2+N/x^3+... which is definitely smaller
	 * than N/x*2.
	 */
	uint64_t vcpu_pages = (DEFAULT_STACK_PGS + num_percpu_pages) * nr_vcpus;
	uint64_t extra_pg_pages = (extra_mem_pages + vcpu_pages) / PTES_PER_MIN_PAGE * 2;
	uint64_t pages = DEFAULT_GUEST_PHY_PAGES + vcpu_pages + extra_pg_pages;
	struct kvm_vm *vm;
	int i;

	TEST_ASSERT(nr_vcpus <= kvm_check_cap(KVM_CAP_MAX_VCPUS),
		    "nr_vcpus = %d too large for host, max-vcpus = %d",
		    nr_vcpus, kvm_check_cap(KVM_CAP_MAX_VCPUS));

	pages = vm_adjust_num_guest_pages(mode, pages);
	vm = vm_create(mode, pages, O_RDWR);

	kvm_vm_elf_load(vm, program_invocation_name, 0, 0);

#ifdef __x86_64__
	vm_create_irqchip(vm);
#endif

	for (i = 0; i < nr_vcpus; ++i) {
		uint32_t vcpuid = vcpuids ? vcpuids[i] : i;

		vm_vcpu_add_default(vm, vcpuid, guest_code);

#ifdef __x86_64__
		vcpu_set_cpuid(vm, vcpuid, kvm_get_supported_cpuid());
#endif
	}

	return vm;
}

struct kvm_vm *vm_create_default_with_vcpus(uint32_t nr_vcpus, uint64_t extra_mem_pages,
					    uint32_t num_percpu_pages, void *guest_code,
					    uint32_t vcpuids[])
{
	return vm_create_with_vcpus(VM_MODE_DEFAULT, nr_vcpus, extra_mem_pages,
				    num_percpu_pages, guest_code, vcpuids);
}

struct kvm_vm *vm_create_default(uint32_t vcpuid, uint64_t extra_mem_pages,
				 void *guest_code)
{
	return vm_create_default_with_vcpus(1, extra_mem_pages, 0, guest_code,
					    (uint32_t []){ vcpuid });
}

/*
 * VM Restart
 *
 * Input Args:
 *   vm - VM that has been released before
 *   perm - permission
 *
 * Output Args: None
 *
 * Reopens the file descriptors associated to the VM and reinstates the
 * global state, such as the irqchip and the memory regions that are mapped
 * into the guest.
 */
void kvm_vm_restart(struct kvm_vm *vmp, int perm)
{
	struct userspace_mem_region *region;

	vm_open(vmp, perm);
	if (vmp->has_irqchip)
		vm_create_irqchip(vmp);

	list_for_each_entry(region, &vmp->userspace_mem_regions, list) {
		int ret = ioctl(vmp->fd, KVM_SET_USER_MEMORY_REGION, &region->region);
		TEST_ASSERT(ret == 0, "KVM_SET_USER_MEMORY_REGION IOCTL failed,\n"
			    "  rc: %i errno: %i\n"
			    "  slot: %u flags: 0x%x\n"
			    "  guest_phys_addr: 0x%llx size: 0x%llx",
			    ret, errno, region->region.slot,
			    region->region.flags,
			    region->region.guest_phys_addr,
			    region->region.memory_size);
	}
}

void kvm_vm_get_dirty_log(struct kvm_vm *vm, int slot, void *log)
{
	struct kvm_dirty_log args = { .dirty_bitmap = log, .slot = slot };
	int ret;

	ret = ioctl(vm->fd, KVM_GET_DIRTY_LOG, &args);
	TEST_ASSERT(ret == 0, "%s: KVM_GET_DIRTY_LOG failed: %s",
		    __func__, strerror(-ret));
}

void kvm_vm_clear_dirty_log(struct kvm_vm *vm, int slot, void *log,
			    uint64_t first_page, uint32_t num_pages)
{
	struct kvm_clear_dirty_log args = { .dirty_bitmap = log, .slot = slot,
		                            .first_page = first_page,
	                                    .num_pages = num_pages };
	int ret;

	ret = ioctl(vm->fd, KVM_CLEAR_DIRTY_LOG, &args);
	TEST_ASSERT(ret == 0, "%s: KVM_CLEAR_DIRTY_LOG failed: %s",
		    __func__, strerror(-ret));
}

uint32_t kvm_vm_reset_dirty_ring(struct kvm_vm *vm)
{
	return ioctl(vm->fd, KVM_RESET_DIRTY_RINGS);
}

/*
 * Userspace Memory Region Find
 *
 * Input Args:
 *   vm - Virtual Machine
 *   start - Starting VM physical address
 *   end - Ending VM physical address, inclusive.
 *
 * Output Args: None
 *
 * Return:
 *   Pointer to overlapping region, NULL if no such region.
 *
 * Searches for a region with any physical memory that overlaps with
 * any portion of the guest physical addresses from start to end
 * inclusive.  If multiple overlapping regions exist, a pointer to any
 * of the regions is returned.  Null is returned only when no overlapping
 * region exists.
 */
static struct userspace_mem_region *
userspace_mem_region_find(struct kvm_vm *vm, uint64_t start, uint64_t end)
{
	struct userspace_mem_region *region;

	list_for_each_entry(region, &vm->userspace_mem_regions, list) {
		uint64_t existing_start = region->region.guest_phys_addr;
		uint64_t existing_end = region->region.guest_phys_addr
			+ region->region.memory_size - 1;
		if (start <= existing_end && end >= existing_start)
			return region;
	}

	return NULL;
}

/*
 * KVM Userspace Memory Region Find
 *
 * Input Args:
 *   vm - Virtual Machine
 *   start - Starting VM physical address
 *   end - Ending VM physical address, inclusive.
 *
 * Output Args: None
 *
 * Return:
 *   Pointer to overlapping region, NULL if no such region.
 *
 * Public interface to userspace_mem_region_find. Allows tests to look up
 * the memslot datastructure for a given range of guest physical memory.
 */
struct kvm_userspace_memory_region *
kvm_userspace_memory_region_find(struct kvm_vm *vm, uint64_t start,
				 uint64_t end)
{
	struct userspace_mem_region *region;

	region = userspace_mem_region_find(vm, start, end);
	if (!region)
		return NULL;

	return &region->region;
}

/*
 * VCPU Find
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *
 * Output Args: None
 *
 * Return:
 *   Pointer to VCPU structure
 *
 * Locates a vcpu structure that describes the VCPU specified by vcpuid and
 * returns a pointer to it.  Returns NULL if the VM doesn't contain a VCPU
 * for the specified vcpuid.
 */
struct vcpu *vcpu_find(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu;

	list_for_each_entry(vcpu, &vm->vcpus, list) {
		if (vcpu->id == vcpuid)
			return vcpu;
	}

	return NULL;
}

/*
 * VM VCPU Remove
 *
 * Input Args:
 *   vcpu - VCPU to remove
 *
 * Output Args: None
 *
 * Return: None, TEST_ASSERT failures for all error conditions
 *
 * Removes a vCPU from a VM and frees its resources.
 */
static void vm_vcpu_rm(struct kvm_vm *vm, struct vcpu *vcpu)
{
	int ret;

	if (vcpu->dirty_gfns) {
		ret = munmap(vcpu->dirty_gfns, vm->dirty_ring_size);
		TEST_ASSERT(ret == 0, "munmap of VCPU dirty ring failed, "
			    "rc: %i errno: %i", ret, errno);
		vcpu->dirty_gfns = NULL;
	}

	ret = munmap(vcpu->state, vcpu_mmap_sz());
	TEST_ASSERT(ret == 0, "munmap of VCPU fd failed, rc: %i "
		"errno: %i", ret, errno);
	close(vcpu->fd);
	TEST_ASSERT(ret == 0, "Close of VCPU fd failed, rc: %i "
		"errno: %i", ret, errno);

	list_del(&vcpu->list);
	free(vcpu);
}

void kvm_vm_release(struct kvm_vm *vmp)
{
	struct vcpu *vcpu, *tmp;
	int ret;

	list_for_each_entry_safe(vcpu, tmp, &vmp->vcpus, list)
		vm_vcpu_rm(vmp, vcpu);

	ret = close(vmp->fd);
	TEST_ASSERT(ret == 0, "Close of vm fd failed,\n"
		"  vmp->fd: %i rc: %i errno: %i", vmp->fd, ret, errno);

	close(vmp->kvm_fd);
	TEST_ASSERT(ret == 0, "Close of /dev/kvm fd failed,\n"
		"  vmp->kvm_fd: %i rc: %i errno: %i", vmp->kvm_fd, ret, errno);
}

static void __vm_mem_region_delete(struct kvm_vm *vm,
				   struct userspace_mem_region *region)
{
	int ret;

	list_del(&region->list);

	region->region.memory_size = 0;
	ret = ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &region->region);
	TEST_ASSERT(ret == 0, "KVM_SET_USER_MEMORY_REGION IOCTL failed, "
		    "rc: %i errno: %i", ret, errno);

	sparsebit_free(&region->unused_phy_pages);
	ret = munmap(region->mmap_start, region->mmap_size);
	TEST_ASSERT(ret == 0, "munmap failed, rc: %i errno: %i", ret, errno);

	free(region);
}

/*
 * Destroys and frees the VM pointed to by vmp.
 */
void kvm_vm_free(struct kvm_vm *vmp)
{
	struct userspace_mem_region *region, *tmp;

	if (vmp == NULL)
		return;

	/* Free userspace_mem_regions. */
	list_for_each_entry_safe(region, tmp, &vmp->userspace_mem_regions, list)
		__vm_mem_region_delete(vmp, region);

	/* Free sparsebit arrays. */
	sparsebit_free(&vmp->vpages_valid);
	sparsebit_free(&vmp->vpages_mapped);

	kvm_vm_release(vmp);

	/* Free the structure describing the VM. */
	free(vmp);
}

/*
 * Memory Compare, host virtual to guest virtual
 *
 * Input Args:
 *   hva - Starting host virtual address
 *   vm - Virtual Machine
 *   gva - Starting guest virtual address
 *   len - number of bytes to compare
 *
 * Output Args: None
 *
 * Input/Output Args: None
 *
 * Return:
 *   Returns 0 if the bytes starting at hva for a length of len
 *   are equal the guest virtual bytes starting at gva.  Returns
 *   a value < 0, if bytes at hva are less than those at gva.
 *   Otherwise a value > 0 is returned.
 *
 * Compares the bytes starting at the host virtual address hva, for
 * a length of len, to the guest bytes starting at the guest virtual
 * address given by gva.
 */
int kvm_memcmp_hva_gva(void *hva, struct kvm_vm *vm, vm_vaddr_t gva, size_t len)
{
	size_t amt;

	/*
	 * Compare a batch of bytes until either a match is found
	 * or all the bytes have been compared.
	 */
	for (uintptr_t offset = 0; offset < len; offset += amt) {
		uintptr_t ptr1 = (uintptr_t)hva + offset;

		/*
		 * Determine host address for guest virtual address
		 * at offset.
		 */
		uintptr_t ptr2 = (uintptr_t)addr_gva2hva(vm, gva + offset);

		/*
		 * Determine amount to compare on this pass.
		 * Don't allow the comparsion to cross a page boundary.
		 */
		amt = len - offset;
		if ((ptr1 >> vm->page_shift) != ((ptr1 + amt) >> vm->page_shift))
			amt = vm->page_size - (ptr1 % vm->page_size);
		if ((ptr2 >> vm->page_shift) != ((ptr2 + amt) >> vm->page_shift))
			amt = vm->page_size - (ptr2 % vm->page_size);

		assert((ptr1 >> vm->page_shift) == ((ptr1 + amt - 1) >> vm->page_shift));
		assert((ptr2 >> vm->page_shift) == ((ptr2 + amt - 1) >> vm->page_shift));

		/*
		 * Perform the comparison.  If there is a difference
		 * return that result to the caller, otherwise need
		 * to continue on looking for a mismatch.
		 */
		int ret = memcmp((void *)ptr1, (void *)ptr2, amt);
		if (ret != 0)
			return ret;
	}

	/*
	 * No mismatch found.  Let the caller know the two memory
	 * areas are equal.
	 */
	return 0;
}

/*
 * VM Userspace Memory Region Add
 *
 * Input Args:
 *   vm - Virtual Machine
 *   backing_src - Storage source for this region.
 *                 NULL to use anonymous memory.
 *   guest_paddr - Starting guest physical address
 *   slot - KVM region slot
 *   npages - Number of physical pages
 *   flags - KVM memory region flags (e.g. KVM_MEM_LOG_DIRTY_PAGES)
 *
 * Output Args: None
 *
 * Return: None
 *
 * Allocates a memory area of the number of pages specified by npages
 * and maps it to the VM specified by vm, at a starting physical address
 * given by guest_paddr.  The region is created with a KVM region slot
 * given by slot, which must be unique and < KVM_MEM_SLOTS_NUM.  The
 * region is created with the flags given by flags.
 */
void vm_userspace_mem_region_add(struct kvm_vm *vm,
	enum vm_mem_backing_src_type src_type,
	uint64_t guest_paddr, uint32_t slot, uint64_t npages,
	uint32_t flags)
{
	int ret;
	struct userspace_mem_region *region;
	size_t huge_page_size = KVM_UTIL_PGS_PER_HUGEPG * vm->page_size;
	size_t alignment;

	TEST_ASSERT(vm_adjust_num_guest_pages(vm->mode, npages) == npages,
		"Number of guest pages is not compatible with the host. "
		"Try npages=%d", vm_adjust_num_guest_pages(vm->mode, npages));

	TEST_ASSERT((guest_paddr % vm->page_size) == 0, "Guest physical "
		"address not on a page boundary.\n"
		"  guest_paddr: 0x%lx vm->page_size: 0x%x",
		guest_paddr, vm->page_size);
	TEST_ASSERT((((guest_paddr >> vm->page_shift) + npages) - 1)
		<= vm->max_gfn, "Physical range beyond maximum "
		"supported physical address,\n"
		"  guest_paddr: 0x%lx npages: 0x%lx\n"
		"  vm->max_gfn: 0x%lx vm->page_size: 0x%x",
		guest_paddr, npages, vm->max_gfn, vm->page_size);

	/*
	 * Confirm a mem region with an overlapping address doesn't
	 * already exist.
	 */
	region = (struct userspace_mem_region *) userspace_mem_region_find(
		vm, guest_paddr, (guest_paddr + npages * vm->page_size) - 1);
	if (region != NULL)
		TEST_FAIL("overlapping userspace_mem_region already "
			"exists\n"
			"  requested guest_paddr: 0x%lx npages: 0x%lx "
			"page_size: 0x%x\n"
			"  existing guest_paddr: 0x%lx size: 0x%lx",
			guest_paddr, npages, vm->page_size,
			(uint64_t) region->region.guest_phys_addr,
			(uint64_t) region->region.memory_size);

	/* Confirm no region with the requested slot already exists. */
	list_for_each_entry(region, &vm->userspace_mem_regions, list) {
		if (region->region.slot != slot)
			continue;

		TEST_FAIL("A mem region with the requested slot "
			"already exists.\n"
			"  requested slot: %u paddr: 0x%lx npages: 0x%lx\n"
			"  existing slot: %u paddr: 0x%lx size: 0x%lx",
			slot, guest_paddr, npages,
			region->region.slot,
			(uint64_t) region->region.guest_phys_addr,
			(uint64_t) region->region.memory_size);
	}

	/* Allocate and initialize new mem region structure. */
	region = calloc(1, sizeof(*region));
	TEST_ASSERT(region != NULL, "Insufficient Memory");
	region->mmap_size = npages * vm->page_size;

#ifdef __s390x__
	/* On s390x, the host address must be aligned to 1M (due to PGSTEs) */
	alignment = 0x100000;
#else
	alignment = 1;
#endif

	if (src_type == VM_MEM_SRC_ANONYMOUS_THP)
		alignment = max(huge_page_size, alignment);

	/* Add enough memory to align up if necessary */
	if (alignment > 1)
		region->mmap_size += alignment;

	region->mmap_start = mmap(NULL, region->mmap_size,
				  PROT_READ | PROT_WRITE,
				  MAP_PRIVATE | MAP_ANONYMOUS
				  | (src_type == VM_MEM_SRC_ANONYMOUS_HUGETLB ? MAP_HUGETLB : 0),
				  -1, 0);
	TEST_ASSERT(region->mmap_start != MAP_FAILED,
		    "test_malloc failed, mmap_start: %p errno: %i",
		    region->mmap_start, errno);

	/* Align host address */
	region->host_mem = align(region->mmap_start, alignment);

	/* As needed perform madvise */
	if (src_type == VM_MEM_SRC_ANONYMOUS || src_type == VM_MEM_SRC_ANONYMOUS_THP) {
		struct stat statbuf;

		ret = stat("/sys/kernel/mm/transparent_hugepage", &statbuf);
		TEST_ASSERT(ret == 0 || (ret == -1 && errno == ENOENT),
			    "stat /sys/kernel/mm/transparent_hugepage");

		TEST_ASSERT(ret == 0 || src_type != VM_MEM_SRC_ANONYMOUS_THP,
			    "VM_MEM_SRC_ANONYMOUS_THP requires THP to be configured in the host kernel");

		if (ret == 0) {
			ret = madvise(region->host_mem, npages * vm->page_size,
				      src_type == VM_MEM_SRC_ANONYMOUS ? MADV_NOHUGEPAGE : MADV_HUGEPAGE);
			TEST_ASSERT(ret == 0, "madvise failed, addr: %p length: 0x%lx src_type: %x",
				    region->host_mem, npages * vm->page_size, src_type);
		}
	}

	region->unused_phy_pages = sparsebit_alloc();
	sparsebit_set_num(region->unused_phy_pages,
		guest_paddr >> vm->page_shift, npages);
	region->region.slot = slot;
	region->region.flags = flags;
	region->region.guest_phys_addr = guest_paddr;
	region->region.memory_size = npages * vm->page_size;
	region->region.userspace_addr = (uintptr_t) region->host_mem;
	ret = ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &region->region);
	TEST_ASSERT(ret == 0, "KVM_SET_USER_MEMORY_REGION IOCTL failed,\n"
		"  rc: %i errno: %i\n"
		"  slot: %u flags: 0x%x\n"
		"  guest_phys_addr: 0x%lx size: 0x%lx",
		ret, errno, slot, flags,
		guest_paddr, (uint64_t) region->region.memory_size);

	/* Add to linked-list of memory regions. */
	list_add(&region->list, &vm->userspace_mem_regions);
}

/*
 * Memslot to region
 *
 * Input Args:
 *   vm - Virtual Machine
 *   memslot - KVM memory slot ID
 *
 * Output Args: None
 *
 * Return:
 *   Pointer to memory region structure that describe memory region
 *   using kvm memory slot ID given by memslot.  TEST_ASSERT failure
 *   on error (e.g. currently no memory region using memslot as a KVM
 *   memory slot ID).
 */
struct userspace_mem_region *
memslot2region(struct kvm_vm *vm, uint32_t memslot)
{
	struct userspace_mem_region *region;

	list_for_each_entry(region, &vm->userspace_mem_regions, list) {
		if (region->region.slot == memslot)
			return region;
	}

	fprintf(stderr, "No mem region with the requested slot found,\n"
		"  requested slot: %u\n", memslot);
	fputs("---- vm dump ----\n", stderr);
	vm_dump(stderr, vm, 2);
	TEST_FAIL("Mem region not found");
	return NULL;
}

/*
 * VM Memory Region Flags Set
 *
 * Input Args:
 *   vm - Virtual Machine
 *   flags - Starting guest physical address
 *
 * Output Args: None
 *
 * Return: None
 *
 * Sets the flags of the memory region specified by the value of slot,
 * to the values given by flags.
 */
void vm_mem_region_set_flags(struct kvm_vm *vm, uint32_t slot, uint32_t flags)
{
	int ret;
	struct userspace_mem_region *region;

	region = memslot2region(vm, slot);

	region->region.flags = flags;

	ret = ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &region->region);

	TEST_ASSERT(ret == 0, "KVM_SET_USER_MEMORY_REGION IOCTL failed,\n"
		"  rc: %i errno: %i slot: %u flags: 0x%x",
		ret, errno, slot, flags);
}

/*
 * VM Memory Region Move
 *
 * Input Args:
 *   vm - Virtual Machine
 *   slot - Slot of the memory region to move
 *   new_gpa - Starting guest physical address
 *
 * Output Args: None
 *
 * Return: None
 *
 * Change the gpa of a memory region.
 */
void vm_mem_region_move(struct kvm_vm *vm, uint32_t slot, uint64_t new_gpa)
{
	struct userspace_mem_region *region;
	int ret;

	region = memslot2region(vm, slot);

	region->region.guest_phys_addr = new_gpa;

	ret = ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &region->region);

	TEST_ASSERT(!ret, "KVM_SET_USER_MEMORY_REGION failed\n"
		    "ret: %i errno: %i slot: %u new_gpa: 0x%lx",
		    ret, errno, slot, new_gpa);
}

/*
 * VM Memory Region Delete
 *
 * Input Args:
 *   vm - Virtual Machine
 *   slot - Slot of the memory region to delete
 *
 * Output Args: None
 *
 * Return: None
 *
 * Delete a memory region.
 */
void vm_mem_region_delete(struct kvm_vm *vm, uint32_t slot)
{
	__vm_mem_region_delete(vm, memslot2region(vm, slot));
}

/*
 * VCPU mmap Size
 *
 * Input Args: None
 *
 * Output Args: None
 *
 * Return:
 *   Size of VCPU state
 *
 * Returns the size of the structure pointed to by the return value
 * of vcpu_state().
 */
static int vcpu_mmap_sz(void)
{
	int dev_fd, ret;

	dev_fd = open(KVM_DEV_PATH, O_RDONLY);
	if (dev_fd < 0)
		exit(KSFT_SKIP);

	ret = ioctl(dev_fd, KVM_GET_VCPU_MMAP_SIZE, NULL);
	TEST_ASSERT(ret >= sizeof(struct kvm_run),
		"%s KVM_GET_VCPU_MMAP_SIZE ioctl failed, rc: %i errno: %i",
		__func__, ret, errno);

	close(dev_fd);

	return ret;
}

/*
 * VM VCPU Add
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *
 * Output Args: None
 *
 * Return: None
 *
 * Adds a virtual CPU to the VM specified by vm with the ID given by vcpuid.
 * No additional VCPU setup is done.
 */
void vm_vcpu_add(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu;

	/* Confirm a vcpu with the specified id doesn't already exist. */
	vcpu = vcpu_find(vm, vcpuid);
	if (vcpu != NULL)
		TEST_FAIL("vcpu with the specified id "
			"already exists,\n"
			"  requested vcpuid: %u\n"
			"  existing vcpuid: %u state: %p",
			vcpuid, vcpu->id, vcpu->state);

	/* Allocate and initialize new vcpu structure. */
	vcpu = calloc(1, sizeof(*vcpu));
	TEST_ASSERT(vcpu != NULL, "Insufficient Memory");
	vcpu->id = vcpuid;
	vcpu->fd = ioctl(vm->fd, KVM_CREATE_VCPU, vcpuid);
	TEST_ASSERT(vcpu->fd >= 0, "KVM_CREATE_VCPU failed, rc: %i errno: %i",
		vcpu->fd, errno);

	TEST_ASSERT(vcpu_mmap_sz() >= sizeof(*vcpu->state), "vcpu mmap size "
		"smaller than expected, vcpu_mmap_sz: %i expected_min: %zi",
		vcpu_mmap_sz(), sizeof(*vcpu->state));
	vcpu->state = (struct kvm_run *) mmap(NULL, vcpu_mmap_sz(),
		PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->fd, 0);
	TEST_ASSERT(vcpu->state != MAP_FAILED, "mmap vcpu_state failed, "
		"vcpu id: %u errno: %i", vcpuid, errno);

	/* Add to linked-list of VCPUs. */
	list_add(&vcpu->list, &vm->vcpus);
}

/*
 * VM Virtual Address Unused Gap
 *
 * Input Args:
 *   vm - Virtual Machine
 *   sz - Size (bytes)
 *   vaddr_min - Minimum Virtual Address
 *
 * Output Args: None
 *
 * Return:
 *   Lowest virtual address at or below vaddr_min, with at least
 *   sz unused bytes.  TEST_ASSERT failure if no area of at least
 *   size sz is available.
 *
 * Within the VM specified by vm, locates the lowest starting virtual
 * address >= vaddr_min, that has at least sz unallocated bytes.  A
 * TEST_ASSERT failure occurs for invalid input or no area of at least
 * sz unallocated bytes >= vaddr_min is available.
 */
static vm_vaddr_t vm_vaddr_unused_gap(struct kvm_vm *vm, size_t sz,
				      vm_vaddr_t vaddr_min)
{
	uint64_t pages = (sz + vm->page_size - 1) >> vm->page_shift;

	/* Determine lowest permitted virtual page index. */
	uint64_t pgidx_start = (vaddr_min + vm->page_size - 1) >> vm->page_shift;
	if ((pgidx_start * vm->page_size) < vaddr_min)
		goto no_va_found;

	/* Loop over section with enough valid virtual page indexes. */
	if (!sparsebit_is_set_num(vm->vpages_valid,
		pgidx_start, pages))
		pgidx_start = sparsebit_next_set_num(vm->vpages_valid,
			pgidx_start, pages);
	do {
		/*
		 * Are there enough unused virtual pages available at
		 * the currently proposed starting virtual page index.
		 * If not, adjust proposed starting index to next
		 * possible.
		 */
		if (sparsebit_is_clear_num(vm->vpages_mapped,
			pgidx_start, pages))
			goto va_found;
		pgidx_start = sparsebit_next_clear_num(vm->vpages_mapped,
			pgidx_start, pages);
		if (pgidx_start == 0)
			goto no_va_found;

		/*
		 * If needed, adjust proposed starting virtual address,
		 * to next range of valid virtual addresses.
		 */
		if (!sparsebit_is_set_num(vm->vpages_valid,
			pgidx_start, pages)) {
			pgidx_start = sparsebit_next_set_num(
				vm->vpages_valid, pgidx_start, pages);
			if (pgidx_start == 0)
				goto no_va_found;
		}
	} while (pgidx_start != 0);

no_va_found:
	TEST_FAIL("No vaddr of specified pages available, pages: 0x%lx", pages);

	/* NOT REACHED */
	return -1;

va_found:
	TEST_ASSERT(sparsebit_is_set_num(vm->vpages_valid,
		pgidx_start, pages),
		"Unexpected, invalid virtual page index range,\n"
		"  pgidx_start: 0x%lx\n"
		"  pages: 0x%lx",
		pgidx_start, pages);
	TEST_ASSERT(sparsebit_is_clear_num(vm->vpages_mapped,
		pgidx_start, pages),
		"Unexpected, pages already mapped,\n"
		"  pgidx_start: 0x%lx\n"
		"  pages: 0x%lx",
		pgidx_start, pages);

	return pgidx_start * vm->page_size;
}

/*
 * VM Virtual Address Allocate
 *
 * Input Args:
 *   vm - Virtual Machine
 *   sz - Size in bytes
 *   vaddr_min - Minimum starting virtual address
 *   data_memslot - Memory region slot for data pages
 *   pgd_memslot - Memory region slot for new virtual translation tables
 *
 * Output Args: None
 *
 * Return:
 *   Starting guest virtual address
 *
 * Allocates at least sz bytes within the virtual address space of the vm
 * given by vm.  The allocated bytes are mapped to a virtual address >=
 * the address given by vaddr_min.  Note that each allocation uses a
 * a unique set of pages, with the minimum real allocation being at least
 * a page.
 */
vm_vaddr_t vm_vaddr_alloc(struct kvm_vm *vm, size_t sz, vm_vaddr_t vaddr_min,
			  uint32_t data_memslot, uint32_t pgd_memslot)
{
	uint64_t pages = (sz >> vm->page_shift) + ((sz % vm->page_size) != 0);

	virt_pgd_alloc(vm, pgd_memslot);

	/*
	 * Find an unused range of virtual page addresses of at least
	 * pages in length.
	 */
	vm_vaddr_t vaddr_start = vm_vaddr_unused_gap(vm, sz, vaddr_min);

	/* Map the virtual pages. */
	for (vm_vaddr_t vaddr = vaddr_start; pages > 0;
		pages--, vaddr += vm->page_size) {
		vm_paddr_t paddr;

		paddr = vm_phy_page_alloc(vm,
				KVM_UTIL_MIN_PFN * vm->page_size, data_memslot);

		virt_pg_map(vm, vaddr, paddr, pgd_memslot);

		sparsebit_set(vm->vpages_mapped,
			vaddr >> vm->page_shift);
	}

	return vaddr_start;
}

/*
 * Map a range of VM virtual address to the VM's physical address
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vaddr - Virtuall address to map
 *   paddr - VM Physical Address
 *   npages - The number of pages to map
 *   pgd_memslot - Memory region slot for new virtual translation tables
 *
 * Output Args: None
 *
 * Return: None
 *
 * Within the VM given by @vm, creates a virtual translation for
 * @npages starting at @vaddr to the page range starting at @paddr.
 */
void virt_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
	      unsigned int npages, uint32_t pgd_memslot)
{
	size_t page_size = vm->page_size;
	size_t size = npages * page_size;

	TEST_ASSERT(vaddr + size > vaddr, "Vaddr overflow");
	TEST_ASSERT(paddr + size > paddr, "Paddr overflow");

	while (npages--) {
		virt_pg_map(vm, vaddr, paddr, pgd_memslot);
		vaddr += page_size;
		paddr += page_size;
	}
}

/*
 * Address VM Physical to Host Virtual
 *
 * Input Args:
 *   vm - Virtual Machine
 *   gpa - VM physical address
 *
 * Output Args: None
 *
 * Return:
 *   Equivalent host virtual address
 *
 * Locates the memory region containing the VM physical address given
 * by gpa, within the VM given by vm.  When found, the host virtual
 * address providing the memory to the vm physical address is returned.
 * A TEST_ASSERT failure occurs if no region containing gpa exists.
 */
void *addr_gpa2hva(struct kvm_vm *vm, vm_paddr_t gpa)
{
	struct userspace_mem_region *region;

	list_for_each_entry(region, &vm->userspace_mem_regions, list) {
		if ((gpa >= region->region.guest_phys_addr)
			&& (gpa <= (region->region.guest_phys_addr
				+ region->region.memory_size - 1)))
			return (void *) ((uintptr_t) region->host_mem
				+ (gpa - region->region.guest_phys_addr));
	}

	TEST_FAIL("No vm physical memory at 0x%lx", gpa);
	return NULL;
}

/*
 * Address Host Virtual to VM Physical
 *
 * Input Args:
 *   vm - Virtual Machine
 *   hva - Host virtual address
 *
 * Output Args: None
 *
 * Return:
 *   Equivalent VM physical address
 *
 * Locates the memory region containing the host virtual address given
 * by hva, within the VM given by vm.  When found, the equivalent
 * VM physical address is returned. A TEST_ASSERT failure occurs if no
 * region containing hva exists.
 */
vm_paddr_t addr_hva2gpa(struct kvm_vm *vm, void *hva)
{
	struct userspace_mem_region *region;

	list_for_each_entry(region, &vm->userspace_mem_regions, list) {
		if ((hva >= region->host_mem)
			&& (hva <= (region->host_mem
				+ region->region.memory_size - 1)))
			return (vm_paddr_t) ((uintptr_t)
				region->region.guest_phys_addr
				+ (hva - (uintptr_t) region->host_mem));
	}

	TEST_FAIL("No mapping to a guest physical address, hva: %p", hva);
	return -1;
}

/*
 * VM Create IRQ Chip
 *
 * Input Args:
 *   vm - Virtual Machine
 *
 * Output Args: None
 *
 * Return: None
 *
 * Creates an interrupt controller chip for the VM specified by vm.
 */
void vm_create_irqchip(struct kvm_vm *vm)
{
	int ret;

	ret = ioctl(vm->fd, KVM_CREATE_IRQCHIP, 0);
	TEST_ASSERT(ret == 0, "KVM_CREATE_IRQCHIP IOCTL failed, "
		"rc: %i errno: %i", ret, errno);

	vm->has_irqchip = true;
}

/*
 * VM VCPU State
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *
 * Output Args: None
 *
 * Return:
 *   Pointer to structure that describes the state of the VCPU.
 *
 * Locates and returns a pointer to a structure that describes the
 * state of the VCPU with the given vcpuid.
 */
struct kvm_run *vcpu_state(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	return vcpu->state;
}

/*
 * VM VCPU Run
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *
 * Output Args: None
 *
 * Return: None
 *
 * Switch to executing the code for the VCPU given by vcpuid, within the VM
 * given by vm.
 */
void vcpu_run(struct kvm_vm *vm, uint32_t vcpuid)
{
	int ret = _vcpu_run(vm, vcpuid);
	TEST_ASSERT(ret == 0, "KVM_RUN IOCTL failed, "
		"rc: %i errno: %i", ret, errno);
}

int _vcpu_run(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int rc;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);
	do {
		rc = ioctl(vcpu->fd, KVM_RUN, NULL);
	} while (rc == -1 && errno == EINTR);

	assert_on_unhandled_exception(vm, vcpuid);

	return rc;
}

int vcpu_get_fd(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	return vcpu->fd;
}

void vcpu_run_complete_io(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	vcpu->state->immediate_exit = 1;
	ret = ioctl(vcpu->fd, KVM_RUN, NULL);
	vcpu->state->immediate_exit = 0;

	TEST_ASSERT(ret == -1 && errno == EINTR,
		    "KVM_RUN IOCTL didn't exit immediately, rc: %i, errno: %i",
		    ret, errno);
}

void vcpu_set_guest_debug(struct kvm_vm *vm, uint32_t vcpuid,
			  struct kvm_guest_debug *debug)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret = ioctl(vcpu->fd, KVM_SET_GUEST_DEBUG, debug);

	TEST_ASSERT(ret == 0, "KVM_SET_GUEST_DEBUG failed: %d", ret);
}

/*
 * VM VCPU Set MP State
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *   mp_state - mp_state to be set
 *
 * Output Args: None
 *
 * Return: None
 *
 * Sets the MP state of the VCPU given by vcpuid, to the state given
 * by mp_state.
 */
void vcpu_set_mp_state(struct kvm_vm *vm, uint32_t vcpuid,
		       struct kvm_mp_state *mp_state)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	ret = ioctl(vcpu->fd, KVM_SET_MP_STATE, mp_state);
	TEST_ASSERT(ret == 0, "KVM_SET_MP_STATE IOCTL failed, "
		"rc: %i errno: %i", ret, errno);
}

/*
 * VM VCPU Get Reg List
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *
 * Output Args:
 *   None
 *
 * Return:
 *   A pointer to an allocated struct kvm_reg_list
 *
 * Get the list of guest registers which are supported for
 * KVM_GET_ONE_REG/KVM_SET_ONE_REG calls
 */
struct kvm_reg_list *vcpu_get_reg_list(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct kvm_reg_list reg_list_n = { .n = 0 }, *reg_list;
	int ret;

	ret = _vcpu_ioctl(vm, vcpuid, KVM_GET_REG_LIST, &reg_list_n);
	TEST_ASSERT(ret == -1 && errno == E2BIG, "KVM_GET_REG_LIST n=0");
	reg_list = calloc(1, sizeof(*reg_list) + reg_list_n.n * sizeof(__u64));
	reg_list->n = reg_list_n.n;
	vcpu_ioctl(vm, vcpuid, KVM_GET_REG_LIST, reg_list);
	return reg_list;
}

/*
 * VM VCPU Regs Get
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *
 * Output Args:
 *   regs - current state of VCPU regs
 *
 * Return: None
 *
 * Obtains the current register state for the VCPU specified by vcpuid
 * and stores it at the location given by regs.
 */
void vcpu_regs_get(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_regs *regs)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	ret = ioctl(vcpu->fd, KVM_GET_REGS, regs);
	TEST_ASSERT(ret == 0, "KVM_GET_REGS failed, rc: %i errno: %i",
		ret, errno);
}

/*
 * VM VCPU Regs Set
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *   regs - Values to set VCPU regs to
 *
 * Output Args: None
 *
 * Return: None
 *
 * Sets the regs of the VCPU specified by vcpuid to the values
 * given by regs.
 */
void vcpu_regs_set(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_regs *regs)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	ret = ioctl(vcpu->fd, KVM_SET_REGS, regs);
	TEST_ASSERT(ret == 0, "KVM_SET_REGS failed, rc: %i errno: %i",
		ret, errno);
}

#ifdef __KVM_HAVE_VCPU_EVENTS
void vcpu_events_get(struct kvm_vm *vm, uint32_t vcpuid,
		     struct kvm_vcpu_events *events)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	ret = ioctl(vcpu->fd, KVM_GET_VCPU_EVENTS, events);
	TEST_ASSERT(ret == 0, "KVM_GET_VCPU_EVENTS, failed, rc: %i errno: %i",
		ret, errno);
}

void vcpu_events_set(struct kvm_vm *vm, uint32_t vcpuid,
		     struct kvm_vcpu_events *events)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	ret = ioctl(vcpu->fd, KVM_SET_VCPU_EVENTS, events);
	TEST_ASSERT(ret == 0, "KVM_SET_VCPU_EVENTS, failed, rc: %i errno: %i",
		ret, errno);
}
#endif

#ifdef __x86_64__
void vcpu_nested_state_get(struct kvm_vm *vm, uint32_t vcpuid,
			   struct kvm_nested_state *state)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	ret = ioctl(vcpu->fd, KVM_GET_NESTED_STATE, state);
	TEST_ASSERT(ret == 0,
		"KVM_SET_NESTED_STATE failed, ret: %i errno: %i",
		ret, errno);
}

int vcpu_nested_state_set(struct kvm_vm *vm, uint32_t vcpuid,
			  struct kvm_nested_state *state, bool ignore_error)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	ret = ioctl(vcpu->fd, KVM_SET_NESTED_STATE, state);
	if (!ignore_error) {
		TEST_ASSERT(ret == 0,
			"KVM_SET_NESTED_STATE failed, ret: %i errno: %i",
			ret, errno);
	}

	return ret;
}
#endif

/*
 * VM VCPU System Regs Get
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *
 * Output Args:
 *   sregs - current state of VCPU system regs
 *
 * Return: None
 *
 * Obtains the current system register state for the VCPU specified by
 * vcpuid and stores it at the location given by sregs.
 */
void vcpu_sregs_get(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_sregs *sregs)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	ret = ioctl(vcpu->fd, KVM_GET_SREGS, sregs);
	TEST_ASSERT(ret == 0, "KVM_GET_SREGS failed, rc: %i errno: %i",
		ret, errno);
}

/*
 * VM VCPU System Regs Set
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *   sregs - Values to set VCPU system regs to
 *
 * Output Args: None
 *
 * Return: None
 *
 * Sets the system regs of the VCPU specified by vcpuid to the values
 * given by sregs.
 */
void vcpu_sregs_set(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_sregs *sregs)
{
	int ret = _vcpu_sregs_set(vm, vcpuid, sregs);
	TEST_ASSERT(ret == 0, "KVM_RUN IOCTL failed, "
		"rc: %i errno: %i", ret, errno);
}

int _vcpu_sregs_set(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_sregs *sregs)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	return ioctl(vcpu->fd, KVM_SET_SREGS, sregs);
}

void vcpu_fpu_get(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_fpu *fpu)
{
	int ret;

	ret = _vcpu_ioctl(vm, vcpuid, KVM_GET_FPU, fpu);
	TEST_ASSERT(ret == 0, "KVM_GET_FPU failed, rc: %i errno: %i (%s)",
		    ret, errno, strerror(errno));
}

void vcpu_fpu_set(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_fpu *fpu)
{
	int ret;

	ret = _vcpu_ioctl(vm, vcpuid, KVM_SET_FPU, fpu);
	TEST_ASSERT(ret == 0, "KVM_SET_FPU failed, rc: %i errno: %i (%s)",
		    ret, errno, strerror(errno));
}

void vcpu_get_reg(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_one_reg *reg)
{
	int ret;

	ret = _vcpu_ioctl(vm, vcpuid, KVM_GET_ONE_REG, reg);
	TEST_ASSERT(ret == 0, "KVM_GET_ONE_REG failed, rc: %i errno: %i (%s)",
		    ret, errno, strerror(errno));
}

void vcpu_set_reg(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_one_reg *reg)
{
	int ret;

	ret = _vcpu_ioctl(vm, vcpuid, KVM_SET_ONE_REG, reg);
	TEST_ASSERT(ret == 0, "KVM_SET_ONE_REG failed, rc: %i errno: %i (%s)",
		    ret, errno, strerror(errno));
}

/*
 * VCPU Ioctl
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *   cmd - Ioctl number
 *   arg - Argument to pass to the ioctl
 *
 * Return: None
 *
 * Issues an arbitrary ioctl on a VCPU fd.
 */
void vcpu_ioctl(struct kvm_vm *vm, uint32_t vcpuid,
		unsigned long cmd, void *arg)
{
	int ret;

	ret = _vcpu_ioctl(vm, vcpuid, cmd, arg);
	TEST_ASSERT(ret == 0, "vcpu ioctl %lu failed, rc: %i errno: %i (%s)",
		cmd, ret, errno, strerror(errno));
}

int _vcpu_ioctl(struct kvm_vm *vm, uint32_t vcpuid,
		unsigned long cmd, void *arg)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	ret = ioctl(vcpu->fd, cmd, arg);

	return ret;
}

void *vcpu_map_dirty_ring(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu;
	uint32_t size = vm->dirty_ring_size;

	TEST_ASSERT(size > 0, "Should enable dirty ring first");

	vcpu = vcpu_find(vm, vcpuid);

	TEST_ASSERT(vcpu, "Cannot find vcpu %u", vcpuid);

	if (!vcpu->dirty_gfns) {
		void *addr;

		addr = mmap(NULL, size, PROT_READ,
			    MAP_PRIVATE, vcpu->fd,
			    vm->page_size * KVM_DIRTY_LOG_PAGE_OFFSET);
		TEST_ASSERT(addr == MAP_FAILED, "Dirty ring mapped private");

		addr = mmap(NULL, size, PROT_READ | PROT_EXEC,
			    MAP_PRIVATE, vcpu->fd,
			    vm->page_size * KVM_DIRTY_LOG_PAGE_OFFSET);
		TEST_ASSERT(addr == MAP_FAILED, "Dirty ring mapped exec");

		addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
			    MAP_SHARED, vcpu->fd,
			    vm->page_size * KVM_DIRTY_LOG_PAGE_OFFSET);
		TEST_ASSERT(addr != MAP_FAILED, "Dirty ring map failed");

		vcpu->dirty_gfns = addr;
		vcpu->dirty_gfns_count = size / sizeof(struct kvm_dirty_gfn);
	}

	return vcpu->dirty_gfns;
}

/*
 * VM Ioctl
 *
 * Input Args:
 *   vm - Virtual Machine
 *   cmd - Ioctl number
 *   arg - Argument to pass to the ioctl
 *
 * Return: None
 *
 * Issues an arbitrary ioctl on a VM fd.
 */
void vm_ioctl(struct kvm_vm *vm, unsigned long cmd, void *arg)
{
	int ret;

	ret = _vm_ioctl(vm, cmd, arg);
	TEST_ASSERT(ret == 0, "vm ioctl %lu failed, rc: %i errno: %i (%s)",
		cmd, ret, errno, strerror(errno));
}

int _vm_ioctl(struct kvm_vm *vm, unsigned long cmd, void *arg)
{
	return ioctl(vm->fd, cmd, arg);
}

/*
 * KVM system ioctl
 *
 * Input Args:
 *   vm - Virtual Machine
 *   cmd - Ioctl number
 *   arg - Argument to pass to the ioctl
 *
 * Return: None
 *
 * Issues an arbitrary ioctl on a KVM fd.
 */
void kvm_ioctl(struct kvm_vm *vm, unsigned long cmd, void *arg)
{
	int ret;

	ret = ioctl(vm->kvm_fd, cmd, arg);
	TEST_ASSERT(ret == 0, "KVM ioctl %lu failed, rc: %i errno: %i (%s)",
		cmd, ret, errno, strerror(errno));
}

int _kvm_ioctl(struct kvm_vm *vm, unsigned long cmd, void *arg)
{
	return ioctl(vm->kvm_fd, cmd, arg);
}

/*
 * VM Dump
 *
 * Input Args:
 *   vm - Virtual Machine
 *   indent - Left margin indent amount
 *
 * Output Args:
 *   stream - Output FILE stream
 *
 * Return: None
 *
 * Dumps the current state of the VM given by vm, to the FILE stream
 * given by stream.
 */
void vm_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent)
{
	struct userspace_mem_region *region;
	struct vcpu *vcpu;

	fprintf(stream, "%*smode: 0x%x\n", indent, "", vm->mode);
	fprintf(stream, "%*sfd: %i\n", indent, "", vm->fd);
	fprintf(stream, "%*spage_size: 0x%x\n", indent, "", vm->page_size);
	fprintf(stream, "%*sMem Regions:\n", indent, "");
	list_for_each_entry(region, &vm->userspace_mem_regions, list) {
		fprintf(stream, "%*sguest_phys: 0x%lx size: 0x%lx "
			"host_virt: %p\n", indent + 2, "",
			(uint64_t) region->region.guest_phys_addr,
			(uint64_t) region->region.memory_size,
			region->host_mem);
		fprintf(stream, "%*sunused_phy_pages: ", indent + 2, "");
		sparsebit_dump(stream, region->unused_phy_pages, 0);
	}
	fprintf(stream, "%*sMapped Virtual Pages:\n", indent, "");
	sparsebit_dump(stream, vm->vpages_mapped, indent + 2);
	fprintf(stream, "%*spgd_created: %u\n", indent, "",
		vm->pgd_created);
	if (vm->pgd_created) {
		fprintf(stream, "%*sVirtual Translation Tables:\n",
			indent + 2, "");
		virt_dump(stream, vm, indent + 4);
	}
	fprintf(stream, "%*sVCPUs:\n", indent, "");
	list_for_each_entry(vcpu, &vm->vcpus, list)
		vcpu_dump(stream, vm, vcpu->id, indent + 2);
}

/* Known KVM exit reasons */
static struct exit_reason {
	unsigned int reason;
	const char *name;
} exit_reasons_known[] = {
	{KVM_EXIT_UNKNOWN, "UNKNOWN"},
	{KVM_EXIT_EXCEPTION, "EXCEPTION"},
	{KVM_EXIT_IO, "IO"},
	{KVM_EXIT_HYPERCALL, "HYPERCALL"},
	{KVM_EXIT_DEBUG, "DEBUG"},
	{KVM_EXIT_HLT, "HLT"},
	{KVM_EXIT_MMIO, "MMIO"},
	{KVM_EXIT_IRQ_WINDOW_OPEN, "IRQ_WINDOW_OPEN"},
	{KVM_EXIT_SHUTDOWN, "SHUTDOWN"},
	{KVM_EXIT_FAIL_ENTRY, "FAIL_ENTRY"},
	{KVM_EXIT_INTR, "INTR"},
	{KVM_EXIT_SET_TPR, "SET_TPR"},
	{KVM_EXIT_TPR_ACCESS, "TPR_ACCESS"},
	{KVM_EXIT_S390_SIEIC, "S390_SIEIC"},
	{KVM_EXIT_S390_RESET, "S390_RESET"},
	{KVM_EXIT_DCR, "DCR"},
	{KVM_EXIT_NMI, "NMI"},
	{KVM_EXIT_INTERNAL_ERROR, "INTERNAL_ERROR"},
	{KVM_EXIT_OSI, "OSI"},
	{KVM_EXIT_PAPR_HCALL, "PAPR_HCALL"},
	{KVM_EXIT_DIRTY_RING_FULL, "DIRTY_RING_FULL"},
	{KVM_EXIT_X86_RDMSR, "RDMSR"},
	{KVM_EXIT_X86_WRMSR, "WRMSR"},
	{KVM_EXIT_XEN, "XEN"},
#ifdef KVM_EXIT_MEMORY_NOT_PRESENT
	{KVM_EXIT_MEMORY_NOT_PRESENT, "MEMORY_NOT_PRESENT"},
#endif
};

/*
 * Exit Reason String
 *
 * Input Args:
 *   exit_reason - Exit reason
 *
 * Output Args: None
 *
 * Return:
 *   Constant string pointer describing the exit reason.
 *
 * Locates and returns a constant string that describes the KVM exit
 * reason given by exit_reason.  If no such string is found, a constant
 * string of "Unknown" is returned.
 */
const char *exit_reason_str(unsigned int exit_reason)
{
	unsigned int n1;

	for (n1 = 0; n1 < ARRAY_SIZE(exit_reasons_known); n1++) {
		if (exit_reason == exit_reasons_known[n1].reason)
			return exit_reasons_known[n1].name;
	}

	return "Unknown";
}

/*
 * Physical Contiguous Page Allocator
 *
 * Input Args:
 *   vm - Virtual Machine
 *   num - number of pages
 *   paddr_min - Physical address minimum
 *   memslot - Memory region to allocate page from
 *
 * Output Args: None
 *
 * Return:
 *   Starting physical address
 *
 * Within the VM specified by vm, locates a range of available physical
 * pages at or above paddr_min. If found, the pages are marked as in use
 * and their base address is returned. A TEST_ASSERT failure occurs if
 * not enough pages are available at or above paddr_min.
 */
vm_paddr_t vm_phy_pages_alloc(struct kvm_vm *vm, size_t num,
			      vm_paddr_t paddr_min, uint32_t memslot)
{
	struct userspace_mem_region *region;
	sparsebit_idx_t pg, base;

	TEST_ASSERT(num > 0, "Must allocate at least one page");

	TEST_ASSERT((paddr_min % vm->page_size) == 0, "Min physical address "
		"not divisible by page size.\n"
		"  paddr_min: 0x%lx page_size: 0x%x",
		paddr_min, vm->page_size);

	region = memslot2region(vm, memslot);
	base = pg = paddr_min >> vm->page_shift;

	do {
		for (; pg < base + num; ++pg) {
			if (!sparsebit_is_set(region->unused_phy_pages, pg)) {
				base = pg = sparsebit_next_set(region->unused_phy_pages, pg);
				break;
			}
		}
	} while (pg && pg != base + num);

	if (pg == 0) {
		fprintf(stderr, "No guest physical page available, "
			"paddr_min: 0x%lx page_size: 0x%x memslot: %u\n",
			paddr_min, vm->page_size, memslot);
		fputs("---- vm dump ----\n", stderr);
		vm_dump(stderr, vm, 2);
		abort();
	}

	for (pg = base; pg < base + num; ++pg)
		sparsebit_clear(region->unused_phy_pages, pg);

	return base * vm->page_size;
}

vm_paddr_t vm_phy_page_alloc(struct kvm_vm *vm, vm_paddr_t paddr_min,
			     uint32_t memslot)
{
	return vm_phy_pages_alloc(vm, 1, paddr_min, memslot);
}

/*
 * Address Guest Virtual to Host Virtual
 *
 * Input Args:
 *   vm - Virtual Machine
 *   gva - VM virtual address
 *
 * Output Args: None
 *
 * Return:
 *   Equivalent host virtual address
 */
void *addr_gva2hva(struct kvm_vm *vm, vm_vaddr_t gva)
{
	return addr_gpa2hva(vm, addr_gva2gpa(vm, gva));
}

/*
 * Is Unrestricted Guest
 *
 * Input Args:
 *   vm - Virtual Machine
 *
 * Output Args: None
 *
 * Return: True if the unrestricted guest is set to 'Y', otherwise return false.
 *
 * Check if the unrestricted guest flag is enabled.
 */
bool vm_is_unrestricted_guest(struct kvm_vm *vm)
{
	char val = 'N';
	size_t count;
	FILE *f;

	if (vm == NULL) {
		/* Ensure that the KVM vendor-specific module is loaded. */
		f = fopen(KVM_DEV_PATH, "r");
		TEST_ASSERT(f != NULL, "Error in opening KVM dev file: %d",
			    errno);
		fclose(f);
	}

	f = fopen("/sys/module/kvm_intel/parameters/unrestricted_guest", "r");
	if (f) {
		count = fread(&val, sizeof(char), 1, f);
		TEST_ASSERT(count == 1, "Unable to read from param file.");
		fclose(f);
	}

	return val == 'Y';
}

unsigned int vm_get_page_size(struct kvm_vm *vm)
{
	return vm->page_size;
}

unsigned int vm_get_page_shift(struct kvm_vm *vm)
{
	return vm->page_shift;
}

unsigned int vm_get_max_gfn(struct kvm_vm *vm)
{
	return vm->max_gfn;
}

int vm_get_fd(struct kvm_vm *vm)
{
	return vm->fd;
}

static unsigned int vm_calc_num_pages(unsigned int num_pages,
				      unsigned int page_shift,
				      unsigned int new_page_shift,
				      bool ceil)
{
	unsigned int n = 1 << (new_page_shift - page_shift);

	if (page_shift >= new_page_shift)
		return num_pages * (1 << (page_shift - new_page_shift));

	return num_pages / n + !!(ceil && num_pages % n);
}

static inline int getpageshift(void)
{
	return __builtin_ffs(getpagesize()) - 1;
}

unsigned int
vm_num_host_pages(enum vm_guest_mode mode, unsigned int num_guest_pages)
{
	return vm_calc_num_pages(num_guest_pages,
				 vm_guest_mode_params[mode].page_shift,
				 getpageshift(), true);
}

unsigned int
vm_num_guest_pages(enum vm_guest_mode mode, unsigned int num_host_pages)
{
	return vm_calc_num_pages(num_host_pages, getpageshift(),
				 vm_guest_mode_params[mode].page_shift, false);
}

unsigned int vm_calc_num_guest_pages(enum vm_guest_mode mode, size_t size)
{
	unsigned int n;
	n = DIV_ROUND_UP(size, vm_guest_mode_params[mode].page_size);
	return vm_adjust_num_guest_pages(mode, n);
}
