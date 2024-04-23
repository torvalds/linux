// SPDX-License-Identifier: GPL-2.0
/*
 * Hyper-V HvCallSendSyntheticClusterIpi{,Ex} tests
 *
 * Copyright (C) 2022, Red Hat, Inc.
 *
 */
#include <pthread.h>
#include <inttypes.h>

#include "kvm_util.h"
#include "hyperv.h"
#include "test_util.h"
#include "vmx.h"

#define RECEIVER_VCPU_ID_1 2
#define RECEIVER_VCPU_ID_2 65

#define IPI_VECTOR	 0xfe

static volatile uint64_t ipis_rcvd[RECEIVER_VCPU_ID_2 + 1];

struct hv_vpset {
	u64 format;
	u64 valid_bank_mask;
	u64 bank_contents[2];
};

enum HV_GENERIC_SET_FORMAT {
	HV_GENERIC_SET_SPARSE_4K,
	HV_GENERIC_SET_ALL,
};

/* HvCallSendSyntheticClusterIpi hypercall */
struct hv_send_ipi {
	u32 vector;
	u32 reserved;
	u64 cpu_mask;
};

/* HvCallSendSyntheticClusterIpiEx hypercall */
struct hv_send_ipi_ex {
	u32 vector;
	u32 reserved;
	struct hv_vpset vp_set;
};

static inline void hv_init(vm_vaddr_t pgs_gpa)
{
	wrmsr(HV_X64_MSR_GUEST_OS_ID, HYPERV_LINUX_OS_ID);
	wrmsr(HV_X64_MSR_HYPERCALL, pgs_gpa);
}

static void receiver_code(void *hcall_page, vm_vaddr_t pgs_gpa)
{
	u32 vcpu_id;

	x2apic_enable();
	hv_init(pgs_gpa);

	vcpu_id = rdmsr(HV_X64_MSR_VP_INDEX);

	/* Signal sender vCPU we're ready */
	ipis_rcvd[vcpu_id] = (u64)-1;

	for (;;)
		asm volatile("sti; hlt; cli");
}

static void guest_ipi_handler(struct ex_regs *regs)
{
	u32 vcpu_id = rdmsr(HV_X64_MSR_VP_INDEX);

	ipis_rcvd[vcpu_id]++;
	wrmsr(HV_X64_MSR_EOI, 1);
}

static inline void nop_loop(void)
{
	int i;

	for (i = 0; i < 100000000; i++)
		asm volatile("nop");
}

