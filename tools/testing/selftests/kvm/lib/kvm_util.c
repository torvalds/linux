// SPDX-License-Identifier: GPL-2.0-only
/*
 * tools/testing/selftests/kvm/lib/kvm_util.c
 *
 * Copyright (C) 2018, Google LLC.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "kvm_util_internal.h"

#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/kernel.h>

#define KVM_UTIL_PGS_PER_HUGEPG 512
#define KVM_UTIL_MIN_PFN	2

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

static void vm_open(struct kvm_vm *vm, int perm, unsigned long type)
{
	vm->kvm_fd = open(KVM_DEV_PATH, perm);
	if (vm->kvm_fd < 0)
		exit(KSFT_SKIP);

	if (!kvm_check_cap(KVM_CAP_IMMEDIATE_EXIT)) {
		fprintf(stderr, "immediate_exit not available, skipping test\n");
		exit(KSFT_SKIP);
	}

	vm->fd = ioctl(vm->kvm_fd, KVM_CREATE_VM, type);
	TEST_ASSERT(vm->fd >= 0, "KVM_CREATE_VM ioctl failed, "
		"rc: %i errno: %i", vm->fd, errno);
}

const char * const vm_guest_mode_string[] = {
	"PA-bits:52, VA-bits:48, 4K pages",
	"PA-bits:52, VA-bits:48, 64K pages",
	"PA-bits:48, VA-bits:48, 4K pages",
	"PA-bits:48, VA-bits:48, 64K pages",
	"PA-bits:40, VA-bits:48, 4K pages",
	"PA-bits:40, VA-bits:48, 64K pages",
};
_Static_assert(sizeof(vm_guest_mode_string)/sizeof(char *) == NUM_VM_MODES,
	       "Missing new mode strings?");

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
struct kvm_vm *_vm_create(enum vm_guest_mode mode, uint64_t phy_pages,
			  int perm, unsigned long type)
{
	struct kvm_vm *vm;

	vm = calloc(1, sizeof(*vm));
	TEST_ASSERT(vm != NULL, "Insufficient Memory");

	vm->mode = mode;
	vm->type = type;
	vm_open(vm, perm, type);

	/* Setup mode specific traits. */
	switch (vm->mode) {
	case VM_MODE_P52V48_4K:
		vm->pgtable_levels = 4;
		vm->pa_bits = 52;
		vm->va_bits = 48;
		vm->page_size = 0x1000;
		vm->page_shift = 12;
		break;
	case VM_MODE_P52V48_64K:
		vm->pgtable_levels = 3;
		vm->pa_bits = 52;
		vm->va_bits = 48;
		vm->page_size = 0x10000;
		vm->page_shift = 16;
		break;
	case VM_MODE_P48V48_4K:
		vm->pgtable_levels = 4;
		vm->pa_bits = 48;
		vm->va_bits = 48;
		vm->page_size = 0x1000;
		vm->page_shift = 12;
		break;
	case VM_MODE_P48V48_64K:
		vm->pgtable_levels = 3;
		vm->pa_bits = 48;
		vm->va_bits = 48;
		vm->page_size = 0x10000;
		vm->page_shift = 16;
		break;
	case VM_MODE_P40V48_4K:
		vm->pgtable_levels = 4;
		vm->pa_bits = 40;
		vm->va_bits = 48;
		vm->page_size = 0x1000;
		vm->page_shift = 12;
		break;
	case VM_MODE_P40V48_64K:
		vm->pgtable_levels = 3;
		vm->pa_bits = 40;
		vm->va_bits = 48;
		vm->page_size = 0x10000;
		vm->page_shift = 16;
		break;
	default:
		TEST_ASSERT(false, "Unknown guest mode, mode: 0x%x", mode);
	}

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

struct kvm_vm *vm_create(enum vm_guest_mode mode, uint64_t phy_pages, int perm)
{
	return _vm_create(mode, phy_pages, perm, 0);
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

	vm_open(vmp, perm, vmp->type);
	if (vmp->has_irqchip)
		vm_create_irqchip(vmp);

	for (region = vmp->userspace_mem_region_head; region;
		region = region->next) {
		int ret = ioctl(vmp->fd, KVM_SET_USER_MEMORY_REGION, &region->region);
		TEST_ASSERT(ret == 0, "KVM_SET_USER_MEMORY_REGION IOCTL failed,\n"
			    "  rc: %i errno: %i\n"
			    "  slot: %u flags: 0x%x\n"
			    "  guest_phys_addr: 0x%lx size: 0x%lx",
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
		    strerror(-ret));
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
		    strerror(-ret));
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

