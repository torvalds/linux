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

#define X86_FEATURE_XSAVE	(1<<26)
#define X86_FEATURE_OSXSAVE	(1<<27)
#define VCPU_ID			1

static inline bool cr4_cpuid_is_sync(void)
{
	int func, subfunc;
	uint32_t eax, ebx, ecx, edx;
	uint64_t cr4;

	func = 0x1;
	subfunc = 0x0;
	__asm__ __volatile__("cpuid"
			     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
			     : "a"(func), "c"(subfunc));

	cr4 = get_cr4();

	return (!!(ecx & X86_FEATURE_OSXSAVE)) == (!!(cr4 & X86_CR4_OSXSAVE));
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
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct kvm_sregs sregs;
	struct kvm_cpuid_entry2 *entry;
	struct ucall uc;
	int rc;

	entry = kvm_get_supported_cpuid_entry(1);
	if (!(entry->ecx & X86_FEATURE_XSAVE)) {
		printf("XSAVE feature not supported, skipping test\n");
		return 0;
	}

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());
	run = vcpu_state(vm, VCPU_ID);

	while (1) {
		rc = _vcpu_run(vm, VCPU_ID);

		TEST_ASSERT(rc == 0, "vcpu_run failed: %d\n", rc);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Unexpected exit reason: %u (%s),\n",
			    run->exit_reason,
			    exit_reason_str(run->exit_reason));

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_SYNC:
			/* emulate hypervisor clearing CR4.OSXSAVE */
			vcpu_sregs_get(vm, VCPU_ID, &sregs);
			sregs.cr4 &= ~X86_CR4_OSXSAVE;
			vcpu_sregs_set(vm, VCPU_ID, &sregs);
			break;
		case UCALL_ABORT:
			TEST_ASSERT(false, "Guest CR4 bit (OSXSAVE) unsynchronized with CPUID bit.");
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_ASSERT(false, "Unknown ucall 0x%x.", uc.cmd);
		}
	}

	kvm_vm_free(vm);

done:
	return 0;
}
