// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/compiler.h>

#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>

/*
 * s390x needs at least 1MB alignment, and the x86_64 MOVE/DELETE tests need a
 * 2MB sized and aligned region so that the initial region corresponds to
 * exactly one large page.
 */
#define MEM_REGION_SIZE		0x200000

#ifdef __x86_64__
/*
 * Somewhat arbitrary location and slot, intended to not overlap anything.
 */
#define MEM_REGION_GPA		0xc0000000
#define MEM_REGION_SLOT		10

static const uint64_t MMIO_VAL = 0xbeefull;

extern const uint64_t final_rip_start;
extern const uint64_t final_rip_end;

static sem_t vcpu_ready;

static inline uint64_t guest_spin_on_val(uint64_t spin_val)
{
	uint64_t val;

	do {
		val = READ_ONCE(*((uint64_t *)MEM_REGION_GPA));
	} while (val == spin_val);

	GUEST_SYNC(0);
	return val;
}

static void *vcpu_worker(void *data)
{
	struct kvm_vcpu *vcpu = data;
	struct kvm_run *run = vcpu->run;
	struct ucall uc;
	uint64_t cmd;

	/*
	 * Loop until the guest is done.  Re-enter the guest on all MMIO exits,
	 * which will occur if the guest attempts to access a memslot after it
	 * has been deleted or while it is being moved .
	 */
	while (1) {
		vcpu_run(vcpu);

		if (run->exit_reason == KVM_EXIT_IO) {
			cmd = get_ucall(vcpu, &uc);
			if (cmd != UCALL_SYNC)
				break;

			sem_post(&vcpu_ready);
			continue;
		}

		if (run->exit_reason != KVM_EXIT_MMIO)
			break;

		TEST_ASSERT(!run->mmio.is_write, "Unexpected exit mmio write");
		TEST_ASSERT(run->mmio.len == 8,
			    "Unexpected exit mmio size = %u", run->mmio.len);

		TEST_ASSERT(run->mmio.phys_addr == MEM_REGION_GPA,
			    "Unexpected exit mmio address = 0x%llx",
			    run->mmio.phys_addr);
		memcpy(run->mmio.data, &MMIO_VAL, 8);
	}

	if (run->exit_reason == KVM_EXIT_IO && cmd == UCALL_ABORT)
		REPORT_GUEST_ASSERT_1(uc, "val = %lu");

	return NULL;
}

static void wait_for_vcpu(void)
{
	struct timespec ts;

	TEST_ASSERT(!clock_gettime(CLOCK_REALTIME, &ts),
		    "clock_gettime() failed: %d\n", errno);

	ts.tv_sec += 2;
	TEST_ASSERT(!sem_timedwait(&vcpu_ready, &ts),
		    "sem_timedwait() failed: %d\n", errno);

	/* Wait for the vCPU thread to reenter the guest. */
	usleep(100000);
}

static struct kvm_vm *spawn_vm(struct kvm_vcpu **vcpu, pthread_t *vcpu_thread,
			       void *guest_code)
{
	struct kvm_vm *vm;
	uint64_t *hva;
	uint64_t gpa;

	vm = vm_create_with_one_vcpu(vcpu, guest_code);

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS_THP,
				    MEM_REGION_GPA, MEM_REGION_SLOT,
				    MEM_REGION_SIZE / getpagesize(), 0);

	/*
	 * Allocate and map two pages so that the GPA accessed by guest_code()
	 * stays valid across the memslot move.
	 */
	gpa = vm_phy_pages_alloc(vm, 2, MEM_REGION_GPA, MEM_REGION_SLOT);
	TEST_ASSERT(gpa == MEM_REGION_GPA, "Failed vm_phy_pages_alloc\n");

	virt_map(vm, MEM_REGION_GPA, MEM_REGION_GPA, 2);

	/* Ditto for the host mapping so that both pages can be zeroed. */
	hva = addr_gpa2hva(vm, MEM_REGION_GPA);
	memset(hva, 0, 2 * 4096);

	pthread_create(vcpu_thread, NULL, vcpu_worker, *vcpu);

	/* Ensure the guest thread is spun up. */
	wait_for_vcpu();

	return vm;
}


