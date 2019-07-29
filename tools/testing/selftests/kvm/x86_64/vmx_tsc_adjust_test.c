/*
 * vmx_tsc_adjust_test
 *
 * Copyright (C) 2018, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 *
 * IA32_TSC_ADJUST test
 *
 * According to the SDM, "if an execution of WRMSR to the
 * IA32_TIME_STAMP_COUNTER MSR adds (or subtracts) value X from the TSC,
 * the logical processor also adds (or subtracts) value X from the
 * IA32_TSC_ADJUST MSR.
 *
 * Note that when L1 doesn't intercept writes to IA32_TSC, a
 * WRMSR(IA32_TSC) from L2 sets L1's TSC value, not L2's perceived TSC
 * value.
 *
 * This test verifies that this unusual case is handled correctly.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#include <string.h>
#include <sys/ioctl.h>

#include "kselftest.h"

#ifndef MSR_IA32_TSC_ADJUST
#define MSR_IA32_TSC_ADJUST 0x3b
#endif

#define PAGE_SIZE	4096
#define VCPU_ID		5

#define TSC_ADJUST_VALUE (1ll << 32)
#define TSC_OFFSET_VALUE -(1ll << 48)

enum {
	PORT_ABORT = 0x1000,
	PORT_REPORT,
	PORT_DONE,
};

enum {
	VMXON_PAGE = 0,
	VMCS_PAGE,
	MSR_BITMAP_PAGE,

	NUM_VMX_PAGES,
};

struct kvm_single_msr {
	struct kvm_msrs header;
	struct kvm_msr_entry entry;
} __attribute__((packed));

/* The virtual machine object. */
static struct kvm_vm *vm;

static void check_ia32_tsc_adjust(int64_t max)
{
	int64_t adjust;

	adjust = rdmsr(MSR_IA32_TSC_ADJUST);
	GUEST_SYNC(adjust);
	GUEST_ASSERT(adjust <= max);
}

static void l2_guest_code(void)
{
	uint64_t l1_tsc = rdtsc() - TSC_OFFSET_VALUE;

	wrmsr(MSR_IA32_TSC, l1_tsc - TSC_ADJUST_VALUE);
	check_ia32_tsc_adjust(-2 * TSC_ADJUST_VALUE);

	/* Exit to L1 */
	__asm__ __volatile__("vmcall");
}

static void l1_guest_code(struct vmx_pages *vmx_pages)
{
#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	uint32_t control;
	uintptr_t save_cr3;

	GUEST_ASSERT(rdtsc() < TSC_ADJUST_VALUE);
	wrmsr(MSR_IA32_TSC, rdtsc() - TSC_ADJUST_VALUE);
	check_ia32_tsc_adjust(-1 * TSC_ADJUST_VALUE);

	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));

	/* Prepare the VMCS for L2 execution. */
	prepare_vmcs(vmx_pages, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);
	control = vmreadz(CPU_BASED_VM_EXEC_CONTROL);
	control |= CPU_BASED_USE_MSR_BITMAPS | CPU_BASED_USE_TSC_OFFSETING;
	vmwrite(CPU_BASED_VM_EXEC_CONTROL, control);
	vmwrite(TSC_OFFSET, TSC_OFFSET_VALUE);

	/* Jump into L2.  First, test failure to load guest CR3.  */
	save_cr3 = vmreadz(GUEST_CR3);
	vmwrite(GUEST_CR3, -1ull);
	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) ==
		     (EXIT_REASON_FAILED_VMENTRY | EXIT_REASON_INVALID_STATE));
	check_ia32_tsc_adjust(-1 * TSC_ADJUST_VALUE);
	vmwrite(GUEST_CR3, save_cr3);

	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	check_ia32_tsc_adjust(-2 * TSC_ADJUST_VALUE);

	GUEST_DONE();
}

void report(int64_t val)
{
	printf("IA32_TSC_ADJUST is %ld (%lld * TSC_ADJUST_VALUE + %lld).\n",
	       val, val / TSC_ADJUST_VALUE, val % TSC_ADJUST_VALUE);
}

int main(int argc, char *argv[])
{
	struct vmx_pages *vmx_pages;
	vm_vaddr_t vmx_pages_gva;
	struct kvm_cpuid_entry2 *entry = kvm_get_supported_cpuid_entry(1);

	if (!(entry->ecx & CPUID_VMX)) {
		fprintf(stderr, "nested VMX not enabled, skipping test\n");
		exit(KSFT_SKIP);
	}

	vm = vm_create_default(VCPU_ID, 0, (void *) l1_guest_code);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	/* Allocate VMX pages and shared descriptors (vmx_pages). */
	vmx_pages = vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vm, VCPU_ID, 1, vmx_pages_gva);

	for (;;) {
		volatile struct kvm_run *run = vcpu_state(vm, VCPU_ID);
		struct ucall uc;

		vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Got exit_reason other than KVM_EXIT_IO: %u (%s)\n",
			    run->exit_reason,
			    exit_reason_str(run->exit_reason));

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_ABORT:
			TEST_ASSERT(false, "%s", (const char *)uc.args[0]);
			/* NOT REACHED */
		case UCALL_SYNC:
			report(uc.args[1]);
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_ASSERT(false, "Unknown ucall 0x%x.", uc.cmd);
		}
	}

	kvm_vm_free(vm);
done:
	return 0;
}
