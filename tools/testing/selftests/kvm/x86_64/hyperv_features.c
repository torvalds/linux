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
	uint64_t ignored;
	uint8_t vector;

	GUEST_ASSERT(msr->idx);

	if (!msr->write)
		vector = rdmsr_safe(msr->idx, &ignored);
	else
		vector = wrmsr_safe(msr->idx, msr->write_val);

	if (msr->available)
		GUEST_ASSERT_2(!vector, msr->idx, vector);
	else
		GUEST_ASSERT_2(vector == GP_VECTOR, msr->idx, vector);
	GUEST_DONE();
}

static void guest_hcall(vm_vaddr_t pgs_gpa, struct hcall_data *hcall)
{
	u64 res, input, output;
	uint8_t vector;

	GUEST_ASSERT(hcall->control);

	wrmsr(HV_X64_MSR_GUEST_OS_ID, HYPERV_LINUX_OS_ID);
	wrmsr(HV_X64_MSR_HYPERCALL, pgs_gpa);

	if (!(hcall->control & HV_HYPERCALL_FAST_BIT)) {
		input = pgs_gpa;
		output = pgs_gpa + 4096;
	} else {
		input = output = 0;
	}

	vector = __hyperv_hypercall(hcall->control, input, output, &res);
	if (hcall->ud_expected) {
		GUEST_ASSERT_2(vector == UD_VECTOR, hcall->control, vector);
	} else {
		GUEST_ASSERT_2(!vector, hcall->control, vector);
		GUEST_ASSERT_2(res == hcall->expect, hcall->expect, res);
	}

	GUEST_DONE();
}

static void vcpu_reset_hv_cpuid(struct kvm_vcpu *vcpu)
{
	/*
	 * Enable all supported Hyper-V features, then clear the leafs holding
	 * the features that will be tested one by one.
	 */
	vcpu_set_hv_cpuid(vcpu);

	vcpu_clear_cpuid_entry(vcpu, HYPERV_CPUID_FEATURES);
	vcpu_clear_cpuid_entry(vcpu, HYPERV_CPUID_ENLIGHTMENT_INFO);
	vcpu_clear_cpuid_entry(vcpu, HYPERV_CPUID_SYNDBG_PLATFORM_CAPABILITIES);
}