static void guest_code_move_memory_region(void)
{
	uint64_t val;

	GUEST_SYNC(0);

	/*
	 * Spin until the memory region starts getting moved to a
	 * misaligned address.
	 * Every region move may or may not trigger MMIO, as the
	 * window where the memslot is invalid is usually quite small.
	 */
	val = guest_spin_on_val(0);
	GUEST_ASSERT_1(val == 1 || val == MMIO_VAL, val);

	/* Spin until the misaligning memory region move completes. */
	val = guest_spin_on_val(MMIO_VAL);
	GUEST_ASSERT_1(val == 1 || val == 0, val);

	/* Spin until the memory region starts to get re-aligned. */
	val = guest_spin_on_val(0);
	GUEST_ASSERT_1(val == 1 || val == MMIO_VAL, val);

	/* Spin until the re-aligning memory region move completes. */
	val = guest_spin_on_val(MMIO_VAL);
	GUEST_ASSERT_1(val == 1, val);

	GUEST_DONE();
}

static void test_move_memory_region(void)
{
	pthread_t vcpu_thread;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	uint64_t *hva;

	vm = spawn_vm(&vcpu, &vcpu_thread, guest_code_move_memory_region);

	hva = addr_gpa2hva(vm, MEM_REGION_GPA);

	/*
	 * Shift the region's base GPA.  The guest should not see "2" as the
	 * hva->gpa translation is misaligned, i.e. the guest is accessing a
	 * different host pfn.
	 */
	vm_mem_region_move(vm, MEM_REGION_SLOT, MEM_REGION_GPA - 4096);
	WRITE_ONCE(*hva, 2);

	/*
	 * The guest _might_ see an invalid memslot and trigger MMIO, but it's
	 * a tiny window.  Spin and defer the sync until the memslot is
	 * restored and guest behavior is once again deterministic.
	 */
	usleep(100000);

	/*
	 * Note, value in memory needs to be changed *before* restoring the
	 * memslot, else the guest could race the update and see "2".
	 */
	WRITE_ONCE(*hva, 1);

	/* Restore the original base, the guest should see "1". */
	vm_mem_region_move(vm, MEM_REGION_SLOT, MEM_REGION_GPA);
	wait_for_vcpu();
	/* Defered sync from when the memslot was misaligned (above). */
	wait_for_vcpu();

	pthread_join(vcpu_thread, NULL);

	kvm_vm_free(vm);
}

static void guest_code_delete_memory_region(void)
{
	uint64_t val;

	GUEST_SYNC(0);

	/* Spin until the memory region is deleted. */
	val = guest_spin_on_val(0);
	GUEST_ASSERT_1(val == MMIO_VAL, val);

	/* Spin until the memory region is recreated. */
	val = guest_spin_on_val(MMIO_VAL);
	GUEST_ASSERT_1(val == 0, val);

	/* Spin until the memory region is deleted. */
	val = guest_spin_on_val(0);
	GUEST_ASSERT_1(val == MMIO_VAL, val);

	asm("1:\n\t"
	    ".pushsection .rodata\n\t"
	    ".global final_rip_start\n\t"
	    "final_rip_start: .quad 1b\n\t"
	    ".popsection");

	/* Spin indefinitely (until the code memslot is deleted). */
	guest_spin_on_val(MMIO_VAL);

	asm("1:\n\t"
	    ".pushsection .rodata\n\t"
	    ".global final_rip_end\n\t"
	    "final_rip_end: .quad 1b\n\t"
	    ".popsection");

	GUEST_ASSERT_1(0, 0);
}

static void test_delete_memory_region(void)
{
	pthread_t vcpu_thread;
	struct kvm_vcpu *vcpu;
	struct kvm_regs regs;
	struct kvm_run *run;
	struct kvm_vm *vm;

	vm = spawn_vm(&vcpu, &vcpu_thread, guest_code_delete_memory_region);

	/* Delete the memory region, the guest should not die. */
	vm_mem_region_delete(vm, MEM_REGION_SLOT);
	wait_for_vcpu();

	/* Recreate the memory region.  The guest should see "0". */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS_THP,
				    MEM_REGION_GPA, MEM_REGION_SLOT,
				    MEM_REGION_SIZE / getpagesize(), 0);
	wait_for_vcpu();

	/* Delete the region again so that there's only one memslot left. */
	vm_mem_region_delete(vm, MEM_REGION_SLOT);
	wait_for_vcpu();

	/*
	 * Delete the primary memslot.  This should cause an emulation error or
	 * shutdown due to the page tables getting nuked.
	 */
	vm_mem_region_delete(vm, 0);

	pthread_join(vcpu_thread, NULL);

	run = vcpu->run;

	TEST_ASSERT(run->exit_reason == KVM_EXIT_SHUTDOWN ||
		    run->exit_reason == KVM_EXIT_INTERNAL_ERROR,
		    "Unexpected exit reason = %d", run->exit_reason);

	vcpu_regs_get(vcpu, &regs);

	/*
	 * On AMD, after KVM_EXIT_SHUTDOWN the VMCB has been reinitialized already,
	 * so the instruction pointer would point to the reset vector.
	 */
	if (run->exit_reason == KVM_EXIT_INTERNAL_ERROR)
		TEST_ASSERT(regs.rip >= final_rip_start &&
			    regs.rip < final_rip_end,
			    "Bad rip, expected 0x%lx - 0x%lx, got 0x%llx\n",
			    final_rip_start, final_rip_end, regs.rip);

	kvm_vm_free(vm);
}