	for (region = vm->userspace_mem_region_head; region;
		region = region->next) {
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
	struct vcpu *vcpup;

	for (vcpup = vm->vcpu_head; vcpup; vcpup = vcpup->next) {
		if (vcpup->id == vcpuid)
			return vcpup;
	}

	return NULL;
}

/*
 * VM VCPU Remove
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *
 * Output Args: None
 *
 * Return: None, TEST_ASSERT failures for all error conditions
 *
 * Within the VM specified by vm, removes the VCPU given by vcpuid.
 */
static void vm_vcpu_rm(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int ret;

	ret = munmap(vcpu->state, sizeof(*vcpu->state));
	TEST_ASSERT(ret == 0, "munmap of VCPU fd failed, rc: %i "
		"errno: %i", ret, errno);
	close(vcpu->fd);
	TEST_ASSERT(ret == 0, "Close of VCPU fd failed, rc: %i "
		"errno: %i", ret, errno);

	if (vcpu->next)
		vcpu->next->prev = vcpu->prev;
	if (vcpu->prev)
		vcpu->prev->next = vcpu->next;
	else
		vm->vcpu_head = vcpu->next;
	free(vcpu);
}

void kvm_vm_release(struct kvm_vm *vmp)
{
	int ret;

	while (vmp->vcpu_head)
		vm_vcpu_rm(vmp, vmp->vcpu_head->id);

	ret = close(vmp->fd);
	TEST_ASSERT(ret == 0, "Close of vm fd failed,\n"
		"  vmp->fd: %i rc: %i errno: %i", vmp->fd, ret, errno);

	close(vmp->kvm_fd);
	TEST_ASSERT(ret == 0, "Close of /dev/kvm fd failed,\n"
		"  vmp->kvm_fd: %i rc: %i errno: %i", vmp->kvm_fd, ret, errno);
}

/*
 * Destroys and frees the VM pointed to by vmp.
 */
void kvm_vm_free(struct kvm_vm *vmp)
{
	int ret;

	if (vmp == NULL)
		return;

	/* Free userspace_mem_regions. */
	while (vmp->userspace_mem_region_head) {
		struct userspace_mem_region *region
			= vmp->userspace_mem_region_head;

		region->region.memory_size = 0;
		ret = ioctl(vmp->fd, KVM_SET_USER_MEMORY_REGION,
			&region->region);
		TEST_ASSERT(ret == 0, "KVM_SET_USER_MEMORY_REGION IOCTL failed, "
			"rc: %i errno: %i", ret, errno);

		vmp->userspace_mem_region_head = region->next;
		sparsebit_free(&region->unused_phy_pages);
		ret = munmap(region->mmap_start, region->mmap_size);
		TEST_ASSERT(ret == 0, "munmap failed, rc: %i errno: %i",
			    ret, errno);

		free(region);
	}

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
		TEST_ASSERT(false, "overlapping userspace_mem_region already "
			"exists\n"
			"  requested guest_paddr: 0x%lx npages: 0x%lx "
			"page_size: 0x%x\n"
			"  existing guest_paddr: 0x%lx size: 0x%lx",
			guest_paddr, npages, vm->page_size,
			(uint64_t) region->region.guest_phys_addr,
			(uint64_t) region->region.memory_size);

	/* Confirm no region with the requested slot already exists. */
	for (region = vm->userspace_mem_region_head; region;
		region = region->next) {
		if (region->region.slot == slot)
			break;
	}
	if (region != NULL)
		TEST_ASSERT(false, "A mem region with the requested slot "
			"already exists.\n"
			"  requested slot: %u paddr: 0x%lx npages: 0x%lx\n"
			"  existing slot: %u paddr: 0x%lx size: 0x%lx",
			slot, guest_paddr, npages,
			region->region.slot,
			(uint64_t) region->region.guest_phys_addr,
			(uint64_t) region->region.memory_size);

	/* Allocate and initialize new mem region structure. */
	region = calloc(1, sizeof(*region));
	TEST_ASSERT(region != NULL, "Insufficient Memory");
	region->mmap_size = npages * vm->page_size;

	/* Enough memory to align up to a huge page. */
	if (src_type == VM_MEM_SRC_ANONYMOUS_THP)
		region->mmap_size += huge_page_size;
	region->mmap_start = mmap(NULL, region->mmap_size,
				  PROT_READ | PROT_WRITE,
				  MAP_PRIVATE | MAP_ANONYMOUS
				  | (src_type == VM_MEM_SRC_ANONYMOUS_HUGETLB ? MAP_HUGETLB : 0),
				  -1, 0);
	TEST_ASSERT(region->mmap_start != MAP_FAILED,
		    "test_malloc failed, mmap_start: %p errno: %i",
		    region->mmap_start, errno);

	/* Align THP allocation up to start of a huge page. */
	region->host_mem = align(region->mmap_start,
				 src_type == VM_MEM_SRC_ANONYMOUS_THP ?  huge_page_size : 1);

	/* As needed perform madvise */
	if (src_type == VM_MEM_SRC_ANONYMOUS || src_type == VM_MEM_SRC_ANONYMOUS_THP) {
		ret = madvise(region->host_mem, npages * vm->page_size,
			     src_type == VM_MEM_SRC_ANONYMOUS ? MADV_NOHUGEPAGE : MADV_HUGEPAGE);
		TEST_ASSERT(ret == 0, "madvise failed,\n"
			    "  addr: %p\n"
			    "  length: 0x%lx\n"
			    "  src_type: %x",
			    region->host_mem, npages * vm->page_size, src_type);
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
	if (vm->userspace_mem_region_head)
		vm->userspace_mem_region_head->prev = region;
	region->next = vm->userspace_mem_region_head;
	vm->userspace_mem_region_head = region;
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
static struct userspace_mem_region *
memslot2region(struct kvm_vm *vm, uint32_t memslot)
{
	struct userspace_mem_region *region;

	for (region = vm->userspace_mem_region_head; region;
		region = region->next) {
		if (region->region.slot == memslot)
			break;
	}
	if (region == NULL) {
		fprintf(stderr, "No mem region with the requested slot found,\n"
			"  requested slot: %u\n", memslot);
		fputs("---- vm dump ----\n", stderr);
		vm_dump(stderr, vm, 2);
		TEST_ASSERT(false, "Mem region not found");
	}

	return region;
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
 * Creates and adds to the VM specified by vm and virtual CPU with
 * the ID given by vcpuid.
 */
void vm_vcpu_add(struct kvm_vm *vm, uint32_t vcpuid, int pgd_memslot,
		 int gdt_memslot)
{
	struct vcpu *vcpu;

	/* Confirm a vcpu with the specified id doesn't already exist. */
	vcpu = vcpu_find(vm, vcpuid);
	if (vcpu != NULL)
		TEST_ASSERT(false, "vcpu with the specified id "
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
	vcpu->state = (struct kvm_run *) mmap(NULL, sizeof(*vcpu->state),
		PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->fd, 0);
	TEST_ASSERT(vcpu->state != MAP_FAILED, "mmap vcpu_state failed, "
		"vcpu id: %u errno: %i", vcpuid, errno);

	/* Add to linked-list of VCPUs. */
	if (vm->vcpu_head)
		vm->vcpu_head->prev = vcpu;
	vcpu->next = vm->vcpu_head;
	vm->vcpu_head = vcpu;

	vcpu_setup(vm, vcpuid, pgd_memslot, gdt_memslot);
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
	TEST_ASSERT(false, "No vaddr of specified pages available, "
		"pages: 0x%lx", pages);

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
 *   size - The size of the range to map
 *   pgd_memslot - Memory region slot for new virtual translation tables
 *
 * Output Args: None
 *
 * Return: None
 *
 * Within the VM given by vm, creates a virtual translation for the
 * page range starting at vaddr to the page range starting at paddr.
 */
void virt_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
	      size_t size, uint32_t pgd_memslot)
{
	size_t page_size = vm->page_size;
	size_t npages = size / page_size;

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
	for (region = vm->userspace_mem_region_head; region;
	     region = region->next) {
		if ((gpa >= region->region.guest_phys_addr)
			&& (gpa <= (region->region.guest_phys_addr
				+ region->region.memory_size - 1)))
			return (void *) ((uintptr_t) region->host_mem
				+ (gpa - region->region.guest_phys_addr));
	}

	TEST_ASSERT(false, "No vm physical memory at 0x%lx", gpa);
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
	for (region = vm->userspace_mem_region_head; region;
	     region = region->next) {
		if ((hva >= region->host_mem)
			&& (hva <= (region->host_mem
				+ region->region.memory_size - 1)))
			return (vm_paddr_t) ((uintptr_t)
				region->region.guest_phys_addr
				+ (hva - (uintptr_t) region->host_mem));
	}

	TEST_ASSERT(false, "No mapping to a guest physical address, "
		"hva: %p", hva);
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
	return rc;
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

	ret = ioctl(vm->fd, cmd, arg);
	TEST_ASSERT(ret == 0, "vm ioctl %lu failed, rc: %i errno: %i (%s)",
		cmd, ret, errno, strerror(errno));
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
	for (region = vm->userspace_mem_region_head; region;
		region = region->next) {
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
	for (vcpu = vm->vcpu_head; vcpu; vcpu = vcpu->next)
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
