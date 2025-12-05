// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "apic.h"
#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"

static bool is_x2apic;

#define IRQ_VECTOR 0x20

/* See also the comment at similar assertion in memslot_perf_test.c */
static_assert(ATOMIC_INT_LOCK_FREE == 2, "atomic int is not lockless");

static atomic_uint tpr_guest_irq_sync_val;

static void tpr_guest_irq_sync_flag_reset(void)
{
	atomic_store_explicit(&tpr_guest_irq_sync_val, 0,
			      memory_order_release);
}

static unsigned int tpr_guest_irq_sync_val_get(void)
{
	return atomic_load_explicit(&tpr_guest_irq_sync_val,
				    memory_order_acquire);
}

static void tpr_guest_irq_sync_val_inc(void)
{
	atomic_fetch_add_explicit(&tpr_guest_irq_sync_val, 1,
				  memory_order_acq_rel);
}

static void tpr_guest_irq_handler_xapic(struct ex_regs *regs)
{
	tpr_guest_irq_sync_val_inc();

	xapic_write_reg(APIC_EOI, 0);
}

static void tpr_guest_irq_handler_x2apic(struct ex_regs *regs)
{
	tpr_guest_irq_sync_val_inc();

	x2apic_write_reg(APIC_EOI, 0);
}

static void tpr_guest_irq_queue(void)
{
	if (is_x2apic) {
		x2apic_write_reg(APIC_SELF_IPI, IRQ_VECTOR);
	} else {
		uint32_t icr, icr2;

		icr = APIC_DEST_SELF | APIC_DEST_PHYSICAL | APIC_DM_FIXED |
			IRQ_VECTOR;
		icr2 = 0;

		xapic_write_reg(APIC_ICR2, icr2);
		xapic_write_reg(APIC_ICR, icr);
	}
}

static uint8_t tpr_guest_tpr_get(void)
{
	uint32_t taskpri;

	if (is_x2apic)
		taskpri = x2apic_read_reg(APIC_TASKPRI);
	else
		taskpri = xapic_read_reg(APIC_TASKPRI);

	return GET_APIC_PRI(taskpri);
}

static uint8_t tpr_guest_ppr_get(void)
{
	uint32_t procpri;

	if (is_x2apic)
		procpri = x2apic_read_reg(APIC_PROCPRI);
	else
		procpri = xapic_read_reg(APIC_PROCPRI);

	return GET_APIC_PRI(procpri);
}

static uint8_t tpr_guest_cr8_get(void)
{
	uint64_t cr8;

	asm volatile ("mov %%cr8, %[cr8]\n\t" : [cr8] "=r"(cr8));

	return cr8 & GENMASK(3, 0);
}

static void tpr_guest_check_tpr_ppr_cr8_equal(void)
{
	uint8_t tpr;

	tpr = tpr_guest_tpr_get();

	GUEST_ASSERT_EQ(tpr_guest_ppr_get(), tpr);
	GUEST_ASSERT_EQ(tpr_guest_cr8_get(), tpr);
}

static void tpr_guest_code(void)
{
	cli();

	if (is_x2apic)
		x2apic_enable();
	else
		xapic_enable();

	GUEST_ASSERT_EQ(tpr_guest_tpr_get(), 0);
	tpr_guest_check_tpr_ppr_cr8_equal();

	tpr_guest_irq_queue();

	/* TPR = 0 but IRQ masked by IF=0, should not fire */
	udelay(1000);
	GUEST_ASSERT_EQ(tpr_guest_irq_sync_val_get(), 0);

	sti();

	/* IF=1 now, IRQ should fire */
	while (tpr_guest_irq_sync_val_get() == 0)
		cpu_relax();
	GUEST_ASSERT_EQ(tpr_guest_irq_sync_val_get(), 1);

	GUEST_SYNC(true);
	tpr_guest_check_tpr_ppr_cr8_equal();

	tpr_guest_irq_queue();

	/* IRQ masked by barely high enough TPR now, should not fire */
	udelay(1000);
	GUEST_ASSERT_EQ(tpr_guest_irq_sync_val_get(), 1);

	GUEST_SYNC(false);
	tpr_guest_check_tpr_ppr_cr8_equal();

	/* TPR barely low enough now to unmask IRQ, should fire */
	while (tpr_guest_irq_sync_val_get() == 1)
		cpu_relax();
	GUEST_ASSERT_EQ(tpr_guest_irq_sync_val_get(), 2);

	GUEST_DONE();
}

