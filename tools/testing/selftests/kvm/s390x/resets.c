// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test for s390x CPU resets
 *
 * Copyright (C) 2020, IBM
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "kselftest.h"

#define LOCAL_IRQS 32

#define ARBITRARY_NON_ZERO_VCPU_ID 3

struct kvm_s390_irq buf[ARBITRARY_NON_ZERO_VCPU_ID + LOCAL_IRQS];

static uint8_t regs_null[512];

static void guest_code_initial(void)
{
	/* set several CRs to "safe" value */
	unsigned long cr2_59 = 0x10;	/* enable guarded storage */
	unsigned long cr8_63 = 0x1;	/* monitor mask = 1 */
	unsigned long cr10 = 1;		/* PER START */
	unsigned long cr11 = -1;	/* PER END */


	/* Dirty registers */
	asm volatile (
		"	lghi	2,0x11\n"	/* Round toward 0 */
		"	sfpc	2\n"		/* set fpc to !=0 */
		"	lctlg	2,2,%0\n"
		"	lctlg	8,8,%1\n"
		"	lctlg	10,10,%2\n"
		"	lctlg	11,11,%3\n"
		/* now clobber some general purpose regs */
		"	llihh	0,0xffff\n"
		"	llihl	1,0x5555\n"
		"	llilh	2,0xaaaa\n"
		"	llill	3,0x0000\n"
		/* now clobber a floating point reg */
		"	lghi	4,0x1\n"
		"	cdgbr	0,4\n"
		/* now clobber an access reg */
		"	sar	9,4\n"
		/* We embed diag 501 here to control register content */
		"	diag 0,0,0x501\n"
		:
		: "m" (cr2_59), "m" (cr8_63), "m" (cr10), "m" (cr11)
		/* no clobber list as this should not return */
		);
}

static void test_one_reg(struct kvm_vcpu *vcpu, uint64_t id, uint64_t value)
{
	uint64_t eval_reg;

	vcpu_get_reg(vcpu, id, &eval_reg);
	TEST_ASSERT(eval_reg == value, "value == 0x%lx", value);
}

static void assert_noirq(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_irq_state irq_state;
	int irqs;

	irq_state.len = sizeof(buf);
	irq_state.buf = (unsigned long)buf;
	irqs = __vcpu_ioctl(vcpu, KVM_S390_GET_IRQ_STATE, &irq_state);
	/*
	 * irqs contains the number of retrieved interrupts. Any interrupt
	 * (notably, the emergency call interrupt we have injected) should
	 * be cleared by the resets, so this should be 0.
	 */
	TEST_ASSERT(irqs >= 0, "Could not fetch IRQs: errno %d\n", errno);
	TEST_ASSERT(!irqs, "IRQ pending");
}

static void assert_clear(struct kvm_vcpu *vcpu)
{
	struct kvm_sync_regs *sync_regs = &vcpu->run->s.regs;
	struct kvm_sregs sregs;
	struct kvm_regs regs;
	struct kvm_fpu fpu;

	vcpu_regs_get(vcpu, &regs);
	TEST_ASSERT(!memcmp(&regs.gprs, regs_null, sizeof(regs.gprs)), "grs == 0");

	vcpu_sregs_get(vcpu, &sregs);
	TEST_ASSERT(!memcmp(&sregs.acrs, regs_null, sizeof(sregs.acrs)), "acrs == 0");

	vcpu_fpu_get(vcpu, &fpu);
	TEST_ASSERT(!memcmp(&fpu.fprs, regs_null, sizeof(fpu.fprs)), "fprs == 0");

	/* sync regs */
	TEST_ASSERT(!memcmp(sync_regs->gprs, regs_null, sizeof(sync_regs->gprs)),
		    "gprs0-15 == 0 (sync_regs)");

	TEST_ASSERT(!memcmp(sync_regs->acrs, regs_null, sizeof(sync_regs->acrs)),
		    "acrs0-15 == 0 (sync_regs)");

	TEST_ASSERT(!memcmp(sync_regs->vrs, regs_null, sizeof(sync_regs->vrs)),
		    "vrs0-15 == 0 (sync_regs)");
}

