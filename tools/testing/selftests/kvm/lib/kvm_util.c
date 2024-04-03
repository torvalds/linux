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
#include <sched.h>
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
	__TEST_REQUIRE(fd >= 0 || errno != ENOENT, "Cannot open %s: %s", path, strerror(errno));
	TEST_ASSERT(fd >= 0, "Failed to open '%s'", path);

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

static ssize_t get_module_param(const char *module_name, const char *param,
				void *buffer, size_t buffer_size)
{
	const int path_size = 128;
	char path[path_size];
	ssize_t bytes_read;
	int fd, r;

	r = snprintf(path, path_size, "/sys/module/%s/parameters/%s",
		     module_name, param);
	TEST_ASSERT(r < path_size,
		    "Failed to construct sysfs path in %d bytes.", path_size);

	fd = open_path_or_exit(path, O_RDONLY);

	bytes_read = read(fd, buffer, buffer_size);
	TEST_ASSERT(bytes_read > 0, "read(%s) returned %ld, wanted %ld bytes",
		    path, bytes_read, buffer_size);

	r = close(fd);
	TEST_ASSERT(!r, "close(%s) failed", path);
	return bytes_read;
}

static int get_module_param_integer(const char *module_name, const char *param)
{
	/*
	 * 16 bytes to hold a 64-bit value (1 byte per char), 1 byte for the
	 * NUL char, and 1 byte because the kernel sucks and inserts a newline
	 * at the end.
	 */
	char value[16 + 1 + 1];
	ssize_t r;

	memset(value, '\0', sizeof(value));

	r = get_module_param(module_name, param, value, sizeof(value));
	TEST_ASSERT(value[r - 1] == '\n',
		    "Expected trailing newline, got char '%c'", value[r - 1]);

	/*
	 * Squash the newline, otherwise atoi_paranoid() will complain about
	 * trailing non-NUL characters in the string.
	 */
	value[r - 1] = '\0';
	return atoi_paranoid(value);
}

static bool get_module_param_bool(const char *module_name, const char *param)
{
	char value;
	ssize_t r;

	r = get_module_param(module_name, param, &value, sizeof(value));
	TEST_ASSERT_EQ(r, 1);

	if (value == 'Y')
		return true;
	else if (value == 'N')
		return false;

	TEST_FAIL("Unrecognized value '%c' for boolean module param", value);
}

bool get_kvm_param_bool(const char *param)
{
	return get_module_param_bool("kvm", param);
}

bool get_kvm_intel_param_bool(const char *param)
{
	return get_module_param_bool("kvm_intel", param);
}

bool get_kvm_amd_param_bool(const char *param)
{
	return get_module_param_bool("kvm_amd", param);
}

int get_kvm_param_integer(const char *param)
{
	return get_module_param_integer("kvm", param);
}

int get_kvm_intel_param_integer(const char *param)
{
	return get_module_param_integer("kvm_intel", param);
}

int get_kvm_amd_param_integer(const char *param)
{
	return get_module_param_integer("kvm_amd", param);
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
unsigned int kvm_check_cap(long cap)
{
	int ret;
	int kvm_fd;

	kvm_fd = open_kvm_dev_path_or_exit();
	ret = __kvm_ioctl(kvm_fd, KVM_CHECK_EXTENSION, (void *)cap);
	TEST_ASSERT(ret >= 0, KVM_IOCTL_ERROR(KVM_CHECK_EXTENSION, ret));

	close(kvm_fd);

	return (unsigned int)ret;
}

void vm_enable_dirty_ring(struct kvm_vm *vm, uint32_t ring_size)
{
	if (vm_check_cap(vm, KVM_CAP_DIRTY_LOG_RING_ACQ_REL))
		vm_enable_cap(vm, KVM_CAP_DIRTY_LOG_RING_ACQ_REL, ring_size);
	else
		vm_enable_cap(vm, KVM_CAP_DIRTY_LOG_RING, ring_size);
	vm->dirty_ring_size = ring_size;
}

static void vm_open(struct kvm_vm *vm)
{
	vm->kvm_fd = _open_kvm_dev_path_or_exit(O_RDWR);

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_IMMEDIATE_EXIT));

	vm->fd = __kvm_ioctl(vm->kvm_fd, KVM_CREATE_VM, (void *)vm->type);
	TEST_ASSERT(vm->fd >= 0, KVM_IOCTL_ERROR(KVM_CREATE_VM, vm->fd));
}

