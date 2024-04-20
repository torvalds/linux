// SPDX-License-Identifier: GPL-2.0-only
/*
 * sbi_pmu_test.c - Tests the riscv64 SBI PMU functionality.
 *
 * Copyright (c) 2024, Rivos Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "kvm_util.h"
#include "test_util.h"
#include "processor.h"
#include "sbi.h"
#include "arch_timer.h"

/* Maximum counters(firmware + hardware) */
#define RISCV_MAX_PMU_COUNTERS 64
union sbi_pmu_ctr_info ctrinfo_arr[RISCV_MAX_PMU_COUNTERS];

/* Snapshot shared memory data */
#define PMU_SNAPSHOT_GPA_BASE		BIT(30)
static void *snapshot_gva;
static vm_paddr_t snapshot_gpa;

static int vcpu_shared_irq_count;
static int counter_in_use;

/* Cache the available counters in a bitmask */
static unsigned long counter_mask_available;

static bool illegal_handler_invoked;

unsigned long pmu_csr_read_num(int csr_num)
{
#define switchcase_csr_read(__csr_num, __val)		{\
	case __csr_num:					\
		__val = csr_read(__csr_num);		\
		break; }
#define switchcase_csr_read_2(__csr_num, __val)		{\
	switchcase_csr_read(__csr_num + 0, __val)	 \
	switchcase_csr_read(__csr_num + 1, __val)}
#define switchcase_csr_read_4(__csr_num, __val)		{\
	switchcase_csr_read_2(__csr_num + 0, __val)	 \
	switchcase_csr_read_2(__csr_num + 2, __val)}
#define switchcase_csr_read_8(__csr_num, __val)		{\
	switchcase_csr_read_4(__csr_num + 0, __val)	 \
	switchcase_csr_read_4(__csr_num + 4, __val)}
#define switchcase_csr_read_16(__csr_num, __val)	{\
	switchcase_csr_read_8(__csr_num + 0, __val)	 \
	switchcase_csr_read_8(__csr_num + 8, __val)}
#define switchcase_csr_read_32(__csr_num, __val)	{\
	switchcase_csr_read_16(__csr_num + 0, __val)	 \
	switchcase_csr_read_16(__csr_num + 16, __val)}

	unsigned long ret = 0;

	switch (csr_num) {
	switchcase_csr_read_32(CSR_CYCLE, ret)
	switchcase_csr_read_32(CSR_CYCLEH, ret)
	default :
		break;
	}

	return ret;
#undef switchcase_csr_read_32
#undef switchcase_csr_read_16
#undef switchcase_csr_read_8
#undef switchcase_csr_read_4
#undef switchcase_csr_read_2
#undef switchcase_csr_read
}

static inline void dummy_func_loop(uint64_t iter)
{
	int i = 0;

	while (i < iter) {
		asm volatile("nop");
		i++;
	}
}

static void start_counter(unsigned long counter, unsigned long start_flags,
			  unsigned long ival)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_START, counter, 1, start_flags,
			ival, 0, 0);
	__GUEST_ASSERT(ret.error == 0, "Unable to start counter %ld\n", counter);
}

/* This should be invoked only for reset counter use case */
static void stop_reset_counter(unsigned long counter, unsigned long stop_flags)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_STOP, counter, 1,
					stop_flags | SBI_PMU_STOP_FLAG_RESET, 0, 0, 0);
	__GUEST_ASSERT(ret.error == SBI_ERR_ALREADY_STOPPED,
			       "Unable to stop counter %ld\n", counter);
}

static void stop_counter(unsigned long counter, unsigned long stop_flags)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_STOP, counter, 1, stop_flags,
			0, 0, 0);
	__GUEST_ASSERT(ret.error == 0, "Unable to stop counter %ld error %ld\n",
			       counter, ret.error);
}

