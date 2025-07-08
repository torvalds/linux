// SPDX-License-Identifier: GPL-2.0-only
/* Test operation exception forwarding.
 *
 * Copyright IBM Corp. 2025
 *
 * Authors:
 *  Janosch Frank <frankja@linux.ibm.com>
 */
#include "kselftest.h"
#include "kvm_util.h"
#include "test_util.h"
#include "sie.h"

#include <linux/kvm.h>

static void guest_code_instr0(void)
{
	asm(".word 0x0000");
}

static void test_user_instr0(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int rc;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code_instr0);
	rc = __vm_enable_cap(vm, KVM_CAP_S390_USER_INSTR0, 0);
	TEST_ASSERT_EQ(0, rc);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_S390_SIEIC);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.icptcode, ICPT_OPEREXC);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.ipa, 0);

	kvm_vm_free(vm);
}

static void guest_code_user_operexec(void)
{
	asm(".word 0x0807");
}

static void test_user_operexec(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int rc;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code_user_operexec);
	rc = __vm_enable_cap(vm, KVM_CAP_S390_USER_OPEREXEC, 0);
	TEST_ASSERT_EQ(0, rc);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_S390_SIEIC);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.icptcode, ICPT_OPEREXC);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.ipa, 0x0807);

	kvm_vm_free(vm);

	/*
	 * Since user_operexec is the superset it can be used for the
	 * 0 instruction.
	 */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code_instr0);
	rc = __vm_enable_cap(vm, KVM_CAP_S390_USER_OPEREXEC, 0);
	TEST_ASSERT_EQ(0, rc);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_S390_SIEIC);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.icptcode, ICPT_OPEREXC);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.ipa, 0);

	kvm_vm_free(vm);
}

/* combine user_instr0 and user_operexec */
static void test_user_operexec_combined(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int rc;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code_user_operexec);
	rc = __vm_enable_cap(vm, KVM_CAP_S390_USER_INSTR0, 0);
	TEST_ASSERT_EQ(0, rc);
	rc = __vm_enable_cap(vm, KVM_CAP_S390_USER_OPEREXEC, 0);
	TEST_ASSERT_EQ(0, rc);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_S390_SIEIC);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.icptcode, ICPT_OPEREXC);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.ipa, 0x0807);

	kvm_vm_free(vm);

	/* Reverse enablement order */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code_user_operexec);
	rc = __vm_enable_cap(vm, KVM_CAP_S390_USER_OPEREXEC, 0);
	TEST_ASSERT_EQ(0, rc);
	rc = __vm_enable_cap(vm, KVM_CAP_S390_USER_INSTR0, 0);
	TEST_ASSERT_EQ(0, rc);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_S390_SIEIC);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.icptcode, ICPT_OPEREXC);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.ipa, 0x0807);

	kvm_vm_free(vm);
}

/*
 * Run all tests above.
 *
 * Enablement after VCPU has been added is automatically tested since
 * we enable the capability after VCPU creation.
 */
static struct testdef {
	const char *name;
	void (*test)(void);
} testlist[] = {
	{ "instr0", test_user_instr0 },
	{ "operexec", test_user_operexec },
	{ "operexec_combined", test_user_operexec_combined},
};

int main(int argc, char *argv[])
{
	int idx;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_S390_USER_INSTR0));

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(testlist));
	for (idx = 0; idx < ARRAY_SIZE(testlist); idx++) {
		testlist[idx].test();
		ksft_test_result_pass("%s\n", testlist[idx].name);
	}
	ksft_finished();
}
