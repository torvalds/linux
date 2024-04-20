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

/* Maximum counters(firmware + hardware) */
#define RISCV_MAX_PMU_COUNTERS 64
union sbi_pmu_ctr_info ctrinfo_arr[RISCV_MAX_PMU_COUNTERS];

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

int main(void)
{
	test_vm_basic_test(test_pmu_basic_sanity);
	pr_info("SBI PMU basic test : PASS\n");

	test_vm_events_test(test_pmu_events);
	pr_info("SBI PMU event verification test : PASS\n");

	return 0;
}
