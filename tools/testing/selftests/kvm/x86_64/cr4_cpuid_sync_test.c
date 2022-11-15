// SPDX-License-Identifier: GPL-2.0
/*
 * CR4 and CPUID sync test
 *
 * Copyright 2018, Red Hat, Inc. and/or its affiliates.
 *
 * Author:
 *   Wei Huang <wei@redhat.com>
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"

#include "kvm_util.h"
#include "processor.h"

static inline bool cr4_cpuid_is_sync(void)
{
	uint64_t cr4 = get_cr4();

	return (this_cpu_has(X86_FEATURE_OSXSAVE) == !!(cr4 & X86_CR4_OSXSAVE));
}

static void guest_code(void)
{
	uint64_t cr4;

	/* turn on CR4.OSXSAVE */
	cr4 = get_cr4();
	cr4 |= X86_CR4_OSXSAVE;
	set_cr4(cr4);

	/* verify CR4.OSXSAVE == CPUID.OSXSAVE */
	GUEST_ASSERT(cr4_cpuid_is_sync());

	/* notify hypervisor to change CR4 */
	GUEST_SYNC(0);

	/* check again */
	GUEST_ASSERT(cr4_cpuid_is_sync());

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct kvm_sregs sregs;
	struct ucall uc;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XSAVE));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	run = vcpu->run;

	while (1) {
		vcpu_run(vcpu);

		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Unexpected exit reason: %u (%s),\n",
			    run->exit_reason,
			    exit_reason_str(run->exit_reason));

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			/* emulate hypervisor clearing CR4.OSXSAVE */
			vcpu_sregs_get(vcpu, &sregs);
			sregs.cr4 &= ~X86_CR4_OSXSAVE;
			vcpu_sregs_set(vcpu, &sregs);
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	kvm_vm_free(vm);
	return 0;
}
