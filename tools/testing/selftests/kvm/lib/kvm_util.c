// SPDX-License-Identifier: GPL-2.0-only
/*
 * tools/testing/selftests/kvm/lib/kvm_util.c
 *
 * Copyright (C) 2018, Google LLC.
 */

#define _GNU_SOURCE /* for program_invocation_name */
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/kernel.h>

#define KVM_UTIL_MIN_PFN	2

static int vcpu_mmap_sz(void);

int open_path_or_exit(const char *path, int flags)
{
	int fd;

	fd = open(path, flags);
	if (fd < 0) {
		print_skip("%s not available (errno: %d)", path, errno);
		exit(KSFT_SKIP);
	}

	return fd;
}

/*
 * Open KVM_DEV_PATH if available, otherwise exit the entire program.
 *
 * Input Args:
 *   flags - The flags to pass when opening KVM_DEV_PATH.
 *
 * Return:
 *   The opened file descriptor of /dev/kvm.
 */
static int _open_kvm_dev_path_or_exit(int flags)
{
	return open_path_or_exit(KVM_DEV_PATH, flags);
}

int open_kvm_dev_path_or_exit(void)
{
	return _open_kvm_dev_path_or_exit(O_RDONLY);
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

	kvm_fd = open_kvm_dev_path_or_exit();
	ret = __kvm_ioctl(kvm_fd, KVM_CHECK_EXTENSION, cap);
	TEST_ASSERT(ret >= 0, KVM_IOCTL_ERROR(KVM_CHECK_EXTENSION, ret));

	close(kvm_fd);

	return ret;
}

void vm_enable_dirty_ring(struct kvm_vm *vm, uint32_t ring_size)
{
	struct kvm_enable_cap cap = { 0 };

	cap.cap = KVM_CAP_DIRTY_LOG_RING;
	cap.args[0] = ring_size;
	vm_enable_cap(vm, &cap);
	vm->dirty_ring_size = ring_size;
}

static void vm_open(struct kvm_vm *vm)
{
	vm->kvm_fd = _open_kvm_dev_path_or_exit(O_RDWR);

	if (!kvm_check_cap(KVM_CAP_IMMEDIATE_EXIT)) {
		print_skip("immediate_exit not available");
		exit(KSFT_SKIP);
	}

	vm->fd = __kvm_ioctl(vm->kvm_fd, KVM_CREATE_VM, vm->type);
	TEST_ASSERT(vm->fd >= 0, KVM_IOCTL_ERROR(KVM_CREATE_VM, vm->fd));
}

const char *vm_guest_mode_string(uint32_t i)
{
	static const char * const strings[] = {
		[VM_MODE_P52V48_4K]	= "PA-bits:52,  VA-bits:48,  4K pages",
		[VM_MODE_P52V48_64K]	= "PA-bits:52,  VA-bits:48, 64K pages",
		[VM_MODE_P48V48_4K]	= "PA-bits:48,  VA-bits:48,  4K pages",
		[VM_MODE_P48V48_16K]	= "PA-bits:48,  VA-bits:48, 16K pages",
		[VM_MODE_P48V48_64K]	= "PA-bits:48,  VA-bits:48, 64K pages",
		[VM_MODE_P40V48_4K]	= "PA-bits:40,  VA-bits:48,  4K pages",
		[VM_MODE_P40V48_16K]	= "PA-bits:40,  VA-bits:48, 16K pages",
		[VM_MODE_P40V48_64K]	= "PA-bits:40,  VA-bits:48, 64K pages",
		[VM_MODE_PXXV48_4K]	= "PA-bits:ANY, VA-bits:48,  4K pages",
		[VM_MODE_P47V64_4K]	= "PA-bits:47,  VA-bits:64,  4K pages",
		[VM_MODE_P44V64_4K]	= "PA-bits:44,  VA-bits:64,  4K pages",
		[VM_MODE_P36V48_4K]	= "PA-bits:36,  VA-bits:48,  4K pages",
		[VM_MODE_P36V48_16K]	= "PA-bits:36,  VA-bits:48, 16K pages",
		[VM_MODE_P36V48_64K]	= "PA-bits:36,  VA-bits:48, 64K pages",
		[VM_MODE_P36V47_16K]	= "PA-bits:36,  VA-bits:47, 16K pages",
	};
	_Static_assert(sizeof(strings)/sizeof(char *) == NUM_VM_MODES,
		       "Missing new mode strings?");

	TEST_ASSERT(i < NUM_VM_MODES, "Guest mode ID %d too big", i);

	return strings[i];
}

