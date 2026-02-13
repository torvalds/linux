// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test for s390x KVM_S390_KEYOP
 *
 * Copyright IBM Corp. 2026
 *
 * Authors:
 *  Claudio Imbrenda <imbrenda@linux.ibm.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <linux/bits.h>

#include "test_util.h"
#include "kvm_util.h"
#include "kselftest.h"
#include "processor.h"

#define BUF_PAGES 128UL
#define GUEST_PAGES 256UL

#define BUF_START_GFN	(GUEST_PAGES - BUF_PAGES)
#define BUF_START_ADDR	(BUF_START_GFN << PAGE_SHIFT)

#define KEY_BITS_ACC	0xf0
#define KEY_BIT_F	0x08
#define KEY_BIT_R	0x04
#define KEY_BIT_C	0x02

#define KEY_BITS_RC	(KEY_BIT_R | KEY_BIT_C)
#define KEY_BITS_ALL	(KEY_BITS_ACC | KEY_BIT_F | KEY_BITS_RC)

static unsigned char tmp[BUF_PAGES];
static unsigned char old[BUF_PAGES];
static unsigned char expected[BUF_PAGES];

static int _get_skeys(struct kvm_vcpu *vcpu, unsigned char skeys[])
{
	struct kvm_s390_skeys skeys_ioctl = {
		.start_gfn = BUF_START_GFN,
		.count = BUF_PAGES,
		.skeydata_addr = (unsigned long)skeys,
	};

	return __vm_ioctl(vcpu->vm, KVM_S390_GET_SKEYS, &skeys_ioctl);
}

static void get_skeys(struct kvm_vcpu *vcpu, unsigned char skeys[])
{
	int r = _get_skeys(vcpu, skeys);

	TEST_ASSERT(!r, "Failed to get storage keys, r=%d", r);
}

static void set_skeys(struct kvm_vcpu *vcpu, unsigned char skeys[])
{
	struct kvm_s390_skeys skeys_ioctl = {
		.start_gfn = BUF_START_GFN,
		.count = BUF_PAGES,
		.skeydata_addr = (unsigned long)skeys,
	};
	int r;

	r = __vm_ioctl(vcpu->vm, KVM_S390_SET_SKEYS, &skeys_ioctl);
	TEST_ASSERT(!r, "Failed to set storage keys, r=%d", r);
}

static int do_keyop(struct kvm_vcpu *vcpu, int op, unsigned long page_idx, unsigned char skey)
{
	struct kvm_s390_keyop keyop = {
		.guest_addr = BUF_START_ADDR + page_idx * PAGE_SIZE,
		.key = skey,
		.operation = op,
	};
	int r;

	r = __vm_ioctl(vcpu->vm, KVM_S390_KEYOP, &keyop);
	TEST_ASSERT(!r, "Failed to perform keyop, r=%d", r);
	TEST_ASSERT((keyop.key & 1) == 0,
		    "Last bit of key is 1, should be 0! page %lu, new key=%#x, old key=%#x",
		    page_idx, skey, keyop.key);

	return keyop.key;
}

static void fault_in_buffer(struct kvm_vcpu *vcpu, int where, int cur_loc)
{
	unsigned long i;
	int r;

	if (where != cur_loc)
		return;

	for (i = 0; i < BUF_PAGES; i++) {
		r = ioctl(vcpu->fd, KVM_S390_VCPU_FAULT, BUF_START_ADDR + i * PAGE_SIZE);
		TEST_ASSERT(!r, "Faulting in buffer page %lu, r=%d", i, r);
	}
}

static inline void set_pattern(unsigned char skeys[])
{
	int i;

	for (i = 0; i < BUF_PAGES; i++)
		skeys[i] = i << 1;
}

static void dump_sk(const unsigned char skeys[], const char *descr)
{
	int i, j;

	fprintf(stderr, "# %s:\n", descr);
	for (i = 0; i < BUF_PAGES; i += 32) {
		fprintf(stderr, "# %3d: ", i);
		for (j = 0; j < 32; j++)
			fprintf(stderr, "%02x ", skeys[i + j]);
		fprintf(stderr, "\n");
	}
}

static inline void compare(const unsigned char what[], const unsigned char expected[],
			   const char *descr, int fault_in_loc)
{
	int i;

	for (i = 0; i < BUF_PAGES; i++) {
		if (expected[i] != what[i]) {
			dump_sk(expected, "Expected");
			dump_sk(what, "Got");
		}
		TEST_ASSERT(expected[i] == what[i],
			    "%s! fault-in location %d, page %d, expected %#x, got %#x",
			    descr, fault_in_loc, i, expected[i], what[i]);
	}
}

static inline void clear_all(void)
{
	memset(tmp, 0, BUF_PAGES);
	memset(old, 0, BUF_PAGES);
	memset(expected, 0, BUF_PAGES);
}

static void test_init(struct kvm_vcpu *vcpu, int fault_in)
{
	/* Set all storage keys to zero */
	fault_in_buffer(vcpu, fault_in, 1);
	set_skeys(vcpu, expected);

	fault_in_buffer(vcpu, fault_in, 2);
	get_skeys(vcpu, tmp);
	compare(tmp, expected, "Setting keys not zero", fault_in);

	/* Set storage keys to a sequential pattern */
	fault_in_buffer(vcpu, fault_in, 3);
	set_pattern(expected);
	set_skeys(vcpu, expected);

	fault_in_buffer(vcpu, fault_in, 4);
	get_skeys(vcpu, tmp);
	compare(tmp, expected, "Setting storage keys failed", fault_in);
}

