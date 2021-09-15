// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021, Red Hat, Inc.
 *
 * Tests for Hyper-V features enablement
 */
#include <asm/kvm_para.h>
#include <linux/kvm_para.h>
#include <stdint.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "hyperv.h"

#define VCPU_ID 0
#define LINUX_OS_ID ((u64)0x8100 << 48)

extern unsigned char rdmsr_start;
extern unsigned char rdmsr_end;

static u64 do_rdmsr(u32 idx)
{
	u32 lo, hi;

	asm volatile("rdmsr_start: rdmsr;"
		     "rdmsr_end:"
		     : "=a"(lo), "=c"(hi)
		     : "c"(idx));

	return (((u64) hi) << 32) | lo;
}

extern unsigned char wrmsr_start;
extern unsigned char wrmsr_end;

static void do_wrmsr(u32 idx, u64 val)
{
	u32 lo, hi;

	lo = val;
	hi = val >> 32;

	asm volatile("wrmsr_start: wrmsr;"
		     "wrmsr_end:"
		     : : "a"(lo), "c"(idx), "d"(hi));
}

static int nr_gp;
static int nr_ud;

static inline u64 hypercall(u64 control, vm_vaddr_t input_address,
			    vm_vaddr_t output_address)
{
	u64 hv_status;

	asm volatile("mov %3, %%r8\n"
		     "vmcall"
		     : "=a" (hv_status),
		       "+c" (control), "+d" (input_address)
		     :  "r" (output_address)
		     : "cc", "memory", "r8", "r9", "r10", "r11");

	return hv_status;
}

static void guest_gp_handler(struct ex_regs *regs)
{
	unsigned char *rip = (unsigned char *)regs->rip;
	bool r, w;

	r = rip == &rdmsr_start;
	w = rip == &wrmsr_start;
	GUEST_ASSERT(r || w);

	nr_gp++;

	if (r)
		regs->rip = (uint64_t)&rdmsr_end;
	else
		regs->rip = (uint64_t)&wrmsr_end;
}

static void guest_ud_handler(struct ex_regs *regs)
{
	nr_ud++;
	regs->rip += 3;
}

struct msr_data {
	uint32_t idx;
	bool available;
	bool write;
	u64 write_val;
};

struct hcall_data {
	uint64_t control;
	uint64_t expect;
	bool ud_expected;
};

static void guest_msr(struct msr_data *msr)
{
	int i = 0;

	while (msr->idx) {
		WRITE_ONCE(nr_gp, 0);
		if (!msr->write)
			do_rdmsr(msr->idx);
		else
			do_wrmsr(msr->idx, msr->write_val);

		if (msr->available)
			GUEST_ASSERT(READ_ONCE(nr_gp) == 0);
		else
			GUEST_ASSERT(READ_ONCE(nr_gp) == 1);

		GUEST_SYNC(i++);
	}

	GUEST_DONE();
}

static void guest_hcall(vm_vaddr_t pgs_gpa, struct hcall_data *hcall)
{
	int i = 0;
	u64 res, input, output;

	wrmsr(HV_X64_MSR_GUEST_OS_ID, LINUX_OS_ID);
	wrmsr(HV_X64_MSR_HYPERCALL, pgs_gpa);

	while (hcall->control) {
		nr_ud = 0;
		if (!(hcall->control & HV_HYPERCALL_FAST_BIT)) {
			input = pgs_gpa;
			output = pgs_gpa + 4096;
		} else {
			input = output = 0;
		}

		res = hypercall(hcall->control, input, output);
		if (hcall->ud_expected)
			GUEST_ASSERT(nr_ud == 1);
		else
			GUEST_ASSERT(res == hcall->expect);

		GUEST_SYNC(i++);
	}

	GUEST_DONE();
}

