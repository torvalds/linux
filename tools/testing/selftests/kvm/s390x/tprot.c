// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test TEST PROTECTION emulation.
 *
 * Copyright IBM Corp. 2021
 */
#include <sys/mman.h>
#include "test_util.h"
#include "kvm_util.h"
#include "kselftest.h"
#include "ucall_common.h"
#include "processor.h"

#define CR0_FETCH_PROTECTION_OVERRIDE	(1UL << (63 - 38))
#define CR0_STORAGE_PROTECTION_OVERRIDE	(1UL << (63 - 39))

static __aligned(PAGE_SIZE) uint8_t pages[2][PAGE_SIZE];
static uint8_t *const page_store_prot = pages[0];
static uint8_t *const page_fetch_prot = pages[1];

/* Nonzero return value indicates that address not mapped */
static int set_storage_key(void *addr, uint8_t key)
{
	int not_mapped = 0;

	asm volatile (
		       "lra	%[addr], 0(0,%[addr])\n"
		"	jz	0f\n"
		"	llill	%[not_mapped],1\n"
		"	j	1f\n"
		"0:	sske	%[key], %[addr]\n"
		"1:"
		: [addr] "+&a" (addr), [not_mapped] "+r" (not_mapped)
		: [key] "r" (key)
		: "cc"
	);
	return -not_mapped;
}

enum permission {
	READ_WRITE = 0,
	READ = 1,
	RW_PROTECTED = 2,
	TRANSL_UNAVAIL = 3,
};

static enum permission test_protection(void *addr, uint8_t key)
{
	uint64_t mask;

	asm volatile (
		       "tprot	%[addr], 0(%[key])\n"
		"	ipm	%[mask]\n"
		: [mask] "=r" (mask)
		: [addr] "Q" (*(char *)addr),
		  [key] "a" (key)
		: "cc"
	);

	return (enum permission)(mask >> 28);
}

enum stage {
	STAGE_INIT_SIMPLE,
	TEST_SIMPLE,
	STAGE_INIT_FETCH_PROT_OVERRIDE,
	TEST_FETCH_PROT_OVERRIDE,
	TEST_STORAGE_PROT_OVERRIDE,
	STAGE_END	/* must be the last entry (it's the amount of tests) */
};

struct test {
	enum stage stage;
	void *addr;
	uint8_t key;
	enum permission expected;
} tests[] = {
	/*
	 * We perform each test in the array by executing TEST PROTECTION on
	 * the specified addr with the specified key and checking if the returned
	 * permissions match the expected value.
	 * Both guest and host cooperate to set up the required test conditions.
	 * A central condition is that the page targeted by addr has to be DAT
	 * protected in the host mappings, in order for KVM to emulate the
	 * TEST PROTECTION instruction.
	 * Since the page tables are shared, the host uses mprotect to achieve
	 * this.
	 *
	 * Test resulting in RW_PROTECTED/TRANSL_UNAVAIL will be interpreted
	 * by SIE, not KVM, but there is no harm in testing them also.
	 * See Enhanced Suppression-on-Protection Facilities in the
	 * Interpretive-Execution Mode
	 */
	/*
	 * guest: set storage key of page_store_prot to 1
	 *        storage key of page_fetch_prot to 9 and enable
	 *        protection for it
	 * STAGE_INIT_SIMPLE
	 * host: write protect both via mprotect
	 */
	/* access key 0 matches any storage key -> RW */
	{ TEST_SIMPLE, page_store_prot, 0x00, READ_WRITE },
	/* access key matches storage key -> RW */
	{ TEST_SIMPLE, page_store_prot, 0x10, READ_WRITE },
	/* mismatched keys, but no fetch protection -> RO */
	{ TEST_SIMPLE, page_store_prot, 0x20, READ },
	/* access key 0 matches any storage key -> RW */
	{ TEST_SIMPLE, page_fetch_prot, 0x00, READ_WRITE },
	/* access key matches storage key -> RW */
	{ TEST_SIMPLE, page_fetch_prot, 0x90, READ_WRITE },
	/* mismatched keys, fetch protection -> inaccessible */
	{ TEST_SIMPLE, page_fetch_prot, 0x10, RW_PROTECTED },
	/* page 0 not mapped yet -> translation not available */
	{ TEST_SIMPLE, (void *)0x00, 0x10, TRANSL_UNAVAIL },
	/*
	 * host: try to map page 0
	 * guest: set storage key of page 0 to 9 and enable fetch protection
	 * STAGE_INIT_FETCH_PROT_OVERRIDE
	 * host: write protect page 0
	 *       enable fetch protection override
	 */
	/* mismatched keys, fetch protection, but override applies -> RO */
	{ TEST_FETCH_PROT_OVERRIDE, (void *)0x00, 0x10, READ },
	/* mismatched keys, fetch protection, override applies to 0-2048 only -> inaccessible */
	{ TEST_FETCH_PROT_OVERRIDE, (void *)2049, 0x10, RW_PROTECTED },
	/*
	 * host: enable storage protection override
	 */
	/* mismatched keys, but override applies (storage key 9) -> RW */
	{ TEST_STORAGE_PROT_OVERRIDE, page_fetch_prot, 0x10, READ_WRITE },
	/* mismatched keys, no fetch protection, override doesn't apply -> RO */
	{ TEST_STORAGE_PROT_OVERRIDE, page_store_prot, 0x20, READ },
	/* mismatched keys, but override applies (storage key 9) -> RW */
	{ TEST_STORAGE_PROT_OVERRIDE, (void *)2049, 0x10, READ_WRITE },
	/* end marker */
	{ STAGE_END, 0, 0, 0 },
};

