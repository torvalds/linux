// SPDX-License-Identifier: GPL-2.0-only
/*
 * vmx_nested_tsc_scaling_test
 *
 * Copyright 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This test case verifies that nested TSC scaling behaves as expected when
 * both L1 and L2 are scaled using different ratios. For this test we scale
 * L1 down and scale L2 up.
 */

#include <time.h>

#include "kvm_util.h"
#include "vmx.h"
#include "kselftest.h"

/* L2 is scaled up (from L1's perspective) by this factor */
#define L2_SCALE_FACTOR 4ULL

#define TSC_OFFSET_L2 ((uint64_t) -33125236320908)
#define TSC_MULTIPLIER_L2 (L2_SCALE_FACTOR << 48)

#define L2_GUEST_STACK_SIZE 64

enum { USLEEP, UCHECK_L1, UCHECK_L2 };
#define GUEST_SLEEP(sec)         ucall(UCALL_SYNC, 2, USLEEP, sec)
#define GUEST_CHECK(level, freq) ucall(UCALL_SYNC, 2, level, freq)


/*
 * This function checks whether the "actual" TSC frequency of a guest matches
 * its expected frequency. In order to account for delays in taking the TSC
 * measurements, a difference of 1% between the actual and the expected value
 * is tolerated.
 */
static void compare_tsc_freq(uint64_t actual, uint64_t expected)
{
	uint64_t tolerance, thresh_low, thresh_high;

	tolerance = expected / 100;
	thresh_low = expected - tolerance;
	thresh_high = expected + tolerance;

	TEST_ASSERT(thresh_low < actual,
		"TSC freq is expected to be between %"PRIu64" and %"PRIu64
		" but it actually is %"PRIu64,
		thresh_low, thresh_high, actual);
	TEST_ASSERT(thresh_high > actual,
		"TSC freq is expected to be between %"PRIu64" and %"PRIu64
		" but it actually is %"PRIu64,
		thresh_low, thresh_high, actual);
}

static void check_tsc_freq(int level)
{
	uint64_t tsc_start, tsc_end, tsc_freq;

	/*
	 * Reading the TSC twice with about a second's difference should give
	 * us an approximation of the TSC frequency from the guest's
	 * perspective. Now, this won't be completely accurate, but it should
	 * be good enough for the purposes of this test.
	 */
	tsc_start = rdmsr(MSR_IA32_TSC);
	GUEST_SLEEP(1);
	tsc_end = rdmsr(MSR_IA32_TSC);

	tsc_freq = tsc_end - tsc_start;

	GUEST_CHECK(level, tsc_freq);
}

static void l2_guest_code(void)
{
	check_tsc_freq(UCHECK_L2);

	/* exit to L1 */
	__asm__ __volatile__("vmcall");
}

static void l1_guest_code(struct vmx_pages *vmx_pages)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	uint32_t control;

	/* check that L1's frequency looks alright before launching L2 */
	check_tsc_freq(UCHECK_L1);

	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));

	/* prepare the VMCS for L2 execution */
	prepare_vmcs(vmx_pages, l2_guest_code, &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	/* enable TSC offsetting and TSC scaling for L2 */
	control = vmreadz(CPU_BASED_VM_EXEC_CONTROL);
	control |= CPU_BASED_USE_MSR_BITMAPS | CPU_BASED_USE_TSC_OFFSETTING;
	vmwrite(CPU_BASED_VM_EXEC_CONTROL, control);

	control = vmreadz(SECONDARY_VM_EXEC_CONTROL);
	control |= SECONDARY_EXEC_TSC_SCALING;
	vmwrite(SECONDARY_VM_EXEC_CONTROL, control);

	vmwrite(TSC_OFFSET, TSC_OFFSET_L2);
	vmwrite(TSC_MULTIPLIER, TSC_MULTIPLIER_L2);
	vmwrite(TSC_MULTIPLIER_HIGH, TSC_MULTIPLIER_L2 >> 32);

	/* launch L2 */
	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	/* check that L1's frequency still looks good */
	check_tsc_freq(UCHECK_L1);

	GUEST_DONE();
}

static void stable_tsc_check_supported(void)
{
	FILE *fp;
	char buf[4];

	fp = fopen("/sys/devices/system/clocksource/clocksource0/current_clocksource", "r");
	if (fp == NULL)
		goto skip_test;

	if (fgets(buf, sizeof(buf), fp) == NULL)
		goto close_fp;

	if (strncmp(buf, "tsc", sizeof(buf)))
		goto close_fp;

	fclose(fp);
	return;

close_fp:
	fclose(fp);
skip_test:
	print_skip("Kernel does not use TSC clocksource - assuming that host TSC is not stable");
	exit(KSFT_SKIP);
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	vm_vaddr_t vmx_pages_gva;

	uint64_t tsc_start, tsc_end;
	uint64_t tsc_khz;
	uint64_t l1_scale_factor;
	uint64_t l0_tsc_freq = 0;
	uint64_t l1_tsc_freq = 0;
	uint64_t l2_tsc_freq = 0;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_TSC_CONTROL));
	stable_tsc_check_supported();

	/*
	 * We set L1's scale factor to be a random number from 2 to 10.
	 * Ideally we would do the same for L2's factor but that one is
	 * referenced by both main() and l1_guest_code() and using a global
	 * variable does not work.
	 */
	srand(time(NULL));
	l1_scale_factor = (rand() % 9) + 2;
	printf("L1's scale down factor is: %"PRIu64"\n", l1_scale_factor);
	printf("L2's scale up factor is: %llu\n", L2_SCALE_FACTOR);

	tsc_start = rdtsc();
	sleep(1);
	tsc_end = rdtsc();

	l0_tsc_freq = tsc_end - tsc_start;
	printf("real TSC frequency is around: %"PRIu64"\n", l0_tsc_freq);

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vcpu, 1, vmx_pages_gva);

	tsc_khz = __vcpu_ioctl(vcpu, KVM_GET_TSC_KHZ, NULL);
	TEST_ASSERT(tsc_khz != -1, "vcpu ioctl KVM_GET_TSC_KHZ failed");

	/* scale down L1's TSC frequency */
	vcpu_ioctl(vcpu, KVM_SET_TSC_KHZ, (void *) (tsc_khz / l1_scale_factor));

	for (;;) {
		struct ucall uc;

		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		case UCALL_SYNC:
			switch (uc.args[0]) {
			case USLEEP:
				sleep(uc.args[1]);
				break;
			case UCHECK_L1:
				l1_tsc_freq = uc.args[1];
				printf("L1's TSC frequency is around: %"PRIu64
				       "\n", l1_tsc_freq);

				compare_tsc_freq(l1_tsc_freq,
						 l0_tsc_freq / l1_scale_factor);
				break;
			case UCHECK_L2:
				l2_tsc_freq = uc.args[1];
				printf("L2's TSC frequency is around: %"PRIu64
				       "\n", l2_tsc_freq);

				compare_tsc_freq(l2_tsc_freq,
						 l1_tsc_freq * L2_SCALE_FACTOR);
				break;
			}
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	kvm_vm_free(vm);
	return 0;
}
