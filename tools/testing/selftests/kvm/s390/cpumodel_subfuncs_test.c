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

#define PLO_FUNCTION_MAX 256

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

static inline int plo_test_bit(unsigned char nr)
{
	unsigned long function = nr | 0x100;
	int cc;

	asm volatile("	lgr	0,%[function]\n"
			/* Parameter registers are ignored for "test bit" */
			"	plo	0,0,0,0(0)\n"
			"	ipm	%0\n"
			"	srl	%0,28\n"
			: "=d" (cc)
			: [function] "d" (function)
			: "cc", "0");
	return cc == 0;
}

/* Testing Perform Locked Operation (PLO) CPU subfunction's ASM block */
static void test_plo_asm_block(u8 (*query)[32])
{
	for (int i = 0; i < PLO_FUNCTION_MAX; ++i) {
		if (plo_test_bit(i))
			(*query)[i >> 3] |= 0x80 >> (i & 7);
	}
}

/* Testing Crypto Compute Message Authentication Code (KMAC) CPU subfunction's ASM block */
static void test_kmac_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rre,0xb91e0000,0,2\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
}

/* Testing Crypto Cipher Message with Chaining (KMC) CPU subfunction's ASM block */
static void test_kmc_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rre,0xb92f0000,2,4\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
}

/* Testing Crypto Cipher Message (KM) CPU subfunction's ASM block */
static void test_km_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rre,0xb92e0000,2,4\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
}

/* Testing Crypto Compute Intermediate Message Digest (KIMD) CPU subfunction's ASM block */
static void test_kimd_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rre,0xb93e0000,0,2\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
}

/* Testing Crypto Compute Last Message Digest (KLMD) CPU subfunction's ASM block */
static void test_klmd_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rre,0xb93f0000,0,2\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
}

/* Testing Crypto Cipher Message with Counter (KMCTR) CPU subfunction's ASM block */
static void test_kmctr_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rrf,0xb92d0000,2,4,6,0\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
}

/* Testing Crypto Cipher Message with Cipher Feedback (KMF) CPU subfunction's ASM block */
static void test_kmf_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rre,0xb92a0000,2,4\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
}

/* Testing Crypto Cipher Message with Output Feedback (KMO) CPU subfunction's ASM block */
static void test_kmo_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rre,0xb92b0000,2,4\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
}

/* Testing Crypto Perform Cryptographic Computation (PCC) CPU subfunction's ASM block */
static void test_pcc_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rre,0xb92c0000,0,0\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
}

/* Testing Crypto Perform Random Number Operation (PRNO) CPU subfunction's ASM block */
static void test_prno_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rre,0xb93c0000,2,4\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
}

/* Testing Crypto Cipher Message with Authentication (KMA) CPU subfunction's ASM block */
static void test_kma_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rrf,0xb9290000,2,4,6,0\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
}

/* Testing Crypto Compute Digital Signature Authentication (KDSA) CPU subfunction's ASM block */
static void test_kdsa_asm_block(u8 (*query)[16])
{
	asm volatile("	la	%%r1,%[query]\n"
			"	xgr	%%r0,%%r0\n"
			"	.insn	rre,0xb93a0000,0,2\n"
			: [query] "=R" (*query)
			:
			: "cc", "r0", "r1");
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

/*
 * Testing Perform Function with Concurrent Results (PFCR)
 * CPU subfunctions's ASM block
 */
static void test_pfcr_asm_block(u8 (*query)[16])
{
	asm volatile("	lghi	0,0\n"
			"	.insn   rsy,0xeb0000000016,0,0,%[query]\n"
			: [query] "=QS" (*query)
			:
			: "cc", "0");
}

typedef void (*testfunc_t)(u8 (*array)[]);

struct testdef {
	const char *subfunc_name;
	u8 *subfunc_array;
	size_t array_size;
	testfunc_t test;
	int facility_bit;
} testlist[] = {
	/*
	 * PLO was introduced in the very first 64-bit machine generation.
	 * Hence it is assumed PLO is always installed in Z Arch.
	 */
	{ "PLO", cpu_subfunc.plo, sizeof(cpu_subfunc.plo), test_plo_asm_block, 1 },
	/* MSA - Facility bit 17 */
	{ "KMAC", cpu_subfunc.kmac, sizeof(cpu_subfunc.kmac), test_kmac_asm_block, 17 },
	{ "KMC", cpu_subfunc.kmc, sizeof(cpu_subfunc.kmc), test_kmc_asm_block, 17 },
	{ "KM", cpu_subfunc.km, sizeof(cpu_subfunc.km), test_km_asm_block, 17 },
	{ "KIMD", cpu_subfunc.kimd, sizeof(cpu_subfunc.kimd), test_kimd_asm_block, 17 },
	{ "KLMD", cpu_subfunc.klmd, sizeof(cpu_subfunc.klmd), test_klmd_asm_block, 17 },
	/* MSA - Facility bit 77 */
	{ "KMCTR", cpu_subfunc.kmctr, sizeof(cpu_subfunc.kmctr), test_kmctr_asm_block, 77 },
	{ "KMF", cpu_subfunc.kmf, sizeof(cpu_subfunc.kmf), test_kmf_asm_block, 77 },
	{ "KMO", cpu_subfunc.kmo, sizeof(cpu_subfunc.kmo), test_kmo_asm_block, 77 },
	{ "PCC", cpu_subfunc.pcc, sizeof(cpu_subfunc.pcc), test_pcc_asm_block, 77 },
	/* MSA5 - Facility bit 57 */
	{ "PPNO", cpu_subfunc.ppno, sizeof(cpu_subfunc.ppno), test_prno_asm_block, 57 },
	/* MSA8 - Facility bit 146 */
	{ "KMA", cpu_subfunc.kma, sizeof(cpu_subfunc.kma), test_kma_asm_block, 146 },
	/* MSA9 - Facility bit 155 */
	{ "KDSA", cpu_subfunc.kdsa, sizeof(cpu_subfunc.kdsa), test_kdsa_asm_block, 155 },
	/* SORTL - Facility bit 150 */
	{ "SORTL", cpu_subfunc.sortl, sizeof(cpu_subfunc.sortl), test_sortl_asm_block, 150 },
	/* DFLTCC - Facility bit 151 */
	{ "DFLTCC", cpu_subfunc.dfltcc, sizeof(cpu_subfunc.dfltcc), test_dfltcc_asm_block, 151 },
	/* Concurrent-function facility - Facility bit 201 */
	{ "PFCR", cpu_subfunc.pfcr, sizeof(cpu_subfunc.pfcr), test_pfcr_asm_block, 201 },
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
