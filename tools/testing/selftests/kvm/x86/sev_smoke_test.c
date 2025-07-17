// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <math.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "linux/psp-sev.h"
#include "sev.h"


#define XFEATURE_MASK_X87_AVX (XFEATURE_MASK_FP | XFEATURE_MASK_SSE | XFEATURE_MASK_YMM)

static void guest_snp_code(void)
{
	uint64_t sev_msr = rdmsr(MSR_AMD64_SEV);

	GUEST_ASSERT(sev_msr & MSR_AMD64_SEV_ENABLED);
	GUEST_ASSERT(sev_msr & MSR_AMD64_SEV_ES_ENABLED);
	GUEST_ASSERT(sev_msr & MSR_AMD64_SEV_SNP_ENABLED);

	wrmsr(MSR_AMD64_SEV_ES_GHCB, GHCB_MSR_TERM_REQ);
	vmgexit();
}

static void guest_sev_es_code(void)
{
	/* TODO: Check CPUID after GHCB-based hypercall support is added. */
	GUEST_ASSERT(rdmsr(MSR_AMD64_SEV) & MSR_AMD64_SEV_ENABLED);
	GUEST_ASSERT(rdmsr(MSR_AMD64_SEV) & MSR_AMD64_SEV_ES_ENABLED);

	/*
	 * TODO: Add GHCB and ucall support for SEV-ES guests.  For now, simply
	 * force "termination" to signal "done" via the GHCB MSR protocol.
	 */
	wrmsr(MSR_AMD64_SEV_ES_GHCB, GHCB_MSR_TERM_REQ);
	vmgexit();
}

static void guest_sev_code(void)
{
	GUEST_ASSERT(this_cpu_has(X86_FEATURE_SEV));
	GUEST_ASSERT(rdmsr(MSR_AMD64_SEV) & MSR_AMD64_SEV_ENABLED);

	GUEST_DONE();
}

/* Stash state passed via VMSA before any compiled code runs.  */
extern void guest_code_xsave(void);
asm("guest_code_xsave:\n"
    "mov $" __stringify(XFEATURE_MASK_X87_AVX) ", %eax\n"
    "xor %edx, %edx\n"
    "xsave (%rdi)\n"
    "jmp guest_sev_es_code");

static void compare_xsave(u8 *from_host, u8 *from_guest)
{
	int i;
	bool bad = false;
	for (i = 0; i < 4095; i++) {
		if (from_host[i] != from_guest[i]) {
			printf("mismatch at %u | %02hhx %02hhx\n",
			       i, from_host[i], from_guest[i]);
			bad = true;
		}
	}

	if (bad)
		abort();
}

static void test_sync_vmsa(uint32_t type, uint64_t policy)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	vm_vaddr_t gva;
	void *hva;

	double x87val = M_PI;
	struct kvm_xsave __attribute__((aligned(64))) xsave = { 0 };

	vm = vm_sev_create_with_one_vcpu(type, guest_code_xsave, &vcpu);
	gva = vm_vaddr_alloc_shared(vm, PAGE_SIZE, KVM_UTIL_MIN_VADDR,
				    MEM_REGION_TEST_DATA);
	hva = addr_gva2hva(vm, gva);

	vcpu_args_set(vcpu, 1, gva);

	asm("fninit\n"
	    "vpcmpeqb %%ymm4, %%ymm4, %%ymm4\n"
	    "fldl %3\n"
	    "xsave (%2)\n"
	    "fstp %%st\n"
	    : "=m"(xsave)
	    : "A"(XFEATURE_MASK_X87_AVX), "r"(&xsave), "m" (x87val)
	    : "ymm4", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");
	vcpu_xsave_set(vcpu, &xsave);

	vm_sev_launch(vm, policy, NULL);

	/* This page is shared, so make it decrypted.  */
	memset(hva, 0, 4096);

	vcpu_run(vcpu);

	TEST_ASSERT(vcpu->run->exit_reason == KVM_EXIT_SYSTEM_EVENT,
		    "Wanted SYSTEM_EVENT, got %s",
		    exit_reason_str(vcpu->run->exit_reason));
	TEST_ASSERT_EQ(vcpu->run->system_event.type, KVM_SYSTEM_EVENT_SEV_TERM);
	TEST_ASSERT_EQ(vcpu->run->system_event.ndata, 1);
	TEST_ASSERT_EQ(vcpu->run->system_event.data[0], GHCB_MSR_TERM_REQ);

	compare_xsave((u8 *)&xsave, (u8 *)hva);

	kvm_vm_free(vm);
}