const struct vm_guest_mode_params vm_guest_mode_params[] = {
	[VM_MODE_P52V48_4K]	= { 52, 48,  0x1000, 12 },
	[VM_MODE_P52V48_64K]	= { 52, 48, 0x10000, 16 },
	[VM_MODE_P48V48_4K]	= { 48, 48,  0x1000, 12 },
	[VM_MODE_P48V48_16K]	= { 48, 48,  0x4000, 14 },
	[VM_MODE_P48V48_64K]	= { 48, 48, 0x10000, 16 },
	[VM_MODE_P40V48_4K]	= { 40, 48,  0x1000, 12 },
	[VM_MODE_P40V48_16K]	= { 40, 48,  0x4000, 14 },
	[VM_MODE_P40V48_64K]	= { 40, 48, 0x10000, 16 },
	[VM_MODE_PXXV48_4K]	= {  0,  0,  0x1000, 12 },
	[VM_MODE_P47V64_4K]	= { 47, 64,  0x1000, 12 },
	[VM_MODE_P44V64_4K]	= { 44, 64,  0x1000, 12 },
	[VM_MODE_P36V48_4K]	= { 36, 48,  0x1000, 12 },
	[VM_MODE_P36V48_16K]	= { 36, 48,  0x4000, 14 },
	[VM_MODE_P36V48_64K]	= { 36, 48, 0x10000, 16 },
	[VM_MODE_P36V47_16K]	= { 36, 47,  0x4000, 14 },
};
_Static_assert(sizeof(vm_guest_mode_params)/sizeof(struct vm_guest_mode_params) == NUM_VM_MODES,
	       "Missing new mode params?");

struct kvm_vm *__vm_create(enum vm_guest_mode mode, uint64_t phy_pages)
{
	struct kvm_vm *vm;

	pr_debug("%s: mode='%s' pages='%ld'\n", __func__,
		 vm_guest_mode_string(mode), phy_pages);

	vm = calloc(1, sizeof(*vm));
	TEST_ASSERT(vm != NULL, "Insufficient Memory");

	INIT_LIST_HEAD(&vm->vcpus);
	vm->regions.gpa_tree = RB_ROOT;
	vm->regions.hva_tree = RB_ROOT;
	hash_init(vm->regions.slot_hash);

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
	case VM_MODE_P36V48_4K:
		vm->pgtable_levels = 4;
		break;
	case VM_MODE_P40V48_64K:
	case VM_MODE_P36V48_64K:
		vm->pgtable_levels = 3;
		break;
	case VM_MODE_P48V48_16K:
	case VM_MODE_P40V48_16K:
	case VM_MODE_P36V48_16K:
		vm->pgtable_levels = 4;
		break;
	case VM_MODE_P36V47_16K:
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
	case VM_MODE_P47V64_4K:
		vm->pgtable_levels = 5;
		break;
	case VM_MODE_P44V64_4K:
		vm->pgtable_levels = 5;
		break;
	default:
		TEST_FAIL("Unknown guest mode, mode: 0x%x", mode);
	}

#ifdef __aarch64__
	if (vm->pa_bits != 40)
		vm->type = KVM_VM_TYPE_ARM_IPA_SIZE(vm->pa_bits);
#endif

	vm_open(vm);

	/* Limit to VA-bit canonical virtual addresses. */
	vm->vpages_valid = sparsebit_alloc();
	sparsebit_set_num(vm->vpages_valid,
		0, (1ULL << (vm->va_bits - 1)) >> vm->page_shift);
	sparsebit_set_num(vm->vpages_valid,
		(~((1ULL << (vm->va_bits - 1)) - 1)) >> vm->page_shift,
		(1ULL << (vm->va_bits - 1)) >> vm->page_shift);

	/* Limit physical addresses to PA-bits. */
	vm->max_gfn = vm_compute_max_gfn(vm);

	/* Allocate and setup memory for guest. */
	vm->vpages_mapped = sparsebit_alloc();
	if (phy_pages != 0)
		vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
					    0, 0, phy_pages, 0);

	return vm;
}

/*
 * VM Create
 *
 * Input Args:
 *   phy_pages - Physical memory pages
 *
 * Output Args: None
 *
 * Return:
 *   Pointer to opaque structure that describes the created VM.
 *
 * Creates a VM with the default physical/virtual address widths and page size.
 * When phy_pages is non-zero, a memory region of phy_pages physical pages
 * is created and mapped starting at guest physical address 0.
 */
struct kvm_vm *vm_create(uint64_t phy_pages)
{
	return __vm_create(VM_MODE_DEFAULT, phy_pages);
}

struct kvm_vm *vm_create_without_vcpus(enum vm_guest_mode mode, uint64_t pages)
{
	struct kvm_vm *vm;

	vm = __vm_create(mode, pages);

	kvm_vm_elf_load(vm, program_invocation_name);

#ifdef __x86_64__
	vm_create_irqchip(vm);
#endif
	return vm;
}

