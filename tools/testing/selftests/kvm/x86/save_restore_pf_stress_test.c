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

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

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

int main(int argc, char *argv[])
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

	vm = vm_create_with_one_vcpu(&vcpu, guest_access_memory);
	vm_install_exception_handler(vm, PF_VECTOR, guest_pf_handler);

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

		kvm_vm_release(vm);
		vcpu = vm_recreate_with_one_vcpu(vm);
		vcpu_load_state(vcpu, state);
		kvm_x86_state_cleanup(state);

		pr_info("\rSave+restore iterations: %d", i);
	}
	pr_info("\n");

	sync_global_from_guest(vm, guest_faults);
	TEST_ASSERT(guest_faults, "No guest page faults triggered");
	pr_info("Guest page faults: %lu\n", guest_faults);

	pthread_cancel(sigusr_thread);
	pthread_join(sigusr_thread, NULL);
	kvm_vm_free(vm);
	return 0;
}
