// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for MSR_IA32_TSC and MSR_IA32_TSC_ADJUST.
 *
 * Copyright (C) 2020, Red Hat, Inc.
 */
#include <stdio.h>
#include <string.h>
#include "kvm_util.h"
#include "processor.h"

#define VCPU_ID 0

#define UNITY                  (1ull << 30)
#define HOST_ADJUST            (UNITY * 64)
#define GUEST_STEP             (UNITY * 4)
#define ROUND(x)               ((x + UNITY / 2) & -UNITY)
#define rounded_rdmsr(x)       ROUND(rdmsr(x))
#define rounded_host_rdmsr(x)  ROUND(vcpu_get_msr(vm, 0, x))

#define GUEST_ASSERT_EQ(a, b) do {				\
	__typeof(a) _a = (a);					\
	__typeof(b) _b = (b);					\
	if (_a != _b)						\
                ucall(UCALL_ABORT, 4,				\
                        "Failed guest assert: "			\
                        #a " == " #b, __LINE__, _a, _b);	\
  } while(0)

static void guest_code(void)
{
	u64 val = 0;

	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC), val);
	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC_ADJUST), val);

	/* Guest: writes to MSR_IA32_TSC affect both MSRs.  */
	val = 1ull * GUEST_STEP;
	wrmsr(MSR_IA32_TSC, val);
	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC), val);
	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC_ADJUST), val);

	/* Guest: writes to MSR_IA32_TSC_ADJUST affect both MSRs.  */
	GUEST_SYNC(2);
	val = 2ull * GUEST_STEP;
	wrmsr(MSR_IA32_TSC_ADJUST, val);
	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC), val);
	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC_ADJUST), val);

	/* Host: setting the TSC offset.  */
	GUEST_SYNC(3);
	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC), HOST_ADJUST + val);
	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC_ADJUST), val);

	/*
	 * Guest: writes to MSR_IA32_TSC_ADJUST do not destroy the
	 * host-side offset and affect both MSRs.
	 */
	GUEST_SYNC(4);
	val = 3ull * GUEST_STEP;
	wrmsr(MSR_IA32_TSC_ADJUST, val);
	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC), HOST_ADJUST + val);
	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC_ADJUST), val);

	/*
	 * Guest: writes to MSR_IA32_TSC affect both MSRs, so the host-side
	 * offset is now visible in MSR_IA32_TSC_ADJUST.
	 */
	GUEST_SYNC(5);
	val = 4ull * GUEST_STEP;
	wrmsr(MSR_IA32_TSC, val);
	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC), val);
	GUEST_ASSERT_EQ(rounded_rdmsr(MSR_IA32_TSC_ADJUST), val - HOST_ADJUST);

	GUEST_DONE();
}

static void run_vcpu(struct kvm_vm *vm, uint32_t vcpuid, int stage)
{
	struct ucall uc;

	vcpu_args_set(vm, vcpuid, 1, vcpuid);

	vcpu_ioctl(vm, vcpuid, KVM_RUN, NULL);

	switch (get_ucall(vm, vcpuid, &uc)) {
	case UCALL_SYNC:
		TEST_ASSERT(!strcmp((const char *)uc.args[0], "hello") &&
                            uc.args[1] == stage + 1, "Stage %d: Unexpected register values vmexit, got %lx",
                            stage + 1, (ulong)uc.args[1]);
		return;
	case UCALL_DONE:
		return;
	case UCALL_ABORT:
		TEST_ASSERT(false, "%s at %s:%ld\n" \
			    "\tvalues: %#lx, %#lx", (const char *)uc.args[0],
			    __FILE__, uc.args[1], uc.args[2], uc.args[3]);
	default:
		TEST_ASSERT(false, "Unexpected exit: %s",
			    exit_reason_str(vcpu_state(vm, vcpuid)->exit_reason));
	}
}

int main(void)
{
	struct kvm_vm *vm;
	uint64_t val;

	vm = vm_create_default(VCPU_ID, 0, guest_code);

	val = 0;
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC), val);
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC_ADJUST), val);

	/* Guest: writes to MSR_IA32_TSC affect both MSRs.  */
	run_vcpu(vm, VCPU_ID, 1);
	val = 1ull * GUEST_STEP;
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC), val);
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC_ADJUST), val);

	/* Guest: writes to MSR_IA32_TSC_ADJUST affect both MSRs.  */
	run_vcpu(vm, VCPU_ID, 2);
	val = 2ull * GUEST_STEP;
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC), val);
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC_ADJUST), val);

	/*
	 * Host: writes to MSR_IA32_TSC set the host-side offset
	 * and therefore do not change MSR_IA32_TSC_ADJUST.
	 */
	vcpu_set_msr(vm, 0, MSR_IA32_TSC, HOST_ADJUST + val);
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC), HOST_ADJUST + val);
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC_ADJUST), val);
	run_vcpu(vm, VCPU_ID, 3);

	/* Host: writes to MSR_IA32_TSC_ADJUST do not modify the TSC.  */
	vcpu_set_msr(vm, 0, MSR_IA32_TSC_ADJUST, UNITY * 123456);
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC), HOST_ADJUST + val);
	ASSERT_EQ(vcpu_get_msr(vm, 0, MSR_IA32_TSC_ADJUST), UNITY * 123456);

	/* Restore previous value.  */
	vcpu_set_msr(vm, 0, MSR_IA32_TSC_ADJUST, val);
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC), HOST_ADJUST + val);
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC_ADJUST), val);

	/*
	 * Guest: writes to MSR_IA32_TSC_ADJUST do not destroy the
	 * host-side offset and affect both MSRs.
	 */
	run_vcpu(vm, VCPU_ID, 4);
	val = 3ull * GUEST_STEP;
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC), HOST_ADJUST + val);
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC_ADJUST), val);

	/*
	 * Guest: writes to MSR_IA32_TSC affect both MSRs, so the host-side
	 * offset is now visible in MSR_IA32_TSC_ADJUST.
	 */
	run_vcpu(vm, VCPU_ID, 5);
	val = 4ull * GUEST_STEP;
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC), val);
	ASSERT_EQ(rounded_host_rdmsr(MSR_IA32_TSC_ADJUST), val - HOST_ADJUST);

	kvm_vm_free(vm);

	return 0;
}
