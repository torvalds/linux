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
#include "diag318_test_handler.h"
#include "kselftest.h"

static void guest_code(void)
{
	/*
	 * We embed diag 501 here instead of doing a ucall to avoid that
	 * the compiler has messed with r11 at the time of the ucall.
	 */
	asm volatile (
		"0:	diag 0,0,0x501\n"
		"	ahi 11,1\n"
		"	j 0b\n"
	);
}

#define REG_COMPARE(reg) \
	TEST_ASSERT(left->reg == right->reg, \
		    "Register " #reg \
		    " values did not match: 0x%llx, 0x%llx\n", \
		    left->reg, right->reg)

#define REG_COMPARE32(reg) \
	TEST_ASSERT(left->reg == right->reg, \
		    "Register " #reg \
		    " values did not match: 0x%x, 0x%x\n", \
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
		REG_COMPARE32(acrs[i]);

	for (i = 0; i < 16; i++)
		REG_COMPARE(crs[i]);
}

#undef REG_COMPARE

#define TEST_SYNC_FIELDS   (KVM_SYNC_GPRS|KVM_SYNC_ACRS|KVM_SYNC_CRS|KVM_SYNC_DIAG318)
#define INVALID_SYNC_FIELD 0x80000000

void test_read_invalid(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	int rv;

	/* Request reading invalid register set from VCPU. */
	run->kvm_valid_regs = INVALID_SYNC_FIELD;
	rv = _vcpu_run(vcpu);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_valid_regs did not cause expected KVM_RUN error: %d\n",
		    rv);
	run->kvm_valid_regs = 0;

	run->kvm_valid_regs = INVALID_SYNC_FIELD | TEST_SYNC_FIELDS;
	rv = _vcpu_run(vcpu);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_valid_regs did not cause expected KVM_RUN error: %d\n",
		    rv);
	run->kvm_valid_regs = 0;
}

void test_set_invalid(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	int rv;

	/* Request setting invalid register set into VCPU. */
	run->kvm_dirty_regs = INVALID_SYNC_FIELD;
	rv = _vcpu_run(vcpu);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_dirty_regs did not cause expected KVM_RUN error: %d\n",
		    rv);
	run->kvm_dirty_regs = 0;

	run->kvm_dirty_regs = INVALID_SYNC_FIELD | TEST_SYNC_FIELDS;
	rv = _vcpu_run(vcpu);
	TEST_ASSERT(rv < 0 && errno == EINVAL,
		    "Invalid kvm_dirty_regs did not cause expected KVM_RUN error: %d\n",
		    rv);
	run->kvm_dirty_regs = 0;
}

void test_req_and_verify_all_valid_regs(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	struct kvm_sregs sregs;
	struct kvm_regs regs;
	int rv;

	/* Request and verify all valid register sets. */
	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	rv = _vcpu_run(vcpu);
	TEST_ASSERT(rv == 0, "vcpu_run failed: %d\n", rv);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_S390_SIEIC);
	TEST_ASSERT(run->s390_sieic.icptcode == 4 &&
		    (run->s390_sieic.ipa >> 8) == 0x83 &&
		    (run->s390_sieic.ipb >> 16) == 0x501,
		    "Unexpected interception code: ic=%u, ipa=0x%x, ipb=0x%x\n",
		    run->s390_sieic.icptcode, run->s390_sieic.ipa,
		    run->s390_sieic.ipb);

	vcpu_regs_get(vcpu, &regs);
	compare_regs(&regs, &run->s.regs);

	vcpu_sregs_get(vcpu, &sregs);
	compare_sregs(&sregs, &run->s.regs);
}

void test_set_and_verify_various_reg_values(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	struct kvm_sregs sregs;
	struct kvm_regs regs;
	int rv;

	/* Set and verify various register values */
	run->s.regs.gprs[11] = 0xBAD1DEA;
	run->s.regs.acrs[0] = 1 << 11;

	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	run->kvm_dirty_regs = KVM_SYNC_GPRS | KVM_SYNC_ACRS;

	if (get_diag318_info() > 0) {
		run->s.regs.diag318 = get_diag318_info();
		run->kvm_dirty_regs |= KVM_SYNC_DIAG318;
	}

	rv = _vcpu_run(vcpu);
	TEST_ASSERT(rv == 0, "vcpu_run failed: %d\n", rv);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_S390_SIEIC);
	TEST_ASSERT(run->s.regs.gprs[11] == 0xBAD1DEA + 1,
		    "r11 sync regs value incorrect 0x%llx.",
		    run->s.regs.gprs[11]);
	TEST_ASSERT(run->s.regs.acrs[0]  == 1 << 11,
		    "acr0 sync regs value incorrect 0x%x.",
		    run->s.regs.acrs[0]);
	TEST_ASSERT(run->s.regs.diag318 == get_diag318_info(),
		    "diag318 sync regs value incorrect 0x%llx.",
		    run->s.regs.diag318);

	vcpu_regs_get(vcpu, &regs);
	compare_regs(&regs, &run->s.regs);

	vcpu_sregs_get(vcpu, &sregs);
	compare_sregs(&sregs, &run->s.regs);
}

void test_clear_kvm_dirty_regs_bits(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	int rv;

	/* Clear kvm_dirty_regs bits, verify new s.regs values are
	 * overwritten with existing guest values.
	 */
	run->kvm_valid_regs = TEST_SYNC_FIELDS;
	run->kvm_dirty_regs = 0;
	run->s.regs.gprs[11] = 0xDEADBEEF;
	run->s.regs.diag318 = 0x4B1D;
	rv = _vcpu_run(vcpu);
	TEST_ASSERT(rv == 0, "vcpu_run failed: %d\n", rv);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_S390_SIEIC);
	TEST_ASSERT(run->s.regs.gprs[11] != 0xDEADBEEF,
		    "r11 sync regs value incorrect 0x%llx.",
		    run->s.regs.gprs[11]);
	TEST_ASSERT(run->s.regs.diag318 != 0x4B1D,
		    "diag318 sync regs value incorrect 0x%llx.",
		    run->s.regs.diag318);
}

struct testdef {
	const char *name;
	void (*test)(struct kvm_vcpu *vcpu);
} testlist[] = {
	{ "read invalid", test_read_invalid },
	{ "set invalid", test_set_invalid },
	{ "request+verify all valid regs", test_req_and_verify_all_valid_regs },
	{ "set+verify various regs", test_set_and_verify_various_reg_values },
	{ "clear kvm_dirty_regs bits", test_clear_kvm_dirty_regs_bits },
};

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int idx;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_SYNC_REGS));

	ksft_print_header();

	ksft_set_plan(ARRAY_SIZE(testlist));

	/* Create VM */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	for (idx = 0; idx < ARRAY_SIZE(testlist); idx++) {
		testlist[idx].test(vcpu);
		ksft_test_result_pass("%s\n", testlist[idx].name);
	}

	kvm_vm_free(vm);

	ksft_finished();	/* Print results and exit() accordingly */
}