const char *vm_guest_mode_string(uint32_t i)
{
	static const char * const strings[] = {
		[VM_MODE_P52V48_4K]	= "PA-bits:52,  VA-bits:48,  4K pages",
		[VM_MODE_P52V48_16K]	= "PA-bits:52,  VA-bits:48, 16K pages",
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
	[VM_MODE_P52V48_16K]	= { 52, 48,  0x4000, 14 },
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

/*
 * Initializes vm->vpages_valid to match the canonical VA space of the
 * architecture.
 *
 * The default implementation is valid for architectures which split the
 * range addressed by a single page table into a low and high region
 * based on the MSB of the VA. On architectures with this behavior
 * the VA region spans [0, 2^(va_bits - 1)), [-(2^(va_bits - 1), -1].
 */
__weak void vm_vaddr_populate_bitmap(struct kvm_vm *vm)
{
	sparsebit_set_num(vm->vpages_valid,
		0, (1ULL << (vm->va_bits - 1)) >> vm->page_shift);
	sparsebit_set_num(vm->vpages_valid,
		(~((1ULL << (vm->va_bits - 1)) - 1)) >> vm->page_shift,
		(1ULL << (vm->va_bits - 1)) >> vm->page_shift);
}

struct kvm_vm *____vm_create(struct vm_shape shape)
{
	struct kvm_vm *vm;

	vm = calloc(1, sizeof(*vm));
	TEST_ASSERT(vm != NULL, "Insufficient Memory");

	INIT_LIST_HEAD(&vm->vcpus);
	vm->regions.gpa_tree = RB_ROOT;
	vm->regions.hva_tree = RB_ROOT;
	hash_init(vm->regions.slot_hash);

	vm->mode = shape.mode;
	vm->type = shape.type;
	vm->subtype = shape.subtype;

	vm->pa_bits = vm_guest_mode_params[vm->mode].pa_bits;
	vm->va_bits = vm_guest_mode_params[vm->mode].va_bits;
	vm->page_size = vm_guest_mode_params[vm->mode].page_size;
	vm->page_shift = vm_guest_mode_params[vm->mode].page_shift;

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
	case VM_MODE_P52V48_16K:
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
		kvm_init_vm_address_properties(vm);
		/*
		 * Ignore KVM support for 5-level paging (vm->va_bits == 57),
		 * it doesn't take effect unless a CR4.LA57 is set, which it
		 * isn't for this mode (48-bit virtual address space).
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
		TEST_FAIL("Unknown guest mode: 0x%x", vm->mode);
	}

#ifdef __aarch64__
	TEST_ASSERT(!vm->type, "ARM doesn't support test-provided types");
	if (vm->pa_bits != 40)
		vm->type = KVM_VM_TYPE_ARM_IPA_SIZE(vm->pa_bits);
#endif

	vm_open(vm);

	/* Limit to VA-bit canonical virtual addresses. */
	vm->vpages_valid = sparsebit_alloc();
	vm_vaddr_populate_bitmap(vm);

	/* Limit physical addresses to PA-bits. */
	vm->max_gfn = vm_compute_max_gfn(vm);

	/* Allocate and setup memory for guest. */
	vm->vpages_mapped = sparsebit_alloc();

	return vm;
}

static uint64_t vm_nr_pages_required(enum vm_guest_mode mode,
				     uint32_t nr_runnable_vcpus,
				     uint64_t extra_mem_pages)
{
	uint64_t page_size = vm_guest_mode_params[mode].page_size;
	uint64_t nr_pages;

	TEST_ASSERT(nr_runnable_vcpus,
		    "Use vm_create_barebones() for VMs that _never_ have vCPUs");

	TEST_ASSERT(nr_runnable_vcpus <= kvm_check_cap(KVM_CAP_MAX_VCPUS),
		    "nr_vcpus = %d too large for host, max-vcpus = %d",
		    nr_runnable_vcpus, kvm_check_cap(KVM_CAP_MAX_VCPUS));

	/*
	 * Arbitrarily allocate 512 pages (2mb when page size is 4kb) for the
	 * test code and other per-VM assets that will be loaded into memslot0.
	 */
	nr_pages = 512;

	/* Account for the per-vCPU stacks on behalf of the test. */
	nr_pages += nr_runnable_vcpus * DEFAULT_STACK_PGS;

	/*
	 * Account for the number of pages needed for the page tables.  The
	 * maximum page table size for a memory region will be when the
	 * smallest page size is used. Considering each page contains x page
	 * table descriptors, the total extra size for page tables (for extra
	 * N pages) will be: N/x+N/x^2+N/x^3+... which is definitely smaller
	 * than N/x*2.
	 */
	nr_pages += (nr_pages + extra_mem_pages) / PTES_PER_MIN_PAGE * 2;

	/* Account for the number of pages needed by ucall. */
	nr_pages += ucall_nr_pages_required(page_size);

	return vm_adjust_num_guest_pages(mode, nr_pages);
}

struct kvm_vm *__vm_create(struct vm_shape shape, uint32_t nr_runnable_vcpus,
			   uint64_t nr_extra_pages)
{
	uint64_t nr_pages = vm_nr_pages_required(shape.mode, nr_runnable_vcpus,
						 nr_extra_pages);
	struct userspace_mem_region *slot0;
	struct kvm_vm *vm;
	int i;

	pr_debug("%s: mode='%s' type='%d', pages='%ld'\n", __func__,
		 vm_guest_mode_string(shape.mode), shape.type, nr_pages);

	vm = ____vm_create(shape);

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS, 0, 0, nr_pages, 0);
	for (i = 0; i < NR_MEM_REGIONS; i++)
		vm->memslots[i] = 0;

	kvm_vm_elf_load(vm, program_invocation_name);

	/*
	 * TODO: Add proper defines to protect the library's memslots, and then
	 * carve out memslot1 for the ucall MMIO address.  KVM treats writes to
	 * read-only memslots as MMIO, and creating a read-only memslot for the
	 * MMIO region would prevent silently clobbering the MMIO region.
	 */
	slot0 = memslot2region(vm, 0);
	ucall_init(vm, slot0->region.guest_phys_addr + slot0->region.memory_size);

	kvm_arch_vm_post_create(vm);

	return vm;
}

/*
 * VM Create with customized parameters
 *
 * Input Args:
 *   mode - VM Mode (e.g. VM_MODE_P52V48_4K)
 *   nr_vcpus - VCPU count
 *   extra_mem_pages - Non-slot0 physical memory total size
 *   guest_code - Guest entry point
 *   vcpuids - VCPU IDs
 *
 * Output Args: None
 *
 * Return:
 *   Pointer to opaque structure that describes the created VM.
 *
 * Creates a VM with the mode specified by mode (e.g. VM_MODE_P52V48_4K).
 * extra_mem_pages is only used to calculate the maximum page table size,
 * no real memory allocation for non-slot0 memory in this function.
 */
struct kvm_vm *__vm_create_with_vcpus(struct vm_shape shape, uint32_t nr_vcpus,
				      uint64_t extra_mem_pages,
				      void *guest_code, struct kvm_vcpu *vcpus[])
{
	struct kvm_vm *vm;
	int i;

	TEST_ASSERT(!nr_vcpus || vcpus, "Must provide vCPU array");

	vm = __vm_create(shape, nr_vcpus, extra_mem_pages);

	for (i = 0; i < nr_vcpus; ++i)
		vcpus[i] = vm_vcpu_add(vm, i, guest_code);

	return vm;
}

struct kvm_vm *__vm_create_shape_with_one_vcpu(struct vm_shape shape,
					       struct kvm_vcpu **vcpu,
					       uint64_t extra_mem_pages,
					       void *guest_code)
{
	struct kvm_vcpu *vcpus[1];
	struct kvm_vm *vm;

	vm = __vm_create_with_vcpus(shape, 1, extra_mem_pages, guest_code, vcpus);

	*vcpu = vcpus[0];
	return vm;
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
		int ret = ioctl(vmp->fd, KVM_SET_USER_MEMORY_REGION2, &region->region);

		TEST_ASSERT(ret == 0, "KVM_SET_USER_MEMORY_REGION2 IOCTL failed,\n"
			    "  rc: %i errno: %i\n"
			    "  slot: %u flags: 0x%x\n"
			    "  guest_phys_addr: 0x%llx size: 0x%llx",
			    ret, errno, region->region.slot,
			    region->region.flags,
			    region->region.guest_phys_addr,
			    region->region.memory_size);
	}
}

