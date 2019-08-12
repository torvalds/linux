// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test for s390x KVM_CAP_SYNC_REGS
 *
 * Based on the same test for x86:
 * Copyright (C) 2018, Google LLC.
 *
 * Adaptions for s390x:
 * Copyright (C) 2019, Red Hat, Inc.
 *
 * Test expected behavior of the KVM_CAP_SYNC_REGS functionality.
 */

#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"

#define VCPU_ID 5

static void guest_code(void)
{
	for (;;) {
		asm volatile ("diag 0,0,0x501");
		asm volatile ("ahi 11,1");
	}
}

#define REG_COMPARE(reg) \
	TEST_ASSERT(left->reg == right->reg, \
		    "Register " #reg \
		    " values did not match: 0x%llx, 0x%llx\n", \
		    left->reg, right->reg)

static void compare_regs(struct kvm_regs *left, struct kvm_sync_regs *right)
{
	int i;

	for (i = 0; i < 16; i++)
		REG_COMPARE(gprs[i]);
}

static void compare_sregs(struct kvm_sregs *left, struct kvm_sync_regs *right)
{
	int i;

	for (i = 0; i < 16; i++)
		REG_COMPARE(acrs[i]);

	for (i = 0; i < 16; i++)
		REG_COMPARE(crs[i]);
}

#undef REG_COMPARE

#define TEST_SYNC_FIELDS   (KVM_SYNC_GPRS|KVM_SYNC_ACRS|KVM_SYNC_CRS)
#define INVALID_SYNC_FIELD 0x80000000

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct kvm_regs regs;
	struct kvm_sregs sregs;
	int rv, cap;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	cap = kvm_check_cap(KVM_CAP_SYNC_REGS);
	if (!cap) {
		fprintf(stderr, "CAP_SYNC_REGS not supported, skipping test\n");
		exit(KSFT_SKIP);
	}

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);

	run = vcpu_state(vm, VCPU_ID);

	/* Request and verify all valid register sets. */
	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(rv == 0, "vcpu_run failed: %d\n", rv);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_S390_SIEIC,
		    "Unexpected exit reason: %u (%s)\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->s390_sieic.icptcode == 4 &&
		    (run->s390_sieic.ipa >> 8) == 0x83 &&
		    (run->s390_sieic.ipb >> 16) == 0x501,
		    "Unexpected interception code: ic=%u, ipa=0x%x, ipb=0x%x\n",
		    run->s390_sieic.icptcode, run->s390_sieic.ipa,
		    run->s390_sieic.ipb);

	vcpu_regs_get(vm, VCPU_ID, &regs);
	compare_regs(&regs, &run->s.regs);

	vcpu_sregs_get(vm, VCPU_ID, &sregs);
	compare_sregs(&sregs, &run->s.regs);

	/* Set and verify various register values */
	run->s.regs.gprs[11] = 0xBAD1DEA;
	run->s.regs.acrs[0] = 1 << 11;

	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	run->kvm_dirty_regs = KVM_SYNC_GPRS | KVM_SYNC_ACRS;
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(rv == 0, "vcpu_run failed: %d\n", rv);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_S390_SIEIC,
		    "Unexpected exit reason: %u (%s)\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->s.regs.gprs[11] == 0xBAD1DEA + 1,
		    "r11 sync regs value incorrect 0x%llx.",
		    run->s.regs.gprs[11]);
	TEST_ASSERT(run->s.regs.acrs[0]  == 1 << 11,
		    "acr0 sync regs value incorrect 0x%llx.",
		    run->s.regs.acrs[0]);

	vcpu_regs_get(vm, VCPU_ID, &regs);
	compare_regs(&regs, &run->s.regs);

	vcpu_sregs_get(vm, VCPU_ID, &sregs);
	compare_sregs(&sregs, &run->s.regs);

	/* Clear kvm_dirty_regs bits, verify new s.regs values are
	 * overwritten with existing guest values.
	 */
	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	run->kvm_dirty_regs = 0;
	run->s.regs.gprs[11] = 0xDEADBEEF;
	rv = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(rv == 0, "vcpu_run failed: %d\n", rv);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_S390_SIEIC,
		    "Unexpected exit reason: %u (%s)\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->s.regs.gprs[11] != 0xDEADBEEF,
		    "r11 sync regs value incorrect 0x%llx.",
		    run->s.regs.gprs[11]);

	kvm_vm_free(vm);

	return 0;
}
