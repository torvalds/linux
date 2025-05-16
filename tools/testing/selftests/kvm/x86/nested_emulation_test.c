// SPDX-License-Identifier: GPL-2.0-only
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"
#include "svm_util.h"

enum {
	SVM_F,
	VMX_F,
	NR_VIRTUALIZATION_FLAVORS,
};

struct emulated_instruction {
	const char name[32];
	uint8_t opcode[15];
	uint32_t exit_reason[NR_VIRTUALIZATION_FLAVORS];
};

static struct emulated_instruction instructions[] = {
	{
		.name = "pause",
		.opcode = { 0xf3, 0x90 },
		.exit_reason = { SVM_EXIT_PAUSE,
				 EXIT_REASON_PAUSE_INSTRUCTION, }
	},
	{
		.name = "hlt",
		.opcode = { 0xf4 },
		.exit_reason = { SVM_EXIT_HLT,
				 EXIT_REASON_HLT, }
	},
};

static uint8_t kvm_fep[] = { 0x0f, 0x0b, 0x6b, 0x76, 0x6d };	/* ud2 ; .ascii "kvm" */
static uint8_t l2_guest_code[sizeof(kvm_fep) + 15];
static uint8_t *l2_instruction = &l2_guest_code[sizeof(kvm_fep)];

static uint32_t get_instruction_length(struct emulated_instruction *insn)
{
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(insn->opcode) && insn->opcode[i]; i++)
		;

	return i;
}

static void guest_code(void *test_data)
{
	int f = this_cpu_has(X86_FEATURE_SVM) ? SVM_F : VMX_F;
	int i;

	memcpy(l2_guest_code, kvm_fep, sizeof(kvm_fep));

	if (f == SVM_F) {
		struct svm_test_data *svm = test_data;
		struct vmcb *vmcb = svm->vmcb;

		generic_svm_setup(svm, NULL, NULL);
		vmcb->save.idtr.limit = 0;
		vmcb->save.rip = (u64)l2_guest_code;

		vmcb->control.intercept |= BIT_ULL(INTERCEPT_SHUTDOWN) |
					   BIT_ULL(INTERCEPT_PAUSE) |
					   BIT_ULL(INTERCEPT_HLT);
		vmcb->control.intercept_exceptions = 0;
	} else {
		GUEST_ASSERT(prepare_for_vmx_operation(test_data));
		GUEST_ASSERT(load_vmcs(test_data));

		prepare_vmcs(test_data, NULL, NULL);
		GUEST_ASSERT(!vmwrite(GUEST_IDTR_LIMIT, 0));
		GUEST_ASSERT(!vmwrite(GUEST_RIP, (u64)l2_guest_code));
		GUEST_ASSERT(!vmwrite(EXCEPTION_BITMAP, 0));

		vmwrite(CPU_BASED_VM_EXEC_CONTROL, vmreadz(CPU_BASED_VM_EXEC_CONTROL) |
						   CPU_BASED_PAUSE_EXITING |
						   CPU_BASED_HLT_EXITING);
	}

	for (i = 0; i < ARRAY_SIZE(instructions); i++) {
		struct emulated_instruction *insn = &instructions[i];
		uint32_t insn_len = get_instruction_length(insn);
		uint32_t exit_insn_len;
		u32 exit_reason;

		/*
		 * Copy the target instruction to the L2 code stream, and fill
		 * the remaining bytes with INT3s so that a missed intercept
		 * results in a consistent failure mode (SHUTDOWN).
		 */
		memcpy(l2_instruction, insn->opcode, insn_len);
		memset(l2_instruction + insn_len, 0xcc, sizeof(insn->opcode) - insn_len);

		if (f == SVM_F) {
			struct svm_test_data *svm = test_data;
			struct vmcb *vmcb = svm->vmcb;

			run_guest(vmcb, svm->vmcb_gpa);
			exit_reason = vmcb->control.exit_code;
			exit_insn_len = vmcb->control.next_rip - vmcb->save.rip;
			GUEST_ASSERT_EQ(vmcb->save.rip, (u64)l2_instruction);
		} else {
			GUEST_ASSERT_EQ(i ? vmresume() : vmlaunch(), 0);
			exit_reason = vmreadz(VM_EXIT_REASON);
			exit_insn_len = vmreadz(VM_EXIT_INSTRUCTION_LEN);
			GUEST_ASSERT_EQ(vmreadz(GUEST_RIP), (u64)l2_instruction);
		}

		__GUEST_ASSERT(exit_reason == insn->exit_reason[f],
			       "Wanted exit_reason '0x%x' for '%s', got '0x%x'",
			       insn->exit_reason[f], insn->name, exit_reason);

		__GUEST_ASSERT(exit_insn_len == insn_len,
			       "Wanted insn_len '%u' for '%s', got '%u'",
			       insn_len, insn->name, exit_insn_len);
	}

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	vm_vaddr_t nested_test_data_gva;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	TEST_REQUIRE(is_forced_emulation_enabled);
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM) || kvm_cpu_has(X86_FEATURE_VMX));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	vm_enable_cap(vm, KVM_CAP_EXCEPTION_PAYLOAD, -2ul);

	if (kvm_cpu_has(X86_FEATURE_SVM))
		vcpu_alloc_svm(vm, &nested_test_data_gva);
	else
		vcpu_alloc_vmx(vm, &nested_test_data_gva);

	vcpu_args_set(vcpu, 1, nested_test_data_gva);

	vcpu_run(vcpu);
	TEST_ASSERT_EQ(get_ucall(vcpu, NULL), UCALL_DONE);

	kvm_vm_free(vm);
}
