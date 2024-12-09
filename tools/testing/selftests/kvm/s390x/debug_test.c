// SPDX-License-Identifier: GPL-2.0-only
/* Test KVM debugging features. */
#include "kvm_util.h"
#include "test_util.h"
#include "sie.h"

#include <linux/kvm.h>

#define __LC_SVC_NEW_PSW 0x1c0
#define __LC_PGM_NEW_PSW 0x1d0
#define IPA0_DIAG 0x8300
#define PGM_SPECIFICATION 0x06

/* Common code for testing single-stepping interruptions. */
extern char int_handler[];
asm("int_handler:\n"
    "j .\n");

static struct kvm_vm *test_step_int_1(struct kvm_vcpu **vcpu, void *guest_code,
				      size_t new_psw_off, uint64_t *new_psw)
{
	struct kvm_guest_debug debug = {};
	struct kvm_regs regs;
	struct kvm_vm *vm;
	char *lowcore;

	vm = vm_create_with_one_vcpu(vcpu, guest_code);
	lowcore = addr_gpa2hva(vm, 0);
	new_psw[0] = (*vcpu)->run->psw_mask;
	new_psw[1] = (uint64_t)int_handler;
	memcpy(lowcore + new_psw_off, new_psw, 16);
	vcpu_regs_get(*vcpu, &regs);
	regs.gprs[2] = -1;
	vcpu_regs_set(*vcpu, &regs);
	debug.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP;
	vcpu_guest_debug_set(*vcpu, &debug);
	vcpu_run(*vcpu);

	return vm;
}

static void test_step_int(void *guest_code, size_t new_psw_off)
{
	struct kvm_vcpu *vcpu;
	uint64_t new_psw[2];
	struct kvm_vm *vm;

	vm = test_step_int_1(&vcpu, guest_code, new_psw_off, new_psw);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_DEBUG);
	TEST_ASSERT_EQ(vcpu->run->psw_mask, new_psw[0]);
	TEST_ASSERT_EQ(vcpu->run->psw_addr, new_psw[1]);
	kvm_vm_free(vm);
}

/* Test single-stepping "boring" program interruptions. */
extern char test_step_pgm_guest_code[];
asm("test_step_pgm_guest_code:\n"
    ".insn rr,0x1d00,%r1,%r0 /* dr %r1,%r0 */\n"
    "j .\n");

static void test_step_pgm(void)
{
	test_step_int(test_step_pgm_guest_code, __LC_PGM_NEW_PSW);
}

/*
 * Test single-stepping program interruptions caused by DIAG.
 * Userspace emulation must not interfere with single-stepping.
 */
extern char test_step_pgm_diag_guest_code[];
asm("test_step_pgm_diag_guest_code:\n"
    "diag %r0,%r0,0\n"
    "j .\n");

static void test_step_pgm_diag(void)
{
	struct kvm_s390_irq irq = {
		.type = KVM_S390_PROGRAM_INT,
		.u.pgm.code = PGM_SPECIFICATION,
	};
	struct kvm_vcpu *vcpu;
	uint64_t new_psw[2];
	struct kvm_vm *vm;

	vm = test_step_int_1(&vcpu, test_step_pgm_diag_guest_code,
			     __LC_PGM_NEW_PSW, new_psw);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_S390_SIEIC);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.icptcode, ICPT_INST);
	TEST_ASSERT_EQ(vcpu->run->s390_sieic.ipa & 0xff00, IPA0_DIAG);
	vcpu_ioctl(vcpu, KVM_S390_IRQ, &irq);
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_DEBUG);
	TEST_ASSERT_EQ(vcpu->run->psw_mask, new_psw[0]);
	TEST_ASSERT_EQ(vcpu->run->psw_addr, new_psw[1]);
	kvm_vm_free(vm);
}

/*
 * Test single-stepping program interruptions caused by ISKE.
 * CPUSTAT_KSS handling must not interfere with single-stepping.
 */
extern char test_step_pgm_iske_guest_code[];
asm("test_step_pgm_iske_guest_code:\n"
    "iske %r2,%r2\n"
    "j .\n");

static void test_step_pgm_iske(void)
{
	test_step_int(test_step_pgm_iske_guest_code, __LC_PGM_NEW_PSW);
}

/*
 * Test single-stepping program interruptions caused by LCTL.
 * KVM emulation must not interfere with single-stepping.
 */
extern char test_step_pgm_lctl_guest_code[];
asm("test_step_pgm_lctl_guest_code:\n"
    "lctl %c0,%c0,1\n"
    "j .\n");

static void test_step_pgm_lctl(void)
{
	test_step_int(test_step_pgm_lctl_guest_code, __LC_PGM_NEW_PSW);
}

/* Test single-stepping supervisor-call interruptions. */
extern char test_step_svc_guest_code[];
asm("test_step_svc_guest_code:\n"
    "svc 0\n"
    "j .\n");

static void test_step_svc(void)
{
	test_step_int(test_step_svc_guest_code, __LC_SVC_NEW_PSW);
}

/* Run all tests above. */
static struct testdef {
	const char *name;
	void (*test)(void);
} testlist[] = {
	{ "single-step pgm", test_step_pgm },
	{ "single-step pgm caused by diag", test_step_pgm_diag },
	{ "single-step pgm caused by iske", test_step_pgm_iske },
	{ "single-step pgm caused by lctl", test_step_pgm_lctl },
	{ "single-step svc", test_step_svc },
};

int main(int argc, char *argv[])
{
	int idx;

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(testlist));
	for (idx = 0; idx < ARRAY_SIZE(testlist); idx++) {
		testlist[idx].test();
		ksft_test_result_pass("%s\n", testlist[idx].name);
	}
	ksft_finished();
}
