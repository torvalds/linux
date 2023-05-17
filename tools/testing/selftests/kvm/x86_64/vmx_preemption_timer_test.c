// SPDX-License-Identifier: GPL-2.0-only
/*
 * VMX-preemption timer test
 *
 * Copyright (C) 2020, Google, LLC.
 *
 * Test to ensure the VM-Enter after migration doesn't
 * incorrectly restarts the timer with the full timer
 * value instead of partially decayed timer value
 *
 */
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"

#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#define PREEMPTION_TIMER_VALUE			100000000ull
#define PREEMPTION_TIMER_VALUE_THRESHOLD1	 80000000ull

u32 vmx_pt_rate;
bool l2_save_restore_done;
static u64 l2_vmx_pt_start;
volatile u64 l2_vmx_pt_finish;

union vmx_basic basic;
union vmx_ctrl_msr ctrl_pin_rev;
union vmx_ctrl_msr ctrl_exit_rev;

void l2_guest_code(void)
{
	u64 vmx_pt_delta;

	vmcall();
	l2_vmx_pt_start = (rdtsc() >> vmx_pt_rate) << vmx_pt_rate;

	/*
	 * Wait until the 1st threshold has passed
	 */
	do {
		l2_vmx_pt_finish = rdtsc();
		vmx_pt_delta = (l2_vmx_pt_finish - l2_vmx_pt_start) >>
				vmx_pt_rate;
	} while (vmx_pt_delta < PREEMPTION_TIMER_VALUE_THRESHOLD1);

	/*
	 * Force L2 through Save and Restore cycle
	 */
	GUEST_SYNC(1);

	l2_save_restore_done = 1;

	/*
	 * Now wait for the preemption timer to fire and
	 * exit to L1
	 */
	while ((l2_vmx_pt_finish = rdtsc()))
		;
}

void l1_guest_code(struct vmx_pages *vmx_pages)
{
#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	u64 l1_vmx_pt_start;
	u64 l1_vmx_pt_finish;
	u64 l1_tsc_deadline, l2_tsc_deadline;

	GUEST_ASSERT(vmx_pages->vmcs_gpa);
	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));
	GUEST_ASSERT(vmptrstz() == vmx_pages->vmcs_gpa);

	prepare_vmcs(vmx_pages, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	/*
	 * Check for Preemption timer support
	 */
	basic.val = rdmsr(MSR_IA32_VMX_BASIC);
	ctrl_pin_rev.val = rdmsr(basic.ctrl ? MSR_IA32_VMX_TRUE_PINBASED_CTLS
			: MSR_IA32_VMX_PINBASED_CTLS);
	ctrl_exit_rev.val = rdmsr(basic.ctrl ? MSR_IA32_VMX_TRUE_EXIT_CTLS
			: MSR_IA32_VMX_EXIT_CTLS);

	if (!(ctrl_pin_rev.clr & PIN_BASED_VMX_PREEMPTION_TIMER) ||
	    !(ctrl_exit_rev.clr & VM_EXIT_SAVE_VMX_PREEMPTION_TIMER))
		return;

	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);
	vmwrite(GUEST_RIP, vmreadz(GUEST_RIP) + vmreadz(VM_EXIT_INSTRUCTION_LEN));

	/*
	 * Turn on PIN control and resume the guest
	 */
	GUEST_ASSERT(!vmwrite(PIN_BASED_VM_EXEC_CONTROL,
			      vmreadz(PIN_BASED_VM_EXEC_CONTROL) |
			      PIN_BASED_VMX_PREEMPTION_TIMER));

	GUEST_ASSERT(!vmwrite(VMX_PREEMPTION_TIMER_VALUE,
			      PREEMPTION_TIMER_VALUE));

	vmx_pt_rate = rdmsr(MSR_IA32_VMX_MISC) & 0x1F;

	l2_save_restore_done = 0;

	l1_vmx_pt_start = (rdtsc() >> vmx_pt_rate) << vmx_pt_rate;

	GUEST_ASSERT(!vmresume());

	l1_vmx_pt_finish = rdtsc();

	/*
	 * Ensure exit from L2 happens after L2 goes through
	 * save and restore
	 */
	GUEST_ASSERT(l2_save_restore_done);

	/*
	 * Ensure the exit from L2 is due to preemption timer expiry
	 */
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_PREEMPTION_TIMER);

	l1_tsc_deadline = l1_vmx_pt_start +
		(PREEMPTION_TIMER_VALUE << vmx_pt_rate);

	l2_tsc_deadline = l2_vmx_pt_start +
		(PREEMPTION_TIMER_VALUE << vmx_pt_rate);

	/*
	 * Sync with the host and pass the l1|l2 pt_expiry_finish times and
	 * tsc deadlines so that host can verify they are as expected
	 */
	GUEST_SYNC_ARGS(2, l1_vmx_pt_finish, l1_tsc_deadline,
		l2_vmx_pt_finish, l2_tsc_deadline);
}