__weak struct kvm_vcpu *vm_arch_vcpu_recreate(struct kvm_vm *vm,
					      uint32_t vcpu_id)
{
	return __vm_vcpu_add(vm, vcpu_id);
}

struct kvm_vcpu *vm_recreate_with_one_vcpu(struct kvm_vm *vm)
{
	kvm_vm_restart(vm);

	return vm_vcpu_recreate(vm, 0);
}

void kvm_pin_this_task_to_pcpu(uint32_t pcpu)
{
	cpu_set_t mask;
	int r;

	CPU_ZERO(&mask);
	CPU_SET(pcpu, &mask);
	r = sched_setaffinity(0, sizeof(mask), &mask);
	TEST_ASSERT(!r, "sched_setaffinity() failed for pCPU '%u'.", pcpu);
}

static uint32_t parse_pcpu(const char *cpu_str, const cpu_set_t *allowed_mask)
{
	uint32_t pcpu = atoi_non_negative("CPU number", cpu_str);

	TEST_ASSERT(CPU_ISSET(pcpu, allowed_mask),
		    "Not allowed to run on pCPU '%d', check cgroups?", pcpu);
	return pcpu;
}

void kvm_print_vcpu_pinning_help(void)
{
	const char *name = program_invocation_name;

	printf(" -c: Pin tasks to physical CPUs.  Takes a list of comma separated\n"
	       "     values (target pCPU), one for each vCPU, plus an optional\n"
	       "     entry for the main application task (specified via entry\n"
	       "     <nr_vcpus + 1>).  If used, entries must be provided for all\n"
	       "     vCPUs, i.e. pinning vCPUs is all or nothing.\n\n"
	       "     E.g. to create 3 vCPUs, pin vCPU0=>pCPU22, vCPU1=>pCPU23,\n"
	       "     vCPU2=>pCPU24, and pin the application task to pCPU50:\n\n"
	       "         %s -v 3 -c 22,23,24,50\n\n"
	       "     To leave the application task unpinned, drop the final entry:\n\n"
	       "         %s -v 3 -c 22,23,24\n\n"
	       "     (default: no pinning)\n", name, name);
}

