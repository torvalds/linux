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

#define MAGIC_HYPERCALL_PORT	0x80

static void guest_code(void)
{
	u32 regs[4] = {
		[KVM_CPUID_EAX] = X86_FEATURE_OSXSAVE.function,
		[KVM_CPUID_ECX] = X86_FEATURE_OSXSAVE.index,
	};

	/* CR4.OSXSAVE should be enabled by default (for selftests vCPUs). */
	GUEST_ASSERT(get_cr4() & X86_CR4_OSXSAVE);

	/* verify CR4.OSXSAVE == CPUID.OSXSAVE */
	GUEST_ASSERT(this_cpu_has(X86_FEATURE_OSXSAVE));

	/*
	 * Notify hypervisor to clear CR4.0SXSAVE, do CPUID and save output,
	 * and then restore CR4.  Do this all in  assembly to ensure no AVX
	 * instructions are executed while OSXSAVE=0.
	 */
	asm volatile (
		"out %%al, $" __stringify(MAGIC_HYPERCALL_PORT) "\n\t"
		"cpuid\n\t"
		"mov %%rdi, %%cr4\n\t"
		: "+a" (regs[KVM_CPUID_EAX]),
		  "=b" (regs[KVM_CPUID_EBX]),
		  "+c" (regs[KVM_CPUID_ECX]),
		  "=d" (regs[KVM_CPUID_EDX])
		: "D" (get_cr4())
	);

	/* Verify KVM cleared OSXSAVE in CPUID when it was cleared in CR4. */
	GUEST_ASSERT(!(regs[X86_FEATURE_OSXSAVE.reg] & BIT(X86_FEATURE_OSXSAVE.bit)));

	/* Verify restoring CR4 also restored OSXSAVE in CPUID. */
	GUEST_ASSERT(this_cpu_has(X86_FEATURE_OSXSAVE));

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct kvm_sregs sregs;
	struct ucall uc;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XSAVE));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	while (1) {
		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		if (vcpu->run->io.port == MAGIC_HYPERCALL_PORT &&
		    vcpu->run->io.direction == KVM_EXIT_IO_OUT) {
			/* emulate hypervisor clearing CR4.OSXSAVE */
			vcpu_sregs_get(vcpu, &sregs);
			sregs.cr4 &= ~X86_CR4_OSXSAVE;
			vcpu_sregs_set(vcpu, &sregs);
			continue;
		}

		switch (get_ucall(vcpu, &uc)) {
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