static void assert_initial_noclear(struct kvm_vcpu *vcpu)
{
	struct kvm_sync_regs *sync_regs = &vcpu->run->s.regs;

	TEST_ASSERT(sync_regs->gprs[0] == 0xffff000000000000UL,
		    "gpr0 == 0xffff000000000000 (sync_regs)");
	TEST_ASSERT(sync_regs->gprs[1] == 0x0000555500000000UL,
		    "gpr1 == 0x0000555500000000 (sync_regs)");
	TEST_ASSERT(sync_regs->gprs[2] == 0x00000000aaaa0000UL,
		    "gpr2 == 0x00000000aaaa0000 (sync_regs)");
	TEST_ASSERT(sync_regs->gprs[3] == 0x0000000000000000UL,
		    "gpr3 == 0x0000000000000000 (sync_regs)");
	TEST_ASSERT(sync_regs->fprs[0] == 0x3ff0000000000000UL,
		    "fpr0 == 0f1 (sync_regs)");
	TEST_ASSERT(sync_regs->acrs[9] == 1, "ar9 == 1 (sync_regs)");
}

static void assert_initial(struct kvm_vcpu *vcpu)
{
	struct kvm_sync_regs *sync_regs = &vcpu->run->s.regs;
	struct kvm_sregs sregs;
	struct kvm_fpu fpu;

	/* KVM_GET_SREGS */
	vcpu_sregs_get(vcpu, &sregs);
	TEST_ASSERT(sregs.crs[0] == 0xE0UL, "cr0 == 0xE0 (KVM_GET_SREGS)");
	TEST_ASSERT(sregs.crs[14] == 0xC2000000UL,
		    "cr14 == 0xC2000000 (KVM_GET_SREGS)");
	TEST_ASSERT(!memcmp(&sregs.crs[1], regs_null, sizeof(sregs.crs[1]) * 12),
		    "cr1-13 == 0 (KVM_GET_SREGS)");
	TEST_ASSERT(sregs.crs[15] == 0, "cr15 == 0 (KVM_GET_SREGS)");

	/* sync regs */
	TEST_ASSERT(sync_regs->crs[0] == 0xE0UL, "cr0 == 0xE0 (sync_regs)");
	TEST_ASSERT(sync_regs->crs[14] == 0xC2000000UL,
		    "cr14 == 0xC2000000 (sync_regs)");
	TEST_ASSERT(!memcmp(&sync_regs->crs[1], regs_null, 8 * 12),
		    "cr1-13 == 0 (sync_regs)");
	TEST_ASSERT(sync_regs->crs[15] == 0, "cr15 == 0 (sync_regs)");
	TEST_ASSERT(sync_regs->fpc == 0, "fpc == 0 (sync_regs)");
	TEST_ASSERT(sync_regs->todpr == 0, "todpr == 0 (sync_regs)");
	TEST_ASSERT(sync_regs->cputm == 0, "cputm == 0 (sync_regs)");
	TEST_ASSERT(sync_regs->ckc == 0, "ckc == 0 (sync_regs)");
	TEST_ASSERT(sync_regs->pp == 0, "pp == 0 (sync_regs)");
	TEST_ASSERT(sync_regs->gbea == 1, "gbea == 1 (sync_regs)");

	/* kvm_run */
	TEST_ASSERT(vcpu->run->psw_addr == 0, "psw_addr == 0 (kvm_run)");
	TEST_ASSERT(vcpu->run->psw_mask == 0, "psw_mask == 0 (kvm_run)");

	vcpu_fpu_get(vcpu, &fpu);
	TEST_ASSERT(!fpu.fpc, "fpc == 0");

	test_one_reg(vcpu, KVM_REG_S390_GBEA, 1);
	test_one_reg(vcpu, KVM_REG_S390_PP, 0);
	test_one_reg(vcpu, KVM_REG_S390_TODPR, 0);
	test_one_reg(vcpu, KVM_REG_S390_CPU_TIMER, 0);
	test_one_reg(vcpu, KVM_REG_S390_CLOCK_COMP, 0);
}

