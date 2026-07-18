// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Red Hat, Inc.
 *
 * Test that vmx_leave_smm() validates vmcs12 controls before re-entering
 * nested guest mode on RSM.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "smm.h"
#include "hyperv.h"
#include "vmx.h"

#define SMRAM_GPA	0x1000000
#define SMRAM_STAGE	0xfe

#define SYNC_PORT	0xe

#define STR(x) #x
#define XSTR(s) STR(s)

/*
 * SMI handler: runs in real-address mode.
 * Reports SMRAM_STAGE via port IO, then does RSM.
 */
static u8 smi_handler[] = {
	0xb0, SMRAM_STAGE,    /* mov $SMRAM_STAGE, %al */
	0xe4, SYNC_PORT,      /* in $SYNC_PORT, %al */
	0x0f, 0xaa,           /* rsm */
};

static inline void sync_with_host(u64 phase)
{
	asm volatile("in $" XSTR(SYNC_PORT) ", %%al \n"
		     : "+a" (phase));
}

static void l2_guest_code(void)
{
	sync_with_host(1);

	/* After SMI+RSM with invalid controls, we should not reach here. */
	vmcall();
}

static void guest_code(struct vmx_pages *vmx_pages,
		       struct hyperv_test_pages *hv_pages)
{
#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	/* Set up Hyper-V enlightenments and eVMCS */
	wrmsr(HV_X64_MSR_GUEST_OS_ID, HYPERV_LINUX_OS_ID);
	enable_vp_assist(hv_pages->vp_assist_gpa, hv_pages->vp_assist);
	evmcs_enable();

	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_evmcs(hv_pages));
	prepare_vmcs(vmx_pages, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	GUEST_ASSERT(!vmlaunch());

	/* L2 exits via vmcall if test fails */
	sync_with_host(2);
}

int main(int argc, char *argv[])
{
	gva_t vmx_pages_gva = 0, hv_pages_gva = 0;
	struct hyperv_test_pages *hv;
	struct hv_enlightened_vmcs *evmcs;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct kvm_regs regs;
	int stage_reported;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_NESTED_STATE));
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_HYPERV_ENLIGHTENED_VMCS));
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_X86_SMM));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	setup_smram(vm, vcpu, SMRAM_GPA, smi_handler, sizeof(smi_handler));

	vcpu_set_hv_cpuid(vcpu);
	vcpu_enable_evmcs(vcpu);
	vcpu_alloc_vmx(vm, &vmx_pages_gva);
	hv = vcpu_alloc_hyperv_test_pages(vm, &hv_pages_gva);
	vcpu_args_set(vcpu, 2, vmx_pages_gva, hv_pages_gva);

	vcpu_run(vcpu);

	/* L2 is running and syncs with host.  */
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
	vcpu_regs_get(vcpu, &regs);
	stage_reported = regs.rax & 0xff;
	TEST_ASSERT(stage_reported == 1,
		    "Expected stage 1, got %d", stage_reported);

	/* Inject SMI while L2 is running.  */
	inject_smi(vcpu);
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
	vcpu_regs_get(vcpu, &regs);
	stage_reported = regs.rax & 0xff;
	TEST_ASSERT(stage_reported == SMRAM_STAGE,
		    "Expected SMM handler stage %#x, got %#x",
		    SMRAM_STAGE, stage_reported);

	/*
	 * Guest is now paused in the SMI handler, about to execute RSM.
	 * Hack the eVMCS page to set-up invalid pin-based execution
	 * control (PIN_BASED_VIRTUAL_NMIS without PIN_BASED_NMI_EXITING).
	 */
	evmcs = hv->enlightened_vmcs_hva;
	evmcs->pin_based_vm_exec_control |= PIN_BASED_VIRTUAL_NMIS;
	evmcs->hv_clean_fields = 0;

	/*
	 * Trigger copy_enlightened_to_vmcs12() via KVM_GET_NESTED_STATE,
	 * copying the invalid pin_based_vm_exec_control into cached_vmcs12.
	 */
	union {
		struct kvm_nested_state state;
		char state_[16384];
	} nested_state_buf;

	memset(&nested_state_buf, 0, sizeof(nested_state_buf));
	nested_state_buf.state.size = sizeof(nested_state_buf);
	vcpu_nested_state_get(vcpu, &nested_state_buf.state);

	/*
	 * Resume the guest.  The SMI handler executes RSM, which calls
	 * vmx_leave_smm().  nested_vmx_check_controls() should detect
	 * VIRTUAL_NMIS without NMI_EXITING and cause a triple fault.
	 */
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_SHUTDOWN);

	kvm_vm_free(vm);
	return 0;
}
