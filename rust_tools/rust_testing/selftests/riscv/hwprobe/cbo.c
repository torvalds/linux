// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Ventana Micro Systems Inc.
 *
 * Run with 'taskset -c <cpu-list> cbo' to only execute hwprobe on a
 * subset of cpus, as well as only executing the tests on those cpus.
 */
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sched.h>
#include <signal.h>
#include <assert.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <asm/ucontext.h>

#include "hwprobe.h"
#include "../../kselftest.h"

#define MK_CBO(fn) le32_bswap((uint32_t)(fn) << 20 | 10 << 15 | 2 << 12 | 0 << 7 | 15)

static char mem[4096] __aligned(4096) = { [0 ... 4095] = 0xa5 };

static bool illegal_insn;

static void sigill_handler(int sig, siginfo_t *info, void *context)
{
	unsigned long *regs = (unsigned long *)&((ucontext_t *)context)->uc_mcontext;
	uint32_t insn = *(uint32_t *)regs[0];

	assert(insn == MK_CBO(regs[11]));

	illegal_insn = true;
	regs[0] += 4;
}

#define cbo_insn(base, fn)							\
({										\
	asm volatile(								\
	"mv	a0, %0\n"							\
	"li	a1, %1\n"							\
	".4byte	%2\n"								\
	: : "r" (base), "i" (fn), "i" (MK_CBO(fn)) : "a0", "a1", "memory");	\
})

static void cbo_inval(char *base) { cbo_insn(base, 0); }
static void cbo_clean(char *base) { cbo_insn(base, 1); }
static void cbo_flush(char *base) { cbo_insn(base, 2); }
static void cbo_zero(char *base)  { cbo_insn(base, 4); }

static void test_no_cbo_inval(void *arg)
{
	ksft_print_msg("Testing cbo.inval instruction remain privileged\n");
	illegal_insn = false;
	cbo_inval(&mem[0]);
	ksft_test_result(illegal_insn, "No cbo.inval\n");
}

static void test_no_zicbom(void *arg)
{
	ksft_print_msg("Testing Zicbom instructions remain privileged\n");

	illegal_insn = false;
	cbo_clean(&mem[0]);
	ksft_test_result(illegal_insn, "No cbo.clean\n");

	illegal_insn = false;
	cbo_flush(&mem[0]);
	ksft_test_result(illegal_insn, "No cbo.flush\n");
}

static void test_no_zicboz(void *arg)
{
	ksft_print_msg("No Zicboz, testing cbo.zero remains privileged\n");

	illegal_insn = false;
	cbo_zero(&mem[0]);
	ksft_test_result(illegal_insn, "No cbo.zero\n");
}

static bool is_power_of_2(__u64 n)
{
	return n != 0 && (n & (n - 1)) == 0;
}

static void test_zicbom(void *arg)
{
	struct riscv_hwprobe pair = {
		.key = RISCV_HWPROBE_KEY_ZICBOM_BLOCK_SIZE,
	};
	cpu_set_t *cpus = (cpu_set_t *)arg;
	__u64 block_size;
	long rc;

	rc = riscv_hwprobe(&pair, 1, sizeof(cpu_set_t), (unsigned long *)cpus, 0);
	block_size = pair.value;
	ksft_test_result(rc == 0 && pair.key == RISCV_HWPROBE_KEY_ZICBOM_BLOCK_SIZE &&
			 is_power_of_2(block_size), "Zicbom block size\n");
	ksft_print_msg("Zicbom block size: %llu\n", block_size);

	illegal_insn = false;
	cbo_clean(&mem[block_size]);
	ksft_test_result(!illegal_insn, "cbo.clean\n");

	illegal_insn = false;
	cbo_flush(&mem[block_size]);
	ksft_test_result(!illegal_insn, "cbo.flush\n");
}

static void test_zicboz(void *arg)
{
	struct riscv_hwprobe pair = {
		.key = RISCV_HWPROBE_KEY_ZICBOZ_BLOCK_SIZE,
	};
	cpu_set_t *cpus = (cpu_set_t *)arg;
	__u64 block_size;
	int i, j;
	long rc;

	rc = riscv_hwprobe(&pair, 1, sizeof(cpu_set_t), (unsigned long *)cpus, 0);
	block_size = pair.value;
	ksft_test_result(rc == 0 && pair.key == RISCV_HWPROBE_KEY_ZICBOZ_BLOCK_SIZE &&
			 is_power_of_2(block_size), "Zicboz block size\n");
	ksft_print_msg("Zicboz block size: %llu\n", block_size);

	illegal_insn = false;
	cbo_zero(&mem[block_size]);
	ksft_test_result(!illegal_insn, "cbo.zero\n");

	if (illegal_insn || !is_power_of_2(block_size)) {
		ksft_test_result_skip("cbo.zero check\n");
		return;
	}

	assert(block_size <= 1024);

	for (i = 0; i < 4096 / block_size; ++i) {
		if (i % 2)
			cbo_zero(&mem[i * block_size]);
	}

	for (i = 0; i < 4096 / block_size; ++i) {
		char expected = i % 2 ? 0x0 : 0xa5;

		for (j = 0; j < block_size; ++j) {
			if (mem[i * block_size + j] != expected) {
				ksft_test_result_fail("cbo.zero check\n");
				ksft_print_msg("cbo.zero check: mem[%llu] != 0x%x\n",
					       i * block_size + j, expected);
				return;
			}
		}
	}

	ksft_test_result_pass("cbo.zero check\n");
}

