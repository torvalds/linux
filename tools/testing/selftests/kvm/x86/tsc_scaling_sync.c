// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2021 Amazon.com, Inc. or its affiliates.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#include <stdint.h>
#include <time.h>
#include <sched.h>
#include <signal.h>
#include <pthread.h>

#define NR_TEST_VCPUS 20

static struct kvm_vm *vm;
pthread_spinlock_t create_lock;

#define TEST_TSC_KHZ    2345678UL
#define TEST_TSC_OFFSET 200000000

uint64_t tsc_sync;
static void guest_code(void)
{
	uint64_t start_tsc, local_tsc, tmp;

	start_tsc = rdtsc();
	do {
		tmp = READ_ONCE(tsc_sync);
		local_tsc = rdtsc();
		WRITE_ONCE(tsc_sync, local_tsc);
		if (unlikely(local_tsc < tmp))
			GUEST_SYNC_ARGS(0, local_tsc, tmp, 0, 0);

	} while (local_tsc - start_tsc < 5000 * TEST_TSC_KHZ);

	GUEST_DONE();
}


static void *run_vcpu(void *_cpu_nr)
{
	unsigned long vcpu_id = (unsigned long)_cpu_nr;
	unsigned long failures = 0;
	static bool first_cpu_done;
	struct kvm_vcpu *vcpu;

	/* The kernel is fine, but vm_vcpu_add() needs locking */
	pthread_spin_lock(&create_lock);

	vcpu = vm_vcpu_add(vm, vcpu_id, guest_code);

	if (!first_cpu_done) {
		first_cpu_done = true;
		vcpu_set_msr(vcpu, MSR_IA32_TSC, TEST_TSC_OFFSET);
	}

	pthread_spin_unlock(&create_lock);

	for (;;) {
                struct ucall uc;

		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
                case UCALL_DONE:
			goto out;

                case UCALL_SYNC:
			printf("Guest %d sync %lx %lx %ld\n", vcpu->id,
			       uc.args[2], uc.args[3], uc.args[2] - uc.args[3]);
			failures++;
			break;

                default:
                        TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}
 out:
	return (void *)failures;
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_VM_TSC_CONTROL));

	vm = vm_create(NR_TEST_VCPUS);
	vm_ioctl(vm, KVM_SET_TSC_KHZ, (void *) TEST_TSC_KHZ);

	pthread_spin_init(&create_lock, PTHREAD_PROCESS_PRIVATE);
	pthread_t cpu_threads[NR_TEST_VCPUS];
	unsigned long cpu;
	for (cpu = 0; cpu < NR_TEST_VCPUS; cpu++)
		pthread_create(&cpu_threads[cpu], NULL, run_vcpu, (void *)cpu);

	unsigned long failures = 0;
	for (cpu = 0; cpu < NR_TEST_VCPUS; cpu++) {
		void *this_cpu_failures;
		pthread_join(cpu_threads[cpu], &this_cpu_failures);
		failures += (unsigned long)this_cpu_failures;
	}

	TEST_ASSERT(!failures, "TSC sync failed");
	pthread_spin_destroy(&create_lock);
	kvm_vm_free(vm);
	return 0;
}
