// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test for KVM_X86_DISABLE_EXITS_APERFMPERF
 *
 * Copyright (C) 2025, Google LLC.
 *
 * Test the ability to disable VM-exits for rdmsr of IA32_APERF and
 * IA32_MPERF. When these VM-exits are disabled, reads of these MSRs
 * return the host's values.
 *
 * Note: Requires read access to /dev/cpu/<lpu>/msr to read host MSRs.
 */

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <asm/msr-index.h>

#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "test_util.h"
#include "vmx.h"

#define NUM_ITERATIONS 10000

static int open_dev_msr(int cpu)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "/dev/cpu/%d/msr", cpu);
	return open_path_or_exit(path, O_RDONLY);
}

static uint64_t read_dev_msr(int msr_fd, uint32_t msr)
{
	uint64_t data;
	ssize_t rc;

	rc = pread(msr_fd, &data, sizeof(data), msr);
	TEST_ASSERT(rc == sizeof(data), "Read of MSR 0x%x failed", msr);

	return data;
}

static void guest_read_aperf_mperf(void)
{
	int i;

	for (i = 0; i < NUM_ITERATIONS; i++)
		GUEST_SYNC2(rdmsr(MSR_IA32_APERF), rdmsr(MSR_IA32_MPERF));
}

#define L2_GUEST_STACK_SIZE	64

static void l2_guest_code(void)
{
	guest_read_aperf_mperf();
	GUEST_DONE();
}

static void l1_svm_code(struct svm_test_data *svm)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	struct vmcb *vmcb = svm->vmcb;

	generic_svm_setup(svm, l2_guest_code, &l2_guest_stack[L2_GUEST_STACK_SIZE]);
	run_guest(vmcb, svm->vmcb_gpa);
}

static void l1_vmx_code(struct vmx_pages *vmx)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	GUEST_ASSERT_EQ(prepare_for_vmx_operation(vmx), true);
	GUEST_ASSERT_EQ(load_vmcs(vmx), true);

	prepare_vmcs(vmx, NULL, &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	/*
	 * Enable MSR bitmaps (the bitmap itself is allocated, zeroed, and set
	 * in the VMCS by prepare_vmcs()), as MSR exiting mandatory on Intel.
	 */
	vmwrite(CPU_BASED_VM_EXEC_CONTROL,
		vmreadz(CPU_BASED_VM_EXEC_CONTROL) | CPU_BASED_USE_MSR_BITMAPS);

	GUEST_ASSERT(!vmwrite(GUEST_RIP, (u64)l2_guest_code));
	GUEST_ASSERT(!vmlaunch());
}

static void guest_code(void *nested_test_data)
{
	guest_read_aperf_mperf();

	if (this_cpu_has(X86_FEATURE_SVM))
		l1_svm_code(nested_test_data);
	else if (this_cpu_has(X86_FEATURE_VMX))
		l1_vmx_code(nested_test_data);
	else
		GUEST_DONE();

	TEST_FAIL("L2 should have signaled 'done'");
}

static void guest_no_aperfmperf(void)
{
	uint64_t msr_val;
	uint8_t vector;

	vector = rdmsr_safe(MSR_IA32_APERF, &msr_val);
	GUEST_ASSERT(vector == GP_VECTOR);

	vector = rdmsr_safe(MSR_IA32_APERF, &msr_val);
	GUEST_ASSERT(vector == GP_VECTOR);

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	const bool has_nested = kvm_cpu_has(X86_FEATURE_SVM) || kvm_cpu_has(X86_FEATURE_VMX);
	uint64_t host_aperf_before, host_mperf_before;
	vm_vaddr_t nested_test_data_gva;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int msr_fd, cpu, i;

	/* Sanity check that APERF/MPERF are unsupported by default. */
	vm = vm_create_with_one_vcpu(&vcpu, guest_no_aperfmperf);
	vcpu_run(vcpu);
	TEST_ASSERT_EQ(get_ucall(vcpu, NULL), UCALL_DONE);
	kvm_vm_free(vm);

	cpu = pin_self_to_any_cpu();

	msr_fd = open_dev_msr(cpu);

	/*
	 * This test requires a non-standard VM initialization, because
	 * KVM_ENABLE_CAP cannot be used on a VM file descriptor after
	 * a VCPU has been created.
	 */
	vm = vm_create(1);

	TEST_REQUIRE(vm_check_cap(vm, KVM_CAP_X86_DISABLE_EXITS) &
		     KVM_X86_DISABLE_EXITS_APERFMPERF);

	vm_enable_cap(vm, KVM_CAP_X86_DISABLE_EXITS,
		      KVM_X86_DISABLE_EXITS_APERFMPERF);

	vcpu = vm_vcpu_add(vm, 0, guest_code);

	if (!has_nested)
		nested_test_data_gva = NONCANONICAL;
	else if (kvm_cpu_has(X86_FEATURE_SVM))
		vcpu_alloc_svm(vm, &nested_test_data_gva);
	else
		vcpu_alloc_vmx(vm, &nested_test_data_gva);

	vcpu_args_set(vcpu, 1, nested_test_data_gva);

	host_aperf_before = read_dev_msr(msr_fd, MSR_IA32_APERF);
	host_mperf_before = read_dev_msr(msr_fd, MSR_IA32_MPERF);

	for (i = 0; i <= NUM_ITERATIONS * (1 + has_nested); i++) {
		uint64_t host_aperf_after, host_mperf_after;
		uint64_t guest_aperf, guest_mperf;
		struct ucall uc;

		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_DONE:
			goto done;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		case UCALL_SYNC:
			guest_aperf = uc.args[0];
			guest_mperf = uc.args[1];

			host_aperf_after = read_dev_msr(msr_fd, MSR_IA32_APERF);
			host_mperf_after = read_dev_msr(msr_fd, MSR_IA32_MPERF);

			TEST_ASSERT(host_aperf_before < guest_aperf,
				    "APERF: host_before (0x%" PRIx64 ") >= guest (0x%" PRIx64 ")",
				    host_aperf_before, guest_aperf);
			TEST_ASSERT(guest_aperf < host_aperf_after,
				    "APERF: guest (0x%" PRIx64 ") >= host_after (0x%" PRIx64 ")",
				    guest_aperf, host_aperf_after);
			TEST_ASSERT(host_mperf_before < guest_mperf,
				    "MPERF: host_before (0x%" PRIx64 ") >= guest (0x%" PRIx64 ")",
				    host_mperf_before, guest_mperf);
			TEST_ASSERT(guest_mperf < host_mperf_after,
				    "MPERF: guest (0x%" PRIx64 ") >= host_after (0x%" PRIx64 ")",
				    guest_mperf, host_mperf_after);

			host_aperf_before = host_aperf_after;
			host_mperf_before = host_mperf_after;

			break;
		}
	}
	TEST_FAIL("Didn't receive UCALL_DONE\n");
done:
	kvm_vm_free(vm);
	close(msr_fd);

	return 0;
}