static void hv_set_cpuid(struct kvm_vm *vm, struct kvm_cpuid2 *cpuid,
			 struct kvm_cpuid_entry2 *feat,
			 struct kvm_cpuid_entry2 *recomm,
			 struct kvm_cpuid_entry2 *dbg)
{
	TEST_ASSERT(set_cpuid(cpuid, feat),
		    "failed to set KVM_CPUID_FEATURES leaf");
	TEST_ASSERT(set_cpuid(cpuid, recomm),
		    "failed to set HYPERV_CPUID_ENLIGHTMENT_INFO leaf");
	TEST_ASSERT(set_cpuid(cpuid, dbg),
		    "failed to set HYPERV_CPUID_SYNDBG_PLATFORM_CAPABILITIES leaf");
	vcpu_set_cpuid(vm, VCPU_ID, cpuid);
}

static void guest_test_msrs_access(struct kvm_vm *vm, struct msr_data *msr,
				   struct kvm_cpuid2 *best)
{
	struct kvm_run *run;
	struct ucall uc;
	int stage = 0, r;
	struct kvm_cpuid_entry2 feat = {
		.function = HYPERV_CPUID_FEATURES
	};
	struct kvm_cpuid_entry2 recomm = {
		.function = HYPERV_CPUID_ENLIGHTMENT_INFO
	};
	struct kvm_cpuid_entry2 dbg = {
		.function = HYPERV_CPUID_SYNDBG_PLATFORM_CAPABILITIES
	};
	struct kvm_enable_cap cap = {0};

	run = vcpu_state(vm, VCPU_ID);

