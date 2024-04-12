// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test for x86 KVM_CAP_SYNC_REGS
 *
 * Copyright (C) 2018, Google LLC.
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
#include <pthread.h>

#include "kvm_test_harness.h"
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#define UCALL_PIO_PORT ((uint16_t)0x1000)

struct ucall uc_none = {
	.cmd = UCALL_NONE,
};

/*
 * ucall is embedded here to protect against compiler reshuffling registers
 * before calling a function. In this test we only need to get KVM_EXIT_IO
 * vmexit and preserve RBX, no additional information is needed.
 */
void guest_code(void)
{
	asm volatile("1: in %[port], %%al\n"
		     "add $0x1, %%rbx\n"
		     "jmp 1b"
		     : : [port] "d" (UCALL_PIO_PORT), "D" (&uc_none)
		     : "rax", "rbx");
}

KVM_ONE_VCPU_TEST_SUITE(sync_regs_test);

static void compare_regs(struct kvm_regs *left, struct kvm_regs *right)
{
#define REG_COMPARE(reg) \
	TEST_ASSERT(left->reg == right->reg, \
		    "Register " #reg \
		    " values did not match: 0x%llx, 0x%llx", \
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

/*
 * Set an exception as pending *and* injected while KVM is processing events.
 * KVM is supposed to ignore/drop pending exceptions if userspace is also
 * requesting that an exception be injected.
 */
static void *race_events_inj_pen(void *arg)
{
	struct kvm_run *run = (struct kvm_run *)arg;
	struct kvm_vcpu_events *events = &run->s.regs.events;

	WRITE_ONCE(events->exception.nr, UD_VECTOR);

	for (;;) {
		WRITE_ONCE(run->kvm_dirty_regs, KVM_SYNC_X86_EVENTS);
		WRITE_ONCE(events->flags, 0);
		WRITE_ONCE(events->exception.injected, 1);
		WRITE_ONCE(events->exception.pending, 1);

		pthread_testcancel();
	}

	return NULL;
}

/*
 * Set an invalid exception vector while KVM is processing events.  KVM is
 * supposed to reject any vector >= 32, as well as NMIs (vector 2).
 */
static void *race_events_exc(void *arg)
{
	struct kvm_run *run = (struct kvm_run *)arg;
	struct kvm_vcpu_events *events = &run->s.regs.events;

	for (;;) {
		WRITE_ONCE(run->kvm_dirty_regs, KVM_SYNC_X86_EVENTS);
		WRITE_ONCE(events->flags, 0);
		WRITE_ONCE(events->exception.nr, UD_VECTOR);
		WRITE_ONCE(events->exception.pending, 1);
		WRITE_ONCE(events->exception.nr, 255);

		pthread_testcancel();
	}

	return NULL;
}

/*
 * Toggle CR4.PAE while KVM is processing SREGS, EFER.LME=1 with CR4.PAE=0 is
 * illegal, and KVM's MMU heavily relies on vCPU state being valid.
 */
static noinline void *race_sregs_cr4(void *arg)
{
	struct kvm_run *run = (struct kvm_run *)arg;
	__u64 *cr4 = &run->s.regs.sregs.cr4;
	__u64 pae_enabled = *cr4;
	__u64 pae_disabled = *cr4 & ~X86_CR4_PAE;

	for (;;) {
		WRITE_ONCE(run->kvm_dirty_regs, KVM_SYNC_X86_SREGS);
		WRITE_ONCE(*cr4, pae_enabled);
		asm volatile(".rept 512\n\t"
			     "nop\n\t"
			     ".endr");
		WRITE_ONCE(*cr4, pae_disabled);

		pthread_testcancel();
	}

	return NULL;
}

static void race_sync_regs(struct kvm_vcpu *vcpu, void *racer)
{
	const time_t TIMEOUT = 2; /* seconds, roughly */
	struct kvm_x86_state *state;
	struct kvm_translation tr;
	struct kvm_run *run;
	pthread_t thread;
	time_t t;

	run = vcpu->run;

	run->kvm_valid_regs = KVM_SYNC_X86_SREGS;
	vcpu_run(vcpu);
	run->kvm_valid_regs = 0;

	/* Save state *before* spawning the thread that mucks with vCPU state. */
	state = vcpu_save_state(vcpu);

	/*
	 * Selftests run 64-bit guests by default, both EFER.LME and CR4.PAE
	 * should already be set in guest state.
	 */
	TEST_ASSERT((run->s.regs.sregs.cr4 & X86_CR4_PAE) &&
		    (run->s.regs.sregs.efer & EFER_LME),
		    "vCPU should be in long mode, CR4.PAE=%d, EFER.LME=%d",
		    !!(run->s.regs.sregs.cr4 & X86_CR4_PAE),
		    !!(run->s.regs.sregs.efer & EFER_LME));

	TEST_ASSERT_EQ(pthread_create(&thread, NULL, racer, (void *)run), 0);

	for (t = time(NULL) + TIMEOUT; time(NULL) < t;) {
		/*
		 * Reload known good state if the vCPU triple faults, e.g. due
		 * to the unhandled #GPs being injected.  VMX preserves state
		 * on shutdown, but SVM synthesizes an INIT as the VMCB state
		 * is architecturally undefined on triple fault.
		 */
		if (!__vcpu_run(vcpu) && run->exit_reason == KVM_EXIT_SHUTDOWN)
			vcpu_load_state(vcpu, state);

		if (racer == race_sregs_cr4) {
			tr = (struct kvm_translation) { .linear_address = 0 };
			__vcpu_ioctl(vcpu, KVM_TRANSLATE, &tr);
		}
	}

	TEST_ASSERT_EQ(pthread_cancel(thread), 0);
	TEST_ASSERT_EQ(pthread_join(thread, NULL), 0);

	kvm_x86_state_cleanup(state);
}

KVM_ONE_VCPU_TEST(sync_regs_test, read_invalid, guest_code)
{
	struct kvm_run *run = vcpu->run;
	int rv;

	/* Request reading invalid register set from VCPU. */
	run->kvm_valid_regs = INVALID_SYNC_FIELD;
	rv = _vcpu_run(vcpu);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_valid_regs did not cause expected KVM_RUN error: %d",
		    rv);
	run->kvm_valid_regs = 0;

	run->kvm_valid_regs = INVALID_SYNC_FIELD | TEST_SYNC_FIELDS;
	rv = _vcpu_run(vcpu);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_valid_regs did not cause expected KVM_RUN error: %d",
		    rv);
	run->kvm_valid_regs = 0;
}

