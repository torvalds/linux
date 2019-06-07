/*
 * Test for x86 KVM_CAP_SYNC_REGS
 *
 * Copyright (C) 2018, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Verifies expected behavior of x86 KVM_CAP_SYNC_REGS functionality,
 * including requesting an invalid register set, updates to/from values
 * in kvm_run.s.regs when kvm_valid_regs and kvm_dirty_regs are toggled.
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

#define VCPU_ID 5

void guest_code(void)
{
	/*
	 * use a callee-save register, otherwise the compiler
	 * saves it around the call to GUEST_SYNC.
	 */
	register u32 stage asm("rbx");
	for (;;) {
		GUEST_SYNC(0);
		stage++;
		asm volatile ("" : : "r" (stage));
	}
}

static void compare_regs(struct kvm_regs *left, struct kvm_regs *right)
{
#define REG_COMPARE(reg) \
	TEST_ASSERT(left->reg == right->reg, \
		    "Register " #reg \
		    " values did not match: 0x%llx, 0x%llx\n", \
		    left->reg, right->reg)
	REG_COMPARE(rax);
	REG_COMPARE(rbx);
	REG_COMPARE(rcx);
	REG_COMPARE(rdx);
	REG_COMPARE(rsi);
	REG_COMPARE(rdi);
	REG_COMPARE(rsp);
	REG_COMPARE(rbp);
	REG_COMPARE(r8);
	REG_COMPARE(r9);
	REG_COMPARE(r10);
	REG_COMPARE(r11);
	REG_COMPARE(r12);
	REG_COMPARE(r13);
	REG_COMPARE(r14);
	REG_COMPARE(r15);
	REG_COMPARE(rip);
	REG_COMPARE(rflags);
#undef REG_COMPARE
}

static void compare_sregs(struct kvm_sregs *left, struct kvm_sregs *right)
{
}

static void compare_vcpu_events(struct kvm_vcpu_events *left,
				struct kvm_vcpu_events *right)
{
}