	while (true) {
		switch (stage) {
		case 0:
			/*
			 * Only available when Hyper-V identification is set
			 */
			msr->idx = HV_X64_MSR_GUEST_OS_ID;
			msr->write = 0;
			msr->available = 0;
			break;
		case 1:
			msr->idx = HV_X64_MSR_HYPERCALL;
			msr->write = 0;
			msr->available = 0;
			break;
		case 2:
			feat.eax |= HV_MSR_HYPERCALL_AVAILABLE;
			/*
			 * HV_X64_MSR_GUEST_OS_ID has to be written first to make
			 * HV_X64_MSR_HYPERCALL available.
			 */
			msr->idx = HV_X64_MSR_GUEST_OS_ID;
			msr->write = 1;
			msr->write_val = LINUX_OS_ID;
			msr->available = 1;
			break;
		case 3:
			msr->idx = HV_X64_MSR_GUEST_OS_ID;
			msr->write = 0;
			msr->available = 1;
			break;
		case 4:
			msr->idx = HV_X64_MSR_HYPERCALL;
			msr->write = 0;
			msr->available = 1;
			break;

		case 5:
			msr->idx = HV_X64_MSR_VP_RUNTIME;
			msr->write = 0;
			msr->available = 0;
			break;
		case 6:
			feat.eax |= HV_MSR_VP_RUNTIME_AVAILABLE;
			msr->write = 0;
			msr->available = 1;
			break;
		case 7:
			/* Read only */
			msr->write = 1;
			msr->write_val = 1;
			msr->available = 0;
			break;

		case 8:
			msr->idx = HV_X64_MSR_TIME_REF_COUNT;
			msr->write = 0;
			msr->available = 0;
			break;
		case 9:
			feat.eax |= HV_MSR_TIME_REF_COUNT_AVAILABLE;
			msr->write = 0;
			msr->available = 1;
			break;
		case 10:
			/* Read only */
			msr->write = 1;
			msr->write_val = 1;
			msr->available = 0;
			break;

		case 11:
			msr->idx = HV_X64_MSR_VP_INDEX;
			msr->write = 0;
			msr->available = 0;
			break;
		case 12:
			feat.eax |= HV_MSR_VP_INDEX_AVAILABLE;
			msr->write = 0;
			msr->available = 1;
			break;
		case 13:
			/* Read only */
			msr->write = 1;
			msr->write_val = 1;
			msr->available = 0;
			break;

		case 14:
			msr->idx = HV_X64_MSR_RESET;
			msr->write = 0;
			msr->available = 0;
			break;
		case 15:
			feat.eax |= HV_MSR_RESET_AVAILABLE;
			msr->write = 0;
			msr->available = 1;
			break;
		case 16:
			msr->write = 1;
			msr->write_val = 0;
			msr->available = 1;
			break;

		case 17:
			msr->idx = HV_X64_MSR_REFERENCE_TSC;
			msr->write = 0;
			msr->available = 0;
			break;
		case 18:
			feat.eax |= HV_MSR_REFERENCE_TSC_AVAILABLE;
			msr->write = 0;
			msr->available = 1;
			break;
		case 19:
			msr->write = 1;
			msr->write_val = 0;
			msr->available = 1;
			break;

		case 20:
			msr->idx = HV_X64_MSR_EOM;
			msr->write = 0;
			msr->available = 0;
			break;
		case 21:
			/*
			 * Remains unavailable even with KVM_CAP_HYPERV_SYNIC2
			 * capability enabled and guest visible CPUID bit unset.
			 */
			cap.cap = KVM_CAP_HYPERV_SYNIC2;
			vcpu_enable_cap(vm, VCPU_ID, &cap);
			break;
		case 22:
			feat.eax |= HV_MSR_SYNIC_AVAILABLE;
			msr->write = 0;
			msr->available = 1;
			break;
		case 23:
			msr->write = 1;
			msr->write_val = 0;
			msr->available = 1;
			break;

		case 24:
			msr->idx = HV_X64_MSR_STIMER0_CONFIG;
			msr->write = 0;
			msr->available = 0;
			break;
		case 25:
			feat.eax |= HV_MSR_SYNTIMER_AVAILABLE;
			msr->write = 0;
			msr->available = 1;
			break;
		case 26:
			msr->write = 1;
			msr->write_val = 0;
			msr->available = 1;
			break;
		case 27:
			/* Direct mode test */
			msr->write = 1;
			msr->write_val = 1 << 12;
			msr->available = 0;
			break;
		case 28:
			feat.edx |= HV_STIMER_DIRECT_MODE_AVAILABLE;
			msr->available = 1;
			break;

		case 29:
			msr->idx = HV_X64_MSR_EOI;
			msr->write = 0;
			msr->available = 0;
			break;
		case 30:
			feat.eax |= HV_MSR_APIC_ACCESS_AVAILABLE;
			msr->write = 1;
			msr->write_val = 1;
			msr->available = 1;
			break;

		case 31:
			msr->idx = HV_X64_MSR_TSC_FREQUENCY;
			msr->write = 0;
			msr->available = 0;
			break;
		case 32:
			feat.eax |= HV_ACCESS_FREQUENCY_MSRS;
			msr->write = 0;
			msr->available = 1;
			break;
		case 33:
			/* Read only */
			msr->write = 1;
			msr->write_val = 1;
			msr->available = 0;
			break;

		case 34:
			msr->idx = HV_X64_MSR_REENLIGHTENMENT_CONTROL;
			msr->write = 0;
			msr->available = 0;
			break;
		case 35:
			feat.eax |= HV_ACCESS_REENLIGHTENMENT;
			msr->write = 0;
			msr->available = 1;
			break;
		case 36:
			msr->write = 1;
			msr->write_val = 1;
			msr->available = 1;
			break;
		case 37:
			/* Can only write '0' */
			msr->idx = HV_X64_MSR_TSC_EMULATION_STATUS;
			msr->write = 1;
			msr->write_val = 1;
			msr->available = 0;
			break;

		case 38:
			msr->idx = HV_X64_MSR_CRASH_P0;
			msr->write = 0;
			msr->available = 0;
			break;
		case 39:
			feat.edx |= HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE;
			msr->write = 0;
			msr->available = 1;
			break;
		case 40:
			msr->write = 1;
			msr->write_val = 1;
			msr->available = 1;
			break;

		case 41:
			msr->idx = HV_X64_MSR_SYNDBG_STATUS;
			msr->write = 0;
			msr->available = 0;
			break;
		case 42:
			feat.edx |= HV_FEATURE_DEBUG_MSRS_AVAILABLE;
			dbg.eax |= HV_X64_SYNDBG_CAP_ALLOW_KERNEL_DEBUGGING;
			msr->write = 0;
			msr->available = 1;
			break;
		case 43:
			msr->write = 1;
			msr->write_val = 0;
			msr->available = 1;
			break;

		case 44:
			/* END */
			msr->idx = 0;
			break;
		}

		hv_set_cpuid(vm, best, &feat, &recomm, &dbg);

		if (msr->idx)
			pr_debug("Stage %d: testing msr: 0x%x for %s\n", stage,
				 msr->idx, msr->write ? "write" : "read");
		else
			pr_debug("Stage %d: finish\n", stage);

		r = _vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(!r, "vcpu_run failed: %d\n", r);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "unexpected exit reason: %u (%s)",
			    run->exit_reason, exit_reason_str(run->exit_reason));

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_SYNC:
			TEST_ASSERT(uc.args[1] == stage,
				    "Unexpected stage: %ld (%d expected)\n",
				    uc.args[1], stage);
			break;
		case UCALL_ABORT:
			TEST_FAIL("%s at %s:%ld", (const char *)uc.args[0],
				  __FILE__, uc.args[1]);
			return;
		case UCALL_DONE:
			return;
		}

		stage++;
	}
}