static enum stage perform_next_stage(int *i, bool mapped_0)
{
	enum stage stage = tests[*i].stage;
	enum permission result;
	bool skip;

	for (; tests[*i].stage == stage; (*i)++) {
		/*
		 * Some fetch protection override tests require that page 0
		 * be mapped, however, when the hosts tries to map that page via
		 * vm_vaddr_alloc, it may happen that some other page gets mapped
		 * instead.
		 * In order to skip these tests we detect this inside the guest
		 */
		skip = tests[*i].addr < (void *)PAGE_SIZE &&
		       tests[*i].expected != TRANSL_UNAVAIL &&
		       !mapped_0;
		if (!skip) {
			result = test_protection(tests[*i].addr, tests[*i].key);
			__GUEST_ASSERT(result == tests[*i].expected,
				       "Wanted %u, got %u, for i = %u",
				       tests[*i].expected, result, *i);
		}
	}
	return stage;
}

static void guest_code(void)
{
	bool mapped_0;
	int i = 0;

	GUEST_ASSERT_EQ(set_storage_key(page_store_prot, 0x10), 0);
	GUEST_ASSERT_EQ(set_storage_key(page_fetch_prot, 0x98), 0);
	GUEST_SYNC(STAGE_INIT_SIMPLE);
	GUEST_SYNC(perform_next_stage(&i, false));

	/* Fetch-protection override */
	mapped_0 = !set_storage_key((void *)0, 0x98);
	GUEST_SYNC(STAGE_INIT_FETCH_PROT_OVERRIDE);
	GUEST_SYNC(perform_next_stage(&i, mapped_0));

	/* Storage-protection override */
	GUEST_SYNC(perform_next_stage(&i, mapped_0));
}

#define HOST_SYNC_NO_TAP(vcpup, stage)				\
({								\
	struct kvm_vcpu *__vcpu = (vcpup);			\
	struct ucall uc;					\
	int __stage = (stage);					\
								\
	vcpu_run(__vcpu);					\
	get_ucall(__vcpu, &uc);					\
	if (uc.cmd == UCALL_ABORT)				\
		REPORT_GUEST_ASSERT(uc);			\
	TEST_ASSERT_EQ(uc.cmd, UCALL_SYNC);			\
	TEST_ASSERT_EQ(uc.args[1], __stage);			\
})

#define HOST_SYNC(vcpu, stage)			\
({						\
	HOST_SYNC_NO_TAP(vcpu, stage);		\
	ksft_test_result_pass("" #stage "\n");	\
})

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct kvm_run *run;
	vm_vaddr_t guest_0_page;

	ksft_print_header();
	ksft_set_plan(STAGE_END);

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	run = vcpu->run;

	HOST_SYNC(vcpu, STAGE_INIT_SIMPLE);
	mprotect(addr_gva2hva(vm, (vm_vaddr_t)pages), PAGE_SIZE * 2, PROT_READ);
	HOST_SYNC(vcpu, TEST_SIMPLE);

	guest_0_page = vm_vaddr_alloc(vm, PAGE_SIZE, 0);
	if (guest_0_page != 0) {
		/* Use NO_TAP so we don't get a PASS print */
		HOST_SYNC_NO_TAP(vcpu, STAGE_INIT_FETCH_PROT_OVERRIDE);
		ksft_test_result_skip("STAGE_INIT_FETCH_PROT_OVERRIDE - "
				      "Did not allocate page at 0\n");
	} else {
		HOST_SYNC(vcpu, STAGE_INIT_FETCH_PROT_OVERRIDE);
	}
	if (guest_0_page == 0)
		mprotect(addr_gva2hva(vm, (vm_vaddr_t)0), PAGE_SIZE, PROT_READ);
	run->s.regs.crs[0] |= CR0_FETCH_PROTECTION_OVERRIDE;
	run->kvm_dirty_regs = KVM_SYNC_CRS;
	HOST_SYNC(vcpu, TEST_FETCH_PROT_OVERRIDE);

	run->s.regs.crs[0] |= CR0_STORAGE_PROTECTION_OVERRIDE;
	run->kvm_dirty_regs = KVM_SYNC_CRS;
	HOST_SYNC(vcpu, TEST_STORAGE_PROT_OVERRIDE);

	kvm_vm_free(vm);

	ksft_finished();	/* Print results and exit() accordingly */
}
