// SPDX-License-Identifier: GPL-2.0-only
/*
 * psci_cpu_on_test - Test that the observable state of a vCPU targeted by the
 * CPU_ON PSCI call matches what the caller requested.
 *
 * Copyright (c) 2021 Google LLC.
 *
 * This is a regression test for a race between KVM servicing the PSCI call and
 * userspace reading the vCPUs registers.
 */

#define _GNU_SOURCE

#include <linux/psci.h>

#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"

#define VCPU_ID_SOURCE 0
#define VCPU_ID_TARGET 1

#define CPU_ON_ENTRY_ADDR 0xfeedf00dul
#define CPU_ON_CONTEXT_ID 0xdeadc0deul

static uint64_t psci_cpu_on(uint64_t target_cpu, uint64_t entry_addr,
			    uint64_t context_id)
{
	register uint64_t x0 asm("x0") = PSCI_0_2_FN64_CPU_ON;
	register uint64_t x1 asm("x1") = target_cpu;
	register uint64_t x2 asm("x2") = entry_addr;
	register uint64_t x3 asm("x3") = context_id;

	asm("hvc #0"
	    : "=r"(x0)
	    : "r"(x0), "r"(x1), "r"(x2), "r"(x3)
	    : "memory");

	return x0;
}

static uint64_t psci_affinity_info(uint64_t target_affinity,
				   uint64_t lowest_affinity_level)
{
	register uint64_t x0 asm("x0") = PSCI_0_2_FN64_AFFINITY_INFO;
	register uint64_t x1 asm("x1") = target_affinity;
	register uint64_t x2 asm("x2") = lowest_affinity_level;

	asm("hvc #0"
	    : "=r"(x0)
	    : "r"(x0), "r"(x1), "r"(x2)
	    : "memory");

	return x0;
}

static void guest_main(uint64_t target_cpu)
{
	GUEST_ASSERT(!psci_cpu_on(target_cpu, CPU_ON_ENTRY_ADDR, CPU_ON_CONTEXT_ID));
	uint64_t target_state;

	do {
		target_state = psci_affinity_info(target_cpu, 0);

		GUEST_ASSERT((target_state == PSCI_0_2_AFFINITY_LEVEL_ON) ||
			     (target_state == PSCI_0_2_AFFINITY_LEVEL_OFF));
	} while (target_state != PSCI_0_2_AFFINITY_LEVEL_ON);

	GUEST_DONE();
}

int main(void)
{
	uint64_t target_mpidr, obs_pc, obs_x0;
	struct kvm_vcpu_init init;
	struct kvm_vm *vm;
	struct ucall uc;

	vm = vm_create(VM_MODE_DEFAULT, DEFAULT_GUEST_PHY_PAGES, O_RDWR);
	kvm_vm_elf_load(vm, program_invocation_name);
	ucall_init(vm, NULL);

	vm_ioctl(vm, KVM_ARM_PREFERRED_TARGET, &init);
	init.features[0] |= (1 << KVM_ARM_VCPU_PSCI_0_2);

	aarch64_vcpu_add_default(vm, VCPU_ID_SOURCE, &init, guest_main);

	/*
	 * make sure the target is already off when executing the test.
	 */
	init.features[0] |= (1 << KVM_ARM_VCPU_POWER_OFF);
	aarch64_vcpu_add_default(vm, VCPU_ID_TARGET, &init, guest_main);

	get_reg(vm, VCPU_ID_TARGET, ARM64_SYS_REG(MPIDR_EL1), &target_mpidr);
	vcpu_args_set(vm, VCPU_ID_SOURCE, 1, target_mpidr & MPIDR_HWID_BITMASK);
	vcpu_run(vm, VCPU_ID_SOURCE);

	switch (get_ucall(vm, VCPU_ID_SOURCE, &uc)) {
	case UCALL_DONE:
		break;
	case UCALL_ABORT:
		TEST_FAIL("%s at %s:%ld", (const char *)uc.args[0], __FILE__,
			  uc.args[1]);
		break;
	default:
		TEST_FAIL("Unhandled ucall: %lu", uc.cmd);
	}

	get_reg(vm, VCPU_ID_TARGET, ARM64_CORE_REG(regs.pc), &obs_pc);
	get_reg(vm, VCPU_ID_TARGET, ARM64_CORE_REG(regs.regs[0]), &obs_x0);

	TEST_ASSERT(obs_pc == CPU_ON_ENTRY_ADDR,
		    "unexpected target cpu pc: %lx (expected: %lx)",
		    obs_pc, CPU_ON_ENTRY_ADDR);
	TEST_ASSERT(obs_x0 == CPU_ON_CONTEXT_ID,
		    "unexpected target context id: %lx (expected: %lx)",
		    obs_x0, CPU_ON_CONTEXT_ID);

	kvm_vm_free(vm);
	return 0;
}
