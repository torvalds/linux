// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <linux/compiler.h>

#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>

#define VCPU_ID 0

/*
 * Somewhat arbitrary location and slot, intended to not overlap anything.  The
 * location and size are specifically 2mb sized/aligned so that the initial
 * region corresponds to exactly one large page.
 */
#define MEM_REGION_GPA		0xc0000000
#define MEM_REGION_SIZE		0x200000
#define MEM_REGION_SLOT		10

static void guest_code(void)
{
	uint64_t val;

	do {
		val = READ_ONCE(*((uint64_t *)MEM_REGION_GPA));
	} while (!val);

	if (val != 1)
		ucall(UCALL_ABORT, 1, val);

	GUEST_DONE();
}

static void *vcpu_worker(void *data)
{
	struct kvm_vm *vm = data;
	struct kvm_run *run;
	struct ucall uc;
	uint64_t cmd;

	/*
	 * Loop until the guest is done.  Re-enter the guest on all MMIO exits,
	 * which will occur if the guest attempts to access a memslot while it
	 * is being moved.
	 */
	run = vcpu_state(vm, VCPU_ID);
	do {
		vcpu_run(vm, VCPU_ID);
	} while (run->exit_reason == KVM_EXIT_MMIO);

	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Unexpected exit reason = %d", run->exit_reason);

	cmd = get_ucall(vm, VCPU_ID, &uc);
	TEST_ASSERT(cmd == UCALL_DONE, "Unexpected val in guest = %lu", uc.args[0]);
	return NULL;
}

static void test_move_memory_region(void)
{
	pthread_t vcpu_thread;
	struct kvm_vm *vm;
	uint64_t *hva;
	uint64_t gpa;

	vm = vm_create_default(VCPU_ID, 0, guest_code);

	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS_THP,
				    MEM_REGION_GPA, MEM_REGION_SLOT,
				    MEM_REGION_SIZE / getpagesize(), 0);

	/*
	 * Allocate and map two pages so that the GPA accessed by guest_code()
	 * stays valid across the memslot move.
	 */
	gpa = vm_phy_pages_alloc(vm, 2, MEM_REGION_GPA, MEM_REGION_SLOT);
	TEST_ASSERT(gpa == MEM_REGION_GPA, "Failed vm_phy_pages_alloc\n");

	virt_map(vm, MEM_REGION_GPA, MEM_REGION_GPA, 2, 0);

	/* Ditto for the host mapping so that both pages can be zeroed. */
	hva = addr_gpa2hva(vm, MEM_REGION_GPA);
	memset(hva, 0, 2 * 4096);

	pthread_create(&vcpu_thread, NULL, vcpu_worker, vm);

	/* Ensure the guest thread is spun up. */
	usleep(100000);

	/*
	 * Shift the region's base GPA.  The guest should not see "2" as the
	 * hva->gpa translation is misaligned, i.e. the guest is accessing a
	 * different host pfn.
	 */
	vm_mem_region_move(vm, MEM_REGION_SLOT, MEM_REGION_GPA - 4096);
	WRITE_ONCE(*hva, 2);

	usleep(100000);

	/*
	 * Note, value in memory needs to be changed *before* restoring the
	 * memslot, else the guest could race the update and see "2".
	 */
	WRITE_ONCE(*hva, 1);

	/* Restore the original base, the guest should see "1". */
	vm_mem_region_move(vm, MEM_REGION_SLOT, MEM_REGION_GPA);

	pthread_join(vcpu_thread, NULL);

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	int i, loops;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	if (argc > 1)
		loops = atoi(argv[1]);
	else
		loops = 10;

	for (i = 0; i < loops; i++)
		test_move_memory_region();

	return 0;
}