void kvm_parse_vcpu_pinning(const char *pcpus_string, uint32_t vcpu_to_pcpu[],
			    int nr_vcpus)
{
	cpu_set_t allowed_mask;
	char *cpu, *cpu_list;
	char delim[2] = ",";
	int i, r;

	cpu_list = strdup(pcpus_string);
	TEST_ASSERT(cpu_list, "strdup() allocation failed.");

	r = sched_getaffinity(0, sizeof(allowed_mask), &allowed_mask);
	TEST_ASSERT(!r, "sched_getaffinity() failed");

	cpu = strtok(cpu_list, delim);

	/* 1. Get all pcpus for vcpus. */
	for (i = 0; i < nr_vcpus; i++) {
		TEST_ASSERT(cpu, "pCPU not provided for vCPU '%d'", i);
		vcpu_to_pcpu[i] = parse_pcpu(cpu, &allowed_mask);
		cpu = strtok(NULL, delim);
	}

	/* 2. Check if the main worker needs to be pinned. */
	if (cpu) {
		kvm_pin_this_task_to_pcpu(parse_pcpu(cpu, &allowed_mask));
		cpu = strtok(NULL, delim);
	}

	TEST_ASSERT(!cpu, "pCPU list contains trailing garbage characters '%s'", cpu);
	free(cpu_list);
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

__weak void vcpu_arch_free(struct kvm_vcpu *vcpu)
{

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
static void vm_vcpu_rm(struct kvm_vm *vm, struct kvm_vcpu *vcpu)
{
	int ret;

	if (vcpu->dirty_gfns) {
		ret = munmap(vcpu->dirty_gfns, vm->dirty_ring_size);
		TEST_ASSERT(!ret, __KVM_SYSCALL_ERROR("munmap()", ret));
		vcpu->dirty_gfns = NULL;
	}

	ret = munmap(vcpu->run, vcpu_mmap_sz());
	TEST_ASSERT(!ret, __KVM_SYSCALL_ERROR("munmap()", ret));

	ret = close(vcpu->fd);
	TEST_ASSERT(!ret,  __KVM_SYSCALL_ERROR("close()", ret));

	list_del(&vcpu->list);

	vcpu_arch_free(vcpu);
	free(vcpu);
}

void kvm_vm_release(struct kvm_vm *vmp)
{
	struct kvm_vcpu *vcpu, *tmp;
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
	vm_ioctl(vm, KVM_SET_USER_MEMORY_REGION2, &region->region);

	sparsebit_free(&region->unused_phy_pages);
	sparsebit_free(&region->protected_phy_pages);
	ret = munmap(region->mmap_start, region->mmap_size);
	TEST_ASSERT(!ret, __KVM_SYSCALL_ERROR("munmap()", ret));
	if (region->fd >= 0) {
		/* There's an extra map when using shared memory. */
		ret = munmap(region->mmap_alias, region->mmap_size);
		TEST_ASSERT(!ret, __KVM_SYSCALL_ERROR("munmap()", ret));
		close(region->fd);
	}
	if (region->region.guest_memfd >= 0)
		close(region->region.guest_memfd);

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

	/* Free cached stats metadata and close FD */
	if (vmp->stats_fd) {
		free(vmp->stats_desc);
		close(vmp->stats_fd);
	}

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

int __vm_set_user_memory_region2(struct kvm_vm *vm, uint32_t slot, uint32_t flags,
				 uint64_t gpa, uint64_t size, void *hva,
				 uint32_t guest_memfd, uint64_t guest_memfd_offset)
{
	struct kvm_userspace_memory_region2 region = {
		.slot = slot,
		.flags = flags,
		.guest_phys_addr = gpa,
		.memory_size = size,
		.userspace_addr = (uintptr_t)hva,
		.guest_memfd = guest_memfd,
		.guest_memfd_offset = guest_memfd_offset,
	};

	return ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION2, &region);
}

void vm_set_user_memory_region2(struct kvm_vm *vm, uint32_t slot, uint32_t flags,
				uint64_t gpa, uint64_t size, void *hva,
				uint32_t guest_memfd, uint64_t guest_memfd_offset)
{
	int ret = __vm_set_user_memory_region2(vm, slot, flags, gpa, size, hva,
					       guest_memfd, guest_memfd_offset);

	TEST_ASSERT(!ret, "KVM_SET_USER_MEMORY_REGION2 failed, errno = %d (%s)",
		    errno, strerror(errno));
}


/* FIXME: This thing needs to be ripped apart and rewritten. */
void vm_mem_add(struct kvm_vm *vm, enum vm_mem_backing_src_type src_type,
		uint64_t guest_paddr, uint32_t slot, uint64_t npages,
		uint32_t flags, int guest_memfd, uint64_t guest_memfd_offset)
{
	int ret;
	struct userspace_mem_region *region;
	size_t backing_src_pagesz = get_backing_src_pagesz(src_type);
	size_t mem_size = npages * vm->page_size;
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
	region->mmap_size = mem_size;

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

	TEST_ASSERT_EQ(guest_paddr, align_up(guest_paddr, backing_src_pagesz));

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
		ret = madvise(region->host_mem, mem_size,
			      src_type == VM_MEM_SRC_ANONYMOUS ? MADV_NOHUGEPAGE : MADV_HUGEPAGE);
		TEST_ASSERT(ret == 0, "madvise failed, addr: %p length: 0x%lx src_type: %s",
			    region->host_mem, mem_size,
			    vm_mem_backing_src_alias(src_type)->name);
	}

	region->backing_src_type = src_type;

	if (flags & KVM_MEM_GUEST_MEMFD) {
		if (guest_memfd < 0) {
			uint32_t guest_memfd_flags = 0;
			TEST_ASSERT(!guest_memfd_offset,
				    "Offset must be zero when creating new guest_memfd");
			guest_memfd = vm_create_guest_memfd(vm, mem_size, guest_memfd_flags);
		} else {
			/*
			 * Install a unique fd for each memslot so that the fd
			 * can be closed when the region is deleted without
			 * needing to track if the fd is owned by the framework
			 * or by the caller.
			 */
			guest_memfd = dup(guest_memfd);
			TEST_ASSERT(guest_memfd >= 0, __KVM_SYSCALL_ERROR("dup()", guest_memfd));
		}

		region->region.guest_memfd = guest_memfd;
		region->region.guest_memfd_offset = guest_memfd_offset;
	} else {
		region->region.guest_memfd = -1;
	}

	region->unused_phy_pages = sparsebit_alloc();
	if (vm_arch_has_protected_memory(vm))
		region->protected_phy_pages = sparsebit_alloc();
	sparsebit_set_num(region->unused_phy_pages,
		guest_paddr >> vm->page_shift, npages);
	region->region.slot = slot;
	region->region.flags = flags;
	region->region.guest_phys_addr = guest_paddr;
	region->region.memory_size = npages * vm->page_size;
	region->region.userspace_addr = (uintptr_t) region->host_mem;
	ret = __vm_ioctl(vm, KVM_SET_USER_MEMORY_REGION2, &region->region);
	TEST_ASSERT(ret == 0, "KVM_SET_USER_MEMORY_REGION2 IOCTL failed,\n"
		"  rc: %i errno: %i\n"
		"  slot: %u flags: 0x%x\n"
		"  guest_phys_addr: 0x%lx size: 0x%lx guest_memfd: %d",
		ret, errno, slot, flags,
		guest_paddr, (uint64_t) region->region.memory_size,
		region->region.guest_memfd);

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

void vm_userspace_mem_region_add(struct kvm_vm *vm,
				 enum vm_mem_backing_src_type src_type,
				 uint64_t guest_paddr, uint32_t slot,
				 uint64_t npages, uint32_t flags)
{
	vm_mem_add(vm, src_type, guest_paddr, slot, npages, flags, -1, 0);
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

	ret = __vm_ioctl(vm, KVM_SET_USER_MEMORY_REGION2, &region->region);

	TEST_ASSERT(ret == 0, "KVM_SET_USER_MEMORY_REGION2 IOCTL failed,\n"
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

	ret = __vm_ioctl(vm, KVM_SET_USER_MEMORY_REGION2, &region->region);

	TEST_ASSERT(!ret, "KVM_SET_USER_MEMORY_REGION2 failed\n"
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

void vm_guest_mem_fallocate(struct kvm_vm *vm, uint64_t base, uint64_t size,
			    bool punch_hole)
{
	const int mode = FALLOC_FL_KEEP_SIZE | (punch_hole ? FALLOC_FL_PUNCH_HOLE : 0);
	struct userspace_mem_region *region;
	uint64_t end = base + size;
	uint64_t gpa, len;
	off_t fd_offset;
	int ret;

	for (gpa = base; gpa < end; gpa += len) {
		uint64_t offset;

		region = userspace_mem_region_find(vm, gpa, gpa);
		TEST_ASSERT(region && region->region.flags & KVM_MEM_GUEST_MEMFD,
			    "Private memory region not found for GPA 0x%lx", gpa);

		offset = gpa - region->region.guest_phys_addr;
		fd_offset = region->region.guest_memfd_offset + offset;
		len = min_t(uint64_t, end - gpa, region->region.memory_size - offset);

		ret = fallocate(region->region.guest_memfd, mode, fd_offset, len);
		TEST_ASSERT(!ret, "fallocate() failed to %s at %lx (len = %lu), fd = %d, mode = %x, offset = %lx",
			    punch_hole ? "punch hole" : "allocate", gpa, len,
			    region->region.guest_memfd, mode, fd_offset);
	}
}

/* Returns the size of a vCPU's kvm_run structure. */
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

static bool vcpu_exists(struct kvm_vm *vm, uint32_t vcpu_id)
{
	struct kvm_vcpu *vcpu;

	list_for_each_entry(vcpu, &vm->vcpus, list) {
		if (vcpu->id == vcpu_id)
			return true;
	}

	return false;
}

/*
 * Adds a virtual CPU to the VM specified by vm with the ID given by vcpu_id.
 * No additional vCPU setup is done.  Returns the vCPU.
 */
struct kvm_vcpu *__vm_vcpu_add(struct kvm_vm *vm, uint32_t vcpu_id)
{
	struct kvm_vcpu *vcpu;

	/* Confirm a vcpu with the specified id doesn't already exist. */
	TEST_ASSERT(!vcpu_exists(vm, vcpu_id), "vCPU%d already exists", vcpu_id);

	/* Allocate and initialize new vcpu structure. */
	vcpu = calloc(1, sizeof(*vcpu));
	TEST_ASSERT(vcpu != NULL, "Insufficient Memory");

	vcpu->vm = vm;
	vcpu->id = vcpu_id;
	vcpu->fd = __vm_ioctl(vm, KVM_CREATE_VCPU, (void *)(unsigned long)vcpu_id);
	TEST_ASSERT_VM_VCPU_IOCTL(vcpu->fd >= 0, KVM_CREATE_VCPU, vcpu->fd, vm);

	TEST_ASSERT(vcpu_mmap_sz() >= sizeof(*vcpu->run), "vcpu mmap size "
		"smaller than expected, vcpu_mmap_sz: %i expected_min: %zi",
		vcpu_mmap_sz(), sizeof(*vcpu->run));
	vcpu->run = (struct kvm_run *) mmap(NULL, vcpu_mmap_sz(),
		PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->fd, 0);
	TEST_ASSERT(vcpu->run != MAP_FAILED,
		    __KVM_SYSCALL_ERROR("mmap()", (int)(unsigned long)MAP_FAILED));

	/* Add to linked-list of VCPUs. */
	list_add(&vcpu->list, &vm->vcpus);

	return vcpu;
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
vm_vaddr_t vm_vaddr_unused_gap(struct kvm_vm *vm, size_t sz,
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

static vm_vaddr_t ____vm_vaddr_alloc(struct kvm_vm *vm, size_t sz,
				     vm_vaddr_t vaddr_min,
				     enum kvm_mem_region_type type,
				     bool protected)
{
	uint64_t pages = (sz >> vm->page_shift) + ((sz % vm->page_size) != 0);

	virt_pgd_alloc(vm);
	vm_paddr_t paddr = __vm_phy_pages_alloc(vm, pages,
						KVM_UTIL_MIN_PFN * vm->page_size,
						vm->memslots[type], protected);

	/*
	 * Find an unused range of virtual page addresses of at least
	 * pages in length.
	 */
	vm_vaddr_t vaddr_start = vm_vaddr_unused_gap(vm, sz, vaddr_min);

	/* Map the virtual pages. */
	for (vm_vaddr_t vaddr = vaddr_start; pages > 0;
		pages--, vaddr += vm->page_size, paddr += vm->page_size) {

		virt_pg_map(vm, vaddr, paddr);

		sparsebit_set(vm->vpages_mapped, vaddr >> vm->page_shift);
	}

	return vaddr_start;
}

vm_vaddr_t __vm_vaddr_alloc(struct kvm_vm *vm, size_t sz, vm_vaddr_t vaddr_min,
			    enum kvm_mem_region_type type)
{
	return ____vm_vaddr_alloc(vm, sz, vaddr_min, type,
				  vm_arch_has_protected_memory(vm));
}

vm_vaddr_t vm_vaddr_alloc_shared(struct kvm_vm *vm, size_t sz,
				 vm_vaddr_t vaddr_min,
				 enum kvm_mem_region_type type)
{
	return ____vm_vaddr_alloc(vm, sz, vaddr_min, type, false);
}

/*
 * VM Virtual Address Allocate
 *
 * Input Args:
 *   vm - Virtual Machine
 *   sz - Size in bytes
 *   vaddr_min - Minimum starting virtual address
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
 * a page. The allocated physical space comes from the TEST_DATA memory region.
 */
vm_vaddr_t vm_vaddr_alloc(struct kvm_vm *vm, size_t sz, vm_vaddr_t vaddr_min)
{
	return __vm_vaddr_alloc(vm, sz, vaddr_min, MEM_REGION_TEST_DATA);
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

vm_vaddr_t __vm_vaddr_alloc_page(struct kvm_vm *vm, enum kvm_mem_region_type type)
{
	return __vm_vaddr_alloc(vm, getpagesize(), KVM_UTIL_MIN_VADDR, type);
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
		sparsebit_set(vm->vpages_mapped, vaddr >> vm->page_shift);

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

	gpa = vm_untag_gpa(vm, gpa);

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
 * Create a writable, shared virtual=>physical alias for the specific GPA.
 * The primary use case is to allow the host selftest to manipulate guest
 * memory without mapping said memory in the guest's address space. And, for
 * userfaultfd-based demand paging, to do so without triggering userfaults.
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

/* Create an interrupt controller chip for the specified VM. */
void vm_create_irqchip(struct kvm_vm *vm)
{
	vm_ioctl(vm, KVM_CREATE_IRQCHIP, NULL);

	vm->has_irqchip = true;
}

int _vcpu_run(struct kvm_vcpu *vcpu)
{
	int rc;

	do {
		rc = __vcpu_run(vcpu);
	} while (rc == -1 && errno == EINTR);

	assert_on_unhandled_exception(vcpu);

	return rc;
}

/*
 * Invoke KVM_RUN on a vCPU until KVM returns something other than -EINTR.
 * Assert if the KVM returns an error (other than -EINTR).
 */
void vcpu_run(struct kvm_vcpu *vcpu)
{
	int ret = _vcpu_run(vcpu);

	TEST_ASSERT(!ret, KVM_IOCTL_ERROR(KVM_RUN, ret));
}

void vcpu_run_complete_io(struct kvm_vcpu *vcpu)
{
	int ret;

	vcpu->run->immediate_exit = 1;
	ret = __vcpu_run(vcpu);
	vcpu->run->immediate_exit = 0;

	TEST_ASSERT(ret == -1 && errno == EINTR,
		    "KVM_RUN IOCTL didn't exit immediately, rc: %i, errno: %i",
		    ret, errno);
}

/*
 * Get the list of guest registers which are supported for
 * KVM_GET_ONE_REG/KVM_SET_ONE_REG ioctls.  Returns a kvm_reg_list pointer,
 * it is the caller's responsibility to free the list.
 */
struct kvm_reg_list *vcpu_get_reg_list(struct kvm_vcpu *vcpu)
{
	struct kvm_reg_list reg_list_n = { .n = 0 }, *reg_list;
	int ret;

	ret = __vcpu_ioctl(vcpu, KVM_GET_REG_LIST, &reg_list_n);
	TEST_ASSERT(ret == -1 && errno == E2BIG, "KVM_GET_REG_LIST n=0");

	reg_list = calloc(1, sizeof(*reg_list) + reg_list_n.n * sizeof(__u64));
	reg_list->n = reg_list_n.n;
	vcpu_ioctl(vcpu, KVM_GET_REG_LIST, reg_list);
	return reg_list;
}

void *vcpu_map_dirty_ring(struct kvm_vcpu *vcpu)
{
	uint32_t page_size = getpagesize();
	uint32_t size = vcpu->vm->dirty_ring_size;

	TEST_ASSERT(size > 0, "Should enable dirty ring first");

	if (!vcpu->dirty_gfns) {
		void *addr;

		addr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, vcpu->fd,
			    page_size * KVM_DIRTY_LOG_PAGE_OFFSET);
		TEST_ASSERT(addr == MAP_FAILED, "Dirty ring mapped private");

		addr = mmap(NULL, size, PROT_READ | PROT_EXEC, MAP_PRIVATE, vcpu->fd,
			    page_size * KVM_DIRTY_LOG_PAGE_OFFSET);
		TEST_ASSERT(addr == MAP_FAILED, "Dirty ring mapped exec");

		addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->fd,
			    page_size * KVM_DIRTY_LOG_PAGE_OFFSET);
		TEST_ASSERT(addr != MAP_FAILED, "Dirty ring map failed");

		vcpu->dirty_gfns = addr;
		vcpu->dirty_gfns_count = size / sizeof(struct kvm_dirty_gfn);
	}

	return vcpu->dirty_gfns;
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
	struct kvm_vcpu *vcpu;

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
		if (region->protected_phy_pages) {
			fprintf(stream, "%*sprotected_phy_pages: ", indent + 2, "");
			sparsebit_dump(stream, region->protected_phy_pages, 0);
		}
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
		vcpu_dump(stream, vcpu, indent + 2);
}

#define KVM_EXIT_STRING(x) {KVM_EXIT_##x, #x}

/* Known KVM exit reasons */
static struct exit_reason {
	unsigned int reason;
	const char *name;
} exit_reasons_known[] = {
	KVM_EXIT_STRING(UNKNOWN),
	KVM_EXIT_STRING(EXCEPTION),
	KVM_EXIT_STRING(IO),
	KVM_EXIT_STRING(HYPERCALL),
	KVM_EXIT_STRING(DEBUG),
	KVM_EXIT_STRING(HLT),
	KVM_EXIT_STRING(MMIO),
	KVM_EXIT_STRING(IRQ_WINDOW_OPEN),
	KVM_EXIT_STRING(SHUTDOWN),
	KVM_EXIT_STRING(FAIL_ENTRY),
	KVM_EXIT_STRING(INTR),
	KVM_EXIT_STRING(SET_TPR),
	KVM_EXIT_STRING(TPR_ACCESS),
	KVM_EXIT_STRING(S390_SIEIC),
	KVM_EXIT_STRING(S390_RESET),
	KVM_EXIT_STRING(DCR),
	KVM_EXIT_STRING(NMI),
	KVM_EXIT_STRING(INTERNAL_ERROR),
	KVM_EXIT_STRING(OSI),
	KVM_EXIT_STRING(PAPR_HCALL),
	KVM_EXIT_STRING(S390_UCONTROL),
	KVM_EXIT_STRING(WATCHDOG),
	KVM_EXIT_STRING(S390_TSCH),
	KVM_EXIT_STRING(EPR),
	KVM_EXIT_STRING(SYSTEM_EVENT),
	KVM_EXIT_STRING(S390_STSI),
	KVM_EXIT_STRING(IOAPIC_EOI),
	KVM_EXIT_STRING(HYPERV),
	KVM_EXIT_STRING(ARM_NISV),
	KVM_EXIT_STRING(X86_RDMSR),
	KVM_EXIT_STRING(X86_WRMSR),
	KVM_EXIT_STRING(DIRTY_RING_FULL),
	KVM_EXIT_STRING(AP_RESET_HOLD),
	KVM_EXIT_STRING(X86_BUS_LOCK),
	KVM_EXIT_STRING(XEN),
	KVM_EXIT_STRING(RISCV_SBI),
	KVM_EXIT_STRING(RISCV_CSR),
	KVM_EXIT_STRING(NOTIFY),
#ifdef KVM_EXIT_MEMORY_NOT_PRESENT
	KVM_EXIT_STRING(MEMORY_NOT_PRESENT),
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
 *   protected - True if the pages will be used as protected/private memory
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
vm_paddr_t __vm_phy_pages_alloc(struct kvm_vm *vm, size_t num,
				vm_paddr_t paddr_min, uint32_t memslot,
				bool protected)
{
	struct userspace_mem_region *region;
	sparsebit_idx_t pg, base;

	TEST_ASSERT(num > 0, "Must allocate at least one page");

	TEST_ASSERT((paddr_min % vm->page_size) == 0, "Min physical address "
		"not divisible by page size.\n"
		"  paddr_min: 0x%lx page_size: 0x%x",
		paddr_min, vm->page_size);

	region = memslot2region(vm, memslot);
	TEST_ASSERT(!protected || region->protected_phy_pages,
		    "Region doesn't support protected memory");

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

	for (pg = base; pg < base + num; ++pg) {
		sparsebit_clear(region->unused_phy_pages, pg);
		if (protected)
			sparsebit_set(region->protected_phy_pages, pg);
	}

	return base * vm->page_size;
}

vm_paddr_t vm_phy_page_alloc(struct kvm_vm *vm, vm_paddr_t paddr_min,
			     uint32_t memslot)
{
	return vm_phy_pages_alloc(vm, 1, paddr_min, memslot);
}

vm_paddr_t vm_alloc_page_table(struct kvm_vm *vm)
{
	return vm_phy_page_alloc(vm, KVM_GUEST_PAGE_TABLE_MIN_PADDR,
				 vm->memslots[MEM_REGION_PT]);
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

unsigned long __weak vm_compute_max_gfn(struct kvm_vm *vm)
{
	return ((1ULL << vm->pa_bits) >> vm->page_shift) - 1;
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

/*
 * Read binary stats descriptors
 *
 * Input Args:
 *   stats_fd - the file descriptor for the binary stats file from which to read
 *   header - the binary stats metadata header corresponding to the given FD
 *
 * Output Args: None
 *
 * Return:
 *   A pointer to a newly allocated series of stat descriptors.
 *   Caller is responsible for freeing the returned kvm_stats_desc.
 *
 * Read the stats descriptors from the binary stats interface.
 */
struct kvm_stats_desc *read_stats_descriptors(int stats_fd,
					      struct kvm_stats_header *header)
{
	struct kvm_stats_desc *stats_desc;
	ssize_t desc_size, total_size, ret;

	desc_size = get_stats_descriptor_size(header);
	total_size = header->num_desc * desc_size;

	stats_desc = calloc(header->num_desc, desc_size);
	TEST_ASSERT(stats_desc, "Allocate memory for stats descriptors");

	ret = pread(stats_fd, stats_desc, total_size, header->desc_offset);
	TEST_ASSERT(ret == total_size, "Read KVM stats descriptors");

	return stats_desc;
}

/*
 * Read stat data for a particular stat
 *
 * Input Args:
 *   stats_fd - the file descriptor for the binary stats file from which to read
 *   header - the binary stats metadata header corresponding to the given FD
 *   desc - the binary stat metadata for the particular stat to be read
 *   max_elements - the maximum number of 8-byte values to read into data
 *
 * Output Args:
 *   data - the buffer into which stat data should be read
 *
 * Read the data values of a specified stat from the binary stats interface.
 */
void read_stat_data(int stats_fd, struct kvm_stats_header *header,
		    struct kvm_stats_desc *desc, uint64_t *data,
		    size_t max_elements)
{
	size_t nr_elements = min_t(ssize_t, desc->size, max_elements);
	size_t size = nr_elements * sizeof(*data);
	ssize_t ret;

	TEST_ASSERT(desc->size, "No elements in stat '%s'", desc->name);
	TEST_ASSERT(max_elements, "Zero elements requested for stat '%s'", desc->name);

	ret = pread(stats_fd, data, size,
		    header->data_offset + desc->offset);

	TEST_ASSERT(ret >= 0, "pread() failed on stat '%s', errno: %i (%s)",
		    desc->name, errno, strerror(errno));
	TEST_ASSERT(ret == size,
		    "pread() on stat '%s' read %ld bytes, wanted %lu bytes",
		    desc->name, size, ret);
}

/*
 * Read the data of the named stat
 *
 * Input Args:
 *   vm - the VM for which the stat should be read
 *   stat_name - the name of the stat to read
 *   max_elements - the maximum number of 8-byte values to read into data
 *
 * Output Args:
 *   data - the buffer into which stat data should be read
 *
 * Read the data values of a specified stat from the binary stats interface.
 */
void __vm_get_stat(struct kvm_vm *vm, const char *stat_name, uint64_t *data,
		   size_t max_elements)
{
	struct kvm_stats_desc *desc;
	size_t size_desc;
	int i;

	if (!vm->stats_fd) {
		vm->stats_fd = vm_get_stats_fd(vm);
		read_stats_header(vm->stats_fd, &vm->stats_header);
		vm->stats_desc = read_stats_descriptors(vm->stats_fd,
							&vm->stats_header);
	}

	size_desc = get_stats_descriptor_size(&vm->stats_header);

	for (i = 0; i < vm->stats_header.num_desc; ++i) {
		desc = (void *)vm->stats_desc + (i * size_desc);

		if (strcmp(desc->name, stat_name))
			continue;

		read_stat_data(vm->stats_fd, &vm->stats_header, desc,
			       data, max_elements);

		break;
	}
}

__weak void kvm_arch_vm_post_create(struct kvm_vm *vm)
{
}

__weak void kvm_selftest_arch_init(void)
{
}

void __attribute((constructor)) kvm_selftest_init(void)
{
	/* Tell stdout not to buffer its content. */
	setbuf(stdout, NULL);

	kvm_selftest_arch_init();
}

bool vm_is_gpa_protected(struct kvm_vm *vm, vm_paddr_t paddr)
{
	sparsebit_idx_t pg = 0;
	struct userspace_mem_region *region;

	if (!vm_arch_has_protected_memory(vm))
		return false;

	region = userspace_mem_region_find(vm, paddr, paddr);
	TEST_ASSERT(region, "No vm physical memory at 0x%lx", paddr);

	pg = paddr >> vm->page_shift;
	return sparsebit_is_set(region->protected_phy_pages, pg);
}