/*
 * VM Create with customized parameters
 *
 * Input Args:
 *   mode - VM Mode (e.g. VM_MODE_P52V48_4K)
 *   nr_vcpus - VCPU count
 *   slot0_mem_pages - Slot0 physical memory size
 *   extra_mem_pages - Non-slot0 physical memory total size
 *   num_percpu_pages - Per-cpu physical memory pages
 *   guest_code - Guest entry point
 *   vcpuids - VCPU IDs
 *
 * Output Args: None
 *
 * Return:
 *   Pointer to opaque structure that describes the created VM.
 *
 * Creates a VM with the mode specified by mode (e.g. VM_MODE_P52V48_4K),
 * with customized slot0 memory size, at least 512 pages currently.
 * extra_mem_pages is only used to calculate the maximum page table size,
 * no real memory allocation for non-slot0 memory in this function.
 */
struct kvm_vm *vm_create_with_vcpus(enum vm_guest_mode mode, uint32_t nr_vcpus,
				    uint64_t slot0_mem_pages, uint64_t extra_mem_pages,
				    uint32_t num_percpu_pages, void *guest_code,
				    uint32_t vcpuids[])
{
	uint64_t vcpu_pages, extra_pg_pages, pages;
	struct kvm_vm *vm;
	int i;

	/* Force slot0 memory size not small than DEFAULT_GUEST_PHY_PAGES */
	if (slot0_mem_pages < DEFAULT_GUEST_PHY_PAGES)
		slot0_mem_pages = DEFAULT_GUEST_PHY_PAGES;

	/* The maximum page table size for a memory region will be when the
	 * smallest pages are used. Considering each page contains x page
	 * table descriptors, the total extra size for page tables (for extra
	 * N pages) will be: N/x+N/x^2+N/x^3+... which is definitely smaller
	 * than N/x*2.
	 */
	vcpu_pages = (DEFAULT_STACK_PGS + num_percpu_pages) * nr_vcpus;
	extra_pg_pages = (slot0_mem_pages + extra_mem_pages + vcpu_pages) / PTES_PER_MIN_PAGE * 2;
	pages = slot0_mem_pages + vcpu_pages + extra_pg_pages;

	TEST_ASSERT(nr_vcpus <= kvm_check_cap(KVM_CAP_MAX_VCPUS),
		    "nr_vcpus = %d too large for host, max-vcpus = %d",
		    nr_vcpus, kvm_check_cap(KVM_CAP_MAX_VCPUS));

	pages = vm_adjust_num_guest_pages(mode, pages);

	vm = vm_create_without_vcpus(mode, pages);

	for (i = 0; i < nr_vcpus; ++i) {
		uint32_t vcpuid = vcpuids ? vcpuids[i] : i;

		vm_vcpu_add_default(vm, vcpuid, guest_code);
	}

	return vm;
}