static void guest_test_hcalls_access(struct kvm_vm *vm, struct hcall_data *hcall,
				     void *input, void *output, struct kvm_cpuid2 *best)
{
	struct kvm_run *run;
	struct ucall uc;
	int stage = 0, r;
	struct kvm_cpuid_entry2 feat = {
		.function = HYPERV_CPUID_FEATURES,
		.eax = HV_MSR_HYPERCALL_AVAILABLE
	};
	struct kvm_cpuid_entry2 recomm = {
		.function = HYPERV_CPUID_ENLIGHTMENT_INFO
	};
	struct kvm_cpuid_entry2 dbg = {
		.function = HYPERV_CPUID_SYNDBG_PLATFORM_CAPABILITIES
	};

	run = vcpu_state(vm, VCPU_ID);

	while (true) {
		switch (stage) {
		case 0:
			hcall->control = 0xdeadbeef;
			hcall->expect = HV_STATUS_INVALID_HYPERCALL_CODE;
			break;

		case 1:
			hcall->control = HVCALL_POST_MESSAGE;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 2:
			feat.ebx |= HV_POST_MESSAGES;
			hcall->expect = HV_STATUS_INVALID_HYPERCALL_INPUT;
			break;

		case 3:
			hcall->control = HVCALL_SIGNAL_EVENT;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 4:
			feat.ebx |= HV_SIGNAL_EVENTS;
			hcall->expect = HV_STATUS_INVALID_HYPERCALL_INPUT;
			break;

		case 5:
			hcall->control = HVCALL_RESET_DEBUG_SESSION;
			hcall->expect = HV_STATUS_INVALID_HYPERCALL_CODE;
			break;
		case 6:
			dbg.eax |= HV_X64_SYNDBG_CAP_ALLOW_KERNEL_DEBUGGING;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 7:
			feat.ebx |= HV_DEBUGGING;
			hcall->expect = HV_STATUS_OPERATION_DENIED;
			break;

		case 8:
			hcall->control = HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 9:
			recomm.eax |= HV_X64_REMOTE_TLB_FLUSH_RECOMMENDED;
			hcall->expect = HV_STATUS_SUCCESS;
			break;
		case 10:
			hcall->control = HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE_EX;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 11:
			recomm.eax |= HV_X64_EX_PROCESSOR_MASKS_RECOMMENDED;
			hcall->expect = HV_STATUS_SUCCESS;
			break;

		case 12:
			hcall->control = HVCALL_SEND_IPI;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 13:
			recomm.eax |= HV_X64_CLUSTER_IPI_RECOMMENDED;
			hcall->expect = HV_STATUS_INVALID_HYPERCALL_INPUT;
			break;
		case 14:
			/* Nothing in 'sparse banks' -> success */
			hcall->control = HVCALL_SEND_IPI_EX;
			hcall->expect = HV_STATUS_SUCCESS;
			break;

		case 15:
			hcall->control = HVCALL_NOTIFY_LONG_SPIN_WAIT;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 16:
			recomm.ebx = 0xfff;
			hcall->expect = HV_STATUS_SUCCESS;
			break;
		case 17:
			/* XMM fast hypercall */
			hcall->control = HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE | HV_HYPERCALL_FAST_BIT;
			hcall->ud_expected = true;
			break;
		case 18:
			feat.edx |= HV_X64_HYPERCALL_XMM_INPUT_AVAILABLE;
			hcall->ud_expected = false;
			hcall->expect = HV_STATUS_SUCCESS;
			break;

		case 19:
			/* END */
			hcall->control = 0;
			break;
		}

		hv_set_cpuid(vm, best, &feat, &recomm, &dbg);

		if (hcall->control)
			pr_debug("Stage %d: testing hcall: 0x%lx\n", stage,
				 hcall->control);
		else
			pr_debug("Stage %d: finish\n", stage);

		r = _vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(!r, "vcpu_run failed: %d\n", r);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "unexpected exit reason: %u (%s)",
			    run->exit_reason, exit_reason_str(run->exit_reason));

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_SYNC:
			TEST_ASSERT(uc.args[1] == stage,
				    "Unexpected stage: %ld (%d expected)\n",
				    uc.args[1], stage);
			break;
		case UCALL_ABORT:
			TEST_FAIL("%s at %s:%ld", (const char *)uc.args[0],
				  __FILE__, uc.args[1]);
			return;
		case UCALL_DONE:
			return;
		}

		stage++;
	}
}

