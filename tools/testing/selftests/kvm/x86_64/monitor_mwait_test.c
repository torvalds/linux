// SPDX-License-Identifier: GPL-2.0
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "kvm_util.h"
#include "processor.h"

#define CPUID_MWAIT (1u << 3)

enum monitor_mwait_testcases {
	MWAIT_QUIRK_DISABLED = BIT(0),
	MISC_ENABLES_QUIRK_DISABLED = BIT(1),
	MWAIT_DISABLED = BIT(2),
};

static void guest_monitor_wait(int testcase)
{
	/*
	 * If both MWAIT and its quirk are disabled, MONITOR/MWAIT should #UD,
	 * in all other scenarios KVM should emulate them as nops.
	 */
	bool fault_wanted = (testcase & MWAIT_QUIRK_DISABLED) &&
			    (testcase & MWAIT_DISABLED);
	u8 vector;

	GUEST_SYNC(testcase);

	/*
	 * Arbitrarily MONITOR this function, SVM performs fault checks before
	 * intercept checks, so the inputs for MONITOR and MWAIT must be valid.
	 */
	vector = kvm_asm_safe("monitor", "a"(guest_monitor_wait), "c"(0), "d"(0));
	if (fault_wanted)
		GUEST_ASSERT_2(vector == UD_VECTOR, testcase, vector);
	else
		GUEST_ASSERT_2(!vector, testcase, vector);

	vector = kvm_asm_safe("mwait", "a"(guest_monitor_wait), "c"(0), "d"(0));
	if (fault_wanted)
		GUEST_ASSERT_2(vector == UD_VECTOR, testcase, vector);
	else
		GUEST_ASSERT_2(!vector, testcase, vector);
}

static void guest_code(void)
{
	guest_monitor_wait(MWAIT_DISABLED);

	guest_monitor_wait(MWAIT_QUIRK_DISABLED | MWAIT_DISABLED);

	guest_monitor_wait(MISC_ENABLES_QUIRK_DISABLED | MWAIT_DISABLED);
	guest_monitor_wait(MISC_ENABLES_QUIRK_DISABLED);

	guest_monitor_wait(MISC_ENABLES_QUIRK_DISABLED | MWAIT_QUIRK_DISABLED | MWAIT_DISABLED);
	guest_monitor_wait(MISC_ENABLES_QUIRK_DISABLED | MWAIT_QUIRK_DISABLED);

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	uint64_t disabled_quirks;
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct ucall uc;
	int testcase;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_DISABLE_QUIRKS2));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	vcpu_clear_cpuid_feature(vcpu, X86_FEATURE_MWAIT);

	run = vcpu->run;

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);

	while (1) {
		vcpu_run(vcpu);

		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Unexpected exit reason: %u (%s),\n",
			    run->exit_reason,
			    exit_reason_str(run->exit_reason));

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			testcase = uc.args[1];
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT_2(uc, "testcase = %lx, vector = %ld");
			goto done;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
			goto done;
		}

		disabled_quirks = 0;
		if (testcase & MWAIT_QUIRK_DISABLED)
			disabled_quirks |= KVM_X86_QUIRK_MWAIT_NEVER_UD_FAULTS;
		if (testcase & MISC_ENABLES_QUIRK_DISABLED)
			disabled_quirks |= KVM_X86_QUIRK_MISC_ENABLE_NO_MWAIT;
		vm_enable_cap(vm, KVM_CAP_DISABLE_QUIRKS2, disabled_quirks);

		/*
		 * If the MISC_ENABLES quirk (KVM neglects to update CPUID to
		 * enable/disable MWAIT) is disabled, toggle the ENABLE_MWAIT
		 * bit in MISC_ENABLES accordingly.  If the quirk is enabled,
		 * the only valid configuration is MWAIT disabled, as CPUID
		 * can't be manually changed after running the vCPU.
		 */
		if (!(testcase & MISC_ENABLES_QUIRK_DISABLED)) {
			TEST_ASSERT(testcase & MWAIT_DISABLED,
				    "Can't toggle CPUID features after running vCPU");
			continue;
		}

		vcpu_set_msr(vcpu, MSR_IA32_MISC_ENABLE,
			     (testcase & MWAIT_DISABLED) ? 0 : MSR_IA32_MISC_ENABLE_MWAIT);
	}

done:
	kvm_vm_free(vm);
	return 0;
}