static void guest_test_msrs_access(void)
{
	struct kvm_cpuid2 *prev_cpuid = NULL;
	struct kvm_cpuid_entry2 *feat, *dbg;
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct ucall uc;
	int stage = 0;
	vm_vaddr_t msr_gva;
	struct msr_data *msr;

	while (true) {
		vm = vm_create_with_one_vcpu(&vcpu, guest_msr);

		msr_gva = vm_vaddr_alloc_page(vm);
		memset(addr_gva2hva(vm, msr_gva), 0x0, getpagesize());
		msr = addr_gva2hva(vm, msr_gva);

		vcpu_args_set(vcpu, 1, msr_gva);
		vcpu_enable_cap(vcpu, KVM_CAP_HYPERV_ENFORCE_CPUID, 1);

		if (!prev_cpuid) {
			vcpu_reset_hv_cpuid(vcpu);

			prev_cpuid = allocate_kvm_cpuid2(vcpu->cpuid->nent);
		} else {
			vcpu_init_cpuid(vcpu, prev_cpuid);
		}

		feat = vcpu_get_cpuid_entry(vcpu, HYPERV_CPUID_FEATURES);
		dbg = vcpu_get_cpuid_entry(vcpu, HYPERV_CPUID_SYNDBG_PLATFORM_CAPABILITIES);

		vm_init_descriptor_tables(vm);
		vcpu_init_descriptor_tables(vcpu);

		run = vcpu->run;

		/* TODO: Make this entire test easier to maintain. */
		if (stage >= 21)
			vcpu_enable_cap(vcpu, KVM_CAP_HYPERV_SYNIC2, 0);

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
			feat->eax |= HV_MSR_HYPERCALL_AVAILABLE;
			/*
			 * HV_X64_MSR_GUEST_OS_ID has to be written first to make
			 * HV_X64_MSR_HYPERCALL available.
			 */
			msr->idx = HV_X64_MSR_GUEST_OS_ID;
			msr->write = 1;
			msr->write_val = HYPERV_LINUX_OS_ID;
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
			feat->eax |= HV_MSR_VP_RUNTIME_AVAILABLE;
			msr->idx = HV_X64_MSR_VP_RUNTIME;
			msr->write = 0;
			msr->available = 1;
			break;
		case 7:
			/* Read only */
			msr->idx = HV_X64_MSR_VP_RUNTIME;
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
			feat->eax |= HV_MSR_TIME_REF_COUNT_AVAILABLE;
			msr->idx = HV_X64_MSR_TIME_REF_COUNT;
			msr->write = 0;
			msr->available = 1;
			break;
		case 10:
			/* Read only */
			msr->idx = HV_X64_MSR_TIME_REF_COUNT;
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
			feat->eax |= HV_MSR_VP_INDEX_AVAILABLE;
			msr->idx = HV_X64_MSR_VP_INDEX;
			msr->write = 0;
			msr->available = 1;
			break;
		case 13:
			/* Read only */
			msr->idx = HV_X64_MSR_VP_INDEX;
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
			feat->eax |= HV_MSR_RESET_AVAILABLE;
			msr->idx = HV_X64_MSR_RESET;
			msr->write = 0;
			msr->available = 1;
			break;
		case 16:
			msr->idx = HV_X64_MSR_RESET;
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
			feat->eax |= HV_MSR_REFERENCE_TSC_AVAILABLE;
			msr->idx = HV_X64_MSR_REFERENCE_TSC;
			msr->write = 0;
			msr->available = 1;
			break;
		case 19:
			msr->idx = HV_X64_MSR_REFERENCE_TSC;
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
			msr->idx = HV_X64_MSR_EOM;
			msr->write = 0;
			msr->available = 0;
			break;
		case 22:
			feat->eax |= HV_MSR_SYNIC_AVAILABLE;
			msr->idx = HV_X64_MSR_EOM;
			msr->write = 0;
			msr->available = 1;
			break;
		case 23:
			msr->idx = HV_X64_MSR_EOM;
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
			feat->eax |= HV_MSR_SYNTIMER_AVAILABLE;
			msr->idx = HV_X64_MSR_STIMER0_CONFIG;
			msr->write = 0;
			msr->available = 1;
			break;
		case 26:
			msr->idx = HV_X64_MSR_STIMER0_CONFIG;
			msr->write = 1;
			msr->write_val = 0;
			msr->available = 1;
			break;
		case 27:
			/* Direct mode test */
			msr->idx = HV_X64_MSR_STIMER0_CONFIG;
			msr->write = 1;
			msr->write_val = 1 << 12;
			msr->available = 0;
			break;
		case 28:
			feat->edx |= HV_STIMER_DIRECT_MODE_AVAILABLE;
			msr->idx = HV_X64_MSR_STIMER0_CONFIG;
			msr->write = 1;
			msr->write_val = 1 << 12;
			msr->available = 1;
			break;

		case 29:
			msr->idx = HV_X64_MSR_EOI;
			msr->write = 0;
			msr->available = 0;
			break;
		case 30:
			feat->eax |= HV_MSR_APIC_ACCESS_AVAILABLE;
			msr->idx = HV_X64_MSR_EOI;
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
			feat->eax |= HV_ACCESS_FREQUENCY_MSRS;
			msr->idx = HV_X64_MSR_TSC_FREQUENCY;
			msr->write = 0;
			msr->available = 1;
			break;
		case 33:
			/* Read only */
			msr->idx = HV_X64_MSR_TSC_FREQUENCY;
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
			feat->eax |= HV_ACCESS_REENLIGHTENMENT;
			msr->idx = HV_X64_MSR_REENLIGHTENMENT_CONTROL;
			msr->write = 0;
			msr->available = 1;
			break;
		case 36:
			msr->idx = HV_X64_MSR_REENLIGHTENMENT_CONTROL;
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
			feat->edx |= HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE;
			msr->idx = HV_X64_MSR_CRASH_P0;
			msr->write = 0;
			msr->available = 1;
			break;
		case 40:
			msr->idx = HV_X64_MSR_CRASH_P0;
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
			feat->edx |= HV_FEATURE_DEBUG_MSRS_AVAILABLE;
			dbg->eax |= HV_X64_SYNDBG_CAP_ALLOW_KERNEL_DEBUGGING;
			msr->idx = HV_X64_MSR_SYNDBG_STATUS;
			msr->write = 0;
			msr->available = 1;
			break;
		case 43:
			msr->idx = HV_X64_MSR_SYNDBG_STATUS;
			msr->write = 1;
			msr->write_val = 0;
			msr->available = 1;
			break;

		case 44:
			kvm_vm_free(vm);
			return;
		}

		vcpu_set_cpuid(vcpu);

		memcpy(prev_cpuid, vcpu->cpuid, kvm_cpuid2_size(vcpu->cpuid->nent));

		pr_debug("Stage %d: testing msr: 0x%x for %s\n", stage,
			 msr->idx, msr->write ? "write" : "read");

		vcpu_run(vcpu);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "unexpected exit reason: %u (%s)",
			    run->exit_reason, exit_reason_str(run->exit_reason));

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT_2(uc, "MSR = %lx, vector = %lx");
			return;
		case UCALL_DONE:
			break;
		default:
			TEST_FAIL("Unhandled ucall: %ld", uc.cmd);
			return;
		}

		stage++;
		kvm_vm_free(vm);
	}
}

