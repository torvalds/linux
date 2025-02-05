// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test edge cases and race conditions in kvm_recalculate_apic_map().
 */

#include <sys/ioctl.h>
#include <pthread.h>
#include <time.h>

#include "processor.h"
#include "test_util.h"
#include "kvm_util.h"
#include "apic.h"

#define TIMEOUT		5	/* seconds */

#define LAPIC_DISABLED	0
#define LAPIC_X2APIC	(MSR_IA32_APICBASE_ENABLE | X2APIC_ENABLE)
#define MAX_XAPIC_ID	0xff

static void *race(void *arg)
{
	struct kvm_lapic_state lapic = {};
	struct kvm_vcpu *vcpu = arg;

	while (1) {
		/* Trigger kvm_recalculate_apic_map(). */
		vcpu_ioctl(vcpu, KVM_SET_LAPIC, &lapic);
		pthread_testcancel();
	}

	return NULL;
}

int main(void)
{
	struct kvm_vcpu *vcpus[KVM_MAX_VCPUS];
	struct kvm_vcpu *vcpuN;
	struct kvm_vm *vm;
	pthread_t thread;
	time_t t;
	int i;

	kvm_static_assert(KVM_MAX_VCPUS > MAX_XAPIC_ID);

	/*
	 * Create the max number of vCPUs supported by selftests so that KVM
	 * has decent amount of work to do when recalculating the map, i.e. to
	 * make the problematic window large enough to hit.
	 */
	vm = vm_create_with_vcpus(KVM_MAX_VCPUS, NULL, vcpus);

	/*
	 * Enable x2APIC on all vCPUs so that KVM doesn't bail from the recalc
	 * due to vCPUs having aliased xAPIC IDs (truncated to 8 bits).
	 */
	for (i = 0; i < KVM_MAX_VCPUS; i++)
		vcpu_set_msr(vcpus[i], MSR_IA32_APICBASE, LAPIC_X2APIC);

	TEST_ASSERT_EQ(pthread_create(&thread, NULL, race, vcpus[0]), 0);

	vcpuN = vcpus[KVM_MAX_VCPUS - 1];
	for (t = time(NULL) + TIMEOUT; time(NULL) < t;) {
		vcpu_set_msr(vcpuN, MSR_IA32_APICBASE, LAPIC_X2APIC);
		vcpu_set_msr(vcpuN, MSR_IA32_APICBASE, LAPIC_DISABLED);
	}

	TEST_ASSERT_EQ(pthread_cancel(thread), 0);
	TEST_ASSERT_EQ(pthread_join(thread, NULL), 0);

	kvm_vm_free(vm);

	return 0;
}