static void guest_illegal_exception_handler(struct ex_regs *regs)
{
	__GUEST_ASSERT(regs->cause == EXC_INST_ILLEGAL,
		       "Unexpected exception handler %lx\n", regs->cause);

	illegal_handler_invoked = true;
	/* skip the trapping instruction */
	regs->epc += 4;
}

static void guest_irq_handler(struct ex_regs *regs)
{
	unsigned int irq_num = regs->cause & ~CAUSE_IRQ_FLAG;
	struct riscv_pmu_snapshot_data *snapshot_data = snapshot_gva;
	unsigned long overflown_mask;
	unsigned long counter_val = 0;

	/* Validate that we are in the correct irq handler */
	GUEST_ASSERT_EQ(irq_num, IRQ_PMU_OVF);

	/* Stop all counters first to avoid further interrupts */
	stop_counter(counter_in_use, SBI_PMU_STOP_FLAG_TAKE_SNAPSHOT);

	csr_clear(CSR_SIP, BIT(IRQ_PMU_OVF));

	overflown_mask = READ_ONCE(snapshot_data->ctr_overflow_mask);
	GUEST_ASSERT(overflown_mask & 0x01);

	WRITE_ONCE(vcpu_shared_irq_count, vcpu_shared_irq_count+1);

	counter_val = READ_ONCE(snapshot_data->ctr_values[0]);
	/* Now start the counter to mimick the real driver behavior */
	start_counter(counter_in_use, SBI_PMU_START_FLAG_SET_INIT_VALUE, counter_val);
}

static unsigned long get_counter_index(unsigned long cbase, unsigned long cmask,
				       unsigned long cflags,
				       unsigned long event)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_CFG_MATCH, cbase, cmask,
			cflags, event, 0, 0);
	__GUEST_ASSERT(ret.error == 0, "config matching failed %ld\n", ret.error);
	GUEST_ASSERT(ret.value < RISCV_MAX_PMU_COUNTERS);
	GUEST_ASSERT(BIT(ret.value) & counter_mask_available);

	return ret.value;
}

static unsigned long get_num_counters(void)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_NUM_COUNTERS, 0, 0, 0, 0, 0, 0);

	__GUEST_ASSERT(ret.error == 0, "Unable to retrieve number of counters from SBI PMU");
	__GUEST_ASSERT(ret.value < RISCV_MAX_PMU_COUNTERS,
		       "Invalid number of counters %ld\n", ret.value);

	return ret.value;
}

static void update_counter_info(int num_counters)
{
	int i = 0;
	struct sbiret ret;

	for (i = 0; i < num_counters; i++) {
		ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_GET_INFO, i, 0, 0, 0, 0, 0);

		/* There can be gaps in logical counter indicies*/
		if (ret.error)
			continue;
		GUEST_ASSERT_NE(ret.value, 0);

		ctrinfo_arr[i].value = ret.value;
		counter_mask_available |= BIT(i);
	}

	GUEST_ASSERT(counter_mask_available > 0);
}

static unsigned long read_fw_counter(int idx, union sbi_pmu_ctr_info ctrinfo)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_FW_READ, idx, 0, 0, 0, 0, 0);
	GUEST_ASSERT(ret.error == 0);
	return ret.value;
}

static unsigned long read_counter(int idx, union sbi_pmu_ctr_info ctrinfo)
{
	unsigned long counter_val = 0;

	__GUEST_ASSERT(ctrinfo.type < 2, "Invalid counter type %d", ctrinfo.type);

	if (ctrinfo.type == SBI_PMU_CTR_TYPE_HW)
		counter_val = pmu_csr_read_num(ctrinfo.csr);
	else if (ctrinfo.type == SBI_PMU_CTR_TYPE_FW)
		counter_val = read_fw_counter(idx, ctrinfo);

	return counter_val;
}

