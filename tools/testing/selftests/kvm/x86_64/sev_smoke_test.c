// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "linux/psp-sev.h"
#include "sev.h"


static void guest_sev_es_code(void)
{
	/* TODO: Check CPUID after GHCB-based hypercall support is added. */
	GUEST_ASSERT(rdmsr(MSR_AMD64_SEV) & MSR_AMD64_SEV_ENABLED);
	GUEST_ASSERT(rdmsr(MSR_AMD64_SEV) & MSR_AMD64_SEV_ES_ENABLED);

	/*
	 * TODO: Add GHCB and ucall support for SEV-ES guests.  For now, simply
	 * force "termination" to signal "done" via the GHCB MSR protocol.
	 */
	wrmsr(MSR_AMD64_SEV_ES_GHCB, GHCB_MSR_TERM_REQ);
	__asm__ __volatile__("rep; vmmcall");
}

static void guest_sev_code(void)
{
	GUEST_ASSERT(this_cpu_has(X86_FEATURE_SEV));
	GUEST_ASSERT(rdmsr(MSR_AMD64_SEV) & MSR_AMD64_SEV_ENABLED);

	GUEST_DONE();
}

static void test_sev(void *guest_code, uint64_t policy)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	vm = vm_sev_create_with_one_vcpu(policy, guest_code, &vcpu);

	for (;;) {
		vcpu_run(vcpu);

		if (policy & SEV_POLICY_ES) {
			TEST_ASSERT(vcpu->run->exit_reason == KVM_EXIT_SYSTEM_EVENT,
				    "Wanted SYSTEM_EVENT, got %s",
				    exit_reason_str(vcpu->run->exit_reason));
			TEST_ASSERT_EQ(vcpu->run->system_event.type, KVM_SYSTEM_EVENT_SEV_TERM);
			TEST_ASSERT_EQ(vcpu->run->system_event.ndata, 1);
			TEST_ASSERT_EQ(vcpu->run->system_event.data[0], GHCB_MSR_TERM_REQ);
			break;
		}

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			continue;
		case UCALL_DONE:
			return;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		default:
			TEST_FAIL("Unexpected exit: %s",
				  exit_reason_str(vcpu->run->exit_reason));
		}
	}

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SEV));

	test_sev(guest_sev_code, SEV_POLICY_NO_DBG);
	test_sev(guest_sev_code, 0);

	if (kvm_cpu_has(X86_FEATURE_SEV_ES)) {
		test_sev(guest_sev_es_code, SEV_POLICY_ES | SEV_POLICY_NO_DBG);
		test_sev(guest_sev_es_code, SEV_POLICY_ES);
	}

	return 0;
}
