// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020, Google LLC.
 *
 * Tests for KVM paravirtual feature disablement
 */
#include <asm/kvm_para.h>
#include <linux/kvm_para.h>
#include <linux/stringify.h>
#include <stdint.h>

#include "apic.h"
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

/* VMCALL and VMMCALL are both 3-byte opcodes. */
#define HYPERCALL_INSN_SIZE	3

static bool ud_expected;

static void guest_ud_handler(struct ex_regs *regs)
{
	GUEST_ASSERT(ud_expected);
	GUEST_DONE();
}

extern uint8_t svm_hypercall_insn[HYPERCALL_INSN_SIZE];
static uint64_t svm_do_sched_yield(uint8_t apic_id)
{
	uint64_t ret;

	asm volatile("mov %1, %%rax\n\t"
		     "mov %2, %%rbx\n\t"
		     "svm_hypercall_insn:\n\t"
		     "vmmcall\n\t"
		     "mov %%rax, %0\n\t"
		     : "=r"(ret)
		     : "r"((uint64_t)KVM_HC_SCHED_YIELD), "r"((uint64_t)apic_id)
		     : "rax", "rbx", "memory");

	return ret;
}

extern uint8_t vmx_hypercall_insn[HYPERCALL_INSN_SIZE];
static uint64_t vmx_do_sched_yield(uint8_t apic_id)
{
	uint64_t ret;

	asm volatile("mov %1, %%rax\n\t"
		     "mov %2, %%rbx\n\t"
		     "vmx_hypercall_insn:\n\t"
		     "vmcall\n\t"
		     "mov %%rax, %0\n\t"
		     : "=r"(ret)
		     : "r"((uint64_t)KVM_HC_SCHED_YIELD), "r"((uint64_t)apic_id)
		     : "rax", "rbx", "memory");

	return ret;
}

static void guest_main(void)
{
	uint8_t *native_hypercall_insn, *hypercall_insn;
	uint8_t apic_id;

	apic_id = GET_APIC_ID_FIELD(xapic_read_reg(APIC_ID));

	if (is_intel_cpu()) {
		native_hypercall_insn = vmx_hypercall_insn;
		hypercall_insn = svm_hypercall_insn;
		svm_do_sched_yield(apic_id);
	} else if (is_amd_cpu()) {
		native_hypercall_insn = svm_hypercall_insn;
		hypercall_insn = vmx_hypercall_insn;
		vmx_do_sched_yield(apic_id);
	} else {
		GUEST_ASSERT(0);
		/* unreachable */
		return;
	}

	/*
	 * The hypercall didn't #UD (guest_ud_handler() signals "done" if a #UD
	 * occurs).  Verify that a #UD is NOT expected and that KVM patched in
	 * the native hypercall.
	 */
	GUEST_ASSERT(!ud_expected);
	GUEST_ASSERT(!memcmp(native_hypercall_insn, hypercall_insn, HYPERCALL_INSN_SIZE));
	GUEST_DONE();
}

static void setup_ud_vector(struct kvm_vcpu *vcpu)
{
	vm_init_descriptor_tables(vcpu->vm);
	vcpu_init_descriptor_tables(vcpu);
	vm_install_exception_handler(vcpu->vm, UD_VECTOR, guest_ud_handler);
}

static void enter_guest(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	struct ucall uc;

	vcpu_run(vcpu);
	switch (get_ucall(vcpu, &uc)) {
	case UCALL_SYNC:
		pr_info("%s: %016lx\n", (const char *)uc.args[2], uc.args[3]);
		break;
	case UCALL_DONE:
		return;
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
	default:
		TEST_FAIL("Unhandled ucall: %ld\nexit_reason: %u (%s)",
			  uc.cmd, run->exit_reason, exit_reason_str(run->exit_reason));
	}
}

static void test_fix_hypercall(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	vm = vm_create_with_one_vcpu(&vcpu, guest_main);
	setup_ud_vector(vcpu);

	ud_expected = false;
	sync_global_to_guest(vm, ud_expected);

	virt_pg_map(vm, APIC_DEFAULT_GPA, APIC_DEFAULT_GPA);

	enter_guest(vcpu);
}

static void test_fix_hypercall_disabled(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	vm = vm_create_with_one_vcpu(&vcpu, guest_main);
	setup_ud_vector(vcpu);

	vm_enable_cap(vm, KVM_CAP_DISABLE_QUIRKS2,
		      KVM_X86_QUIRK_FIX_HYPERCALL_INSN);

	ud_expected = true;
	sync_global_to_guest(vm, ud_expected);

	virt_pg_map(vm, APIC_DEFAULT_GPA, APIC_DEFAULT_GPA);

	enter_guest(vcpu);
}

int main(void)
{
	TEST_REQUIRE(kvm_check_cap(KVM_CAP_DISABLE_QUIRKS2) & KVM_X86_QUIRK_FIX_HYPERCALL_INSN);

	test_fix_hypercall();
	test_fix_hypercall_disabled();
}