static void sender_guest_code(void *hcall_page, vm_vaddr_t pgs_gpa)
{
	struct hv_send_ipi *ipi = (struct hv_send_ipi *)hcall_page;
	struct hv_send_ipi_ex *ipi_ex = (struct hv_send_ipi_ex *)hcall_page;
	int stage = 1, ipis_expected[2] = {0};

	hv_init(pgs_gpa);
	GUEST_SYNC(stage++);

	/* Wait for receiver vCPUs to come up */
	while (!ipis_rcvd[RECEIVER_VCPU_ID_1] || !ipis_rcvd[RECEIVER_VCPU_ID_2])
		nop_loop();
	ipis_rcvd[RECEIVER_VCPU_ID_1] = ipis_rcvd[RECEIVER_VCPU_ID_2] = 0;

	/* 'Slow' HvCallSendSyntheticClusterIpi to RECEIVER_VCPU_ID_1 */
	ipi->vector = IPI_VECTOR;
	ipi->cpu_mask = 1 << RECEIVER_VCPU_ID_1;
	hyperv_hypercall(HVCALL_SEND_IPI, pgs_gpa, pgs_gpa + 4096);
	nop_loop();
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_1] == ++ipis_expected[0]);
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_2] == ipis_expected[1]);
	GUEST_SYNC(stage++);
	/* 'Fast' HvCallSendSyntheticClusterIpi to RECEIVER_VCPU_ID_1 */
	hyperv_hypercall(HVCALL_SEND_IPI | HV_HYPERCALL_FAST_BIT,
			 IPI_VECTOR, 1 << RECEIVER_VCPU_ID_1);
	nop_loop();
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_1] == ++ipis_expected[0]);
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_2] == ipis_expected[1]);
	GUEST_SYNC(stage++);

	/* 'Slow' HvCallSendSyntheticClusterIpiEx to RECEIVER_VCPU_ID_1 */
	memset(hcall_page, 0, 4096);
	ipi_ex->vector = IPI_VECTOR;
	ipi_ex->vp_set.format = HV_GENERIC_SET_SPARSE_4K;
	ipi_ex->vp_set.valid_bank_mask = 1 << 0;
	ipi_ex->vp_set.bank_contents[0] = BIT(RECEIVER_VCPU_ID_1);
	hyperv_hypercall(HVCALL_SEND_IPI_EX | (1 << HV_HYPERCALL_VARHEAD_OFFSET),
			 pgs_gpa, pgs_gpa + 4096);
	nop_loop();
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_1] == ++ipis_expected[0]);
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_2] == ipis_expected[1]);
	GUEST_SYNC(stage++);
	/* 'XMM Fast' HvCallSendSyntheticClusterIpiEx to RECEIVER_VCPU_ID_1 */
	hyperv_write_xmm_input(&ipi_ex->vp_set.valid_bank_mask, 1);
	hyperv_hypercall(HVCALL_SEND_IPI_EX | HV_HYPERCALL_FAST_BIT |
			 (1 << HV_HYPERCALL_VARHEAD_OFFSET),
			 IPI_VECTOR, HV_GENERIC_SET_SPARSE_4K);
	nop_loop();
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_1] == ++ipis_expected[0]);
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_2] == ipis_expected[1]);
	GUEST_SYNC(stage++);

	/* 'Slow' HvCallSendSyntheticClusterIpiEx to RECEIVER_VCPU_ID_2 */
	memset(hcall_page, 0, 4096);
	ipi_ex->vector = IPI_VECTOR;
	ipi_ex->vp_set.format = HV_GENERIC_SET_SPARSE_4K;
	ipi_ex->vp_set.valid_bank_mask = 1 << 1;
	ipi_ex->vp_set.bank_contents[0] = BIT(RECEIVER_VCPU_ID_2 - 64);
	hyperv_hypercall(HVCALL_SEND_IPI_EX | (1 << HV_HYPERCALL_VARHEAD_OFFSET),
			 pgs_gpa, pgs_gpa + 4096);
	nop_loop();
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_1] == ipis_expected[0]);
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_2] == ++ipis_expected[1]);
	GUEST_SYNC(stage++);
	/* 'XMM Fast' HvCallSendSyntheticClusterIpiEx to RECEIVER_VCPU_ID_2 */
	hyperv_write_xmm_input(&ipi_ex->vp_set.valid_bank_mask, 1);
	hyperv_hypercall(HVCALL_SEND_IPI_EX | HV_HYPERCALL_FAST_BIT |
			 (1 << HV_HYPERCALL_VARHEAD_OFFSET),
			 IPI_VECTOR, HV_GENERIC_SET_SPARSE_4K);
	nop_loop();
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_1] == ipis_expected[0]);
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_2] == ++ipis_expected[1]);
	GUEST_SYNC(stage++);

	/* 'Slow' HvCallSendSyntheticClusterIpiEx to both RECEIVER_VCPU_ID_{1,2} */
	memset(hcall_page, 0, 4096);
	ipi_ex->vector = IPI_VECTOR;
	ipi_ex->vp_set.format = HV_GENERIC_SET_SPARSE_4K;
	ipi_ex->vp_set.valid_bank_mask = 1 << 1 | 1;
	ipi_ex->vp_set.bank_contents[0] = BIT(RECEIVER_VCPU_ID_1);
	ipi_ex->vp_set.bank_contents[1] = BIT(RECEIVER_VCPU_ID_2 - 64);
	hyperv_hypercall(HVCALL_SEND_IPI_EX | (2 << HV_HYPERCALL_VARHEAD_OFFSET),
			 pgs_gpa, pgs_gpa + 4096);
	nop_loop();
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_1] == ++ipis_expected[0]);
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_2] == ++ipis_expected[1]);
	GUEST_SYNC(stage++);
	/* 'XMM Fast' HvCallSendSyntheticClusterIpiEx to both RECEIVER_VCPU_ID_{1, 2} */
	hyperv_write_xmm_input(&ipi_ex->vp_set.valid_bank_mask, 2);
	hyperv_hypercall(HVCALL_SEND_IPI_EX | HV_HYPERCALL_FAST_BIT |
			 (2 << HV_HYPERCALL_VARHEAD_OFFSET),
			 IPI_VECTOR, HV_GENERIC_SET_SPARSE_4K);
	nop_loop();
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_1] == ++ipis_expected[0]);
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_2] == ++ipis_expected[1]);
	GUEST_SYNC(stage++);

	/* 'Slow' HvCallSendSyntheticClusterIpiEx to HV_GENERIC_SET_ALL */
	memset(hcall_page, 0, 4096);
	ipi_ex->vector = IPI_VECTOR;
	ipi_ex->vp_set.format = HV_GENERIC_SET_ALL;
	hyperv_hypercall(HVCALL_SEND_IPI_EX, pgs_gpa, pgs_gpa + 4096);
	nop_loop();
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_1] == ++ipis_expected[0]);
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_2] == ++ipis_expected[1]);
	GUEST_SYNC(stage++);
	/*
	 * 'XMM Fast' HvCallSendSyntheticClusterIpiEx to HV_GENERIC_SET_ALL.
	 */
	ipi_ex->vp_set.valid_bank_mask = 0;
	hyperv_write_xmm_input(&ipi_ex->vp_set.valid_bank_mask, 2);
	hyperv_hypercall(HVCALL_SEND_IPI_EX | HV_HYPERCALL_FAST_BIT,
			 IPI_VECTOR, HV_GENERIC_SET_ALL);
	nop_loop();
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_1] == ++ipis_expected[0]);
	GUEST_ASSERT(ipis_rcvd[RECEIVER_VCPU_ID_2] == ++ipis_expected[1]);
	GUEST_SYNC(stage++);

	GUEST_DONE();
}

