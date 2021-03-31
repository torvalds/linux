// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <string.h>

#include "debug.h"
#include "tests/tests.h"
#include "arch-tests.h"
#include "../../../../arch/x86/include/asm/insn.h"

#include "intel-pt-decoder/intel-pt-insn-decoder.h"

struct test_data {
	u8 data[MAX_INSN_SIZE];
	int expected_length;
	int expected_rel;
	const char *expected_op_str;
	const char *expected_branch_str;
	const char *asm_rep;
};

struct test_data test_data_32[] = {
#include "insn-x86-dat-32.c"
	{{0x0f, 0x01, 0xee}, 3, 0, NULL, NULL, "0f 01 ee             \trdpkru"},
	{{0x0f, 0x01, 0xef}, 3, 0, NULL, NULL, "0f 01 ef             \twrpkru"},
	{{0}, 0, 0, NULL, NULL, NULL},
};

struct test_data test_data_64[] = {
#include "insn-x86-dat-64.c"
	{{0x0f, 0x01, 0xee}, 3, 0, NULL, NULL, "0f 01 ee             \trdpkru"},
	{{0x0f, 0x01, 0xef}, 3, 0, NULL, NULL, "0f 01 ef             \twrpkru"},
	{{0}, 0, 0, NULL, NULL, NULL},
};

static int get_op(const char *op_str)
{
	struct val_data {
		const char *name;
		int val;
	} vals[] = {
		{"other",   INTEL_PT_OP_OTHER},
		{"call",    INTEL_PT_OP_CALL},
		{"ret",     INTEL_PT_OP_RET},
		{"jcc",     INTEL_PT_OP_JCC},
		{"jmp",     INTEL_PT_OP_JMP},
		{"loop",    INTEL_PT_OP_LOOP},
		{"iret",    INTEL_PT_OP_IRET},
		{"int",     INTEL_PT_OP_INT},
		{"syscall", INTEL_PT_OP_SYSCALL},
		{"sysret",  INTEL_PT_OP_SYSRET},
		{"vmentry",  INTEL_PT_OP_VMENTRY},
		{NULL, 0},
	};
	struct val_data *val;

	if (!op_str || !strlen(op_str))
		return 0;

	for (val = vals; val->name; val++) {
		if (!strcmp(val->name, op_str))
			return val->val;
	}

	pr_debug("Failed to get op\n");

	return -1;
}

static int get_branch(const char *branch_str)
{
	struct val_data {
		const char *name;
		int val;
	} vals[] = {
		{"no_branch",     INTEL_PT_BR_NO_BRANCH},
		{"indirect",      INTEL_PT_BR_INDIRECT},
		{"conditional",   INTEL_PT_BR_CONDITIONAL},
		{"unconditional", INTEL_PT_BR_UNCONDITIONAL},
		{NULL, 0},
	};
	struct val_data *val;

	if (!branch_str || !strlen(branch_str))
		return 0;

	for (val = vals; val->name; val++) {
		if (!strcmp(val->name, branch_str))
			return val->val;
	}

	pr_debug("Failed to get branch\n");

	return -1;
}

static int test_data_item(struct test_data *dat, int x86_64)
{
	struct intel_pt_insn intel_pt_insn;
	struct insn insn;
	int op, branch;

	insn_init(&insn, dat->data, MAX_INSN_SIZE, x86_64);
	insn_get_length(&insn);

	if (!insn_complete(&insn)) {
		pr_debug("Failed to decode: %s\n", dat->asm_rep);
		return -1;
	}

	if (insn.length != dat->expected_length) {
		pr_debug("Failed to decode length (%d vs expected %d): %s\n",
			 insn.length, dat->expected_length, dat->asm_rep);
		return -1;
	}

	op = get_op(dat->expected_op_str);
	branch = get_branch(dat->expected_branch_str);

	if (intel_pt_get_insn(dat->data, MAX_INSN_SIZE, x86_64, &intel_pt_insn)) {
		pr_debug("Intel PT failed to decode: %s\n", dat->asm_rep);
		return -1;
	}

	if ((int)intel_pt_insn.op != op) {
		pr_debug("Failed to decode 'op' value (%d vs expected %d): %s\n",
			 intel_pt_insn.op, op, dat->asm_rep);
		return -1;
	}

	if ((int)intel_pt_insn.branch != branch) {
		pr_debug("Failed to decode 'branch' value (%d vs expected %d): %s\n",
			 intel_pt_insn.branch, branch, dat->asm_rep);
		return -1;
	}

	if (intel_pt_insn.rel != dat->expected_rel) {
		pr_debug("Failed to decode 'rel' value (%#x vs expected %#x): %s\n",
			 intel_pt_insn.rel, dat->expected_rel, dat->asm_rep);
		return -1;
	}

	pr_debug("Decoded ok: %s\n", dat->asm_rep);

	return 0;
}

static int test_data_set(struct test_data *dat_set, int x86_64)
{
	struct test_data *dat;
	int ret = 0;

	for (dat = dat_set; dat->expected_length; dat++) {
		if (test_data_item(dat, x86_64))
			ret = -1;
	}

	return ret;
}

/**
 * test__insn_x86 - test x86 instruction decoder - new instructions.
 *
 * This function implements a test that decodes a selection of instructions and
 * checks the results.  The Intel PT function that further categorizes
 * instructions (i.e. intel_pt_get_insn()) is also checked.
 *
 * The instructions are originally in insn-x86-dat-src.c which has been
 * processed by scripts gen-insn-x86-dat.sh and gen-insn-x86-dat.awk to produce
 * insn-x86-dat-32.c and insn-x86-dat-64.c which are included into this program.
 * i.e. to add new instructions to the test, edit insn-x86-dat-src.c, run the
 * gen-insn-x86-dat.sh script, make perf, and then run the test.
 *
 * If the test passes %0 is returned, otherwise %-1 is returned.  Use the
 * verbose (-v) option to see all the instructions and whether or not they
 * decoded successfully.
 */
int test__insn_x86(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	int ret = 0;

	if (test_data_set(test_data_32, 0))
		ret = -1;

	if (test_data_set(test_data_64, 1))
		ret = -1;

	return ret;
}
