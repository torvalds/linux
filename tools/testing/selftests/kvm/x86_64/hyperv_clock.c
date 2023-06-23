// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021, Red Hat, Inc.
 *
 * Tests for Hyper-V clocksources
 */
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "hyperv.h"

struct ms_hyperv_tsc_page {
	volatile u32 tsc_sequence;
	u32 reserved1;
	volatile u64 tsc_scale;
	volatile s64 tsc_offset;
} __packed;

/* Simplified mul_u64_u64_shr() */
static inline u64 mul_u64_u64_shr64(u64 a, u64 b)
{
	union {
		u64 ll;
		struct {
			u32 low, high;
		} l;
	} rm, rn, rh, a0, b0;
	u64 c;

	a0.ll = a;
	b0.ll = b;

	rm.ll = (u64)a0.l.low * b0.l.high;
	rn.ll = (u64)a0.l.high * b0.l.low;
	rh.ll = (u64)a0.l.high * b0.l.high;

	rh.l.low = c = rm.l.high + rn.l.high + rh.l.low;
	rh.l.high = (c >> 32) + rh.l.high;

	return rh.ll;
}

static inline void nop_loop(void)
{
	int i;

	for (i = 0; i < 100000000; i++)
		asm volatile("nop");
}

static inline void check_tsc_msr_rdtsc(void)
{
	u64 tsc_freq, r1, r2, t1, t2;
	s64 delta_ns;

	tsc_freq = rdmsr(HV_X64_MSR_TSC_FREQUENCY);
	GUEST_ASSERT(tsc_freq > 0);

	/* For increased accuracy, take mean rdtsc() before and afrer rdmsr() */
	r1 = rdtsc();
	t1 = rdmsr(HV_X64_MSR_TIME_REF_COUNT);
	r1 = (r1 + rdtsc()) / 2;
	nop_loop();
	r2 = rdtsc();
	t2 = rdmsr(HV_X64_MSR_TIME_REF_COUNT);
	r2 = (r2 + rdtsc()) / 2;

	GUEST_ASSERT(r2 > r1 && t2 > t1);

	/* HV_X64_MSR_TIME_REF_COUNT is in 100ns */
	delta_ns = ((t2 - t1) * 100) - ((r2 - r1) * 1000000000 / tsc_freq);
	if (delta_ns < 0)
		delta_ns = -delta_ns;

	/* 1% tolerance */
	GUEST_ASSERT(delta_ns * 100 < (t2 - t1) * 100);
}

static inline u64 get_tscpage_ts(struct ms_hyperv_tsc_page *tsc_page)
{
	return mul_u64_u64_shr64(rdtsc(), tsc_page->tsc_scale) + tsc_page->tsc_offset;
}

static inline void check_tsc_msr_tsc_page(struct ms_hyperv_tsc_page *tsc_page)
{
	u64 r1, r2, t1, t2;

	/* Compare TSC page clocksource with HV_X64_MSR_TIME_REF_COUNT */
	t1 = get_tscpage_ts(tsc_page);
	r1 = rdmsr(HV_X64_MSR_TIME_REF_COUNT);

	/* 10 ms tolerance */
	GUEST_ASSERT(r1 >= t1 && r1 - t1 < 100000);
	nop_loop();

	t2 = get_tscpage_ts(tsc_page);
	r2 = rdmsr(HV_X64_MSR_TIME_REF_COUNT);
	GUEST_ASSERT(r2 >= t1 && r2 - t2 < 100000);
}

