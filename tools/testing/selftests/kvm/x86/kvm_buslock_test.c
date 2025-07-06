// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 */
#include <linux/atomic.h>

#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "vmx.h"
#include "test_util.h"

#define NR_BUS_LOCKS_PER_LEVEL 100
#define CACHE_LINE_SIZE		64

/*
 * To generate a bus lock, carve out a buffer that precisely occupies two cache
 * lines and perform an atomic access that splits the two lines.
 */
static u8 buffer[CACHE_LINE_SIZE * 2] __aligned(CACHE_LINE_SIZE);
static atomic_t *val = (void *)&buffer[CACHE_LINE_SIZE - (sizeof(*val) / 2)];

static void guest_generate_buslocks(void)
{
	for (int i = 0; i < NR_BUS_LOCKS_PER_LEVEL; i++)
		atomic_inc(val);
}

#define L2_GUEST_STACK_SIZE	64

static void l2_guest_code(void)
{
	guest_generate_buslocks();
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

	GUEST_ASSERT(!vmwrite(GUEST_RIP, (u64)l2_guest_code));
	GUEST_ASSERT(!vmlaunch());
}

static void guest_code(void *test_data)
{
	guest_generate_buslocks();

	if (this_cpu_has(X86_FEATURE_SVM))
		l1_svm_code(test_data);
	else if (this_cpu_has(X86_FEATURE_VMX))
		l1_vmx_code(test_data);
	else
		GUEST_DONE();

	TEST_FAIL("L2 should have signaled 'done'");
}

int main(int argc, char *argv[])
{
	const bool has_nested = kvm_cpu_has(X86_FEATURE_SVM) || kvm_cpu_has(X86_FEATURE_VMX);
	vm_vaddr_t nested_test_data_gva;
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;
	int i, bus_locks = 0;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_X86_BUS_LOCK_EXIT));

	vm = vm_create(1);
	vm_enable_cap(vm, KVM_CAP_X86_BUS_LOCK_EXIT, KVM_BUS_LOCK_DETECTION_EXIT);
	vcpu = vm_vcpu_add(vm, 0, guest_code);

	if (kvm_cpu_has(X86_FEATURE_SVM))
		vcpu_alloc_svm(vm, &nested_test_data_gva);
	else
		vcpu_alloc_vmx(vm, &nested_test_data_gva);

	vcpu_args_set(vcpu, 1, nested_test_data_gva);

	run = vcpu->run;

	for (i = 0; i <= NR_BUS_LOCKS_PER_LEVEL * (1 + has_nested); i++) {
		struct ucall uc;

		vcpu_run(vcpu);

		if (run->exit_reason == KVM_EXIT_IO) {
			switch (get_ucall(vcpu, &uc)) {
			case UCALL_ABORT:
				REPORT_GUEST_ASSERT(uc);
				goto done;
			case UCALL_SYNC:
				continue;
			case UCALL_DONE:
				goto done;
			default:
				TEST_FAIL("Unknown ucall 0x%lx.", uc.cmd);
			}
		}

		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_X86_BUS_LOCK);

		/*
		 * Verify the counter is actually getting incremented, e.g. that
		 * KVM isn't skipping the instruction.  On Intel, the exit is
		 * trap-like, i.e. the counter should already have been
		 * incremented.  On AMD, it's fault-like, i.e. the counter will
		 * be incremented when the guest re-executes the instruction.
		 */
		sync_global_from_guest(vm, *val);
		TEST_ASSERT_EQ(atomic_read(val), bus_locks + host_cpu_is_intel);

		bus_locks++;
	}
	TEST_FAIL("Didn't receive UCALL_DONE, took %u bus lock exits\n", bus_locks);
done:
	TEST_ASSERT_EQ(i, bus_locks);
	kvm_vm_free(vm);
	return 0;
}