static void test_sev(void *guest_code, uint32_t type, uint64_t policy)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	vm = vm_sev_create_with_one_vcpu(type, guest_code, &vcpu);

	/* TODO: Validate the measurement is as expected. */
	vm_sev_launch(vm, policy, NULL);

	for (;;) {
		vcpu_run(vcpu);

		if (is_sev_es_vm(vm)) {
			TEST_ASSERT(vcpu->run->exit_reason == KVM_EXIT_SYSTEM_EVENT,
				    "Wanted SYSTEM_EVENT, got %s",
				    exit_reason_str(vcpu->run->exit_reason));
			TEST_ASSERT_EQ(vcpu->run->system_event.type, KVM_SYSTEM_EVENT_SEV_TERM);
			TEST_ASSERT_EQ(vcpu->run->system_event.ndata, 1);
			TEST_ASSERT_EQ(vcpu->run->system_event.data[0], GHCB_MSR_TERM_REQ);
			break;
		}

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			continue;
		case UCALL_DONE:
			return;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		default:
			TEST_FAIL("Unexpected exit: %s",
				  exit_reason_str(vcpu->run->exit_reason));
		}
	}

	kvm_vm_free(vm);
}

static void guest_shutdown_code(void)
{
	struct desc_ptr idt;

	/* Clobber the IDT so that #UD is guaranteed to trigger SHUTDOWN. */
	memset(&idt, 0, sizeof(idt));
	set_idt(&idt);

	__asm__ __volatile__("ud2");
}

static void test_sev_shutdown(uint32_t type, uint64_t policy)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	vm = vm_sev_create_with_one_vcpu(type, guest_shutdown_code, &vcpu);

	vm_sev_launch(vm, policy, NULL);

	vcpu_run(vcpu);
	TEST_ASSERT(vcpu->run->exit_reason == KVM_EXIT_SHUTDOWN,
		    "Wanted SHUTDOWN, got %s",
		    exit_reason_str(vcpu->run->exit_reason));

	kvm_vm_free(vm);
}

static void test_sev_smoke(void *guest, uint32_t type, uint64_t policy)
{
	const u64 xf_mask = XFEATURE_MASK_X87_AVX;

	if (type == KVM_X86_SNP_VM)
		test_sev(guest, type, policy | SNP_POLICY_DBG);
	else
		test_sev(guest, type, policy | SEV_POLICY_NO_DBG);
	test_sev(guest, type, policy);

	if (type == KVM_X86_SEV_VM)
		return;

	test_sev_shutdown(type, policy);

	if (kvm_has_cap(KVM_CAP_XCRS) &&
	    (xgetbv(0) & kvm_cpu_supported_xcr0() & xf_mask) == xf_mask) {
		test_sync_vmsa(type, policy);
		if (type == KVM_X86_SNP_VM)
			test_sync_vmsa(type, policy | SNP_POLICY_DBG);
		else
			test_sync_vmsa(type, policy | SEV_POLICY_NO_DBG);
	}
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SEV));

	test_sev_smoke(guest_sev_code, KVM_X86_SEV_VM, 0);

	if (kvm_cpu_has(X86_FEATURE_SEV_ES))
		test_sev_smoke(guest_sev_es_code, KVM_X86_SEV_ES_VM, SEV_POLICY_ES);

	if (kvm_cpu_has(X86_FEATURE_SEV_SNP))
		test_sev_smoke(guest_snp_code, KVM_X86_SNP_VM, snp_default_policy());

	return 0;
}
