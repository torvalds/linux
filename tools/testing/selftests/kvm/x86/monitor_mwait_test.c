// SPDX-License-Identifier: GPL-2.0
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "kvm_util.h"
#include "processor.h"
#include "kselftest.h"

#define CPUID_MWAIT (1u << 3)

enum monitor_mwait_testcases {
	MWAIT_QUIRK_DISABLED = BIT(0),
	MISC_ENABLES_QUIRK_DISABLED = BIT(1),
	MWAIT_DISABLED = BIT(2),
	CPUID_DISABLED = BIT(3),
	TEST_MAX = CPUID_DISABLED * 2 - 1,
};

/*
 * If both MWAIT and its quirk are disabled, MONITOR/MWAIT should #UD, in all
 * other scenarios KVM should emulate them as nops.
 */
#define GUEST_ASSERT_MONITOR_MWAIT(insn, testcase, vector)		\
do {									\
	bool fault_wanted = ((testcase) & MWAIT_QUIRK_DISABLED) &&	\
			    ((testcase) & MWAIT_DISABLED);		\
									\
	if (fault_wanted)						\
		__GUEST_ASSERT((vector) == UD_VECTOR,			\
			       "Expected #UD on " insn " for testcase '0x%x', got '0x%x'", \
			       testcase, vector);			\
	else								\
		__GUEST_ASSERT(!(vector),				\
			       "Expected success on " insn " for testcase '0x%x', got '0x%x'", \
			       testcase, vector);			\
} while (0)

static void guest_monitor_wait(void *arg)
{
	int testcase = (int) (long) arg;
	u8 vector;

	u64 val = rdmsr(MSR_IA32_MISC_ENABLE) & ~MSR_IA32_MISC_ENABLE_MWAIT;
	if (!(testcase & MWAIT_DISABLED))
		val |= MSR_IA32_MISC_ENABLE_MWAIT;
	wrmsr(MSR_IA32_MISC_ENABLE, val);

	__GUEST_ASSERT(this_cpu_has(X86_FEATURE_MWAIT) == !(testcase & MWAIT_DISABLED),
		       "Expected CPUID.MWAIT %s\n",
		       (testcase & MWAIT_DISABLED) ? "cleared" : "set");

	/*
	 * Arbitrarily MONITOR this function, SVM performs fault checks before
	 * intercept checks, so the inputs for MONITOR and MWAIT must be valid.
	 */
	vector = kvm_asm_safe("monitor", "a"(guest_monitor_wait), "c"(0), "d"(0));
	GUEST_ASSERT_MONITOR_MWAIT("MONITOR", testcase, vector);

	vector = kvm_asm_safe("mwait", "a"(guest_monitor_wait), "c"(0), "d"(0));
	GUEST_ASSERT_MONITOR_MWAIT("MWAIT", testcase, vector);

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	uint64_t disabled_quirks;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	int testcase;
	char test[80];

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_DISABLE_QUIRKS2));

	ksft_print_header();
	ksft_set_plan(12);
	for (testcase = 0; testcase <= TEST_MAX; testcase++) {
		vm = vm_create_with_one_vcpu(&vcpu, guest_monitor_wait);
		vcpu_args_set(vcpu, 1, (void *)(long)testcase);

		disabled_quirks = 0;
		if (testcase & MWAIT_QUIRK_DISABLED) {
			disabled_quirks |= KVM_X86_QUIRK_MWAIT_NEVER_UD_FAULTS;
			strcpy(test, "MWAIT can fault");
		} else {
			strcpy(test, "MWAIT never faults");
		}
		if (testcase & MISC_ENABLES_QUIRK_DISABLED) {
			disabled_quirks |= KVM_X86_QUIRK_MISC_ENABLE_NO_MWAIT;
			strcat(test, ", MISC_ENABLE updates CPUID");
		} else {
			strcat(test, ", no CPUID updates");
		}

		vm_enable_cap(vm, KVM_CAP_DISABLE_QUIRKS2, disabled_quirks);

		if (!(testcase & MISC_ENABLES_QUIRK_DISABLED) &&
		    (!!(testcase & CPUID_DISABLED) ^ !!(testcase & MWAIT_DISABLED)))
			continue;

		if (testcase & CPUID_DISABLED) {
			strcat(test, ", CPUID clear");
			vcpu_clear_cpuid_feature(vcpu, X86_FEATURE_MWAIT);
		} else {
			strcat(test, ", CPUID set");
			vcpu_set_cpuid_feature(vcpu, X86_FEATURE_MWAIT);
		}

		if (testcase & MWAIT_DISABLED)
			strcat(test, ", MWAIT disabled");

		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			/* Detected in vcpu_run */
			break;
		case UCALL_DONE:
			ksft_test_result_pass("%s\n", test);
			break;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
			break;
		}
		kvm_vm_free(vm);
	}
	ksft_finished();

	return 0;
}
