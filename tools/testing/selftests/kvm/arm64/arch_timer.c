// SPDX-License-Identifier: GPL-2.0-only
/*
 * The test validates both the virtual and physical timer IRQs using
 * CVAL and TVAL registers.
 *
 * Copyright (c) 2021, Google LLC.
 */
#include "arch_timer.h"
#include "delay.h"
#include "gic.h"
#include "processor.h"
#include "timer_test.h"
#include "ucall_common.h"
#include "vgic.h"

enum guest_stage {
	GUEST_STAGE_VTIMER_CVAL = 1,
	GUEST_STAGE_VTIMER_TVAL,
	GUEST_STAGE_PTIMER_CVAL,
	GUEST_STAGE_PTIMER_TVAL,
	GUEST_STAGE_MAX,
};

static int vtimer_irq, ptimer_irq;

static void
guest_configure_timer_action(struct test_vcpu_shared_data *shared_data)
{
	switch (shared_data->guest_stage) {
	case GUEST_STAGE_VTIMER_CVAL:
		timer_set_next_cval_ms(VIRTUAL, test_args.timer_period_ms);
		shared_data->xcnt = timer_get_cntct(VIRTUAL);
		timer_set_ctl(VIRTUAL, CTL_ENABLE);
		break;
	case GUEST_STAGE_VTIMER_TVAL:
		timer_set_next_tval_ms(VIRTUAL, test_args.timer_period_ms);
		shared_data->xcnt = timer_get_cntct(VIRTUAL);
		timer_set_ctl(VIRTUAL, CTL_ENABLE);
		break;
	case GUEST_STAGE_PTIMER_CVAL:
		timer_set_next_cval_ms(PHYSICAL, test_args.timer_period_ms);
		shared_data->xcnt = timer_get_cntct(PHYSICAL);
		timer_set_ctl(PHYSICAL, CTL_ENABLE);
		break;
	case GUEST_STAGE_PTIMER_TVAL:
		timer_set_next_tval_ms(PHYSICAL, test_args.timer_period_ms);
		shared_data->xcnt = timer_get_cntct(PHYSICAL);
		timer_set_ctl(PHYSICAL, CTL_ENABLE);
		break;
	default:
		GUEST_ASSERT(0);
	}
}

static void guest_validate_irq(unsigned int intid,
				struct test_vcpu_shared_data *shared_data)
{
	enum guest_stage stage = shared_data->guest_stage;
	uint64_t xcnt = 0, xcnt_diff_us, cval = 0;
	unsigned long xctl = 0;
	unsigned int timer_irq = 0;
	unsigned int accessor;

	if (intid == IAR_SPURIOUS)
		return;

	switch (stage) {
	case GUEST_STAGE_VTIMER_CVAL:
	case GUEST_STAGE_VTIMER_TVAL:
		accessor = VIRTUAL;
		timer_irq = vtimer_irq;
		break;
	case GUEST_STAGE_PTIMER_CVAL:
	case GUEST_STAGE_PTIMER_TVAL:
		accessor = PHYSICAL;
		timer_irq = ptimer_irq;
		break;
	default:
		GUEST_ASSERT(0);
		return;
	}

	xctl = timer_get_ctl(accessor);
	if ((xctl & CTL_IMASK) || !(xctl & CTL_ENABLE))
		return;

	timer_set_ctl(accessor, CTL_IMASK);
	xcnt = timer_get_cntct(accessor);
	cval = timer_get_cval(accessor);

	xcnt_diff_us = cycles_to_usec(xcnt - shared_data->xcnt);

	/* Make sure we are dealing with the correct timer IRQ */
	GUEST_ASSERT_EQ(intid, timer_irq);

	/* Basic 'timer condition met' check */
	__GUEST_ASSERT(xcnt >= cval,
		       "xcnt = 0x%lx, cval = 0x%lx, xcnt_diff_us = 0x%lx",
		       xcnt, cval, xcnt_diff_us);
	__GUEST_ASSERT(xctl & CTL_ISTATUS, "xctl = 0x%lx", xctl);

	WRITE_ONCE(shared_data->nr_iter, shared_data->nr_iter + 1);
}

