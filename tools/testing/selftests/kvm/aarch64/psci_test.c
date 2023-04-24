// SPDX-License-Identifier: GPL-2.0-only
/*
 * psci_test - Tests relating to KVM's PSCI implementation.
 *
 * Copyright (c) 2021 Google LLC.
 *
 * This test includes:
 *  - A regression test for a race between KVM servicing the PSCI CPU_ON call
 *    and userspace reading the targeted vCPU's registers.
 *  - A test for KVM's handling of PSCI SYSTEM_SUSPEND and the associated
 *    KVM_SYSTEM_EVENT_SUSPEND UAPI.
 */

#define _GNU_SOURCE

#include <linux/psci.h>

#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"

#define CPU_ON_ENTRY_ADDR 0xfeedf00dul
#define CPU_ON_CONTEXT_ID 0xdeadc0deul

static uint64_t psci_cpu_on(uint64_t target_cpu, uint64_t entry_addr,
			    uint64_t context_id)
{
	struct arm_smccc_res res;

	smccc_hvc(PSCI_0_2_FN64_CPU_ON, target_cpu, entry_addr, context_id,
		  0, 0, 0, 0, &res);

	return res.a0;
}

static uint64_t psci_affinity_info(uint64_t target_affinity,
				   uint64_t lowest_affinity_level)
{
	struct arm_smccc_res res;

	smccc_hvc(PSCI_0_2_FN64_AFFINITY_INFO, target_affinity, lowest_affinity_level,
		  0, 0, 0, 0, 0, &res);

	return res.a0;
}

static uint64_t psci_system_suspend(uint64_t entry_addr, uint64_t context_id)
{
	struct arm_smccc_res res;

	smccc_hvc(PSCI_1_0_FN64_SYSTEM_SUSPEND, entry_addr, context_id,
		  0, 0, 0, 0, 0, &res);

	return res.a0;
}

static uint64_t psci_features(uint32_t func_id)
{
	struct arm_smccc_res res;

	smccc_hvc(PSCI_1_0_FN_PSCI_FEATURES, func_id, 0, 0, 0, 0, 0, 0, &res);

	return res.a0;
}

static void vcpu_power_off(struct kvm_vcpu *vcpu)
{
	struct kvm_mp_state mp_state = {
		.mp_state = KVM_MP_STATE_STOPPED,
	};

	vcpu_mp_state_set(vcpu, &mp_state);
}

static struct kvm_vm *setup_vm(void *guest_code, struct kvm_vcpu **source,
			       struct kvm_vcpu **target)
{
	struct kvm_vcpu_init init;
	struct kvm_vm *vm;

	vm = vm_create(2);

	vm_ioctl(vm, KVM_ARM_PREFERRED_TARGET, &init);
	init.features[0] |= (1 << KVM_ARM_VCPU_PSCI_0_2);

	*source = aarch64_vcpu_add(vm, 0, &init, guest_code);
	*target = aarch64_vcpu_add(vm, 1, &init, guest_code);

	return vm;
}

static void enter_guest(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	vcpu_run(vcpu);
	if (get_ucall(vcpu, &uc) == UCALL_ABORT)
		REPORT_GUEST_ASSERT(uc);
}

static void assert_vcpu_reset(struct kvm_vcpu *vcpu)
{
	uint64_t obs_pc, obs_x0;

	vcpu_get_reg(vcpu, ARM64_CORE_REG(regs.pc), &obs_pc);
	vcpu_get_reg(vcpu, ARM64_CORE_REG(regs.regs[0]), &obs_x0);

	TEST_ASSERT(obs_pc == CPU_ON_ENTRY_ADDR,
		    "unexpected target cpu pc: %lx (expected: %lx)",
		    obs_pc, CPU_ON_ENTRY_ADDR);
	TEST_ASSERT(obs_x0 == CPU_ON_CONTEXT_ID,
		    "unexpected target context id: %lx (expected: %lx)",
		    obs_x0, CPU_ON_CONTEXT_ID);
}

static void guest_test_cpu_on(uint64_t target_cpu)
{
	uint64_t target_state;

	GUEST_ASSERT(!psci_cpu_on(target_cpu, CPU_ON_ENTRY_ADDR, CPU_ON_CONTEXT_ID));

	do {
		target_state = psci_affinity_info(target_cpu, 0);

		GUEST_ASSERT((target_state == PSCI_0_2_AFFINITY_LEVEL_ON) ||
			     (target_state == PSCI_0_2_AFFINITY_LEVEL_OFF));
	} while (target_state != PSCI_0_2_AFFINITY_LEVEL_ON);

	GUEST_DONE();
}

static void host_test_cpu_on(void)
{
	struct kvm_vcpu *source, *target;
	uint64_t target_mpidr;
	struct kvm_vm *vm;
	struct ucall uc;

	vm = setup_vm(guest_test_cpu_on, &source, &target);

	/*
	 * make sure the target is already off when executing the test.
	 */
	vcpu_power_off(target);

	vcpu_get_reg(target, KVM_ARM64_SYS_REG(SYS_MPIDR_EL1), &target_mpidr);
	vcpu_args_set(source, 1, target_mpidr & MPIDR_HWID_BITMASK);
	enter_guest(source);

	if (get_ucall(source, &uc) != UCALL_DONE)
		TEST_FAIL("Unhandled ucall: %lu", uc.cmd);

	assert_vcpu_reset(target);
	kvm_vm_free(vm);
}

static void guest_test_system_suspend(void)
{
	uint64_t ret;

	/* assert that SYSTEM_SUSPEND is discoverable */
	GUEST_ASSERT(!psci_features(PSCI_1_0_FN_SYSTEM_SUSPEND));
	GUEST_ASSERT(!psci_features(PSCI_1_0_FN64_SYSTEM_SUSPEND));

	ret = psci_system_suspend(CPU_ON_ENTRY_ADDR, CPU_ON_CONTEXT_ID);
	GUEST_SYNC(ret);
}

static void host_test_system_suspend(void)
{
	struct kvm_vcpu *source, *target;
	struct kvm_run *run;
	struct kvm_vm *vm;

	vm = setup_vm(guest_test_system_suspend, &source, &target);
	vm_enable_cap(vm, KVM_CAP_ARM_SYSTEM_SUSPEND, 0);

	vcpu_power_off(target);
	run = source->run;

	enter_guest(source);

	TEST_ASSERT_KVM_EXIT_REASON(source, KVM_EXIT_SYSTEM_EVENT);
	TEST_ASSERT(run->system_event.type == KVM_SYSTEM_EVENT_SUSPEND,
		    "Unhandled system event: %u (expected: %u)",
		    run->system_event.type, KVM_SYSTEM_EVENT_SUSPEND);

	kvm_vm_free(vm);
}

int main(void)
{
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_ARM_SYSTEM_SUSPEND));

	host_test_cpu_on();
	host_test_system_suspend();
	return 0;
}
