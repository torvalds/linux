// SPDX-License-Identifier: GPL-2.0-only
/*
 * The test validates periodic/one-shot constant timer IRQ using
 * CSR.TCFG and CSR.TVAL registers.
 */
#include "arch_timer.h"
#include "kvm_util.h"
#include "processor.h"
#include "timer_test.h"
#include "ucall_common.h"

static void do_idle(void)
{
	unsigned int intid;
	unsigned long estat;

	__asm__ __volatile__("idle 0" : : : "memory");

	estat = csr_read(LOONGARCH_CSR_ESTAT);
	intid = !!(estat & BIT(INT_TI));

	/* Make sure pending timer IRQ arrived */
	GUEST_ASSERT_EQ(intid, 1);
	csr_write(CSR_TINTCLR_TI, LOONGARCH_CSR_TINTCLR);
}

static void guest_irq_handler(struct ex_regs *regs)
{
	unsigned int intid;
	uint32_t cpu = guest_get_vcpuid();
	uint64_t xcnt, val, cfg, xcnt_diff_us;
	struct test_vcpu_shared_data *shared_data = &vcpu_shared_data[cpu];

	intid = !!(regs->estat & BIT(INT_TI));

	/* Make sure we are dealing with the correct timer IRQ */
	GUEST_ASSERT_EQ(intid, 1);

	cfg = timer_get_cfg();
	if (cfg & CSR_TCFG_PERIOD) {
		WRITE_ONCE(shared_data->nr_iter, shared_data->nr_iter - 1);
		if (shared_data->nr_iter == 0)
			disable_timer();
		csr_write(CSR_TINTCLR_TI, LOONGARCH_CSR_TINTCLR);
		return;
	}

	/*
	 * On real machine, value of LOONGARCH_CSR_TVAL is BIT_ULL(48) - 1
	 * On virtual machine, its value counts down from BIT_ULL(48) - 1
	 */
	val = timer_get_val();
	xcnt = timer_get_cycles();
	xcnt_diff_us = cycles_to_usec(xcnt - shared_data->xcnt);

	/* Basic 'timer condition met' check */
	__GUEST_ASSERT(val > cfg,
			"val = 0x%lx, cfg = 0x%lx, xcnt_diff_us = 0x%lx",
			val, cfg, xcnt_diff_us);

	csr_write(CSR_TINTCLR_TI, LOONGARCH_CSR_TINTCLR);
	WRITE_ONCE(shared_data->nr_iter, shared_data->nr_iter + 1);
}

static void guest_test_period_timer(uint32_t cpu)
{
	uint32_t irq_iter, config_iter;
	uint64_t us;
	struct test_vcpu_shared_data *shared_data = &vcpu_shared_data[cpu];

	shared_data->nr_iter = test_args.nr_iter;
	shared_data->xcnt = timer_get_cycles();
	us = msecs_to_usecs(test_args.timer_period_ms) + test_args.timer_err_margin_us;
	timer_set_next_cmp_ms(test_args.timer_period_ms, true);

	for (config_iter = 0; config_iter < test_args.nr_iter; config_iter++) {
		/* Setup a timeout for the interrupt to arrive */
		udelay(us);
	}

	irq_iter = READ_ONCE(shared_data->nr_iter);
	__GUEST_ASSERT(irq_iter == 0,
			"irq_iter = 0x%x.\n"
			"  Guest period timer interrupt was not triggered within the specified\n"
			"  interval, try to increase the error margin by [-e] option.\n",
			irq_iter);
}

static void guest_test_oneshot_timer(uint32_t cpu)
{
	uint32_t irq_iter, config_iter;
	uint64_t us;
	struct test_vcpu_shared_data *shared_data = &vcpu_shared_data[cpu];

	shared_data->nr_iter = 0;
	shared_data->guest_stage = 0;
	us = msecs_to_usecs(test_args.timer_period_ms) + test_args.timer_err_margin_us;
	for (config_iter = 0; config_iter < test_args.nr_iter; config_iter++) {
		shared_data->xcnt = timer_get_cycles();

		/* Setup the next interrupt */
		timer_set_next_cmp_ms(test_args.timer_period_ms, false);
		/* Setup a timeout for the interrupt to arrive */
		udelay(us);

		irq_iter = READ_ONCE(shared_data->nr_iter);
		__GUEST_ASSERT(config_iter + 1 == irq_iter,
				"config_iter + 1 = 0x%x, irq_iter = 0x%x.\n"
				"  Guest timer interrupt was not triggered within the specified\n"
				"  interval, try to increase the error margin by [-e] option.\n",
				config_iter + 1, irq_iter);
	}
}

static void guest_test_emulate_timer(uint32_t cpu)
{
	uint32_t config_iter;
	uint64_t xcnt_diff_us, us;
	struct test_vcpu_shared_data *shared_data = &vcpu_shared_data[cpu];

	local_irq_disable();
	shared_data->nr_iter = 0;
	us = msecs_to_usecs(test_args.timer_period_ms);
	for (config_iter = 0; config_iter < test_args.nr_iter; config_iter++) {
		shared_data->xcnt = timer_get_cycles();

		/* Setup the next interrupt */
		timer_set_next_cmp_ms(test_args.timer_period_ms, false);
		do_idle();

		xcnt_diff_us = cycles_to_usec(timer_get_cycles() - shared_data->xcnt);
		__GUEST_ASSERT(xcnt_diff_us >= us,
				"xcnt_diff_us = 0x%lx, us = 0x%lx.\n",
				xcnt_diff_us, us);
	}
	local_irq_enable();
}

static void guest_time_count_test(uint32_t cpu)
{
	uint32_t config_iter;
	unsigned long start, end, prev, us;

	/* Assuming that test case starts to run in 1 second */
	start = timer_get_cycles();
	us = msec_to_cycles(1000);
	__GUEST_ASSERT(start <= us,
			"start = 0x%lx, us = 0x%lx.\n",
			start, us);

	us = msec_to_cycles(test_args.timer_period_ms);
	for (config_iter = 0; config_iter < test_args.nr_iter; config_iter++) {
		start = timer_get_cycles();
		end = start + us;
		/* test time count growing up always */
		while (start < end) {
			prev = start;
			start = timer_get_cycles();
			__GUEST_ASSERT(prev <= start,
					"prev = 0x%lx, start = 0x%lx.\n",
					prev, start);
		}
	}
}

static void guest_code(void)
{
	uint32_t cpu = guest_get_vcpuid();

	/* must run at first */
	guest_time_count_test(cpu);

	timer_irq_enable();
	local_irq_enable();
	guest_test_period_timer(cpu);
	guest_test_oneshot_timer(cpu);
	guest_test_emulate_timer(cpu);

	GUEST_DONE();
}

struct kvm_vm *test_vm_create(void)
{
	struct kvm_vm *vm;
	int nr_vcpus = test_args.nr_vcpus;

	vm = vm_create_with_vcpus(nr_vcpus, guest_code, vcpus);
	vm_init_descriptor_tables(vm);
	vm_install_exception_handler(vm, EXCCODE_INT, guest_irq_handler);

	/* Make all the test's cmdline args visible to the guest */
	sync_global_to_guest(vm, test_args);

	return vm;
}

void test_vm_cleanup(struct kvm_vm *vm)
{
	kvm_vm_free(vm);
}