static void test_rrbe(struct kvm_vcpu *vcpu, int fault_in)
{
	unsigned char k;
	int i;

	/* Set storage keys to a sequential pattern */
	fault_in_buffer(vcpu, fault_in, 1);
	set_pattern(expected);
	set_skeys(vcpu, expected);

	/* Call the RRBE KEYOP ioctl on each page and verify the result */
	fault_in_buffer(vcpu, fault_in, 2);
	for (i = 0; i < BUF_PAGES; i++) {
		k = do_keyop(vcpu, KVM_S390_KEYOP_RRBE, i, 0xff);
		TEST_ASSERT((expected[i] & KEY_BITS_RC) == k,
			    "Old R or C value mismatch! expected: %#x, got %#x",
			    expected[i] & KEY_BITS_RC, k);
		if (i == BUF_PAGES / 2)
			fault_in_buffer(vcpu, fault_in, 3);
	}

	for (i = 0; i < BUF_PAGES; i++)
		expected[i] &= ~KEY_BIT_R;

	/* Verify that only the R bit has been cleared */
	fault_in_buffer(vcpu, fault_in, 4);
	get_skeys(vcpu, tmp);
	compare(tmp, expected, "New value mismatch", fault_in);
}

static void test_iske(struct kvm_vcpu *vcpu, int fault_in)
{
	int i;

	/* Set storage keys to a sequential pattern */
	fault_in_buffer(vcpu, fault_in, 1);
	set_pattern(expected);
	set_skeys(vcpu, expected);

	/* Call the ISKE KEYOP ioctl on each page and verify the result */
	fault_in_buffer(vcpu, fault_in, 2);
	for (i = 0; i < BUF_PAGES; i++) {
		tmp[i] = do_keyop(vcpu, KVM_S390_KEYOP_ISKE, i, 0xff);
		if (i == BUF_PAGES / 2)
			fault_in_buffer(vcpu, fault_in, 3);
	}
	compare(tmp, expected, "Old value mismatch", fault_in);

	/* Check storage keys have not changed */
	fault_in_buffer(vcpu, fault_in, 4);
	get_skeys(vcpu, tmp);
	compare(tmp, expected, "Storage keys values changed", fault_in);
}

static void test_sske(struct kvm_vcpu *vcpu, int fault_in)
{
	int i;

	/* Set storage keys to a sequential pattern */
	fault_in_buffer(vcpu, fault_in, 1);
	set_pattern(tmp);
	set_skeys(vcpu, tmp);

	/* Call the SSKE KEYOP ioctl on each page and verify the result */
	fault_in_buffer(vcpu, fault_in, 2);
	for (i = 0; i < BUF_PAGES; i++) {
		expected[i] = ~tmp[i] & KEY_BITS_ALL;
		/* Set the new storage keys to be the bit-inversion of the previous ones */
		old[i] = do_keyop(vcpu, KVM_S390_KEYOP_SSKE, i, expected[i] | 1);
		if (i == BUF_PAGES / 2)
			fault_in_buffer(vcpu, fault_in, 3);
	}
	compare(old, tmp, "Old value mismatch", fault_in);

	/* Verify that the storage keys have been set correctly */
	fault_in_buffer(vcpu, fault_in, 4);
	get_skeys(vcpu, tmp);
	compare(tmp, expected, "New value mismatch", fault_in);
}

static struct testdef {
	const char *name;
	void (*test)(struct kvm_vcpu *vcpu, int fault_in_location);
	int n_fault_in_locations;
} testplan[] = {
	{ "Initialization", test_init, 5 },
	{ "RRBE", test_rrbe, 5 },
	{ "ISKE", test_iske, 5 },
	{ "SSKE", test_sske, 5 },
};

static void run_test(void (*the_test)(struct kvm_vcpu *, int), int fault_in_location)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int r;

	vm = vm_create_barebones();
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS, 0, 0, GUEST_PAGES, 0);
	vcpu = __vm_vcpu_add(vm, 0);

	r = _get_skeys(vcpu, tmp);
	TEST_ASSERT(r == KVM_S390_GET_SKEYS_NONE,
		    "Storage keys are not disabled initially, r=%d", r);

	clear_all();

	the_test(vcpu, fault_in_location);

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	int i, f;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_S390_KEYOP));
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_S390_UCONTROL));

	ksft_print_header();
	for (i = 0, f = 0; i < ARRAY_SIZE(testplan); i++)
		f += testplan[i].n_fault_in_locations;
	ksft_set_plan(f);

	for (i = 0; i < ARRAY_SIZE(testplan); i++) {
		for (f = 0; f < testplan[i].n_fault_in_locations; f++) {
			run_test(testplan[i].test, f);
			ksft_test_result_pass("%s (fault-in location %d)\n", testplan[i].name, f);
		}
	}

	ksft_finished();	/* Print results and exit() accordingly */
}
