// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "vmx.h"

#define NR_ITERATIONS		500

#define PTRS_PER_PTE		512
#define PXD_INDEX(vaddr, level)	(((vaddr) >> PG_LEVEL_SHIFT(level)) & (PTRS_PER_PTE - 1))

#define TEST_MEM_BASE_GVA	0xc0000000ULL
#define TEST_PGTABLE_GVA_OFFSET	0xd0000000ULL
#define PATTERN			0xabcdefabcdefabcdULL

static u64 expected_vaddr;
static u64 guest_faults;

static u64 *guest_get_pte(u64 vaddr)
{
	u64 pgtable_pa, pte;
	u64 *pgtable;
	int level;

	level = (get_cr4() & X86_CR4_LA57) ? PG_LEVEL_256T : PG_LEVEL_512G;

	pgtable_pa = get_cr3() & PHYSICAL_PAGE_MASK;
	for (; level > PG_LEVEL_4K; level--) {
		pgtable = (u64 *)(pgtable_pa + TEST_PGTABLE_GVA_OFFSET);
		pte = pgtable[PXD_INDEX(vaddr, level)];
		GUEST_ASSERT(pte & PTE_PRESENT_MASK(&guest_mmu));
		GUEST_ASSERT(!(pte & PTE_HUGE_MASK(&guest_mmu)));
		pgtable_pa = PTE_GET_PA(pte);
	}

	pgtable = (u64 *)(pgtable_pa + TEST_PGTABLE_GVA_OFFSET);
	return &pgtable[PXD_INDEX(vaddr, PG_LEVEL_4K)];
}

static void guest_pf_handler(struct ex_regs *regs)
{
	u64 fault_addr;
	u64 *ptep;

	fault_addr = get_cr2();
	GUEST_ASSERT_EQ(fault_addr, READ_ONCE(expected_vaddr));

	ptep = guest_get_pte(fault_addr);
	GUEST_ASSERT(ptep);
	GUEST_ASSERT(!(*ptep & PTE_PRESENT_MASK(&guest_mmu)));

	*ptep |= PTE_PRESENT_MASK(&guest_mmu);
	guest_faults++;
}

static void guest_access_memory(void *arg)
{
	u64 vaddr, val;
	int i;

	for (i = 0; ; i++) {
		vaddr = TEST_MEM_BASE_GVA + (i % PTRS_PER_PTE) * PAGE_SIZE;
		WRITE_ONCE(expected_vaddr, vaddr);

		/* Read to trigger #PF */
		val = READ_ONCE(*(u64 *)vaddr);
		GUEST_ASSERT_EQ(val, PATTERN);

		/* Clear the present bit again so it faults next time */
		*guest_get_pte(vaddr) &= ~PTE_PRESENT_MASK(&guest_mmu);
		invlpg(vaddr);
	}
}

static void l1_svm_code(struct svm_test_data *svm)
{
	generic_svm_setup(svm, guest_access_memory);
	svm->vmcb->control.intercept_exceptions |= BIT(UD_VECTOR);

	while (1) {
		run_guest(svm->vmcb, svm->vmcb_gpa);
		GUEST_ASSERT_EQ(svm->vmcb->control.exit_code,
				(SVM_EXIT_EXCP_BASE + UD_VECTOR));
	}
}

static void l1_vmx_code(struct vmx_pages *vmx)
{
	GUEST_ASSERT(prepare_for_vmx_operation(vmx));
	GUEST_ASSERT(load_vmcs(vmx));
	prepare_vmcs(vmx, guest_access_memory);

	GUEST_ASSERT(!vmwrite(EXCEPTION_BITMAP, BIT(UD_VECTOR)));

	GUEST_ASSERT(!vmlaunch());
	while (1) {
		GUEST_ASSERT_EQ(vmreadz(VM_EXIT_REASON), EXIT_REASON_EXCEPTION_NMI);
		GUEST_ASSERT_EQ(vmreadz(VM_EXIT_INTR_INFO) & 0xff, UD_VECTOR);
		GUEST_ASSERT(!vmresume());
	}
}

static void l1_guest_code(void *test_data)
{
	if (this_cpu_has(X86_FEATURE_SVM))
		l1_svm_code(test_data);
	else
		l1_vmx_code(test_data);
}

static void *sigusr_thread_fn(void *arg)
{
	pthread_t vcpu_thread = (pthread_t)arg;

	for (;;) {
		pthread_testcancel();
		pthread_kill(vcpu_thread, SIGUSR1);
		usleep(msecs_to_usecs(1));
	}
	return NULL;
}

static void dummy_signal_handler(int signo) {}
static struct sigaction sa;

static void vcpu_sigusr_listen(void)
{
	sa.sa_handler = dummy_signal_handler;
	sigaction(SIGUSR1, &sa, NULL);
}

static void vcpu_sigusr_ignore(void)
{
	sa.sa_handler = SIG_IGN;
	sigaction(SIGUSR1, &sa, NULL);
}

static void kvm_x86_state_queue_ud(struct kvm_x86_state *state)
{
	if (state->events.exception.pending || state->events.exception.injected)
		return;

	state->events.flags |= KVM_VCPUEVENT_VALID_PAYLOAD;
	state->events.exception.pending = true;
	state->events.exception.injected = false;
	state->events.exception.nr = UD_VECTOR;
	state->events.exception.has_error_code = false;
	state->events.exception_has_payload = false;
}

