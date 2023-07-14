// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE /* for program_invocation_short_name */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"
#include "svm_util.h"

#define L2_GUEST_STACK_SIZE 256

/*
 * Arbitrary, never shoved into KVM/hardware, just need to avoid conflict with
 * the "real" exceptions used, #SS/#GP/#DF (12/13/8).
 */
#define FAKE_TRIPLE_FAULT_VECTOR	0xaa

/* Arbitrary 32-bit error code injected by this test. */
#define SS_ERROR_CODE 0xdeadbeef

/*
 * Bit '0' is set on Intel if the exception occurs while delivering a previous
 * event/exception.  AMD's wording is ambiguous, but presumably the bit is set
 * if the exception occurs while delivering an external event, e.g. NMI or INTR,
 * but not for exceptions that occur when delivering other exceptions or
 * software interrupts.
 *
 * Note, Intel's name for it, "External event", is misleading and much more
 * aligned with AMD's behavior, but the SDM is quite clear on its behavior.
 */
#define ERROR_CODE_EXT_FLAG	BIT(0)

/*
 * Bit '1' is set if the fault occurred when looking up a descriptor in the
 * IDT, which is the case here as the IDT is empty/NULL.
 */
#define ERROR_CODE_IDT_FLAG	BIT(1)

/*
 * The #GP that occurs when vectoring #SS should show the index into the IDT
 * for #SS, plus have the "IDT flag" set.
 */
#define GP_ERROR_CODE_AMD ((SS_VECTOR * 8) | ERROR_CODE_IDT_FLAG)
#define GP_ERROR_CODE_INTEL ((SS_VECTOR * 8) | ERROR_CODE_IDT_FLAG | ERROR_CODE_EXT_FLAG)

/*
 * Intel and AMD both shove '0' into the error code on #DF, regardless of what
 * led to the double fault.
 */
#define DF_ERROR_CODE 0

#define INTERCEPT_SS		(BIT_ULL(SS_VECTOR))
#define INTERCEPT_SS_DF		(INTERCEPT_SS | BIT_ULL(DF_VECTOR))
#define INTERCEPT_SS_GP_DF	(INTERCEPT_SS_DF | BIT_ULL(GP_VECTOR))

static void l2_ss_pending_test(void)
{
	GUEST_SYNC(SS_VECTOR);
}

static void l2_ss_injected_gp_test(void)
{
	GUEST_SYNC(GP_VECTOR);
}

static void l2_ss_injected_df_test(void)
{
	GUEST_SYNC(DF_VECTOR);
}

static void l2_ss_injected_tf_test(void)
{
	GUEST_SYNC(FAKE_TRIPLE_FAULT_VECTOR);
}

static void svm_run_l2(struct svm_test_data *svm, void *l2_code, int vector,
		       uint32_t error_code)
{
	struct vmcb *vmcb = svm->vmcb;
	struct vmcb_control_area *ctrl = &vmcb->control;

	vmcb->save.rip = (u64)l2_code;
	run_guest(vmcb, svm->vmcb_gpa);

	if (vector == FAKE_TRIPLE_FAULT_VECTOR)
		return;

	GUEST_ASSERT_EQ(ctrl->exit_code, (SVM_EXIT_EXCP_BASE + vector));
	GUEST_ASSERT_EQ(ctrl->exit_info_1, error_code);
}

static void l1_svm_code(struct svm_test_data *svm)
{
	struct vmcb_control_area *ctrl = &svm->vmcb->control;
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	generic_svm_setup(svm, NULL, &l2_guest_stack[L2_GUEST_STACK_SIZE]);
	svm->vmcb->save.idtr.limit = 0;
	ctrl->intercept |= BIT_ULL(INTERCEPT_SHUTDOWN);

	ctrl->intercept_exceptions = INTERCEPT_SS_GP_DF;
	svm_run_l2(svm, l2_ss_pending_test, SS_VECTOR, SS_ERROR_CODE);
	svm_run_l2(svm, l2_ss_injected_gp_test, GP_VECTOR, GP_ERROR_CODE_AMD);

	ctrl->intercept_exceptions = INTERCEPT_SS_DF;
	svm_run_l2(svm, l2_ss_injected_df_test, DF_VECTOR, DF_ERROR_CODE);

	ctrl->intercept_exceptions = INTERCEPT_SS;
	svm_run_l2(svm, l2_ss_injected_tf_test, FAKE_TRIPLE_FAULT_VECTOR, 0);
	GUEST_ASSERT_EQ(ctrl->exit_code, SVM_EXIT_SHUTDOWN);

	GUEST_DONE();
}