KVM_ONE_VCPU_TEST(sync_regs_test, set_invalid, guest_code)
{
	struct kvm_run *run = vcpu->run;
	int rv;

	/* Request setting invalid register set into VCPU. */
	run->kvm_dirty_regs = INVALID_SYNC_FIELD;
	rv = _vcpu_run(vcpu);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_dirty_regs did not cause expected KVM_RUN error: %d",
		    rv);
	run->kvm_dirty_regs = 0;

	run->kvm_dirty_regs = INVALID_SYNC_FIELD | TEST_SYNC_FIELDS;
	rv = _vcpu_run(vcpu);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_dirty_regs did not cause expected KVM_RUN error: %d",
		    rv);
	run->kvm_dirty_regs = 0;
}

KVM_ONE_VCPU_TEST(sync_regs_test, req_and_verify_all_valid, guest_code)
{
	struct kvm_run *run = vcpu->run;
	struct kvm_vcpu_events events;
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	/* Request and verify all valid register sets. */
	/* TODO: BUILD TIME CHECK: TEST_ASSERT(KVM_SYNC_X86_NUM_FIELDS != 3); */
	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

	vcpu_regs_get(vcpu, &regs);
	compare_regs(&regs, &run->s.regs.regs);

	vcpu_sregs_get(vcpu, &sregs);
	compare_sregs(&sregs, &run->s.regs.sregs);

	vcpu_events_get(vcpu, &events);
	compare_vcpu_events(&events, &run->s.regs.events);
}