static void test_zero_memory_regions(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;

	pr_info("Testing KVM_RUN with zero added memory regions\n");

	vm = vm_create_barebones();
	vcpu = __vm_vcpu_add(vm, 0);

	vm_ioctl(vm, KVM_SET_NR_MMU_PAGES, (void *)64ul);
	vcpu_run(vcpu);

	run = vcpu->run;
	TEST_ASSERT(run->exit_reason == KVM_EXIT_INTERNAL_ERROR,
		    "Unexpected exit_reason = %u\n", run->exit_reason);

	kvm_vm_free(vm);
}
#endif /* __x86_64__ */

/*
 * Test it can be added memory slots up to KVM_CAP_NR_MEMSLOTS, then any
 * tentative to add further slots should fail.
 */
static void test_add_max_memory_regions(void)
{
	int ret;
	struct kvm_vm *vm;
	uint32_t max_mem_slots;
	uint32_t slot;
	void *mem, *mem_aligned, *mem_extra;
	size_t alignment;

#ifdef __s390x__
	/* On s390x, the host address must be aligned to 1M (due to PGSTEs) */
	alignment = 0x100000;
#else
	alignment = 1;
#endif

	max_mem_slots = kvm_check_cap(KVM_CAP_NR_MEMSLOTS);
	TEST_ASSERT(max_mem_slots > 0,
		    "KVM_CAP_NR_MEMSLOTS should be greater than 0");
	pr_info("Allowed number of memory slots: %i\n", max_mem_slots);

	vm = vm_create_barebones();

	/* Check it can be added memory slots up to the maximum allowed */
	pr_info("Adding slots 0..%i, each memory region with %dK size\n",
		(max_mem_slots - 1), MEM_REGION_SIZE >> 10);

	mem = mmap(NULL, (size_t)max_mem_slots * MEM_REGION_SIZE + alignment,
		   PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	TEST_ASSERT(mem != MAP_FAILED, "Failed to mmap() host");
	mem_aligned = (void *)(((size_t) mem + alignment - 1) & ~(alignment - 1));

	for (slot = 0; slot < max_mem_slots; slot++)
		vm_set_user_memory_region(vm, slot, 0,
					  ((uint64_t)slot * MEM_REGION_SIZE),
					  MEM_REGION_SIZE,
					  mem_aligned + (uint64_t)slot * MEM_REGION_SIZE);

	/* Check it cannot be added memory slots beyond the limit */
	mem_extra = mmap(NULL, MEM_REGION_SIZE, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	TEST_ASSERT(mem_extra != MAP_FAILED, "Failed to mmap() host");

	ret = __vm_set_user_memory_region(vm, max_mem_slots, 0,
					  (uint64_t)max_mem_slots * MEM_REGION_SIZE,
					  MEM_REGION_SIZE, mem_extra);
	TEST_ASSERT(ret == -1 && errno == EINVAL,
		    "Adding one more memory slot should fail with EINVAL");

	munmap(mem, (size_t)max_mem_slots * MEM_REGION_SIZE + alignment);
	munmap(mem_extra, MEM_REGION_SIZE);
	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
#ifdef __x86_64__
	int i, loops;
#endif

#ifdef __x86_64__
	/*
	 * FIXME: the zero-memslot test fails on aarch64 and s390x because
	 * KVM_RUN fails with ENOEXEC or EFAULT.
	 */
	test_zero_memory_regions();
#endif

	test_add_max_memory_regions();

#ifdef __x86_64__
	if (argc > 1)
		loops = atoi_positive("Number of iterations", argv[1]);
	else
		loops = 10;

	pr_info("Testing MOVE of in-use region, %d loops\n", loops);
	for (i = 0; i < loops; i++)
		test_move_memory_region();

	pr_info("Testing DELETE of in-use region, %d loops\n", loops);
	for (i = 0; i < loops; i++)
		test_delete_memory_region();
#endif

	return 0;
}