static void vmx_run_l2(void *l2_code, int vector, uint32_t error_code)
{
	GUEST_ASSERT(!vmwrite(GUEST_RIP, (u64)l2_code));

	GUEST_ASSERT_EQ(vector == SS_VECTOR ? vmlaunch() : vmresume(), 0);

	if (vector == FAKE_TRIPLE_FAULT_VECTOR)
		return;

	GUEST_ASSERT_EQ(vmreadz(VM_EXIT_REASON), EXIT_REASON_EXCEPTION_NMI);
	GUEST_ASSERT_EQ((vmreadz(VM_EXIT_INTR_INFO) & 0xff), vector);
	GUEST_ASSERT_EQ(vmreadz(VM_EXIT_INTR_ERROR_CODE), error_code);
}

static void l1_vmx_code(struct vmx_pages *vmx)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	GUEST_ASSERT_EQ(prepare_for_vmx_operation(vmx), true);

	GUEST_ASSERT_EQ(load_vmcs(vmx), true);

	prepare_vmcs(vmx, NULL, &l2_guest_stack[L2_GUEST_STACK_SIZE]);
	GUEST_ASSERT_EQ(vmwrite(GUEST_IDTR_LIMIT, 0), 0);

	/*
	 * VMX disallows injecting an exception with error_code[31:16] != 0,
	 * and hardware will never generate a VM-Exit with bits 31:16 set.
	 * KVM should likewise truncate the "bad" userspace value.
	 */
	GUEST_ASSERT_EQ(vmwrite(EXCEPTION_BITMAP, INTERCEPT_SS_GP_DF), 0);
	vmx_run_l2(l2_ss_pending_test, SS_VECTOR, (u16)SS_ERROR_CODE);
	vmx_run_l2(l2_ss_injected_gp_test, GP_VECTOR, GP_ERROR_CODE_INTEL);

	GUEST_ASSERT_EQ(vmwrite(EXCEPTION_BITMAP, INTERCEPT_SS_DF), 0);
	vmx_run_l2(l2_ss_injected_df_test, DF_VECTOR, DF_ERROR_CODE);

	GUEST_ASSERT_EQ(vmwrite(EXCEPTION_BITMAP, INTERCEPT_SS), 0);
	vmx_run_l2(l2_ss_injected_tf_test, FAKE_TRIPLE_FAULT_VECTOR, 0);
	GUEST_ASSERT_EQ(vmreadz(VM_EXIT_REASON), EXIT_REASON_TRIPLE_FAULT);

	GUEST_DONE();
}

static void __attribute__((__flatten__)) l1_guest_code(void *test_data)
{
	if (this_cpu_has(X86_FEATURE_SVM))
		l1_svm_code(test_data);
	else
		l1_vmx_code(test_data);
}

static void assert_ucall_vector(struct kvm_vcpu *vcpu, int vector)
{
	struct ucall uc;

	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_SYNC:
		TEST_ASSERT(vector == uc.args[1],
			    "Expected L2 to ask for %d, got %ld", vector, uc.args[1]);
		break;
	case UCALL_DONE:
		TEST_ASSERT(vector == -1,
			    "Expected L2 to ask for %d, L2 says it's done", vector);
		break;
	case UCALL_ABORT:
		TEST_FAIL("%s at %s:%ld (0x%lx != 0x%lx)",
			  (const char *)uc.args[0], __FILE__, uc.args[1],
			  uc.args[2], uc.args[3]);
		break;
	default:
		TEST_FAIL("Expected L2 to ask for %d, got unexpected ucall %lu", vector, uc.cmd);
	}
}