static void assert_normal_noclear(struct kvm_vcpu *vcpu)
{
	struct kvm_sync_regs *sync_regs = &vcpu->run->s.regs;

	TEST_ASSERT(sync_regs->crs[2] == 0x10, "cr2 == 10 (sync_regs)");
	TEST_ASSERT(sync_regs->crs[8] == 1, "cr10 == 1 (sync_regs)");
	TEST_ASSERT(sync_regs->crs[10] == 1, "cr10 == 1 (sync_regs)");
	TEST_ASSERT(sync_regs->crs[11] == -1, "cr11 == -1 (sync_regs)");
}

static void assert_normal(struct kvm_vcpu *vcpu)
{
	test_one_reg(vcpu, KVM_REG_S390_PFTOKEN, KVM_S390_PFAULT_TOKEN_INVALID);
	TEST_ASSERT(vcpu->run->s.regs.pft == KVM_S390_PFAULT_TOKEN_INVALID,
			"pft == 0xff.....  (sync_regs)");
	assert_noirq(vcpu);
}

static void inject_irq(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_irq_state irq_state;
	struct kvm_s390_irq *irq = &buf[0];
	int irqs;

	/* Inject IRQ */
	irq_state.len = sizeof(struct kvm_s390_irq);
	irq_state.buf = (unsigned long)buf;
	irq->type = KVM_S390_INT_EMERGENCY;
	irq->u.emerg.code = vcpu->id;
	irqs = __vcpu_ioctl(vcpu, KVM_S390_SET_IRQ_STATE, &irq_state);
	TEST_ASSERT(irqs >= 0, "Error injecting EMERGENCY IRQ errno %d\n", errno);
}

static struct kvm_vm *create_vm(struct kvm_vcpu **vcpu)
{
	struct kvm_vm *vm;

	vm = vm_create(1);

	*vcpu = vm_vcpu_add(vm, ARBITRARY_NON_ZERO_VCPU_ID, guest_code_initial);

	return vm;
}

static void test_normal(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	ksft_print_msg("Testing normal reset\n");
	vm = create_vm(&vcpu);

	vcpu_run(vcpu);

	inject_irq(vcpu);

	vcpu_ioctl(vcpu, KVM_S390_NORMAL_RESET, NULL);

	/* must clears */
	assert_normal(vcpu);
	/* must not clears */
	assert_normal_noclear(vcpu);
	assert_initial_noclear(vcpu);

	kvm_vm_free(vm);
}

static void test_initial(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	ksft_print_msg("Testing initial reset\n");
	vm = create_vm(&vcpu);

	vcpu_run(vcpu);

	inject_irq(vcpu);

	vcpu_ioctl(vcpu, KVM_S390_INITIAL_RESET, NULL);

	/* must clears */
	assert_normal(vcpu);
	assert_initial(vcpu);
	/* must not clears */
	assert_initial_noclear(vcpu);

	kvm_vm_free(vm);
}

static void test_clear(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	ksft_print_msg("Testing clear reset\n");
	vm = create_vm(&vcpu);

	vcpu_run(vcpu);

	inject_irq(vcpu);

	vcpu_ioctl(vcpu, KVM_S390_CLEAR_RESET, NULL);

	/* must clears */
	assert_normal(vcpu);
	assert_initial(vcpu);
	assert_clear(vcpu);

	kvm_vm_free(vm);
}

struct testdef {
	const char *name;
	void (*test)(void);
	bool needs_cap;
} testlist[] = {
	{ "initial", test_initial, false },
	{ "normal", test_normal, true },
	{ "clear", test_clear, true },
};

int main(int argc, char *argv[])
{
	bool has_s390_vcpu_resets = kvm_check_cap(KVM_CAP_S390_VCPU_RESETS);
	int idx;

	setbuf(stdout, NULL);	/* Tell stdout not to buffer its content */

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(testlist));

	for (idx = 0; idx < ARRAY_SIZE(testlist); idx++) {
		if (!testlist[idx].needs_cap || has_s390_vcpu_resets) {
			testlist[idx].test();
			ksft_test_result_pass("%s\n", testlist[idx].name);
		} else {
			ksft_test_result_skip("%s - no VCPU_RESETS capability\n",
					      testlist[idx].name);
		}
	}

	ksft_finished();	/* Print results and exit() accordingly */
}