static inline void verify_sbi_requirement_assert(void)
{
	long out_val = 0;
	bool probe;

	probe = guest_sbi_probe_extension(SBI_EXT_PMU, &out_val);
	GUEST_ASSERT(probe && out_val == 1);

	if (get_host_sbi_spec_version() < sbi_mk_version(2, 0))
		__GUEST_ASSERT(0, "SBI implementation version doesn't support PMU Snapshot");
}

static void snapshot_set_shmem(vm_paddr_t gpa, unsigned long flags)
{
	unsigned long lo = (unsigned long)gpa;
#if __riscv_xlen == 32
	unsigned long hi = (unsigned long)(gpa >> 32);
#else
	unsigned long hi = gpa == -1 ? -1 : 0;
#endif
	struct sbiret ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_SNAPSHOT_SET_SHMEM,
				      lo, hi, flags, 0, 0, 0);

	GUEST_ASSERT(ret.value == 0 && ret.error == 0);
}

static void test_pmu_event(unsigned long event)
{
	unsigned long counter;
	unsigned long counter_value_pre, counter_value_post;
	unsigned long counter_init_value = 100;

	counter = get_counter_index(0, counter_mask_available, 0, event);
	counter_value_pre = read_counter(counter, ctrinfo_arr[counter]);

	/* Do not set the initial value */
	start_counter(counter, 0, 0);
	dummy_func_loop(10000);
	stop_counter(counter, 0);

	counter_value_post = read_counter(counter, ctrinfo_arr[counter]);
	__GUEST_ASSERT(counter_value_post > counter_value_pre,
		       "Event update verification failed: post [%lx] pre [%lx]\n",
		       counter_value_post, counter_value_pre);

	/*
	 * We can't just update the counter without starting it.
	 * Do start/stop twice to simulate that by first initializing to a very
	 * high value and a low value after that.
	 */
	start_counter(counter, SBI_PMU_START_FLAG_SET_INIT_VALUE, ULONG_MAX/2);
	stop_counter(counter, 0);
	counter_value_pre = read_counter(counter, ctrinfo_arr[counter]);

	start_counter(counter, SBI_PMU_START_FLAG_SET_INIT_VALUE, counter_init_value);
	stop_counter(counter, 0);
	counter_value_post = read_counter(counter, ctrinfo_arr[counter]);
	__GUEST_ASSERT(counter_value_pre > counter_value_post,
		       "Counter reinitialization verification failed : post [%lx] pre [%lx]\n",
		       counter_value_post, counter_value_pre);

	/* Now set the initial value and compare */
	start_counter(counter, SBI_PMU_START_FLAG_SET_INIT_VALUE, counter_init_value);
	dummy_func_loop(10000);
	stop_counter(counter, 0);

	counter_value_post = read_counter(counter, ctrinfo_arr[counter]);
	__GUEST_ASSERT(counter_value_post > counter_init_value,
		       "Event update verification failed: post [%lx] pre [%lx]\n",
		       counter_value_post, counter_init_value);

	stop_reset_counter(counter, 0);
}

