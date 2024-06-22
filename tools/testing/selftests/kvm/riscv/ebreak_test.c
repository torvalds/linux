// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V KVM ebreak test.
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *
 */
#include "kvm_util.h"
#include "ucall_common.h"

#define LABEL_ADDRESS(v) ((uint64_t)&(v))

extern unsigned char sw_bp_1, sw_bp_2;
static uint64_t sw_bp_addr;

static void guest_code(void)
{
	asm volatile(
		".option push\n"
		".option norvc\n"
		"sw_bp_1: ebreak\n"
		"sw_bp_2: ebreak\n"
		".option pop\n"
	);
	GUEST_ASSERT_EQ(READ_ONCE(sw_bp_addr), LABEL_ADDRESS(sw_bp_2));

	GUEST_DONE();
}

static void guest_breakpoint_handler(struct ex_regs *regs)
{
	WRITE_ONCE(sw_bp_addr, regs->epc);
	regs->epc += 4;
}

int main(void)
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	uint64_t pc;
	struct kvm_guest_debug debug = {
		.control = KVM_GUESTDBG_ENABLE,
	};

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_SET_GUEST_DEBUG));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	vm_init_vector_tables(vm);
	vcpu_init_vector_tables(vcpu);
	vm_install_exception_handler(vm, EXC_BREAKPOINT,
					guest_breakpoint_handler);

	/*
	 * Enable the guest debug.
	 * ebreak should exit to the VMM with KVM_EXIT_DEBUG reason.
	 */
	vcpu_guest_debug_set(vcpu, &debug);
	vcpu_run(vcpu);

	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_DEBUG);

	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.pc), &pc);
	TEST_ASSERT_EQ(pc, LABEL_ADDRESS(sw_bp_1));

	/* skip sw_bp_1 */
	vcpu_set_reg(vcpu, RISCV_CORE_REG(regs.pc), pc + 4);

	/*
	 * Disable all debug controls.
	 * Guest should handle the ebreak without exiting to the VMM.
	 */
	memset(&debug, 0, sizeof(debug));
	vcpu_guest_debug_set(vcpu, &debug);

	vcpu_run(vcpu);

	TEST_ASSERT_EQ(get_ucall(vcpu, NULL), UCALL_DONE);

	kvm_vm_free(vm);

	return 0;
}
