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

#define VCPU_ID 0

static bool ud_expected;

static void guest_ud_handler(struct ex_regs *regs)
{
	GUEST_ASSERT(ud_expected);
	GUEST_DONE();
}

extern unsigned char svm_hypercall_insn;
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

extern unsigned char vmx_hypercall_insn;
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

static void assert_hypercall_insn(unsigned char *exp_insn, unsigned char *obs_insn)
{
	uint32_t exp = 0, obs = 0;

	memcpy(&exp, exp_insn, sizeof(exp));
	memcpy(&obs, obs_insn, sizeof(obs));

	GUEST_ASSERT_EQ(exp, obs);
}

static void guest_main(void)
{
	unsigned char *native_hypercall_insn, *hypercall_insn;
	uint8_t apic_id;

	apic_id = GET_APIC_ID_FIELD(xapic_read_reg(APIC_ID));

	if (is_intel_cpu()) {
		native_hypercall_insn = &vmx_hypercall_insn;
		hypercall_insn = &svm_hypercall_insn;
		svm_do_sched_yield(apic_id);
	} else if (is_amd_cpu()) {
		native_hypercall_insn = &svm_hypercall_insn;
		hypercall_insn = &vmx_hypercall_insn;
		vmx_do_sched_yield(apic_id);
	} else {
		GUEST_ASSERT(0);
		/* unreachable */
		return;
	}

	GUEST_ASSERT(!ud_expected);
	assert_hypercall_insn(native_hypercall_insn, hypercall_insn);
	GUEST_DONE();
}

static void setup_ud_vector(struct kvm_vm *vm)
{
	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);
	vm_install_exception_handler(vm, UD_VECTOR, guest_ud_handler);
}

static void enter_guest(struct kvm_vm *vm)
{
	struct kvm_run *run;
	struct ucall uc;

	run = vcpu_state(vm, VCPU_ID);

	vcpu_run(vm, VCPU_ID);
	switch (get_ucall(vm, VCPU_ID, &uc)) {
	case UCALL_SYNC:
		pr_info("%s: %016lx\n", (const char *)uc.args[2], uc.args[3]);
		break;
	case UCALL_DONE:
		return;
	case UCALL_ABORT:
		TEST_FAIL("%s at %s:%ld", (const char *)uc.args[0], __FILE__, uc.args[1]);
	default:
		TEST_FAIL("Unhandled ucall: %ld\nexit_reason: %u (%s)",
			  uc.cmd, run->exit_reason, exit_reason_str(run->exit_reason));
	}
}

static void test_fix_hypercall(void)
{
	struct kvm_vm *vm;

	vm = vm_create_default(VCPU_ID, 0, guest_main);
	setup_ud_vector(vm);

	ud_expected = false;
	sync_global_to_guest(vm, ud_expected);

	virt_pg_map(vm, APIC_DEFAULT_GPA, APIC_DEFAULT_GPA);

	enter_guest(vm);
}

static void test_fix_hypercall_disabled(void)
{
	struct kvm_enable_cap cap = {0};
	struct kvm_vm *vm;

	vm = vm_create_default(VCPU_ID, 0, guest_main);
	setup_ud_vector(vm);

	cap.cap = KVM_CAP_DISABLE_QUIRKS2;
	cap.args[0] = KVM_X86_QUIRK_FIX_HYPERCALL_INSN;
	vm_enable_cap(vm, &cap);

	ud_expected = true;
	sync_global_to_guest(vm, ud_expected);

	virt_pg_map(vm, APIC_DEFAULT_GPA, APIC_DEFAULT_GPA);

	enter_guest(vm);
}

int main(void)
{
	if (!(kvm_check_cap(KVM_CAP_DISABLE_QUIRKS2) & KVM_X86_QUIRK_FIX_HYPERCALL_INSN)) {
		print_skip("KVM_X86_QUIRK_HYPERCALL_INSN not supported");
		exit(KSFT_SKIP);
	}

	test_fix_hypercall();
	test_fix_hypercall_disabled();
}