static void test_pmu_event_snapshot(unsigned long event)
{
	unsigned long counter;
	unsigned long counter_value_pre, counter_value_post;
	unsigned long counter_init_value = 100;
	struct riscv_pmu_snapshot_data *snapshot_data = snapshot_gva;

	counter = get_counter_index(0, counter_mask_available, 0, event);
	counter_value_pre = read_counter(counter, ctrinfo_arr[counter]);

	/* Do not set the initial value */
	start_counter(counter, 0, 0);
	dummy_func_loop(10000);
	stop_counter(counter, SBI_PMU_STOP_FLAG_TAKE_SNAPSHOT);

	/* The counter value is updated w.r.t relative index of cbase */
	counter_value_post = READ_ONCE(snapshot_data->ctr_values[0]);
	__GUEST_ASSERT(counter_value_post > counter_value_pre,
		       "Event update verification failed: post [%lx] pre [%lx]\n",
		       counter_value_post, counter_value_pre);

	/*
	 * We can't just update the counter without starting it.
	 * Do start/stop twice to simulate that by first initializing to a very
	 * high value and a low value after that.
	 */
	WRITE_ONCE(snapshot_data->ctr_values[0], ULONG_MAX/2);
	start_counter(counter, SBI_PMU_START_FLAG_INIT_SNAPSHOT, 0);
	stop_counter(counter, SBI_PMU_STOP_FLAG_TAKE_SNAPSHOT);
	counter_value_pre = READ_ONCE(snapshot_data->ctr_values[0]);

	WRITE_ONCE(snapshot_data->ctr_values[0], counter_init_value);
	start_counter(counter, SBI_PMU_START_FLAG_INIT_SNAPSHOT, 0);
	stop_counter(counter, SBI_PMU_STOP_FLAG_TAKE_SNAPSHOT);
	counter_value_post = READ_ONCE(snapshot_data->ctr_values[0]);
	__GUEST_ASSERT(counter_value_pre > counter_value_post,
		       "Counter reinitialization verification failed : post [%lx] pre [%lx]\n",
		       counter_value_post, counter_value_pre);

	/* Now set the initial value and compare */
	WRITE_ONCE(snapshot_data->ctr_values[0], counter_init_value);
	start_counter(counter, SBI_PMU_START_FLAG_INIT_SNAPSHOT, 0);
	dummy_func_loop(10000);
	stop_counter(counter, SBI_PMU_STOP_FLAG_TAKE_SNAPSHOT);

	counter_value_post = READ_ONCE(snapshot_data->ctr_values[0]);
	__GUEST_ASSERT(counter_value_post > counter_init_value,
		       "Event update verification failed: post [%lx] pre [%lx]\n",
		       counter_value_post, counter_init_value);

	stop_reset_counter(counter, 0);
}

static void test_pmu_event_overflow(unsigned long event)
{
	unsigned long counter;
	unsigned long counter_value_post;
	unsigned long counter_init_value = ULONG_MAX - 10000;
	struct riscv_pmu_snapshot_data *snapshot_data = snapshot_gva;

	counter = get_counter_index(0, counter_mask_available, 0, event);
	counter_in_use = counter;

	/* The counter value is updated w.r.t relative index of cbase passed to start/stop */
	WRITE_ONCE(snapshot_data->ctr_values[0], counter_init_value);
	start_counter(counter, SBI_PMU_START_FLAG_INIT_SNAPSHOT, 0);
	dummy_func_loop(10000);
	udelay(msecs_to_usecs(2000));
	/* irq handler should have stopped the counter */
	stop_counter(counter, SBI_PMU_STOP_FLAG_TAKE_SNAPSHOT);

	counter_value_post = READ_ONCE(snapshot_data->ctr_values[0]);
	/* The counter value after stopping should be less the init value due to overflow */
	__GUEST_ASSERT(counter_value_post < counter_init_value,
		       "counter_value_post %lx counter_init_value %lx for counter\n",
		       counter_value_post, counter_init_value);

	stop_reset_counter(counter, 0);
}

static void test_invalid_event(void)
{
	struct sbiret ret;
	unsigned long event = 0x1234; /* A random event */

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_CFG_MATCH, 0,
			counter_mask_available, 0, event, 0, 0);
	GUEST_ASSERT_EQ(ret.error, SBI_ERR_NOT_SUPPORTED);
}

static void test_pmu_events(void)
{
	int num_counters = 0;

	/* Get the counter details */
	num_counters = get_num_counters();
	update_counter_info(num_counters);

	/* Sanity testing for any random invalid event */
	test_invalid_event();

	/* Only these two events are guaranteed to be present */
	test_pmu_event(SBI_PMU_HW_CPU_CYCLES);
	test_pmu_event(SBI_PMU_HW_INSTRUCTIONS);

	GUEST_DONE();
}

