// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, Google LLC.
 *
 * Tests for KVM_CAP_EXIT_ON_EMULATION_FAILURE capability.
 */

#define _GNU_SOURCE /* for program_invocation_short_name */

#include "test_util.h"
#include "kvm_util.h"
#include "vmx.h"

#define MAXPHYADDR 36

#define MEM_REGION_GVA	0x0000123456789000
#define MEM_REGION_GPA	0x0000000700000000
#define MEM_REGION_SLOT	10
#define MEM_REGION_SIZE PAGE_SIZE

static void guest_code(void)
{
	__asm__ __volatile__("flds (%[addr])"
			     :: [addr]"r"(MEM_REGION_GVA));

	GUEST_DONE();
}

/*
 * Accessors to get R/M, REG, and Mod bits described in the SDM vol 2,
 * figure 2-2 "Table Interpretation of ModR/M Byte (C8H)".
 */
#define GET_RM(insn_byte) (insn_byte & 0x7)
#define GET_REG(insn_byte) ((insn_byte & 0x38) >> 3)
#define GET_MOD(insn_byte) ((insn_byte & 0xc) >> 6)

/* Ensure we are dealing with a simple 2-byte flds instruction. */
static bool is_flds(uint8_t *insn_bytes, uint8_t insn_size)
{
	return insn_size >= 2 &&
	       insn_bytes[0] == 0xd9 &&
	       GET_REG(insn_bytes[1]) == 0x0 &&
	       GET_MOD(insn_bytes[1]) == 0x0 &&
	       /* Ensure there is no SIB byte. */
	       GET_RM(insn_bytes[1]) != 0x4 &&
	       /* Ensure there is no displacement byte. */
	       GET_RM(insn_bytes[1]) != 0x5;
}

static void process_exit_on_emulation_error(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	struct kvm_regs regs;
	uint8_t *insn_bytes;
	uint8_t insn_size;
	uint64_t flags;

	TEST_ASSERT(run->exit_reason == KVM_EXIT_INTERNAL_ERROR,
		    "Unexpected exit reason: %u (%s)",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));

	TEST_ASSERT(run->emulation_failure.suberror == KVM_INTERNAL_ERROR_EMULATION,
		    "Unexpected suberror: %u",
		    run->emulation_failure.suberror);

	if (run->emulation_failure.ndata >= 1) {
		flags = run->emulation_failure.flags;
		if ((flags & KVM_INTERNAL_ERROR_EMULATION_FLAG_INSTRUCTION_BYTES) &&
		    run->emulation_failure.ndata >= 3) {
			insn_size = run->emulation_failure.insn_size;
			insn_bytes = run->emulation_failure.insn_bytes;

			TEST_ASSERT(insn_size <= 15 && insn_size > 0,
				    "Unexpected instruction size: %u",
				    insn_size);

			TEST_ASSERT(is_flds(insn_bytes, insn_size),
				    "Unexpected instruction.  Expected 'flds' (0xd9 /0)");

			/*
			 * If is_flds() succeeded then the instruction bytes
			 * contained an flds instruction that is 2-bytes in
			 * length (ie: no prefix, no SIB, no displacement).
			 */
			vcpu_regs_get(vcpu, &regs);
			regs.rip += 2;
			vcpu_regs_set(vcpu, &regs);
		}
	}
}

static void do_guest_assert(struct ucall *uc)
{
	REPORT_GUEST_ASSERT(*uc);
}

static void check_for_guest_assert(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	if (vcpu->run->exit_reason == KVM_EXIT_IO &&
	    get_ucall(vcpu, &uc) == UCALL_ABORT) {
		do_guest_assert(&uc);
	}
}

static void process_ucall_done(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	struct ucall uc;

	check_for_guest_assert(vcpu);

	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Unexpected exit reason: %u (%s)",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));

	TEST_ASSERT(get_ucall(vcpu, &uc) == UCALL_DONE,
		    "Unexpected ucall command: %lu, expected UCALL_DONE (%d)",
		    uc.cmd, UCALL_DONE);
}

static uint64_t process_ucall(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	struct ucall uc;

	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Unexpected exit reason: %u (%s)",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_SYNC:
		break;
	case UCALL_ABORT:
		do_guest_assert(&uc);
		break;
	case UCALL_DONE:
		process_ucall_done(vcpu);
		break;
	default:
		TEST_ASSERT(false, "Unexpected ucall");
	}

	return uc.cmd;
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	uint64_t gpa, pte;
	uint64_t *hva;
	int rc;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_SMALLER_MAXPHYADDR));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	vcpu_set_cpuid_maxphyaddr(vcpu, MAXPHYADDR);

	rc = kvm_check_cap(KVM_CAP_EXIT_ON_EMULATION_FAILURE);
	TEST_ASSERT(rc, "KVM_CAP_EXIT_ON_EMULATION_FAILURE is unavailable");
	vm_enable_cap(vm, KVM_CAP_EXIT_ON_EMULATION_FAILURE, 1);

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    MEM_REGION_GPA, MEM_REGION_SLOT,
				    MEM_REGION_SIZE / PAGE_SIZE, 0);
	gpa = vm_phy_pages_alloc(vm, MEM_REGION_SIZE / PAGE_SIZE,
				 MEM_REGION_GPA, MEM_REGION_SLOT);
	TEST_ASSERT(gpa == MEM_REGION_GPA, "Failed vm_phy_pages_alloc\n");
	virt_map(vm, MEM_REGION_GVA, MEM_REGION_GPA, 1);
	hva = addr_gpa2hva(vm, MEM_REGION_GPA);
	memset(hva, 0, PAGE_SIZE);
	pte = vm_get_page_table_entry(vm, vcpu, MEM_REGION_GVA);
	vm_set_page_table_entry(vm, vcpu, MEM_REGION_GVA, pte | (1ull << 36));

	vcpu_run(vcpu);
	process_exit_on_emulation_error(vcpu);
	vcpu_run(vcpu);

	TEST_ASSERT(process_ucall(vcpu) == UCALL_DONE, "Expected UCALL_DONE");

	kvm_vm_free(vm);

	return 0;
}
