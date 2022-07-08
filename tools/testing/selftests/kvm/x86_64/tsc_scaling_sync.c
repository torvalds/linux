// SPDX-License-Identifier: GPL-2.0-only
/*
 * svm_vmcall_test
 *
 * Copyright © 2021 Amazon.com, Inc. or its affiliates.
 *
 * Xen shared_info / pvclock testing
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
	unsigned long cpu = (unsigned long)_cpu_nr;
	unsigned long failures = 0;
	static bool first_cpu_done;

	/* The kernel is fine, but vm_vcpu_add_default() needs locking */
	pthread_spin_lock(&create_lock);

	vm_vcpu_add_default(vm, cpu, guest_code);

	if (!first_cpu_done) {
		first_cpu_done = true;
		vcpu_set_msr(vm, cpu, MSR_IA32_TSC, TEST_TSC_OFFSET);
	}

	pthread_spin_unlock(&create_lock);

	for (;;) {
		volatile struct kvm_run *run = vcpu_state(vm, cpu);
                struct ucall uc;

                vcpu_run(vm, cpu);
                TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
                            "Got exit_reason other than KVM_EXIT_IO: %u (%s)\n",
                            run->exit_reason,
                            exit_reason_str(run->exit_reason));

                switch (get_ucall(vm, cpu, &uc)) {
                case UCALL_DONE:
			goto out;

                case UCALL_SYNC:
			printf("Guest %ld sync %lx %lx %ld\n", cpu, uc.args[2], uc.args[3], uc.args[2] - uc.args[3]);
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
        if (!kvm_check_cap(KVM_CAP_VM_TSC_CONTROL)) {
		print_skip("KVM_CAP_VM_TSC_CONTROL not available");
		exit(KSFT_SKIP);
	}

	vm = vm_create_default_with_vcpus(0, DEFAULT_STACK_PGS * NR_TEST_VCPUS, 0, guest_code, NULL);
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