static void check_no_zicbo_cpus(cpu_set_t *cpus, __u64 cbo)
{
	struct riscv_hwprobe pair = {
		.key = RISCV_HWPROBE_KEY_IMA_EXT_0,
	};
	cpu_set_t one_cpu;
	int i = 0, c = 0;
	long rc;
	char *cbostr;

	while (i++ < CPU_COUNT(cpus)) {
		while (!CPU_ISSET(c, cpus))
			++c;

		CPU_ZERO(&one_cpu);
		CPU_SET(c, &one_cpu);

		rc = riscv_hwprobe(&pair, 1, sizeof(cpu_set_t), (unsigned long *)&one_cpu, 0);
		assert(rc == 0 && pair.key == RISCV_HWPROBE_KEY_IMA_EXT_0);

		cbostr = cbo == RISCV_HWPROBE_EXT_ZICBOZ ? "Zicboz" : "Zicbom";

		if (pair.value & cbo)
			ksft_exit_fail_msg("%s is only present on a subset of harts.\n"
					   "Use taskset to select a set of harts where %s\n"
					   "presence (present or not) is consistent for each hart\n",
					   cbostr, cbostr);
		++c;
	}
}

enum {
	TEST_ZICBOZ,
	TEST_NO_ZICBOZ,
	TEST_ZICBOM,
	TEST_NO_ZICBOM,
	TEST_NO_CBO_INVAL,
};

static struct test_info {
	bool enabled;
	unsigned int nr_tests;
	void (*test_fn)(void *arg);
} tests[] = {
	[TEST_ZICBOZ]		= { .nr_tests = 3, test_zicboz },
	[TEST_NO_ZICBOZ]	= { .nr_tests = 1, test_no_zicboz },
	[TEST_ZICBOM]		= { .nr_tests = 3, test_zicbom },
	[TEST_NO_ZICBOM]	= { .nr_tests = 2, test_no_zicbom },
	[TEST_NO_CBO_INVAL]	= { .nr_tests = 1, test_no_cbo_inval },
};

int main(int argc, char **argv)
{
	struct sigaction act = {
		.sa_sigaction = &sigill_handler,
		.sa_flags = SA_SIGINFO,
	};
	struct riscv_hwprobe pair;
	unsigned int plan = 0;
	cpu_set_t cpus;
	long rc;
	int i;

	if (argc > 1 && !strcmp(argv[1], "--sigill")) {
		rc = sigaction(SIGILL, &act, NULL);
		assert(rc == 0);
		tests[TEST_NO_ZICBOZ].enabled = true;
		tests[TEST_NO_ZICBOM].enabled = true;
		tests[TEST_NO_CBO_INVAL].enabled = true;
	}

	rc = sched_getaffinity(0, sizeof(cpu_set_t), &cpus);
	assert(rc == 0);

	ksft_print_header();

	pair.key = RISCV_HWPROBE_KEY_IMA_EXT_0;
	rc = riscv_hwprobe(&pair, 1, sizeof(cpu_set_t), (unsigned long *)&cpus, 0);
	if (rc < 0)
		ksft_exit_fail_msg("hwprobe() failed with %ld\n", rc);
	assert(rc == 0 && pair.key == RISCV_HWPROBE_KEY_IMA_EXT_0);

	if (pair.value & RISCV_HWPROBE_EXT_ZICBOZ) {
		tests[TEST_ZICBOZ].enabled = true;
		tests[TEST_NO_ZICBOZ].enabled = false;
	} else {
		check_no_zicbo_cpus(&cpus, RISCV_HWPROBE_EXT_ZICBOZ);
	}

	if (pair.value & RISCV_HWPROBE_EXT_ZICBOM) {
		tests[TEST_ZICBOM].enabled = true;
		tests[TEST_NO_ZICBOM].enabled = false;
	} else {
		check_no_zicbo_cpus(&cpus, RISCV_HWPROBE_EXT_ZICBOM);
	}

	for (i = 0; i < ARRAY_SIZE(tests); ++i)
		plan += tests[i].enabled ? tests[i].nr_tests : 0;

	if (plan == 0)
		ksft_print_msg("No tests enabled.\n");
	else
		ksft_set_plan(plan);

	for (i = 0; i < ARRAY_SIZE(tests); ++i) {
		if (tests[i].enabled)
			tests[i].test_fn(&cpus);
	}

	ksft_finished();
}
