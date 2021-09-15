// SPDX-License-Identifier: GPL-2.0

#include "kvm_util.h"
#include "processor.h"

#define VCPU_ID			1

#define MMIO_GPA	0x100000000ull

static void guest_code(void)
{
	(void)READ_ONCE(*((uint64_t *)MMIO_GPA));
	(void)READ_ONCE(*((uint64_t *)MMIO_GPA));

	GUEST_ASSERT(0);
}

static void guest_pf_handler(struct ex_regs *regs)
{
	/* PFEC == RSVD | PRESENT (read, kernel). */
	GUEST_ASSERT(regs->error_code == 0x9);
	GUEST_DONE();
}

static void mmu_role_test(u32 *cpuid_reg, u32 evil_cpuid_val)
{
	u32 good_cpuid_val = *cpuid_reg;
	struct kvm_run *run;
	struct kvm_vm *vm;
	uint64_t cmd;
	int r;

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);
	run = vcpu_state(vm, VCPU_ID);

	/* Map 1gb page without a backing memlot. */
	__virt_pg_map(vm, MMIO_GPA, MMIO_GPA, X86_PAGE_SIZE_1G);

	r = _vcpu_run(vm, VCPU_ID);

	/* Guest access to the 1gb page should trigger MMIO. */
	TEST_ASSERT(r == 0, "vcpu_run failed: %d\n", r);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_MMIO,
		    "Unexpected exit reason: %u (%s), expected MMIO exit (1gb page w/o memslot)\n",
		    run->exit_reason, exit_reason_str(run->exit_reason));

	TEST_ASSERT(run->mmio.len == 8, "Unexpected exit mmio size = %u", run->mmio.len);

	TEST_ASSERT(run->mmio.phys_addr == MMIO_GPA,
		    "Unexpected exit mmio address = 0x%llx", run->mmio.phys_addr);

	/*
	 * Effect the CPUID change for the guest and re-enter the guest.  Its
	 * access should now #PF due to the PAGE_SIZE bit being reserved or
	 * the resulting GPA being invalid.  Note, kvm_get_supported_cpuid()
	 * returns the struct that contains the entry being modified.  Eww.
	 */
	*cpuid_reg = evil_cpuid_val;
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	/*
	 * Add a dummy memslot to coerce KVM into bumping the MMIO generation.
	 * KVM does not "officially" support mucking with CPUID after KVM_RUN,
	 * and will incorrectly reuse MMIO SPTEs.  Don't delete the memslot!
	 * KVM x86 zaps all shadow pages on memslot deletion.
	 */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    MMIO_GPA << 1, 10, 1, 0);

	/* Set up a #PF handler to eat the RSVD #PF and signal all done! */
	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);
	vm_install_exception_handler(vm, PF_VECTOR, guest_pf_handler);

	r = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(r == 0, "vcpu_run failed: %d\n", r);

	cmd = get_ucall(vm, VCPU_ID, NULL);
	TEST_ASSERT(cmd == UCALL_DONE,
		    "Unexpected guest exit, exit_reason=%s, ucall.cmd = %lu\n",
		    exit_reason_str(run->exit_reason), cmd);

	/*
	 * Restore the happy CPUID value for the next test.  Yes, changes are
	 * indeed persistent across VM destruction.
	 */
	*cpuid_reg = good_cpuid_val;

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	struct kvm_cpuid_entry2 *entry;
	int opt;

	/*
	 * All tests are opt-in because TDP doesn't play nice with reserved #PF
	 * in the GVA->GPA translation.  The hardware page walker doesn't let
	 * software change GBPAGES or MAXPHYADDR, and KVM doesn't manually walk
	 * the GVA on fault for performance reasons.
	 */
	bool do_gbpages = false;
	bool do_maxphyaddr = false;

	setbuf(stdout, NULL);

	while ((opt = getopt(argc, argv, "gm")) != -1) {
		switch (opt) {
		case 'g':
			do_gbpages = true;
			break;
		case 'm':
			do_maxphyaddr = true;
			break;
		case 'h':
		default:
			printf("usage: %s [-g (GBPAGES)] [-m (MAXPHYADDR)]\n", argv[0]);
			break;
		}
	}

	if (!do_gbpages && !do_maxphyaddr) {
		print_skip("No sub-tests selected");
		return 0;
	}

	entry = kvm_get_supported_cpuid_entry(0x80000001);
	if (!(entry->edx & CPUID_GBPAGES)) {
		print_skip("1gb hugepages not supported");
		return 0;
	}

	if (do_gbpages) {
		pr_info("Test MMIO after toggling CPUID.GBPAGES\n\n");
		mmu_role_test(&entry->edx, entry->edx & ~CPUID_GBPAGES);
	}

	if (do_maxphyaddr) {
		pr_info("Test MMIO after changing CPUID.MAXPHYADDR\n\n");
		entry = kvm_get_supported_cpuid_entry(0x80000008);
		mmu_role_test(&entry->eax, (entry->eax & ~0xff) | 0x20);
	}

	return 0;
}