int main(void)
{
	struct kvm_cpuid2 *best;
	struct kvm_vm *vm;
	vm_vaddr_t msr_gva, hcall_page, hcall_params;
	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_HYPERV_ENFORCE_CPUID,
		.args = {1}
	};

	/* Test MSRs */
	vm = vm_create_default(VCPU_ID, 0, guest_msr);

	msr_gva = vm_vaddr_alloc_page(vm);
	memset(addr_gva2hva(vm, msr_gva), 0x0, getpagesize());
	vcpu_args_set(vm, VCPU_ID, 1, msr_gva);
	vcpu_enable_cap(vm, VCPU_ID, &cap);

	vcpu_set_hv_cpuid(vm, VCPU_ID);

	best = kvm_get_supported_hv_cpuid();

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);
	vm_install_exception_handler(vm, GP_VECTOR, guest_gp_handler);

	pr_info("Testing access to Hyper-V specific MSRs\n");
	guest_test_msrs_access(vm, addr_gva2hva(vm, msr_gva),
			       best);
	kvm_vm_free(vm);

	/* Test hypercalls */
	vm = vm_create_default(VCPU_ID, 0, guest_hcall);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);
	vm_install_exception_handler(vm, UD_VECTOR, guest_ud_handler);

	/* Hypercall input/output */
	hcall_page = vm_vaddr_alloc_pages(vm, 2);
	memset(addr_gva2hva(vm, hcall_page), 0x0, 2 * getpagesize());

	hcall_params = vm_vaddr_alloc_page(vm);
	memset(addr_gva2hva(vm, hcall_params), 0x0, getpagesize());

	vcpu_args_set(vm, VCPU_ID, 2, addr_gva2gpa(vm, hcall_page), hcall_params);
	vcpu_enable_cap(vm, VCPU_ID, &cap);

	vcpu_set_hv_cpuid(vm, VCPU_ID);

	best = kvm_get_supported_hv_cpuid();

	pr_info("Testing access to Hyper-V hypercalls\n");
	guest_test_hcalls_access(vm, addr_gva2hva(vm, hcall_params),
				 addr_gva2hva(vm, hcall_page),
				 addr_gva2hva(vm, hcall_page) + getpagesize(),
				 best);

	kvm_vm_free(vm);
}