void guest_code(struct vmx_pages *vmx_pages)
{
	if (vmx_pages)
		l1_guest_code(vmx_pages);

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	vm_vaddr_t vmx_pages_gva = 0;

	struct kvm_regs regs1, regs2;
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	struct kvm_x86_state *state;
	struct ucall uc;
	int stage;

	/*
	 * AMD currently does not implement any VMX features, so for now we
	 * just early out.
	 */
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_NESTED_STATE));

	/* Create VM */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	vcpu_regs_get(vcpu, &regs1);

	vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vcpu, 1, vmx_pages_gva);

	for (stage = 1;; stage++) {
		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		case UCALL_SYNC:
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}

		/* UCALL_SYNC is handled here.  */
		TEST_ASSERT(!strcmp((const char *)uc.args[0], "hello") &&
			    uc.args[1] == stage, "Stage %d: Unexpected register values vmexit, got %lx",
			    stage, (ulong)uc.args[1]);
		/*
		 * If this stage 2 then we should verify the vmx pt expiry
		 * is as expected.
		 * From L1's perspective verify Preemption timer hasn't
		 * expired too early.
		 * From L2's perspective verify Preemption timer hasn't
		 * expired too late.
		 */
		if (stage == 2) {

			pr_info("Stage %d: L1 PT expiry TSC (%lu) , L1 TSC deadline (%lu)\n",
				stage, uc.args[2], uc.args[3]);

			pr_info("Stage %d: L2 PT expiry TSC (%lu) , L2 TSC deadline (%lu)\n",
				stage, uc.args[4], uc.args[5]);

			TEST_ASSERT(uc.args[2] >= uc.args[3],
				"Stage %d: L1 PT expiry TSC (%lu) < L1 TSC deadline (%lu)",
				stage, uc.args[2], uc.args[3]);

			TEST_ASSERT(uc.args[4] < uc.args[5],
				"Stage %d: L2 PT expiry TSC (%lu) > L2 TSC deadline (%lu)",
				stage, uc.args[4], uc.args[5]);
		}

		state = vcpu_save_state(vcpu);
		memset(&regs1, 0, sizeof(regs1));
		vcpu_regs_get(vcpu, &regs1);

		kvm_vm_release(vm);

		/* Restore state in a new VM.  */
		vcpu = vm_recreate_with_one_vcpu(vm);
		vcpu_load_state(vcpu, state);
		kvm_x86_state_cleanup(state);

		memset(&regs2, 0, sizeof(regs2));
		vcpu_regs_get(vcpu, &regs2);
		TEST_ASSERT(!memcmp(&regs1, &regs2, sizeof(regs2)),
			    "Unexpected register values after vcpu_load_state; rdi: %lx rsi: %lx",
			    (ulong) regs2.rdi, (ulong) regs2.rsi);
	}

done:
	kvm_vm_free(vm);
}
