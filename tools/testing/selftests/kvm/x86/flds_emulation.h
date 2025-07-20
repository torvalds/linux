/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_KVM_FLDS_EMULATION_H
#define SELFTEST_KVM_FLDS_EMULATION_H

#include "kvm_util.h"

#define FLDS_MEM_EAX ".byte 0xd9, 0x00"

/*
 * flds is an instruction that the KVM instruction emulator is known not to
 * support. This can be used in guest code along with a mechanism to force
 * KVM to emulate the instruction (e.g. by providing an MMIO address) to
 * exercise emulation failures.
 */
static inline void flds(uint64_t address)
{
	__asm__ __volatile__(FLDS_MEM_EAX :: "a"(address));
}

static inline void handle_flds_emulation_failure_exit(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	struct kvm_regs regs;
	uint8_t *insn_bytes;
	uint64_t flags;

	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_INTERNAL_ERROR);

	TEST_ASSERT(run->emulation_failure.suberror == KVM_INTERNAL_ERROR_EMULATION,
		    "Unexpected suberror: %u",
		    run->emulation_failure.suberror);

	flags = run->emulation_failure.flags;
	TEST_ASSERT(run->emulation_failure.ndata >= 3 &&
		    flags & KVM_INTERNAL_ERROR_EMULATION_FLAG_INSTRUCTION_BYTES,
		    "run->emulation_failure is missing instruction bytes");

	TEST_ASSERT(run->emulation_failure.insn_size >= 2,
		    "Expected a 2-byte opcode for 'flds', got %d bytes",
		    run->emulation_failure.insn_size);

	insn_bytes = run->emulation_failure.insn_bytes;
	TEST_ASSERT(insn_bytes[0] == 0xd9 && insn_bytes[1] == 0,
		    "Expected 'flds [eax]', opcode '0xd9 0x00', got opcode 0x%02x 0x%02x",
		    insn_bytes[0], insn_bytes[1]);

	vcpu_regs_get(vcpu, &regs);
	regs.rip += 2;
	vcpu_regs_set(vcpu, &regs);
}

#endif /* !SELFTEST_KVM_FLDS_EMULATION_H */
