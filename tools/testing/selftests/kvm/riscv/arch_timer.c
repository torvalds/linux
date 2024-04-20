// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch_timer.c - Tests the riscv64 sstc timer IRQ functionality
 *
 * The test validates the sstc timer IRQs using vstimecmp registers.
 * It's ported from the aarch64 arch_timer test.
 *
 * Copyright (c) 2024, Intel Corporation.
 */

#define _GNU_SOURCE

#include "arch_timer.h"
#include "kvm_util.h"
#include "processor.h"
#include "timer_test.h"

static int timer_irq = IRQ_S_TIMER;

static void guest_irq_handler(struct ex_regs *regs)
{
	uint64_t xcnt, xcnt_diff_us, cmp;
	unsigned int intid = regs->cause & ~CAUSE_IRQ_FLAG;
	uint32_t cpu = guest_get_vcpuid();
	struct test_vcpu_shared_data *shared_data = &vcpu_shared_data[cpu];

	timer_irq_disable();

	xcnt = timer_get_cycles();
	cmp = timer_get_cmp();
	xcnt_diff_us = cycles_to_usec(xcnt - shared_data->xcnt);

	/* Make sure we are dealing with the correct timer IRQ */
	GUEST_ASSERT_EQ(intid, timer_irq);

	__GUEST_ASSERT(xcnt >= cmp,
			"xcnt = 0x%"PRIx64", cmp = 0x%"PRIx64", xcnt_diff_us = 0x%" PRIx64,
			xcnt, cmp, xcnt_diff_us);

	WRITE_ONCE(shared_data->nr_iter, shared_data->nr_iter + 1);
}

static void guest_run(struct test_vcpu_shared_data *shared_data)
{
	uint32_t irq_iter, config_iter;

	shared_data->nr_iter = 0;
	shared_data->guest_stage = 0;

	for (config_iter = 0; config_iter < test_args.nr_iter; config_iter++) {
		/* Setup the next interrupt */
		timer_set_next_cmp_ms(test_args.timer_period_ms);
		shared_data->xcnt = timer_get_cycles();
		timer_irq_enable();

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

	timer_irq_disable();
	local_irq_enable();

	guest_run(shared_data);

	GUEST_DONE();
}

struct kvm_vm *test_vm_create(void)
{
	struct kvm_vm *vm;
	int nr_vcpus = test_args.nr_vcpus;

	vm = vm_create_with_vcpus(nr_vcpus, guest_code, vcpus);
	__TEST_REQUIRE(__vcpu_has_isa_ext(vcpus[0], KVM_RISCV_ISA_EXT_SSTC),
				   "SSTC not available, skipping test\n");

	vm_init_vector_tables(vm);
	vm_install_interrupt_handler(vm, guest_irq_handler);

	for (int i = 0; i < nr_vcpus; i++)
		vcpu_init_vector_tables(vcpus[i]);

	/* Initialize guest timer frequency. */
	vcpu_get_reg(vcpus[0], RISCV_TIMER_REG(frequency), &timer_freq);
	sync_global_to_guest(vm, timer_freq);
	pr_debug("timer_freq: %lu\n", timer_freq);

	/* Make all the test's cmdline args visible to the guest */
	sync_global_to_guest(vm, test_args);

	return vm;
}

void test_vm_cleanup(struct kvm_vm *vm)
{
	kvm_vm_free(vm);
}