static void test_pmu_basic_sanity(void)
{
	long out_val = 0;
	bool probe;
	struct sbiret ret;
	int num_counters = 0, i;
	union sbi_pmu_ctr_info ctrinfo;

	probe = guest_sbi_probe_extension(SBI_EXT_PMU, &out_val);
	GUEST_ASSERT(probe && out_val == 1);

	num_counters = get_num_counters();

	for (i = 0; i < num_counters; i++) {
		ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_GET_INFO, i,
				0, 0, 0, 0, 0);

		/* There can be gaps in logical counter indicies*/
		if (ret.error)
			continue;
		GUEST_ASSERT_NE(ret.value, 0);

		ctrinfo.value = ret.value;

		/**
		 * Accessibility check of hardware and read capability of firmware counters.
		 * The spec doesn't mandate any initial value. No need to check any value.
		 */
		if (ctrinfo.type == SBI_PMU_CTR_TYPE_HW) {
			pmu_csr_read_num(ctrinfo.csr);
			GUEST_ASSERT(illegal_handler_invoked);
		} else if (ctrinfo.type == SBI_PMU_CTR_TYPE_FW) {
			read_fw_counter(i, ctrinfo);
		}
	}

	GUEST_DONE();
}

static void test_pmu_events_snaphost(void)
{
	int num_counters = 0;
	struct riscv_pmu_snapshot_data *snapshot_data = snapshot_gva;
	int i;

	/* Verify presence of SBI PMU and minimum requrired SBI version */
	verify_sbi_requirement_assert();

	snapshot_set_shmem(snapshot_gpa, 0);

	/* Get the counter details */
	num_counters = get_num_counters();
	update_counter_info(num_counters);

	/* Validate shared memory access */
	GUEST_ASSERT_EQ(READ_ONCE(snapshot_data->ctr_overflow_mask), 0);
	for (i = 0; i < num_counters; i++) {
		if (counter_mask_available & (BIT(i)))
			GUEST_ASSERT_EQ(READ_ONCE(snapshot_data->ctr_values[i]), 0);
	}
	/* Only these two events are guranteed to be present */
	test_pmu_event_snapshot(SBI_PMU_HW_CPU_CYCLES);
	test_pmu_event_snapshot(SBI_PMU_HW_INSTRUCTIONS);

	GUEST_DONE();
}

static void test_pmu_events_overflow(void)
{
	int num_counters = 0;

	/* Verify presence of SBI PMU and minimum requrired SBI version */
	verify_sbi_requirement_assert();

	snapshot_set_shmem(snapshot_gpa, 0);
	csr_set(CSR_IE, BIT(IRQ_PMU_OVF));
	local_irq_enable();

	/* Get the counter details */
	num_counters = get_num_counters();
	update_counter_info(num_counters);

	/*
	 * Qemu supports overflow for cycle/instruction.
	 * This test may fail on any platform that do not support overflow for these two events.
	 */
	test_pmu_event_overflow(SBI_PMU_HW_CPU_CYCLES);
	GUEST_ASSERT_EQ(vcpu_shared_irq_count, 1);

	test_pmu_event_overflow(SBI_PMU_HW_INSTRUCTIONS);
	GUEST_ASSERT_EQ(vcpu_shared_irq_count, 2);

	GUEST_DONE();
}

static void run_vcpu(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	vcpu_run(vcpu);
	switch (get_ucall(vcpu, &uc)) {
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
		break;
	case UCALL_DONE:
	case UCALL_SYNC:
		break;
	default:
		TEST_FAIL("Unknown ucall %lu", uc.cmd);
		break;
	}
}

void test_vm_destroy(struct kvm_vm *vm)
{
	memset(ctrinfo_arr, 0, sizeof(union sbi_pmu_ctr_info) * RISCV_MAX_PMU_COUNTERS);
	counter_mask_available = 0;
	kvm_vm_free(vm);
}

