// SPDX-License-Identifier: GPL-2.0

#include "kvm_util.h"
#include "processor.h"

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
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;
	uint64_t cmd;

	/* Create VM */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	run = vcpu->run;

	/* Map 1gb page without a backing memlot. */
	__virt_pg_map(vm, MMIO_GPA, MMIO_GPA, PG_LEVEL_1G);

	vcpu_run(vcpu);

	/* Guest access to the 1gb page should trigger MMIO. */
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
	vcpu_set_cpuid(vcpu, kvm_get_supported_cpuid());

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
	vcpu_init_descriptor_tables(vcpu);
	vm_install_exception_handler(vm, PF_VECTOR, guest_pf_handler);

	vcpu_run(vcpu);

	cmd = get_ucall(vcpu, NULL);
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

	__TEST_REQUIRE(do_gbpages || do_maxphyaddr, "No sub-tests selected");

	entry = kvm_get_supported_cpuid_entry(0x80000001);
	TEST_REQUIRE(entry->edx & CPUID_GBPAGES);

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