#define TEST_SYNC_FIELDS   (KVM_SYNC_X86_REGS|KVM_SYNC_X86_SREGS|KVM_SYNC_X86_EVENTS)
#define INVALID_SYNC_FIELD 0x80000000

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct kvm_regs regs;
	struct kvm_sregs sregs;
	struct kvm_vcpu_events events;
	int rv, cap;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	cap = kvm_check_cap(KVM_CAP_SYNC_REGS);
	if ((cap & TEST_SYNC_FIELDS) != TEST_SYNC_FIELDS) {
		fprintf(stderr, "KVM_CAP_SYNC_REGS not supported, skipping test\n");
		exit(KSFT_SKIP);
	}
	if ((cap & INVALID_SYNC_FIELD) != 0) {
		fprintf(stderr, "The \"invalid\" field is not invalid, skipping test\n");
		exit(KSFT_SKIP);
	}

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);

	run = vcpu_state(vm, VCPU_ID);

	/* Request reading invalid register set from VCPU. */
	run->kvm_valid_regs = INVALID_SYNC_FIELD;
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_valid_regs did not cause expected KVM_RUN error: %d\n",
		    rv);
	vcpu_state(vm, VCPU_ID)->kvm_valid_regs = 0;

	run->kvm_valid_regs = INVALID_SYNC_FIELD | TEST_SYNC_FIELDS;
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_valid_regs did not cause expected KVM_RUN error: %d\n",
		    rv);
	vcpu_state(vm, VCPU_ID)->kvm_valid_regs = 0;

	/* Request setting invalid register set into VCPU. */
	run->kvm_dirty_regs = INVALID_SYNC_FIELD;
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_dirty_regs did not cause expected KVM_RUN error: %d\n",
		    rv);
	vcpu_state(vm, VCPU_ID)->kvm_dirty_regs = 0;

	run->kvm_dirty_regs = INVALID_SYNC_FIELD | TEST_SYNC_FIELDS;
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_dirty_regs did not cause expected KVM_RUN error: %d\n",
		    rv);
	vcpu_state(vm, VCPU_ID)->kvm_dirty_regs = 0;

	/* Request and verify all valid register sets. */
	/* TODO: BUILD TIME CHECK: TEST_ASSERT(KVM_SYNC_X86_NUM_FIELDS != 3); */
	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Unexpected exit reason: %u (%s),\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));

	vcpu_regs_get(vm, VCPU_ID, &regs);
	compare_regs(&regs, &run->s.regs.regs);

	vcpu_sregs_get(vm, VCPU_ID, &sregs);
	compare_sregs(&sregs, &run->s.regs.sregs);

	vcpu_events_get(vm, VCPU_ID, &events);
	compare_vcpu_events(&events, &run->s.regs.events);

	/* Set and verify various register values. */
	run->s.regs.regs.rbx = 0xBAD1DEA;
	run->s.regs.sregs.apic_base = 1 << 11;
	/* TODO run->s.regs.events.XYZ = ABC; */

	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	run->kvm_dirty_regs = KVM_SYNC_X86_REGS | KVM_SYNC_X86_SREGS;
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Unexpected exit reason: %u (%s),\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->s.regs.regs.rbx == 0xBAD1DEA + 1,
		    "rbx sync regs value incorrect 0x%llx.",
		    run->s.regs.regs.rbx);
	TEST_ASSERT(run->s.regs.sregs.apic_base == 1 << 11,
		    "apic_base sync regs value incorrect 0x%llx.",
		    run->s.regs.sregs.apic_base);

	vcpu_regs_get(vm, VCPU_ID, &regs);
	compare_regs(&regs, &run->s.regs.regs);

	vcpu_sregs_get(vm, VCPU_ID, &sregs);
	compare_sregs(&sregs, &run->s.regs.sregs);

	vcpu_events_get(vm, VCPU_ID, &events);
	compare_vcpu_events(&events, &run->s.regs.events);

	/* Clear kvm_dirty_regs bits, verify new s.regs values are
	 * overwritten with existing guest values.
	 */
	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	run->kvm_dirty_regs = 0;
	run->s.regs.regs.rbx = 0xDEADBEEF;
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Unexpected exit reason: %u (%s),\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->s.regs.regs.rbx != 0xDEADBEEF,
		    "rbx sync regs value incorrect 0x%llx.",
		    run->s.regs.regs.rbx);

	/* Clear kvm_valid_regs bits and kvm_dirty_bits.
	 * Verify s.regs values are not overwritten with existing guest values
	 * and that guest values are not overwritten with kvm_sync_regs values.
	 */
	run->kvm_valid_regs = 0;
	run->kvm_dirty_regs = 0;
	run->s.regs.regs.rbx = 0xAAAA;
	regs.rbx = 0xBAC0;
	vcpu_regs_set(vm, VCPU_ID, &regs);
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Unexpected exit reason: %u (%s),\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->s.regs.regs.rbx == 0xAAAA,
		    "rbx sync regs value incorrect 0x%llx.",
		    run->s.regs.regs.rbx);
	vcpu_regs_get(vm, VCPU_ID, &regs);
	TEST_ASSERT(regs.rbx == 0xBAC0 + 1,
		    "rbx guest value incorrect 0x%llx.",
		    regs.rbx);

	/* Clear kvm_valid_regs bits. Verify s.regs values are not overwritten
	 * with existing guest values but that guest values are overwritten
	 * with kvm_sync_regs values.
	 */
	run->kvm_valid_regs = 0;
	run->kvm_dirty_regs = TEST_SYNC_FIELDS;
	run->s.regs.regs.rbx = 0xBBBB;
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Unexpected exit reason: %u (%s),\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->s.regs.regs.rbx == 0xBBBB,
		    "rbx sync regs value incorrect 0x%llx.",
		    run->s.regs.regs.rbx);
	vcpu_regs_get(vm, VCPU_ID, &regs);
	TEST_ASSERT(regs.rbx == 0xBBBB + 1,
		    "rbx guest value incorrect 0x%llx.",
		    regs.rbx);

	kvm_vm_free(vm);

	return 0;
}