static uint8_t lapic_tpr_get(struct kvm_lapic_state *xapic)
{
	return GET_APIC_PRI(*((u32 *)&xapic->regs[APIC_TASKPRI]));
}

static void lapic_tpr_set(struct kvm_lapic_state *xapic, uint8_t val)
{
	u32 *taskpri = (u32 *)&xapic->regs[APIC_TASKPRI];

	*taskpri = SET_APIC_PRI(*taskpri, val);
}

static uint8_t sregs_tpr(struct kvm_sregs *sregs)
{
	return sregs->cr8 & GENMASK(3, 0);
}

static void test_tpr_check_tpr_zero(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic_state xapic;

	vcpu_ioctl(vcpu, KVM_GET_LAPIC, &xapic);

	TEST_ASSERT_EQ(lapic_tpr_get(&xapic), 0);
}

static void test_tpr_check_tpr_cr8_equal(struct kvm_vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_lapic_state xapic;

	vcpu_sregs_get(vcpu, &sregs);
	vcpu_ioctl(vcpu, KVM_GET_LAPIC, &xapic);

	TEST_ASSERT_EQ(sregs_tpr(&sregs), lapic_tpr_get(&xapic));
}

static void test_tpr_set_tpr_for_irq(struct kvm_vcpu *vcpu, bool mask)
{
	struct kvm_lapic_state xapic;
	uint8_t tpr;

	static_assert(IRQ_VECTOR >= 16, "invalid IRQ vector number");
	tpr = IRQ_VECTOR / 16;
	if (!mask)
		tpr--;

	vcpu_ioctl(vcpu, KVM_GET_LAPIC, &xapic);
	lapic_tpr_set(&xapic, tpr);
	vcpu_ioctl(vcpu, KVM_SET_LAPIC, &xapic);
}

static void test_tpr(bool __is_x2apic)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	bool done = false;

	is_x2apic = __is_x2apic;

	vm = vm_create_with_one_vcpu(&vcpu, tpr_guest_code);
	if (is_x2apic) {
		vm_install_exception_handler(vm, IRQ_VECTOR,
					     tpr_guest_irq_handler_x2apic);
	} else {
		vm_install_exception_handler(vm, IRQ_VECTOR,
					     tpr_guest_irq_handler_xapic);
		vcpu_clear_cpuid_feature(vcpu, X86_FEATURE_X2APIC);
		virt_pg_map(vm, APIC_DEFAULT_GPA, APIC_DEFAULT_GPA);
	}

	sync_global_to_guest(vcpu->vm, is_x2apic);

	/* According to the SDM/APM the TPR value at reset is 0 */
	test_tpr_check_tpr_zero(vcpu);
	test_tpr_check_tpr_cr8_equal(vcpu);

	tpr_guest_irq_sync_flag_reset();
	sync_global_to_guest(vcpu->vm, tpr_guest_irq_sync_val);

	while (!done) {
		struct ucall uc;

		alarm(2);
		vcpu_run(vcpu);
		alarm(0);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		case UCALL_DONE:
			test_tpr_check_tpr_cr8_equal(vcpu);
			done = true;
			break;
		case UCALL_SYNC:
			test_tpr_check_tpr_cr8_equal(vcpu);
			test_tpr_set_tpr_for_irq(vcpu, uc.args[1]);
			break;
		default:
			TEST_FAIL("Unknown ucall result 0x%lx", uc.cmd);
			break;
		}
	}
	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	/*
	 * Use separate VMs for the xAPIC and x2APIC tests so that x2APIC can
	 * be fully hidden from the guest.  KVM disallows changing CPUID after
	 * KVM_RUN and AVIC is disabled if _any_ vCPU is allowed to use x2APIC.
	 */
	test_tpr(false);
	test_tpr(true);
}