static void test_vm_basic_test(void *guest_code)
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	__TEST_REQUIRE(__vcpu_has_sbi_ext(vcpu, KVM_RISCV_SBI_EXT_PMU),
				   "SBI PMU not available, skipping test");
	vm_init_vector_tables(vm);
	/* Illegal instruction handler is required to verify read access without configuration */
	vm_install_exception_handler(vm, EXC_INST_ILLEGAL, guest_illegal_exception_handler);

	vcpu_init_vector_tables(vcpu);
	run_vcpu(vcpu);

	test_vm_destroy(vm);
}

static void test_vm_events_test(void *guest_code)
{
	struct kvm_vm *vm = NULL;
	struct kvm_vcpu *vcpu = NULL;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	__TEST_REQUIRE(__vcpu_has_sbi_ext(vcpu, KVM_RISCV_SBI_EXT_PMU),
				   "SBI PMU not available, skipping test");
	run_vcpu(vcpu);

	test_vm_destroy(vm);
}

static void test_vm_setup_snapshot_mem(struct kvm_vm *vm, struct kvm_vcpu *vcpu)
{
	/* PMU Snapshot requires single page only */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS, PMU_SNAPSHOT_GPA_BASE, 1, 1, 0);
	/* PMU_SNAPSHOT_GPA_BASE is identity mapped */
	virt_map(vm, PMU_SNAPSHOT_GPA_BASE, PMU_SNAPSHOT_GPA_BASE, 1);

	snapshot_gva = (void *)(PMU_SNAPSHOT_GPA_BASE);
	snapshot_gpa = addr_gva2gpa(vcpu->vm, (vm_vaddr_t)snapshot_gva);
	sync_global_to_guest(vcpu->vm, snapshot_gva);
	sync_global_to_guest(vcpu->vm, snapshot_gpa);
}

static void test_vm_events_snapshot_test(void *guest_code)
{
	struct kvm_vm *vm = NULL;
	struct kvm_vcpu *vcpu;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	__TEST_REQUIRE(__vcpu_has_sbi_ext(vcpu, KVM_RISCV_SBI_EXT_PMU),
				   "SBI PMU not available, skipping test");

	test_vm_setup_snapshot_mem(vm, vcpu);

	run_vcpu(vcpu);

	test_vm_destroy(vm);
}

static void test_vm_events_overflow(void *guest_code)
{
	struct kvm_vm *vm = NULL;
	struct kvm_vcpu *vcpu;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	__TEST_REQUIRE(__vcpu_has_sbi_ext(vcpu, KVM_RISCV_SBI_EXT_PMU),
				   "SBI PMU not available, skipping test");

	__TEST_REQUIRE(__vcpu_has_isa_ext(vcpu, KVM_RISCV_ISA_EXT_SSCOFPMF),
				   "Sscofpmf is not available, skipping overflow test");

	test_vm_setup_snapshot_mem(vm, vcpu);
	vm_init_vector_tables(vm);
	vm_install_interrupt_handler(vm, guest_irq_handler);

	vcpu_init_vector_tables(vcpu);
	/* Initialize guest timer frequency. */
	vcpu_get_reg(vcpu, RISCV_TIMER_REG(frequency), &timer_freq);
	sync_global_to_guest(vm, timer_freq);

	run_vcpu(vcpu);

	test_vm_destroy(vm);
}

int main(void)
{
	test_vm_basic_test(test_pmu_basic_sanity);
	pr_info("SBI PMU basic test : PASS\n");

	test_vm_events_test(test_pmu_events);
	pr_info("SBI PMU event verification test : PASS\n");

	test_vm_events_snapshot_test(test_pmu_events_snaphost);
	pr_info("SBI PMU event verification with snapshot test : PASS\n");

	test_vm_events_overflow(test_pmu_events_overflow);
	pr_info("SBI PMU event verification with overflow test : PASS\n");

	return 0;
}