static void queue_ss_exception(struct kvm_vcpu *vcpu, bool inject)
{
	struct kvm_vcpu_events events;

	vcpu_events_get(vcpu, &events);

	TEST_ASSERT(!events.exception.pending,
		    "Vector %d unexpectedlt pending", events.exception.nr);
	TEST_ASSERT(!events.exception.injected,
		    "Vector %d unexpectedly injected", events.exception.nr);

	events.flags = KVM_VCPUEVENT_VALID_PAYLOAD;
	events.exception.pending = !inject;
	events.exception.injected = inject;
	events.exception.nr = SS_VECTOR;
	events.exception.has_error_code = true;
	events.exception.error_code = SS_ERROR_CODE;
	vcpu_events_set(vcpu, &events);
}

/*
 * Verify KVM_{G,S}ET_EVENTS play nice with pending vs. injected exceptions
 * when an exception is being queued for L2.  Specifically, verify that KVM
 * honors L1 exception intercept controls when a #SS is pending/injected,
 * triggers a #GP on vectoring the #SS, morphs to #DF if #GP isn't intercepted
 * by L1, and finally causes (nested) SHUTDOWN if #DF isn't intercepted by L1.
 */
int main(int argc, char *argv[])
{
	vm_vaddr_t nested_test_data_gva;
	struct kvm_vcpu_events events;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_EXCEPTION_PAYLOAD));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM) || kvm_cpu_has(X86_FEATURE_VMX));

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	vm_enable_cap(vm, KVM_CAP_EXCEPTION_PAYLOAD, -2ul);

	if (kvm_cpu_has(X86_FEATURE_SVM))
		vcpu_alloc_svm(vm, &nested_test_data_gva);
	else
		vcpu_alloc_vmx(vm, &nested_test_data_gva);

	vcpu_args_set(vcpu, 1, nested_test_data_gva);

	/* Run L1 => L2.  L2 should sync and request #SS. */
	vcpu_run(vcpu);
	assert_ucall_vector(vcpu, SS_VECTOR);

	/* Pend #SS and request immediate exit.  #SS should still be pending. */
	queue_ss_exception(vcpu, false);
	vcpu->run->immediate_exit = true;
	vcpu_run_complete_io(vcpu);

	/* Verify the pending events comes back out the same as it went in. */
	vcpu_events_get(vcpu, &events);
	ASSERT_EQ(events.flags & KVM_VCPUEVENT_VALID_PAYLOAD,
		  KVM_VCPUEVENT_VALID_PAYLOAD);
	ASSERT_EQ(events.exception.pending, true);
	ASSERT_EQ(events.exception.nr, SS_VECTOR);
	ASSERT_EQ(events.exception.has_error_code, true);
	ASSERT_EQ(events.exception.error_code, SS_ERROR_CODE);

	/*
	 * Run for real with the pending #SS, L1 should get a VM-Exit due to
	 * #SS interception and re-enter L2 to request #GP (via injected #SS).
	 */
	vcpu->run->immediate_exit = false;
	vcpu_run(vcpu);
	assert_ucall_vector(vcpu, GP_VECTOR);

	/*
	 * Inject #SS, the #SS should bypass interception and cause #GP, which
	 * L1 should intercept before KVM morphs it to #DF.  L1 should then
	 * disable #GP interception and run L2 to request #DF (via #SS => #GP).
	 */
	queue_ss_exception(vcpu, true);
	vcpu_run(vcpu);
	assert_ucall_vector(vcpu, DF_VECTOR);

	/*
	 * Inject #SS, the #SS should bypass interception and cause #GP, which
	 * L1 is no longer interception, and so should see a #DF VM-Exit.  L1
	 * should then signal that is done.
	 */
	queue_ss_exception(vcpu, true);
	vcpu_run(vcpu);
	assert_ucall_vector(vcpu, FAKE_TRIPLE_FAULT_VECTOR);

	/*
	 * Inject #SS yet again.  L1 is not intercepting #GP or #DF, and so
	 * should see nested TRIPLE_FAULT / SHUTDOWN.
	 */
	queue_ss_exception(vcpu, true);
	vcpu_run(vcpu);
	assert_ucall_vector(vcpu, -1);

	kvm_vm_free(vm);
}