KVM_ONE_VCPU_TEST(sync_regs_test, set_and_verify_various, guest_code)
{
	struct kvm_run *run = vcpu->run;
	struct kvm_vcpu_events events;
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	/* Run once to get register set */
	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

	/* Set and verify various register values. */
	run->s.regs.regs.rbx = 0xBAD1DEA;
	run->s.regs.sregs.apic_base = 1 << 11;
	/* TODO run->s.regs.events.XYZ = ABC; */

	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	run->kvm_dirty_regs = KVM_SYNC_X86_REGS | KVM_SYNC_X86_SREGS;
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
	TEST_ASSERT(run->s.regs.regs.rbx == 0xBAD1DEA + 1,
		    "rbx sync regs value incorrect 0x%llx.",
		    run->s.regs.regs.rbx);
	TEST_ASSERT(run->s.regs.sregs.apic_base == 1 << 11,
		    "apic_base sync regs value incorrect 0x%llx.",
		    run->s.regs.sregs.apic_base);

	vcpu_regs_get(vcpu, &regs);
	compare_regs(&regs, &run->s.regs.regs);

	vcpu_sregs_get(vcpu, &sregs);
	compare_sregs(&sregs, &run->s.regs.sregs);

	vcpu_events_get(vcpu, &events);
	compare_vcpu_events(&events, &run->s.regs.events);
}

KVM_ONE_VCPU_TEST(sync_regs_test, clear_kvm_dirty_regs_bits, guest_code)
{
	struct kvm_run *run = vcpu->run;

	/* Clear kvm_dirty_regs bits, verify new s.regs values are
	 * overwritten with existing guest values.
	 */
	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	run->kvm_dirty_regs = 0;
	run->s.regs.regs.rbx = 0xDEADBEEF;
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
	TEST_ASSERT(run->s.regs.regs.rbx != 0xDEADBEEF,
		    "rbx sync regs value incorrect 0x%llx.",
		    run->s.regs.regs.rbx);
}

KVM_ONE_VCPU_TEST(sync_regs_test, clear_kvm_valid_and_dirty_regs, guest_code)
{
	struct kvm_run *run = vcpu->run;
	struct kvm_regs regs;

	/* Run once to get register set */
	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

	/* Clear kvm_valid_regs bits and kvm_dirty_bits.
	 * Verify s.regs values are not overwritten with existing guest values
	 * and that guest values are not overwritten with kvm_sync_regs values.
	 */
	run->kvm_valid_regs = 0;
	run->kvm_dirty_regs = 0;
	run->s.regs.regs.rbx = 0xAAAA;
	vcpu_regs_get(vcpu, &regs);
	regs.rbx = 0xBAC0;
	vcpu_regs_set(vcpu, &regs);
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
	TEST_ASSERT(run->s.regs.regs.rbx == 0xAAAA,
		    "rbx sync regs value incorrect 0x%llx.",
		    run->s.regs.regs.rbx);
	vcpu_regs_get(vcpu, &regs);
	TEST_ASSERT(regs.rbx == 0xBAC0 + 1,
		    "rbx guest value incorrect 0x%llx.",
		    regs.rbx);
}

KVM_ONE_VCPU_TEST(sync_regs_test, clear_kvm_valid_regs_bits, guest_code)
{
	struct kvm_run *run = vcpu->run;
	struct kvm_regs regs;

	/* Run once to get register set */
	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

	/* Clear kvm_valid_regs bits. Verify s.regs values are not overwritten
	 * with existing guest values but that guest values are overwritten
	 * with kvm_sync_regs values.
	 */
	run->kvm_valid_regs = 0;
	run->kvm_dirty_regs = TEST_SYNC_FIELDS;
	run->s.regs.regs.rbx = 0xBBBB;
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
	TEST_ASSERT(run->s.regs.regs.rbx == 0xBBBB,
		    "rbx sync regs value incorrect 0x%llx.",
		    run->s.regs.regs.rbx);
	vcpu_regs_get(vcpu, &regs);
	TEST_ASSERT(regs.rbx == 0xBBBB + 1,
		    "rbx guest value incorrect 0x%llx.",
		    regs.rbx);
}

KVM_ONE_VCPU_TEST(sync_regs_test, race_cr4, guest_code)
{
	race_sync_regs(vcpu, race_sregs_cr4);
}

KVM_ONE_VCPU_TEST(sync_regs_test, race_exc, guest_code)
{
	race_sync_regs(vcpu, race_events_exc);
}

KVM_ONE_VCPU_TEST(sync_regs_test, race_inj_pen, guest_code)
{
	race_sync_regs(vcpu, race_events_inj_pen);
}

int main(int argc, char *argv[])
{
	int cap;

	cap = kvm_check_cap(KVM_CAP_SYNC_REGS);
	TEST_REQUIRE((cap & TEST_SYNC_FIELDS) == TEST_SYNC_FIELDS);
	TEST_REQUIRE(!(cap & INVALID_SYNC_FIELD));

	return test_harness_run(argc, argv);
}