static void guest_main(struct ms_hyperv_tsc_page *tsc_page, vm_paddr_t tsc_page_gpa)
{
	u64 tsc_scale, tsc_offset;

	/* Set Guest OS id to enable Hyper-V emulation */
	GUEST_SYNC(1);
	wrmsr(HV_X64_MSR_GUEST_OS_ID, (u64)0x8100 << 48);
	GUEST_SYNC(2);

	check_tsc_msr_rdtsc();

	GUEST_SYNC(3);

	/* Set up TSC page is disabled state, check that it's clean */
	wrmsr(HV_X64_MSR_REFERENCE_TSC, tsc_page_gpa);
	GUEST_ASSERT(tsc_page->tsc_sequence == 0);
	GUEST_ASSERT(tsc_page->tsc_scale == 0);
	GUEST_ASSERT(tsc_page->tsc_offset == 0);

	GUEST_SYNC(4);

	/* Set up TSC page is enabled state */
	wrmsr(HV_X64_MSR_REFERENCE_TSC, tsc_page_gpa | 0x1);
	GUEST_ASSERT(tsc_page->tsc_sequence != 0);

	GUEST_SYNC(5);

	check_tsc_msr_tsc_page(tsc_page);

	GUEST_SYNC(6);

	tsc_offset = tsc_page->tsc_offset;
	/* Call KVM_SET_CLOCK from userspace, check that TSC page was updated */

	GUEST_SYNC(7);
	/* Sanity check TSC page timestamp, it should be close to 0 */
	GUEST_ASSERT(get_tscpage_ts(tsc_page) < 100000);

	GUEST_ASSERT(tsc_page->tsc_offset != tsc_offset);

	nop_loop();

	/*
	 * Enable Re-enlightenment and check that TSC page stays constant across
	 * KVM_SET_CLOCK.
	 */
	wrmsr(HV_X64_MSR_REENLIGHTENMENT_CONTROL, 0x1 << 16 | 0xff);
	wrmsr(HV_X64_MSR_TSC_EMULATION_CONTROL, 0x1);
	tsc_offset = tsc_page->tsc_offset;
	tsc_scale = tsc_page->tsc_scale;
	GUEST_SYNC(8);
	GUEST_ASSERT(tsc_page->tsc_offset == tsc_offset);
	GUEST_ASSERT(tsc_page->tsc_scale == tsc_scale);

	GUEST_SYNC(9);

	check_tsc_msr_tsc_page(tsc_page);

	/*
	 * Disable re-enlightenment and TSC page, check that KVM doesn't update
	 * it anymore.
	 */
	wrmsr(HV_X64_MSR_REENLIGHTENMENT_CONTROL, 0);
	wrmsr(HV_X64_MSR_TSC_EMULATION_CONTROL, 0);
	wrmsr(HV_X64_MSR_REFERENCE_TSC, 0);
	memset(tsc_page, 0, sizeof(*tsc_page));

	GUEST_SYNC(10);
	GUEST_ASSERT(tsc_page->tsc_sequence == 0);
	GUEST_ASSERT(tsc_page->tsc_offset == 0);
	GUEST_ASSERT(tsc_page->tsc_scale == 0);

	GUEST_DONE();
}

static void host_check_tsc_msr_rdtsc(struct kvm_vcpu *vcpu)
{
	u64 tsc_freq, r1, r2, t1, t2;
	s64 delta_ns;

	tsc_freq = vcpu_get_msr(vcpu, HV_X64_MSR_TSC_FREQUENCY);
	TEST_ASSERT(tsc_freq > 0, "TSC frequency must be nonzero");

	/* For increased accuracy, take mean rdtsc() before and afrer ioctl */
	r1 = rdtsc();
	t1 = vcpu_get_msr(vcpu, HV_X64_MSR_TIME_REF_COUNT);
	r1 = (r1 + rdtsc()) / 2;
	nop_loop();
	r2 = rdtsc();
	t2 = vcpu_get_msr(vcpu, HV_X64_MSR_TIME_REF_COUNT);
	r2 = (r2 + rdtsc()) / 2;

	TEST_ASSERT(t2 > t1, "Time reference MSR is not monotonic (%ld <= %ld)", t1, t2);

	/* HV_X64_MSR_TIME_REF_COUNT is in 100ns */
	delta_ns = ((t2 - t1) * 100) - ((r2 - r1) * 1000000000 / tsc_freq);
	if (delta_ns < 0)
		delta_ns = -delta_ns;

	/* 1% tolerance */
	TEST_ASSERT(delta_ns * 100 < (t2 - t1) * 100,
		    "Elapsed time does not match (MSR=%ld, TSC=%ld)",
		    (t2 - t1) * 100, (r2 - r1) * 1000000000 / tsc_freq);
}

int main(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct ucall uc;
	vm_vaddr_t tsc_page_gva;
	int stage;

	vm = vm_create_with_one_vcpu(&vcpu, guest_main);
	run = vcpu->run;

	vcpu_set_hv_cpuid(vcpu);

	tsc_page_gva = vm_vaddr_alloc_page(vm);
	memset(addr_gva2hva(vm, tsc_page_gva), 0x0, getpagesize());
	TEST_ASSERT((addr_gva2gpa(vm, tsc_page_gva) & (getpagesize() - 1)) == 0,
		"TSC page has to be page aligned\n");
	vcpu_args_set(vcpu, 2, tsc_page_gva, addr_gva2gpa(vm, tsc_page_gva));

	host_check_tsc_msr_rdtsc(vcpu);

	for (stage = 1;; stage++) {
		vcpu_run(vcpu);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Stage %d: unexpected exit reason: %u (%s),\n",
			    stage, run->exit_reason,
			    exit_reason_str(run->exit_reason));

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		case UCALL_SYNC:
			break;
		case UCALL_DONE:
			/* Keep in sync with guest_main() */
			TEST_ASSERT(stage == 11, "Testing ended prematurely, stage %d\n",
				    stage);
			goto out;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}

		TEST_ASSERT(!strcmp((const char *)uc.args[0], "hello") &&
			    uc.args[1] == stage,
			    "Stage %d: Unexpected register values vmexit, got %lx",
			    stage, (ulong)uc.args[1]);

		/* Reset kvmclock triggering TSC page update */
		if (stage == 7 || stage == 8 || stage == 10) {
			struct kvm_clock_data clock = {0};

			vm_ioctl(vm, KVM_SET_CLOCK, &clock);
		}
	}

out:
	kvm_vm_free(vm);
}
