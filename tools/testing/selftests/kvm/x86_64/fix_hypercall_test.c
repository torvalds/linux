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

#include "kvm_test_harness.h"
#include "apic.h"
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

/* VMCALL and VMMCALL are both 3-byte opcodes. */
#define HYPERCALL_INSN_SIZE	3

static bool quirk_disabled;

static void guest_ud_handler(struct ex_regs *regs)
{
	regs->rax = -EFAULT;
	regs->rip += HYPERCALL_INSN_SIZE;
}

static const uint8_t vmx_vmcall[HYPERCALL_INSN_SIZE]  = { 0x0f, 0x01, 0xc1 };
static const uint8_t svm_vmmcall[HYPERCALL_INSN_SIZE] = { 0x0f, 0x01, 0xd9 };

extern uint8_t hypercall_insn[HYPERCALL_INSN_SIZE];
static uint64_t do_sched_yield(uint8_t apic_id)
{
	uint64_t ret;

	asm volatile("hypercall_insn:\n\t"
		     ".byte 0xcc,0xcc,0xcc\n\t"
		     : "=a"(ret)
		     : "a"((uint64_t)KVM_HC_SCHED_YIELD), "b"((uint64_t)apic_id)
		     : "memory");

	return ret;
}

static void guest_main(void)
{
	const uint8_t *native_hypercall_insn;
	const uint8_t *other_hypercall_insn;
	uint64_t ret;

	if (host_cpu_is_intel) {
		native_hypercall_insn = vmx_vmcall;
		other_hypercall_insn  = svm_vmmcall;
	} else if (host_cpu_is_amd) {
		native_hypercall_insn = svm_vmmcall;
		other_hypercall_insn  = vmx_vmcall;
	} else {
		GUEST_ASSERT(0);
		/* unreachable */
		return;
	}

	memcpy(hypercall_insn, other_hypercall_insn, HYPERCALL_INSN_SIZE);

	ret = do_sched_yield(GET_APIC_ID_FIELD(xapic_read_reg(APIC_ID)));

	/*
	 * If the quirk is disabled, verify that guest_ud_handler() "returned"
	 * -EFAULT and that KVM did NOT patch the hypercall.  If the quirk is
	 * enabled, verify that the hypercall succeeded and that KVM patched in
	 * the "right" hypercall.
	 */
	if (quirk_disabled) {
		GUEST_ASSERT(ret == (uint64_t)-EFAULT);
		GUEST_ASSERT(!memcmp(other_hypercall_insn, hypercall_insn,
			     HYPERCALL_INSN_SIZE));
	} else {
		GUEST_ASSERT(!ret);
		GUEST_ASSERT(!memcmp(native_hypercall_insn, hypercall_insn,
			     HYPERCALL_INSN_SIZE));
	}

	GUEST_DONE();
}

KVM_ONE_VCPU_TEST_SUITE(fix_hypercall);

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

static void test_fix_hypercall(struct kvm_vcpu *vcpu, bool disable_quirk)
{
	struct kvm_vm *vm = vcpu->vm;

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);
	vm_install_exception_handler(vcpu->vm, UD_VECTOR, guest_ud_handler);

	if (disable_quirk)
		vm_enable_cap(vm, KVM_CAP_DISABLE_QUIRKS2,
			      KVM_X86_QUIRK_FIX_HYPERCALL_INSN);

	quirk_disabled = disable_quirk;
	sync_global_to_guest(vm, quirk_disabled);

	virt_pg_map(vm, APIC_DEFAULT_GPA, APIC_DEFAULT_GPA);

	enter_guest(vcpu);
}

KVM_ONE_VCPU_TEST(fix_hypercall, enable_quirk, guest_main)
{
	test_fix_hypercall(vcpu, false);
}

KVM_ONE_VCPU_TEST(fix_hypercall, disable_quirk, guest_main)
{
	test_fix_hypercall(vcpu, true);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_check_cap(KVM_CAP_DISABLE_QUIRKS2) & KVM_X86_QUIRK_FIX_HYPERCALL_INSN);

	return test_harness_run(argc, argv);
}