static void run_test(bool nested)
{
	struct kvm_x86_state *state;
	int r, i, level;
	pthread_t sigusr_thread;
	gpa_t gpa, pgtable_gpa;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	u64 *pgtable;
	gva_t gva;
	u64 pte;

	vm = vm_create_with_one_vcpu(&vcpu, nested ? l1_guest_code : guest_access_memory);
	vm_install_exception_handler(vm, PF_VECTOR, guest_pf_handler);

	if (nested) {
		vm_enable_cap(vm, KVM_CAP_EXCEPTION_PAYLOAD, -2ul);
		if (kvm_cpu_has(X86_FEATURE_SVM))
			vcpu_alloc_svm(vm, &gva);
		else
			vcpu_alloc_vmx(vm, &gva);
		vcpu_args_set(vcpu, 1, gva);
	}

	/* Allocate a page and write the pattern to it */
	gva = vm_alloc_page(vm);
	*(u64 *)addr_gva2hva(vm, gva) = PATTERN;
	gpa = addr_gva2gpa(vm, gva);

	/*
	 * Map all virtual addresses to the pattern page and clear the present
	 * bit such that guest accesses will cause a #PF.
	 */
	for (i = 0; i < PTRS_PER_PTE; i++) {
		gva = TEST_MEM_BASE_GVA + i * getpagesize();
		virt_pg_map(vm, gva, gpa);
		*vm_get_pte(vm, gva) &= ~PTE_PRESENT_MASK(&vm->mmu);
	}

	/*
	 * Now create mappings for the page tables created above so that the
	 * guest #PF handler can walk them. All PTEs for test virtual addresses
	 * should lie on the same PTE page, so one page is mapped for each page
	 * table level.
	 *
	 * Use an offset for the GVA instead of creating identity mappings to
	 * avoid collision with existing mappings at low GVAs (e.g. ELF).
	 */
	pgtable_gpa = vm->mmu.pgd;
	for (level = vm->mmu.pgtable_levels; level >= PG_LEVEL_4K; level--) {
		virt_map(vm, pgtable_gpa + TEST_PGTABLE_GVA_OFFSET, pgtable_gpa, 1);
		pgtable = addr_gpa2hva(vm, pgtable_gpa);
		pte = pgtable[PXD_INDEX(TEST_MEM_BASE_GVA, level)];
		pgtable_gpa = PTE_GET_PA(pte);
	}

	/* Initialize the thread sending SIGUSR and install the handler */
	vcpu_sigusr_ignore();
	r = pthread_create(&sigusr_thread, NULL, sigusr_thread_fn,
			   (void *)pthread_self());
	TEST_ASSERT(!r, "pthread_create() failed: %d", r);

	for (i = 1; i <= NR_ITERATIONS; i++) {
		/*
		 * Only handle SIGUSR while the vCPU is running, otherwise
		 * ignore it to avoid interrupting other ioctls/syscalls.
		 */
		vcpu_sigusr_listen();
		r = __vcpu_run(vcpu);
		TEST_ASSERT(!r || errno == EINTR, "Expected success or SIGUSR1");
		vcpu_sigusr_ignore();

		/* The guest only exits due to a signal or failed assertion */
		if (!r) {
			TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
			TEST_ASSERT_EQ(get_ucall(vcpu, &uc), UCALL_ABORT);
			REPORT_GUEST_ASSERT(uc);
			break;
		}

		state = vcpu_save_state(vcpu);

		/*
		 * If the vCPU is in guest mode, inject a #UD to trigger an
		 * L2->L1 VM-Exit every other iteration.
		 */
		if (kvm_x86_state_is_guest_mode(state) && i % 2 == 0)
			kvm_x86_state_queue_ud(state);

		kvm_vm_release(vm);
		vcpu = vm_recreate_with_one_vcpu(vm);
		if (nested)
			vm_enable_cap(vm, KVM_CAP_EXCEPTION_PAYLOAD, -2ul);
		vcpu_load_state(vcpu, state);
		kvm_x86_state_cleanup(state);

		pr_info("\rSave+restore iterations: %d", i);
	}
	pr_info("\n");

	sync_global_from_guest(vm, guest_faults);
	TEST_ASSERT(guest_faults, "No guest page faults triggered");
	pr_info("Guest page faults%s: %lu\n", nested ? " (in L2)" : "", guest_faults);

	pthread_cancel(sigusr_thread);
	pthread_join(sigusr_thread, NULL);
	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	pr_info("Running save+restore stress test...\n");
	run_test(/*nested=*/false);

	if (!kvm_has_cap(KVM_CAP_EXCEPTION_PAYLOAD) ||
	    !kvm_has_cap(KVM_CAP_NESTED_STATE) ||
	    (!kvm_cpu_has(X86_FEATURE_SVM) && !kvm_cpu_has(X86_FEATURE_VMX))) {
		pr_info("Nested virtualization not supported, skipping nested test\n");
		return 0;
	}

	pr_info("Running save+restore stress test with a nested guest...\n");
	run_test(/*nested=*/true);
	return 0;
}