static void guest_irq_handler(struct ex_regs *regs)
{
	unsigned int intid = gic_get_and_ack_irq();
	uint32_t cpu = guest_get_vcpuid();
	struct test_vcpu_shared_data *shared_data = &vcpu_shared_data[cpu];

	guest_validate_irq(intid, shared_data);

	gic_set_eoi(intid);
}

static void guest_run_stage(struct test_vcpu_shared_data *shared_data,
				enum guest_stage stage)
{
	uint32_t irq_iter, config_iter;

	shared_data->guest_stage = stage;
	shared_data->nr_iter = 0;

	for (config_iter = 0; config_iter < test_args.nr_iter; config_iter++) {
		/* Setup the next interrupt */
		guest_configure_timer_action(shared_data);

		/* Setup a timeout for the interrupt to arrive */
		udelay(msecs_to_usecs(test_args.timer_period_ms) +
			test_args.timer_err_margin_us);

		irq_iter = READ_ONCE(shared_data->nr_iter);
		__GUEST_ASSERT(config_iter + 1 == irq_iter,
				"config_iter + 1 = 0x%x, irq_iter = 0x%x.\n"
				"  Guest timer interrupt was not triggered within the specified\n"
				"  interval, try to increase the error margin by [-e] option.\n",
				config_iter + 1, irq_iter);
	}
}

static void guest_code(void)
{
	uint32_t cpu = guest_get_vcpuid();
	struct test_vcpu_shared_data *shared_data = &vcpu_shared_data[cpu];

	local_irq_disable();

	gic_init(GIC_V3, test_args.nr_vcpus);

	timer_set_ctl(VIRTUAL, CTL_IMASK);
	timer_set_ctl(PHYSICAL, CTL_IMASK);

	gic_irq_enable(vtimer_irq);
	gic_irq_enable(ptimer_irq);
	local_irq_enable();

	guest_run_stage(shared_data, GUEST_STAGE_VTIMER_CVAL);
	guest_run_stage(shared_data, GUEST_STAGE_VTIMER_TVAL);
	guest_run_stage(shared_data, GUEST_STAGE_PTIMER_CVAL);
	guest_run_stage(shared_data, GUEST_STAGE_PTIMER_TVAL);

	GUEST_DONE();
}

static void test_init_timer_irq(struct kvm_vm *vm)
{
	/* Timer initid should be same for all the vCPUs, so query only vCPU-0 */
	ptimer_irq = vcpu_get_ptimer_irq(vcpus[0]);
	vtimer_irq = vcpu_get_vtimer_irq(vcpus[0]);

	sync_global_to_guest(vm, ptimer_irq);
	sync_global_to_guest(vm, vtimer_irq);

	pr_debug("ptimer_irq: %d; vtimer_irq: %d\n", ptimer_irq, vtimer_irq);
}

struct kvm_vm *test_vm_create(void)
{
	struct kvm_vm *vm;
	unsigned int i;
	int nr_vcpus = test_args.nr_vcpus;

	TEST_REQUIRE(kvm_supports_vgic_v3());

	vm = vm_create_with_vcpus(nr_vcpus, guest_code, vcpus);

	vm_init_descriptor_tables(vm);
	vm_install_exception_handler(vm, VECTOR_IRQ_CURRENT, guest_irq_handler);

	if (!test_args.reserved) {
		if (kvm_has_cap(KVM_CAP_COUNTER_OFFSET)) {
			struct kvm_arm_counter_offset offset = {
				.counter_offset = test_args.counter_offset,
				.reserved = 0,
			};
			vm_ioctl(vm, KVM_ARM_SET_COUNTER_OFFSET, &offset);
		} else
			TEST_FAIL("no support for global offset");
	}

	for (i = 0; i < nr_vcpus; i++)
		vcpu_init_descriptor_tables(vcpus[i]);

	test_init_timer_irq(vm);

	/* Make all the test's cmdline args visible to the guest */
	sync_global_to_guest(vm, test_args);

	return vm;
}

void test_vm_cleanup(struct kvm_vm *vm)
{
	kvm_vm_free(vm);
}
