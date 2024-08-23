// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright IBM Corp. 2024
 *
 * Authors:
 *  Hariharan Mari <hari55@linux.ibm.com>
 *
 * The tests compare the result of the KVM ioctl for obtaining CPU subfunction data with those
 * from an ASM block performing the same CPU subfunction. Currently KVM doesn't mask instruction
 * query data reported via the CPU Model, allowing us to directly compare it with the data
 * acquired through executing the queries in the test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include "facility.h"

#include "kvm_util.h"

/* Query available CPU subfunctions */
struct kvm_s390_vm_cpu_subfunc cpu_subfunc;

static void get_cpu_machine_subfuntions(struct kvm_vm *vm,
					struct kvm_s390_vm_cpu_subfunc *cpu_subfunc)
{
	int r;

	r = __kvm_device_attr_get(vm->fd, KVM_S390_VM_CPU_MODEL,
				  KVM_S390_VM_CPU_MACHINE_SUBFUNC, cpu_subfunc);

	TEST_ASSERT(!r, "Get cpu subfunctions failed r=%d errno=%d", r, errno);
}

/* Testing Sort Lists (SORTL) CPU subfunction's ASM block */
static void test_sortl_asm_block(u8 (*query)[32])
{
	asm volatile("	lghi	0,0\n"
			"	la	1,%[query]\n"
			"	.insn	rre,0xb9380000,2,4\n"
			: [query] "=R" (*query)
			:
			: "cc", "0", "1");
}

/* Testing Deflate Conversion Call (DFLTCC) CPU subfunction's ASM block */
static void test_dfltcc_asm_block(u8 (*query)[32])
{
	asm volatile("	lghi	0,0\n"
			"	la	1,%[query]\n"
			"	.insn	rrf,0xb9390000,2,4,6,0\n"
			: [query] "=R" (*query)
			:
			: "cc", "0", "1");
}

typedef void (*testfunc_t)(u8 (*array)[]);

struct testdef {
	const char *subfunc_name;
	u8 *subfunc_array;
	size_t array_size;
	testfunc_t test;
	int facility_bit;
} testlist[] = {
	/* SORTL - Facility bit 150 */
	{ "SORTL", cpu_subfunc.sortl, sizeof(cpu_subfunc.sortl), test_sortl_asm_block, 150 },
	/* DFLTCC - Facility bit 151 */
	{ "DFLTCC", cpu_subfunc.dfltcc, sizeof(cpu_subfunc.dfltcc), test_dfltcc_asm_block, 151 },
};

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	int idx;

	ksft_print_header();

	vm = vm_create(1);

	memset(&cpu_subfunc, 0, sizeof(cpu_subfunc));
	get_cpu_machine_subfuntions(vm, &cpu_subfunc);

	ksft_set_plan(ARRAY_SIZE(testlist));
	for (idx = 0; idx < ARRAY_SIZE(testlist); idx++) {
		if (test_facility(testlist[idx].facility_bit)) {
			u8 *array = malloc(testlist[idx].array_size);

			testlist[idx].test((u8 (*)[testlist[idx].array_size])array);

			TEST_ASSERT_EQ(memcmp(testlist[idx].subfunc_array,
					      array, testlist[idx].array_size), 0);

			ksft_test_result_pass("%s\n", testlist[idx].subfunc_name);
			free(array);
		} else {
			ksft_test_result_skip("%s feature is not avaialable\n",
					      testlist[idx].subfunc_name);
		}
	}

	kvm_vm_free(vm);
	ksft_finished();
}