struct kvm_vm *vm_create_default_with_vcpus(uint32_t nr_vcpus, uint64_t extra_mem_pages,
					    uint32_t num_percpu_pages, void *guest_code,
					    uint32_t vcpuids[])
{
	return vm_create_with_vcpus(VM_MODE_DEFAULT, nr_vcpus, DEFAULT_GUEST_PHY_PAGES,
				    extra_mem_pages, num_percpu_pages, guest_code, vcpuids);
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
 *
 * Output Args: None
 *
 * Reopens the file descriptors associated to the VM and reinstates the
 * global state, such as the irqchip and the memory regions that are mapped
 * into the guest.
 */
void kvm_vm_restart(struct kvm_vm *vmp)
{
	int ctr;
	struct userspace_mem_region *region;

	vm_open(vmp);
	if (vmp->has_irqchip)
		vm_create_irqchip(vmp);

	hash_for_each(vmp->regions.slot_hash, ctr, region, slot_node) {
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
	struct rb_node *node;

	for (node = vm->regions.gpa_tree.rb_node; node; ) {
		struct userspace_mem_region *region =
			container_of(node, struct userspace_mem_region, gpa_node);
		uint64_t existing_start = region->region.guest_phys_addr;
		uint64_t existing_end = region->region.guest_phys_addr
			+ region->region.memory_size - 1;
		if (start <= existing_end && end >= existing_start)
			return region;

		if (start < existing_start)
			node = node->rb_left;
		else
			node = node->rb_right;
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

static struct vcpu *vcpu_find(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu;

	list_for_each_entry(vcpu, &vm->vcpus, list) {
		if (vcpu->id == vcpuid)
			return vcpu;
	}

	return NULL;
}

struct vcpu *vcpu_get(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);

	TEST_ASSERT(vcpu, "vCPU %d does not exist", vcpuid);
	return vcpu;
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
		TEST_ASSERT(!ret, __KVM_SYSCALL_ERROR("munmap()", ret));
		vcpu->dirty_gfns = NULL;
	}

	ret = munmap(vcpu->state, vcpu_mmap_sz());
	TEST_ASSERT(!ret, __KVM_SYSCALL_ERROR("munmap()", ret));

	ret = close(vcpu->fd);
	TEST_ASSERT(!ret,  __KVM_SYSCALL_ERROR("close()", ret));

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
	TEST_ASSERT(!ret,  __KVM_SYSCALL_ERROR("close()", ret));

	ret = close(vmp->kvm_fd);
	TEST_ASSERT(!ret,  __KVM_SYSCALL_ERROR("close()", ret));
}

static void __vm_mem_region_delete(struct kvm_vm *vm,
				   struct userspace_mem_region *region,
				   bool unlink)
{
	int ret;

	if (unlink) {
		rb_erase(&region->gpa_node, &vm->regions.gpa_tree);
		rb_erase(&region->hva_node, &vm->regions.hva_tree);
		hash_del(&region->slot_node);
	}

	region->region.memory_size = 0;
	vm_ioctl(vm, KVM_SET_USER_MEMORY_REGION, &region->region);

	sparsebit_free(&region->unused_phy_pages);
	ret = munmap(region->mmap_start, region->mmap_size);
	TEST_ASSERT(!ret, __KVM_SYSCALL_ERROR("munmap()", ret));

	free(region);
}

/*
 * Destroys and frees the VM pointed to by vmp.
 */
void kvm_vm_free(struct kvm_vm *vmp)
{
	int ctr;
	struct hlist_node *node;
	struct userspace_mem_region *region;

	if (vmp == NULL)
		return;

	/* Free userspace_mem_regions. */
	hash_for_each_safe(vmp->regions.slot_hash, ctr, node, region, slot_node)
		__vm_mem_region_delete(vmp, region, false);

	/* Free sparsebit arrays. */
	sparsebit_free(&vmp->vpages_valid);
	sparsebit_free(&vmp->vpages_mapped);

	kvm_vm_release(vmp);

	/* Free the structure describing the VM. */
	free(vmp);
}

int kvm_memfd_alloc(size_t size, bool hugepages)
{
	int memfd_flags = MFD_CLOEXEC;
	int fd, r;

	if (hugepages)
		memfd_flags |= MFD_HUGETLB;

	fd = memfd_create("kvm_selftest", memfd_flags);
	TEST_ASSERT(fd != -1, __KVM_SYSCALL_ERROR("memfd_create()", fd));

	r = ftruncate(fd, size);
	TEST_ASSERT(!r, __KVM_SYSCALL_ERROR("ftruncate()", r));

	r = fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, size);
	TEST_ASSERT(!r, __KVM_SYSCALL_ERROR("fallocate()", r));

	return fd;
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

static void vm_userspace_mem_region_gpa_insert(struct rb_root *gpa_tree,
					       struct userspace_mem_region *region)
{
	struct rb_node **cur, *parent;

	for (cur = &gpa_tree->rb_node, parent = NULL; *cur; ) {
		struct userspace_mem_region *cregion;

		cregion = container_of(*cur, typeof(*cregion), gpa_node);
		parent = *cur;
		if (region->region.guest_phys_addr <
		    cregion->region.guest_phys_addr)
			cur = &(*cur)->rb_left;
		else {
			TEST_ASSERT(region->region.guest_phys_addr !=
				    cregion->region.guest_phys_addr,
				    "Duplicate GPA in region tree");

			cur = &(*cur)->rb_right;
		}
	}

	rb_link_node(&region->gpa_node, parent, cur);
	rb_insert_color(&region->gpa_node, gpa_tree);
}

static void vm_userspace_mem_region_hva_insert(struct rb_root *hva_tree,
					       struct userspace_mem_region *region)
{
	struct rb_node **cur, *parent;

	for (cur = &hva_tree->rb_node, parent = NULL; *cur; ) {
		struct userspace_mem_region *cregion;

		cregion = container_of(*cur, typeof(*cregion), hva_node);
		parent = *cur;
		if (region->host_mem < cregion->host_mem)
			cur = &(*cur)->rb_left;
		else {
			TEST_ASSERT(region->host_mem !=
				    cregion->host_mem,
				    "Duplicate HVA in region tree");

			cur = &(*cur)->rb_right;
		}
	}

	rb_link_node(&region->hva_node, parent, cur);
	rb_insert_color(&region->hva_node, hva_tree);
}


int __vm_set_user_memory_region(struct kvm_vm *vm, uint32_t slot, uint32_t flags,
				uint64_t gpa, uint64_t size, void *hva)
{
	struct kvm_userspace_memory_region region = {
		.slot = slot,
		.flags = flags,
		.guest_phys_addr = gpa,
		.memory_size = size,
		.userspace_addr = (uintptr_t)hva,
	};

	return ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &region);
}

void vm_set_user_memory_region(struct kvm_vm *vm, uint32_t slot, uint32_t flags,
			       uint64_t gpa, uint64_t size, void *hva)
{
	int ret = __vm_set_user_memory_region(vm, slot, flags, gpa, size, hva);

	TEST_ASSERT(!ret, "KVM_SET_USER_MEMORY_REGION failed, errno = %d (%s)",
		    errno, strerror(errno));
}

/*
 * VM Userspace Memory Region Add
 *
 * Input Args:
 *   vm - Virtual Machine
 *   src_type - Storage source for this region.
 *              NULL to use anonymous memory.
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
	size_t backing_src_pagesz = get_backing_src_pagesz(src_type);
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
	hash_for_each_possible(vm->regions.slot_hash, region, slot_node,
			       slot) {
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

	/*
	 * When using THP mmap is not guaranteed to returned a hugepage aligned
	 * address so we have to pad the mmap. Padding is not needed for HugeTLB
	 * because mmap will always return an address aligned to the HugeTLB
	 * page size.
	 */
	if (src_type == VM_MEM_SRC_ANONYMOUS_THP)
		alignment = max(backing_src_pagesz, alignment);

	ASSERT_EQ(guest_paddr, align_up(guest_paddr, backing_src_pagesz));

	/* Add enough memory to align up if necessary */
	if (alignment > 1)
		region->mmap_size += alignment;

	region->fd = -1;
	if (backing_src_is_shared(src_type))
		region->fd = kvm_memfd_alloc(region->mmap_size,
					     src_type == VM_MEM_SRC_SHARED_HUGETLB);

	region->mmap_start = mmap(NULL, region->mmap_size,
				  PROT_READ | PROT_WRITE,
				  vm_mem_backing_src_alias(src_type)->flag,
				  region->fd, 0);
	TEST_ASSERT(region->mmap_start != MAP_FAILED,
		    __KVM_SYSCALL_ERROR("mmap()", (int)(unsigned long)MAP_FAILED));

	TEST_ASSERT(!is_backing_src_hugetlb(src_type) ||
		    region->mmap_start == align_ptr_up(region->mmap_start, backing_src_pagesz),
		    "mmap_start %p is not aligned to HugeTLB page size 0x%lx",
		    region->mmap_start, backing_src_pagesz);

	/* Align host address */
	region->host_mem = align_ptr_up(region->mmap_start, alignment);

	/* As needed perform madvise */
	if ((src_type == VM_MEM_SRC_ANONYMOUS ||
	     src_type == VM_MEM_SRC_ANONYMOUS_THP) && thp_configured()) {
		ret = madvise(region->host_mem, npages * vm->page_size,
			      src_type == VM_MEM_SRC_ANONYMOUS ? MADV_NOHUGEPAGE : MADV_HUGEPAGE);
		TEST_ASSERT(ret == 0, "madvise failed, addr: %p length: 0x%lx src_type: %s",
			    region->host_mem, npages * vm->page_size,
			    vm_mem_backing_src_alias(src_type)->name);
	}

	region->unused_phy_pages = sparsebit_alloc();
	sparsebit_set_num(region->unused_phy_pages,
		guest_paddr >> vm->page_shift, npages);
	region->region.slot = slot;
	region->region.flags = flags;
	region->region.guest_phys_addr = guest_paddr;
	region->region.memory_size = npages * vm->page_size;
	region->region.userspace_addr = (uintptr_t) region->host_mem;
	ret = __vm_ioctl(vm, KVM_SET_USER_MEMORY_REGION, &region->region);
	TEST_ASSERT(ret == 0, "KVM_SET_USER_MEMORY_REGION IOCTL failed,\n"
		"  rc: %i errno: %i\n"
		"  slot: %u flags: 0x%x\n"
		"  guest_phys_addr: 0x%lx size: 0x%lx",
		ret, errno, slot, flags,
		guest_paddr, (uint64_t) region->region.memory_size);

	/* Add to quick lookup data structures */
	vm_userspace_mem_region_gpa_insert(&vm->regions.gpa_tree, region);
	vm_userspace_mem_region_hva_insert(&vm->regions.hva_tree, region);
	hash_add(vm->regions.slot_hash, &region->slot_node, slot);

	/* If shared memory, create an alias. */
	if (region->fd >= 0) {
		region->mmap_alias = mmap(NULL, region->mmap_size,
					  PROT_READ | PROT_WRITE,
					  vm_mem_backing_src_alias(src_type)->flag,
					  region->fd, 0);
		TEST_ASSERT(region->mmap_alias != MAP_FAILED,
			    __KVM_SYSCALL_ERROR("mmap()",  (int)(unsigned long)MAP_FAILED));

		/* Align host alias address */
		region->host_alias = align_ptr_up(region->mmap_alias, alignment);
	}
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

	hash_for_each_possible(vm->regions.slot_hash, region, slot_node,
			       memslot)
		if (region->region.slot == memslot)
			return region;

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

	ret = __vm_ioctl(vm, KVM_SET_USER_MEMORY_REGION, &region->region);

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

	ret = __vm_ioctl(vm, KVM_SET_USER_MEMORY_REGION, &region->region);

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
	__vm_mem_region_delete(vm, memslot2region(vm, slot), true);
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

	dev_fd = open_kvm_dev_path_or_exit();

	ret = ioctl(dev_fd, KVM_GET_VCPU_MMAP_SIZE, NULL);
	TEST_ASSERT(ret >= sizeof(struct kvm_run),
		    KVM_IOCTL_ERROR(KVM_GET_VCPU_MMAP_SIZE, ret));

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
	vcpu->fd = __vm_ioctl(vm, KVM_CREATE_VCPU, (void *)(unsigned long)vcpuid);
	TEST_ASSERT(vcpu->fd >= 0, KVM_IOCTL_ERROR(KVM_CREATE_VCPU, vcpu->fd));

	TEST_ASSERT(vcpu_mmap_sz() >= sizeof(*vcpu->state), "vcpu mmap size "
		"smaller than expected, vcpu_mmap_sz: %i expected_min: %zi",
		vcpu_mmap_sz(), sizeof(*vcpu->state));
	vcpu->state = (struct kvm_run *) mmap(NULL, vcpu_mmap_sz(),
		PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->fd, 0);
	TEST_ASSERT(vcpu->state != MAP_FAILED,
		    __KVM_SYSCALL_ERROR("mmap()", (int)(unsigned long)MAP_FAILED));

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
vm_vaddr_t vm_vaddr_alloc(struct kvm_vm *vm, size_t sz, vm_vaddr_t vaddr_min)
{
	uint64_t pages = (sz >> vm->page_shift) + ((sz % vm->page_size) != 0);

	virt_pgd_alloc(vm);
	vm_paddr_t paddr = vm_phy_pages_alloc(vm, pages,
					      KVM_UTIL_MIN_PFN * vm->page_size, 0);

	/*
	 * Find an unused range of virtual page addresses of at least
	 * pages in length.
	 */
	vm_vaddr_t vaddr_start = vm_vaddr_unused_gap(vm, sz, vaddr_min);

	/* Map the virtual pages. */
	for (vm_vaddr_t vaddr = vaddr_start; pages > 0;
		pages--, vaddr += vm->page_size, paddr += vm->page_size) {

		virt_pg_map(vm, vaddr, paddr);

		sparsebit_set(vm->vpages_mapped,
			vaddr >> vm->page_shift);
	}

	return vaddr_start;
}

/*
 * VM Virtual Address Allocate Pages
 *
 * Input Args:
 *   vm - Virtual Machine
 *
 * Output Args: None
 *
 * Return:
 *   Starting guest virtual address
 *
 * Allocates at least N system pages worth of bytes within the virtual address
 * space of the vm.
 */
vm_vaddr_t vm_vaddr_alloc_pages(struct kvm_vm *vm, int nr_pages)
{
	return vm_vaddr_alloc(vm, nr_pages * getpagesize(), KVM_UTIL_MIN_VADDR);
}

/*
 * VM Virtual Address Allocate Page
 *
 * Input Args:
 *   vm - Virtual Machine
 *
 * Output Args: None
 *
 * Return:
 *   Starting guest virtual address
 *
 * Allocates at least one system page worth of bytes within the virtual address
 * space of the vm.
 */
vm_vaddr_t vm_vaddr_alloc_page(struct kvm_vm *vm)
{
	return vm_vaddr_alloc_pages(vm, 1);
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
	      unsigned int npages)
{
	size_t page_size = vm->page_size;
	size_t size = npages * page_size;

	TEST_ASSERT(vaddr + size > vaddr, "Vaddr overflow");
	TEST_ASSERT(paddr + size > paddr, "Paddr overflow");

	while (npages--) {
		virt_pg_map(vm, vaddr, paddr);
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

	region = userspace_mem_region_find(vm, gpa, gpa);
	if (!region) {
		TEST_FAIL("No vm physical memory at 0x%lx", gpa);
		return NULL;
	}

	return (void *)((uintptr_t)region->host_mem
		+ (gpa - region->region.guest_phys_addr));
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
	struct rb_node *node;

	for (node = vm->regions.hva_tree.rb_node; node; ) {
		struct userspace_mem_region *region =
			container_of(node, struct userspace_mem_region, hva_node);

		if (hva >= region->host_mem) {
			if (hva <= (region->host_mem
				+ region->region.memory_size - 1))
				return (vm_paddr_t)((uintptr_t)
					region->region.guest_phys_addr
					+ (hva - (uintptr_t)region->host_mem));

			node = node->rb_right;
		} else
			node = node->rb_left;
	}

	TEST_FAIL("No mapping to a guest physical address, hva: %p", hva);
	return -1;
}

/*
 * Address VM physical to Host Virtual *alias*.
 *
 * Input Args:
 *   vm - Virtual Machine
 *   gpa - VM physical address
 *
 * Output Args: None
 *
 * Return:
 *   Equivalent address within the host virtual *alias* area, or NULL
 *   (without failing the test) if the guest memory is not shared (so
 *   no alias exists).
 *
 * When vm_create() and related functions are called with a shared memory
 * src_type, we also create a writable, shared alias mapping of the
 * underlying guest memory. This allows the host to manipulate guest memory
 * without mapping that memory in the guest's address space. And, for
 * userfaultfd-based demand paging, we can do so without triggering userfaults.
 */
void *addr_gpa2alias(struct kvm_vm *vm, vm_paddr_t gpa)
{
	struct userspace_mem_region *region;
	uintptr_t offset;

	region = userspace_mem_region_find(vm, gpa, gpa);
	if (!region)
		return NULL;

	if (!region->host_alias)
		return NULL;

	offset = gpa - region->region.guest_phys_addr;
	return (void *) ((uintptr_t) region->host_alias + offset);
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
	vm_ioctl(vm, KVM_CREATE_IRQCHIP, NULL);

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
	struct vcpu *vcpu = vcpu_get(vm, vcpuid);

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

	TEST_ASSERT(!ret, KVM_IOCTL_ERROR(KVM_RUN, ret));
}

int _vcpu_run(struct kvm_vm *vm, uint32_t vcpuid)
{
	int rc;

	do {
		rc = __vcpu_run(vm, vcpuid);
	} while (rc == -1 && errno == EINTR);

	assert_on_unhandled_exception(vm, vcpuid);

	return rc;
}

void vcpu_run_complete_io(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu = vcpu_get(vm, vcpuid);
	int ret;

	vcpu->state->immediate_exit = 1;
	ret = __vcpu_run(vm, vcpuid);
	vcpu->state->immediate_exit = 0;

	TEST_ASSERT(ret == -1 && errno == EINTR,
		    "KVM_RUN IOCTL didn't exit immediately, rc: %i, errno: %i",
		    ret, errno);
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

	ret = __vcpu_ioctl(vm, vcpuid, KVM_GET_REG_LIST, &reg_list_n);
	TEST_ASSERT(ret == -1 && errno == E2BIG, "KVM_GET_REG_LIST n=0");
	reg_list = calloc(1, sizeof(*reg_list) + reg_list_n.n * sizeof(__u64));
	reg_list->n = reg_list_n.n;
	vcpu_ioctl(vm, vcpuid, KVM_GET_REG_LIST, reg_list);
	return reg_list;
}

int __vcpu_ioctl(struct kvm_vm *vm, uint32_t vcpuid,
		 unsigned long cmd, void *arg)
{
	struct vcpu *vcpu = vcpu_get(vm, vcpuid);

	return ioctl(vcpu->fd, cmd, arg);
}

void _vcpu_ioctl(struct kvm_vm *vm, uint32_t vcpuid, unsigned long cmd,
		 const char *name, void *arg)
{
	int ret = __vcpu_ioctl(vm, vcpuid, cmd, arg);

	TEST_ASSERT(!ret, __KVM_IOCTL_ERROR(name, ret));
}

void *vcpu_map_dirty_ring(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu = vcpu_get(vm, vcpuid);
	uint32_t size = vm->dirty_ring_size;

	TEST_ASSERT(size > 0, "Should enable dirty ring first");

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

int __vm_ioctl(struct kvm_vm *vm, unsigned long cmd, void *arg)
{
	return ioctl(vm->fd, cmd, arg);
}

void _vm_ioctl(struct kvm_vm *vm, unsigned long cmd, const char *name, void *arg)
{
	int ret = __vm_ioctl(vm, cmd, arg);

	TEST_ASSERT(!ret, __KVM_IOCTL_ERROR(name, ret));
}

/*
 * Device Ioctl
 */

int __kvm_has_device_attr(int dev_fd, uint32_t group, uint64_t attr)
{
	struct kvm_device_attr attribute = {
		.group = group,
		.attr = attr,
		.flags = 0,
	};

	return ioctl(dev_fd, KVM_HAS_DEVICE_ATTR, &attribute);
}

int __kvm_test_create_device(struct kvm_vm *vm, uint64_t type)
{
	struct kvm_create_device create_dev = {
		.type = type,
		.flags = KVM_CREATE_DEVICE_TEST,
	};

	return __vm_ioctl(vm, KVM_CREATE_DEVICE, &create_dev);
}

int __kvm_create_device(struct kvm_vm *vm, uint64_t type)
{
	struct kvm_create_device create_dev = {
		.type = type,
		.fd = -1,
		.flags = 0,
	};
	int err;

	err = __vm_ioctl(vm, KVM_CREATE_DEVICE, &create_dev);
	TEST_ASSERT(err <= 0, "KVM_CREATE_DEVICE shouldn't return a positive value");
	return err ? : create_dev.fd;
}

int __kvm_device_attr_get(int dev_fd, uint32_t group, uint64_t attr, void *val)
{
	struct kvm_device_attr kvmattr = {
		.group = group,
		.attr = attr,
		.flags = 0,
		.addr = (uintptr_t)val,
	};

	return __kvm_ioctl(dev_fd, KVM_GET_DEVICE_ATTR, &kvmattr);
}

int __kvm_device_attr_set(int dev_fd, uint32_t group, uint64_t attr, void *val)
{
	struct kvm_device_attr kvmattr = {
		.group = group,
		.attr = attr,
		.flags = 0,
		.addr = (uintptr_t)val,
	};

	return __kvm_ioctl(dev_fd, KVM_SET_DEVICE_ATTR, &kvmattr);
}

int __vcpu_device_attr_get(struct kvm_vm *vm, uint32_t vcpuid, uint32_t group,
			   uint64_t attr, void *val)
{
	return __kvm_device_attr_get(vcpu_get(vm, vcpuid)->fd, group, attr, val);
}

void vcpu_device_attr_get(struct kvm_vm *vm, uint32_t vcpuid, uint32_t group,
			  uint64_t attr, void *val)
{
	kvm_device_attr_get(vcpu_get(vm, vcpuid)->fd, group, attr, val);
}

int __vcpu_device_attr_set(struct kvm_vm *vm, uint32_t vcpuid, uint32_t group,
			   uint64_t attr, void *val)
{
	return __kvm_device_attr_set(vcpu_get(vm, vcpuid)->fd, group, attr, val);
}

void vcpu_device_attr_set(struct kvm_vm *vm, uint32_t vcpuid, uint32_t group,
			  uint64_t attr, void *val)
{
	kvm_device_attr_set(vcpu_get(vm, vcpuid)->fd, group, attr, val);
}

int __vcpu_has_device_attr(struct kvm_vm *vm, uint32_t vcpuid, uint32_t group,
			  uint64_t attr)
{
	struct vcpu *vcpu = vcpu_get(vm, vcpuid);

	return __kvm_has_device_attr(vcpu->fd, group, attr);
}

/*
 * IRQ related functions.
 */

int _kvm_irq_line(struct kvm_vm *vm, uint32_t irq, int level)
{
	struct kvm_irq_level irq_level = {
		.irq    = irq,
		.level  = level,
	};

	return __vm_ioctl(vm, KVM_IRQ_LINE, &irq_level);
}

void kvm_irq_line(struct kvm_vm *vm, uint32_t irq, int level)
{
	int ret = _kvm_irq_line(vm, irq, level);

	TEST_ASSERT(ret >= 0, KVM_IOCTL_ERROR(KVM_IRQ_LINE, ret));
}

struct kvm_irq_routing *kvm_gsi_routing_create(void)
{
	struct kvm_irq_routing *routing;
	size_t size;

	size = sizeof(struct kvm_irq_routing);
	/* Allocate space for the max number of entries: this wastes 196 KBs. */
	size += KVM_MAX_IRQ_ROUTES * sizeof(struct kvm_irq_routing_entry);
	routing = calloc(1, size);
	assert(routing);

	return routing;
}

void kvm_gsi_routing_irqchip_add(struct kvm_irq_routing *routing,
		uint32_t gsi, uint32_t pin)
{
	int i;

	assert(routing);
	assert(routing->nr < KVM_MAX_IRQ_ROUTES);

	i = routing->nr;
	routing->entries[i].gsi = gsi;
	routing->entries[i].type = KVM_IRQ_ROUTING_IRQCHIP;
	routing->entries[i].flags = 0;
	routing->entries[i].u.irqchip.irqchip = 0;
	routing->entries[i].u.irqchip.pin = pin;
	routing->nr++;
}

int _kvm_gsi_routing_write(struct kvm_vm *vm, struct kvm_irq_routing *routing)
{
	int ret;

	assert(routing);
	ret = __vm_ioctl(vm, KVM_SET_GSI_ROUTING, routing);
	free(routing);

	return ret;
}

void kvm_gsi_routing_write(struct kvm_vm *vm, struct kvm_irq_routing *routing)
{
	int ret;

	ret = _kvm_gsi_routing_write(vm, routing);
	TEST_ASSERT(!ret, KVM_IOCTL_ERROR(KVM_SET_GSI_ROUTING, ret));
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
	int ctr;
	struct userspace_mem_region *region;
	struct vcpu *vcpu;

	fprintf(stream, "%*smode: 0x%x\n", indent, "", vm->mode);
	fprintf(stream, "%*sfd: %i\n", indent, "", vm->fd);
	fprintf(stream, "%*spage_size: 0x%x\n", indent, "", vm->page_size);
	fprintf(stream, "%*sMem Regions:\n", indent, "");
	hash_for_each(vm->regions.slot_hash, ctr, region, slot_node) {
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

/* Arbitrary minimum physical address used for virtual translation tables. */
#define KVM_GUEST_PAGE_TABLE_MIN_PADDR 0x180000

vm_paddr_t vm_alloc_page_table(struct kvm_vm *vm)
{
	return vm_phy_page_alloc(vm, KVM_GUEST_PAGE_TABLE_MIN_PADDR, 0);
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
		close(open_kvm_dev_path_or_exit());
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

unsigned long __attribute__((weak)) vm_compute_max_gfn(struct kvm_vm *vm)
{
	return ((1ULL << vm->pa_bits) >> vm->page_shift) - 1;
}

uint64_t vm_get_max_gfn(struct kvm_vm *vm)
{
	return vm->max_gfn;
}

int vm_get_kvm_fd(struct kvm_vm *vm)
{
	return vm->kvm_fd;
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
