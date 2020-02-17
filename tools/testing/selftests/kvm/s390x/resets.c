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

#define VCPU_ID 3
#define LOCAL_IRQS 32

struct kvm_s390_irq buf[VCPU_ID + LOCAL_IRQS];

struct kvm_vm *vm;
struct kvm_run *run;
struct kvm_sync_regs *regs;
static uint64_t regs_null[16];

static uint64_t crs[16] = { 0x40000ULL,
			    0x42000ULL,
			    0, 0, 0, 0, 0,
			    0x43000ULL,
			    0, 0, 0, 0, 0,
			    0x44000ULL,
			    0, 0
};

static void guest_code_initial(void)
{
	/* Round toward 0 */
	uint32_t fpc = 0x11;

	/* Dirty registers */
	asm volatile (
		"	lctlg	0,15,%0\n"
		"	sfpc	%1\n"
		: : "Q" (crs), "d" (fpc));
	GUEST_SYNC(0);
}

static void test_one_reg(uint64_t id, uint64_t value)
{
	struct kvm_one_reg reg;
	uint64_t eval_reg;

	reg.addr = (uintptr_t)&eval_reg;
	reg.id = id;
	vcpu_get_reg(vm, VCPU_ID, &reg);
	TEST_ASSERT(eval_reg == value, "value == %s", value);
}

static void assert_noirq(void)
{
	struct kvm_s390_irq_state irq_state;
	int irqs;

	irq_state.len = sizeof(buf);
	irq_state.buf = (unsigned long)buf;
	irqs = _vcpu_ioctl(vm, VCPU_ID, KVM_S390_GET_IRQ_STATE, &irq_state);
	/*
	 * irqs contains the number of retrieved interrupts. Any interrupt
	 * (notably, the emergency call interrupt we have injected) should
	 * be cleared by the resets, so this should be 0.
	 */
	TEST_ASSERT(irqs >= 0, "Could not fetch IRQs: errno %d\n", errno);
	TEST_ASSERT(!irqs, "IRQ pending");
}

static void assert_clear(void)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;
	struct kvm_fpu fpu;

	vcpu_regs_get(vm, VCPU_ID, &regs);
	TEST_ASSERT(!memcmp(&regs.gprs, regs_null, sizeof(regs.gprs)), "grs == 0");

	vcpu_sregs_get(vm, VCPU_ID, &sregs);
	TEST_ASSERT(!memcmp(&sregs.acrs, regs_null, sizeof(sregs.acrs)), "acrs == 0");

	vcpu_fpu_get(vm, VCPU_ID, &fpu);
	TEST_ASSERT(!memcmp(&fpu.fprs, regs_null, sizeof(fpu.fprs)), "fprs == 0");
}

static void assert_initial(void)
{
	struct kvm_sregs sregs;
	struct kvm_fpu fpu;

	vcpu_sregs_get(vm, VCPU_ID, &sregs);
	TEST_ASSERT(sregs.crs[0] == 0xE0UL, "cr0 == 0xE0");
	TEST_ASSERT(sregs.crs[14] == 0xC2000000UL, "cr14 == 0xC2000000");
	TEST_ASSERT(!memcmp(&sregs.crs[1], regs_null, sizeof(sregs.crs[1]) * 12),
		    "cr1-13 == 0");
	TEST_ASSERT(sregs.crs[15] == 0, "cr15 == 0");

	vcpu_fpu_get(vm, VCPU_ID, &fpu);
	TEST_ASSERT(!fpu.fpc, "fpc == 0");

	test_one_reg(KVM_REG_S390_GBEA, 1);
	test_one_reg(KVM_REG_S390_PP, 0);
	test_one_reg(KVM_REG_S390_TODPR, 0);
	test_one_reg(KVM_REG_S390_CPU_TIMER, 0);
	test_one_reg(KVM_REG_S390_CLOCK_COMP, 0);
}

static void assert_normal(void)
{
	test_one_reg(KVM_REG_S390_PFTOKEN, KVM_S390_PFAULT_TOKEN_INVALID);
	assert_noirq();
}

static void inject_irq(int cpu_id)
{
	struct kvm_s390_irq_state irq_state;
	struct kvm_s390_irq *irq = &buf[0];
	int irqs;

	/* Inject IRQ */
	irq_state.len = sizeof(struct kvm_s390_irq);
	irq_state.buf = (unsigned long)buf;
	irq->type = KVM_S390_INT_EMERGENCY;
	irq->u.emerg.code = cpu_id;
	irqs = _vcpu_ioctl(vm, cpu_id, KVM_S390_SET_IRQ_STATE, &irq_state);
	TEST_ASSERT(irqs >= 0, "Error injecting EMERGENCY IRQ errno %d\n", errno);
}

static void test_normal(void)
{
	printf("Testing normal reset\n");
	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code_initial);
	run = vcpu_state(vm, VCPU_ID);
	regs = &run->s.regs;

	vcpu_run(vm, VCPU_ID);

	inject_irq(VCPU_ID);

	vcpu_ioctl(vm, VCPU_ID, KVM_S390_NORMAL_RESET, 0);
	assert_normal();
	kvm_vm_free(vm);
}

static void test_initial(void)
{
	printf("Testing initial reset\n");
	vm = vm_create_default(VCPU_ID, 0, guest_code_initial);
	run = vcpu_state(vm, VCPU_ID);
	regs = &run->s.regs;

	vcpu_run(vm, VCPU_ID);

	inject_irq(VCPU_ID);

	vcpu_ioctl(vm, VCPU_ID, KVM_S390_INITIAL_RESET, 0);
	assert_normal();
	assert_initial();
	kvm_vm_free(vm);
}

static void test_clear(void)
{
	printf("Testing clear reset\n");
	vm = vm_create_default(VCPU_ID, 0, guest_code_initial);
	run = vcpu_state(vm, VCPU_ID);
	regs = &run->s.regs;

	vcpu_run(vm, VCPU_ID);

	inject_irq(VCPU_ID);

	vcpu_ioctl(vm, VCPU_ID, KVM_S390_CLEAR_RESET, 0);
	assert_normal();
	assert_initial();
	assert_clear();
	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	setbuf(stdout, NULL);	/* Tell stdout not to buffer its content */

	test_initial();
	if (kvm_check_cap(KVM_CAP_S390_VCPU_RESETS)) {
		test_normal();
		test_clear();
	}
	return 0;
}
