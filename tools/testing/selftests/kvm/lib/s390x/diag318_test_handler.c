// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test handler for the s390x DIAGNOSE 0x0318 instruction.
 *
 * Copyright (C) 2020, IBM
 */

#include "test_util.h"
#include "kvm_util.h"

#define ICPT_INSTRUCTION	0x04
#define IPA0_DIAG		0x8300

static void guest_code(void)
{
	uint64_t diag318_info = 0x12345678;

	asm volatile ("diag %0,0,0x318\n" : : "d" (diag318_info));
}

/*
 * The DIAGNOSE 0x0318 instruction call must be handled via userspace. As such,
 * we create an ad-hoc VM here to handle the instruction then extract the
 * necessary data. It is up to the caller to decide what to do with that data.
 */
static uint64_t diag318_handler(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct kvm_run *run;
	uint64_t reg;
	uint64_t diag318_info;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	vcpu_run(vcpu);
	run = vcpu->run;

	TEST_ASSERT(run->exit_reason == KVM_EXIT_S390_SIEIC,
		    "DIAGNOSE 0x0318 instruction was not intercepted");
	TEST_ASSERT(run->s390_sieic.icptcode == ICPT_INSTRUCTION,
		    "Unexpected intercept code: 0x%x", run->s390_sieic.icptcode);
	TEST_ASSERT((run->s390_sieic.ipa & 0xff00) == IPA0_DIAG,
		    "Unexpected IPA0 code: 0x%x", (run->s390_sieic.ipa & 0xff00));

	reg = (run->s390_sieic.ipa & 0x00f0) >> 4;
	diag318_info = run->s.regs.gprs[reg];

	TEST_ASSERT(diag318_info != 0, "DIAGNOSE 0x0318 info not set");

	kvm_vm_free(vm);

	return diag318_info;
}

uint64_t get_diag318_info(void)
{
	static uint64_t diag318_info;
	static bool printed_skip;

	/*
	 * If KVM does not support diag318, then return 0 to
	 * ensure tests do not break.
	 */
	if (!kvm_has_cap(KVM_CAP_S390_DIAG318)) {
		if (!printed_skip) {
			fprintf(stdout, "KVM_CAP_S390_DIAG318 not supported. "
				"Skipping diag318 test.\n");
			printed_skip = true;
		}
		return 0;
	}

	/*
	 * If a test has previously requested the diag318 info,
	 * then don't bother spinning up a temporary VM again.
	 */
	if (!diag318_info)
		diag318_info = diag318_handler();

	return diag318_info;
}
