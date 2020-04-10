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

static const uint64_t MMIO_VAL = 0xbeefull;

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
	struct kvm_vm *vm = data;
	struct kvm_run *run;
	struct ucall uc;
	uint64_t cmd;

	/*
	 * Loop until the guest is done.  Re-enter the guest on all MMIO exits,
	 * which will occur if the guest attempts to access a memslot after it
	 * has been deleted or while it is being moved .
	 */
	run = vcpu_state(vm, VCPU_ID);

	while (1) {
		vcpu_run(vm, VCPU_ID);

		if (run->exit_reason == KVM_EXIT_IO) {
			cmd = get_ucall(vm, VCPU_ID, &uc);
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
		TEST_FAIL("%s at %s:%ld, val = %lu", (const char *)uc.args[0],
			  __FILE__, uc.args[1], uc.args[2]);

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

static struct kvm_vm *spawn_vm(pthread_t *vcpu_thread, void *guest_code)
{
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

	pthread_create(vcpu_thread, NULL, vcpu_worker, vm);

	/* Ensure the guest thread is spun up. */
	wait_for_vcpu();

	return vm;
}


static void guest_code_move_memory_region(void)
{
	uint64_t val;

	GUEST_SYNC(0);

	/*
	 * Spin until the memory region is moved to a misaligned address.  This
	 * may or may not trigger MMIO, as the window where the memslot is
	 * invalid is quite small.
	 */
	val = guest_spin_on_val(0);
	GUEST_ASSERT_1(val == 1 || val == MMIO_VAL, val);

	/* Spin until the memory region is realigned. */
	val = guest_spin_on_val(MMIO_VAL);
	GUEST_ASSERT_1(val == 1, val);

	GUEST_DONE();
}

static void test_move_memory_region(void)
{
	pthread_t vcpu_thread;
	struct kvm_vm *vm;
	uint64_t *hva;

	vm = spawn_vm(&vcpu_thread, guest_code_move_memory_region);

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