static void guest_test_hcalls_access(void)
{
	struct kvm_cpuid_entry2 *feat, *recomm, *dbg;
	struct kvm_cpuid2 *prev_cpuid = NULL;
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct ucall uc;
	int stage = 0;
	vm_vaddr_t hcall_page, hcall_params;
	struct hcall_data *hcall;

	while (true) {
		vm = vm_create_with_one_vcpu(&vcpu, guest_hcall);

		vm_init_descriptor_tables(vm);
		vcpu_init_descriptor_tables(vcpu);

		/* Hypercall input/output */
		hcall_page = vm_vaddr_alloc_pages(vm, 2);
		memset(addr_gva2hva(vm, hcall_page), 0x0, 2 * getpagesize());

		hcall_params = vm_vaddr_alloc_page(vm);
		memset(addr_gva2hva(vm, hcall_params), 0x0, getpagesize());
		hcall = addr_gva2hva(vm, hcall_params);

		vcpu_args_set(vcpu, 2, addr_gva2gpa(vm, hcall_page), hcall_params);
		vcpu_enable_cap(vcpu, KVM_CAP_HYPERV_ENFORCE_CPUID, 1);

		if (!prev_cpuid) {
			vcpu_reset_hv_cpuid(vcpu);

			prev_cpuid = allocate_kvm_cpuid2(vcpu->cpuid->nent);
		} else {
			vcpu_init_cpuid(vcpu, prev_cpuid);
		}

		feat = vcpu_get_cpuid_entry(vcpu, HYPERV_CPUID_FEATURES);
		recomm = vcpu_get_cpuid_entry(vcpu, HYPERV_CPUID_ENLIGHTMENT_INFO);
		dbg = vcpu_get_cpuid_entry(vcpu, HYPERV_CPUID_SYNDBG_PLATFORM_CAPABILITIES);

		run = vcpu->run;

		switch (stage) {
		case 0:
			feat->eax |= HV_MSR_HYPERCALL_AVAILABLE;
			hcall->control = 0xbeef;
			hcall->expect = HV_STATUS_INVALID_HYPERCALL_CODE;
			break;

		case 1:
			hcall->control = HVCALL_POST_MESSAGE;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 2:
			feat->ebx |= HV_POST_MESSAGES;
			hcall->control = HVCALL_POST_MESSAGE;
			hcall->expect = HV_STATUS_INVALID_HYPERCALL_INPUT;
			break;

		case 3:
			hcall->control = HVCALL_SIGNAL_EVENT;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 4:
			feat->ebx |= HV_SIGNAL_EVENTS;
			hcall->control = HVCALL_SIGNAL_EVENT;
			hcall->expect = HV_STATUS_INVALID_HYPERCALL_INPUT;
			break;

		case 5:
			hcall->control = HVCALL_RESET_DEBUG_SESSION;
			hcall->expect = HV_STATUS_INVALID_HYPERCALL_CODE;
			break;
		case 6:
			dbg->eax |= HV_X64_SYNDBG_CAP_ALLOW_KERNEL_DEBUGGING;
			hcall->control = HVCALL_RESET_DEBUG_SESSION;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 7:
			feat->ebx |= HV_DEBUGGING;
			hcall->control = HVCALL_RESET_DEBUG_SESSION;
			hcall->expect = HV_STATUS_OPERATION_DENIED;
			break;

		case 8:
			hcall->control = HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 9:
			recomm->eax |= HV_X64_REMOTE_TLB_FLUSH_RECOMMENDED;
			hcall->control = HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE;
			hcall->expect = HV_STATUS_SUCCESS;
			break;
		case 10:
			hcall->control = HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE_EX;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 11:
			recomm->eax |= HV_X64_EX_PROCESSOR_MASKS_RECOMMENDED;
			hcall->control = HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE_EX;
			hcall->expect = HV_STATUS_SUCCESS;
			break;

		case 12:
			hcall->control = HVCALL_SEND_IPI;
			hcall->expect = HV_STATUS_ACCESS_DENIED;
			break;
		case 13:
			recomm->eax |= HV_X64_CLUSTER_IPI_RECOMMENDED;
			hcall->control = HVCALL_SEND_IPI;
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
			recomm->ebx = 0xfff;
			hcall->control = HVCALL_NOTIFY_LONG_SPIN_WAIT;
			hcall->expect = HV_STATUS_SUCCESS;
			break;
		case 17:
			/* XMM fast hypercall */
			hcall->control = HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE | HV_HYPERCALL_FAST_BIT;
			hcall->ud_expected = true;
			break;
		case 18:
			feat->edx |= HV_X64_HYPERCALL_XMM_INPUT_AVAILABLE;
			hcall->control = HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE | HV_HYPERCALL_FAST_BIT;
			hcall->ud_expected = false;
			hcall->expect = HV_STATUS_SUCCESS;
			break;
		case 19:
			kvm_vm_free(vm);
			return;
		}

		vcpu_set_cpuid(vcpu);

		memcpy(prev_cpuid, vcpu->cpuid, kvm_cpuid2_size(vcpu->cpuid->nent));

		pr_debug("Stage %d: testing hcall: 0x%lx\n", stage, hcall->control);

		vcpu_run(vcpu);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "unexpected exit reason: %u (%s)",
			    run->exit_reason, exit_reason_str(run->exit_reason));

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT_2(uc, "arg1 = %lx, arg2 = %lx");
			return;
		case UCALL_DONE:
			break;
		default:
			TEST_FAIL("Unhandled ucall: %ld", uc.cmd);
			return;
		}

		stage++;
		kvm_vm_free(vm);
	}
}

int main(void)
{
	pr_info("Testing access to Hyper-V specific MSRs\n");
	guest_test_msrs_access();

	pr_info("Testing access to Hyper-V hypercalls\n");
	guest_test_hcalls_access();
}