static void *vcpu_thread(void *arg)
{
	struct kvm_vcpu *vcpu = (struct kvm_vcpu *)arg;
	int old, r;

	r = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old);
	TEST_ASSERT(!r, "pthread_setcanceltype failed on vcpu_id=%u with errno=%d",
		    vcpu->id, r);

	vcpu_run(vcpu);

	TEST_FAIL("vCPU %u exited unexpectedly", vcpu->id);

	return NULL;
}

static void cancel_join_vcpu_thread(pthread_t thread, struct kvm_vcpu *vcpu)
{
	void *retval;
	int r;

	r = pthread_cancel(thread);
	TEST_ASSERT(!r, "pthread_cancel on vcpu_id=%d failed with errno=%d",
		    vcpu->id, r);

	r = pthread_join(thread, &retval);
	TEST_ASSERT(!r, "pthread_join on vcpu_id=%d failed with errno=%d",
		    vcpu->id, r);
	TEST_ASSERT(retval == PTHREAD_CANCELED,
		    "expected retval=%p, got %p", PTHREAD_CANCELED,
		    retval);
}

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu[3];
	vm_vaddr_t hcall_page;
	pthread_t threads[2];
	int stage = 1, r;
	struct ucall uc;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_HYPERV_SEND_IPI));

	vm = vm_create_with_one_vcpu(&vcpu[0], sender_guest_code);

	/* Hypercall input/output */
	hcall_page = vm_vaddr_alloc_pages(vm, 2);
	memset(addr_gva2hva(vm, hcall_page), 0x0, 2 * getpagesize());

	vm_init_descriptor_tables(vm);

	vcpu[1] = vm_vcpu_add(vm, RECEIVER_VCPU_ID_1, receiver_code);
	vcpu_init_descriptor_tables(vcpu[1]);
	vcpu_args_set(vcpu[1], 2, hcall_page, addr_gva2gpa(vm, hcall_page));
	vcpu_set_msr(vcpu[1], HV_X64_MSR_VP_INDEX, RECEIVER_VCPU_ID_1);
	vcpu_set_hv_cpuid(vcpu[1]);

	vcpu[2] = vm_vcpu_add(vm, RECEIVER_VCPU_ID_2, receiver_code);
	vcpu_init_descriptor_tables(vcpu[2]);
	vcpu_args_set(vcpu[2], 2, hcall_page, addr_gva2gpa(vm, hcall_page));
	vcpu_set_msr(vcpu[2], HV_X64_MSR_VP_INDEX, RECEIVER_VCPU_ID_2);
	vcpu_set_hv_cpuid(vcpu[2]);

	vm_install_exception_handler(vm, IPI_VECTOR, guest_ipi_handler);

	vcpu_args_set(vcpu[0], 2, hcall_page, addr_gva2gpa(vm, hcall_page));
	vcpu_set_hv_cpuid(vcpu[0]);

	r = pthread_create(&threads[0], NULL, vcpu_thread, vcpu[1]);
	TEST_ASSERT(!r, "pthread_create failed errno=%d", r);

	r = pthread_create(&threads[1], NULL, vcpu_thread, vcpu[2]);
	TEST_ASSERT(!r, "pthread_create failed errno=%d", errno);

	while (true) {
		vcpu_run(vcpu[0]);

		TEST_ASSERT_KVM_EXIT_REASON(vcpu[0], KVM_EXIT_IO);

		switch (get_ucall(vcpu[0], &uc)) {
		case UCALL_SYNC:
			TEST_ASSERT(uc.args[1] == stage,
				    "Unexpected stage: %ld (%d expected)",
				    uc.args[1], stage);
			break;
		case UCALL_DONE:
			goto done;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}

		stage++;
	}

done:
	cancel_join_vcpu_thread(threads[0], vcpu[1]);
	cancel_join_vcpu_thread(threads[1], vcpu[2]);
	kvm_vm_free(vm);

	return r;
}
