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
#include <getopt.h>

#include "hwprobe.h"
#include "kselftest.h"

#define MK_CBO(fn) le32_bswap((uint32_t)(fn) << 20 | 10 << 15 | 2 << 12 | 0 << 7 | 15)
#define MK_PREFETCH(fn) \
	le32_bswap(0 << 25 | (uint32_t)(fn) << 20 | 10 << 15 | 6 << 12 | 0 << 7 | 19)

static char mem[4096] __aligned(4096) = { [0 ... 4095] = 0xa5 };

static bool got_fault;

static void fault_handler(int sig, siginfo_t *info, void *context)
{
	unsigned long *regs = (unsigned long *)&((ucontext_t *)context)->uc_mcontext;
	uint32_t insn = *(uint32_t *)regs[0];

	if (sig == SIGILL)
		assert(insn == MK_CBO(regs[11]));

	if (sig == SIGSEGV || sig == SIGBUS)
		assert(insn == MK_PREFETCH(regs[11]));

	got_fault = true;
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

#define prefetch_insn(base, fn)							\
({										\
	asm volatile(								\
	"mv	a0, %0\n"							\
	"li	a1, %1\n"							\
	".4byte	%2\n"								\
	: : "r" (base), "i" (fn), "i" (MK_PREFETCH(fn)) : "a0", "a1");		\
})

static void cbo_inval(char *base) { cbo_insn(base, 0); }
static void cbo_clean(char *base) { cbo_insn(base, 1); }
static void cbo_flush(char *base) { cbo_insn(base, 2); }
static void cbo_zero(char *base)  { cbo_insn(base, 4); }
static void prefetch_i(char *base) { prefetch_insn(base, 0); }
static void prefetch_r(char *base) { prefetch_insn(base, 1); }
static void prefetch_w(char *base) { prefetch_insn(base, 3); }

static void test_no_cbo_inval(void *arg)
{
	ksft_print_msg("Testing cbo.inval instruction remain privileged\n");
	got_fault = false;
	cbo_inval(&mem[0]);
	ksft_test_result(got_fault, "No cbo.inval\n");
}

static void test_no_zicbom(void *arg)
{
	ksft_print_msg("Testing Zicbom instructions remain privileged\n");

	got_fault = false;
	cbo_clean(&mem[0]);
	ksft_test_result(got_fault, "No cbo.clean\n");

	got_fault = false;
	cbo_flush(&mem[0]);
	ksft_test_result(got_fault, "No cbo.flush\n");
}

static void test_no_zicboz(void *arg)
{
	ksft_print_msg("No Zicboz, testing cbo.zero remains privileged\n");

	got_fault = false;
	cbo_zero(&mem[0]);
	ksft_test_result(got_fault, "No cbo.zero\n");
}

static bool is_power_of_2(__u64 n)
{
	return n != 0 && (n & (n - 1)) == 0;
}

static void test_zicbop(void *arg)
{
	struct riscv_hwprobe pair = {
		.key = RISCV_HWPROBE_KEY_ZICBOP_BLOCK_SIZE,
	};
	struct sigaction act = {
		.sa_sigaction = &fault_handler,
		.sa_flags = SA_SIGINFO
	};
	struct sigaction dfl = {
		.sa_handler = SIG_DFL
	};
	cpu_set_t *cpus = (cpu_set_t *)arg;
	__u64 block_size;
	long rc;

	rc = sigaction(SIGSEGV, &act, NULL);
	assert(rc == 0);
	rc = sigaction(SIGBUS, &act, NULL);
	assert(rc == 0);

	rc = riscv_hwprobe(&pair, 1, sizeof(cpu_set_t), (unsigned long *)cpus, 0);
	block_size = pair.value;
	ksft_test_result(rc == 0 && pair.key == RISCV_HWPROBE_KEY_ZICBOP_BLOCK_SIZE &&
			 is_power_of_2(block_size), "Zicbop block size\n");
	ksft_print_msg("Zicbop block size: %llu\n", block_size);

	got_fault = false;
	prefetch_i(&mem[0]);
	prefetch_r(&mem[0]);
	prefetch_w(&mem[0]);
	ksft_test_result(!got_fault, "Zicbop prefetch.* on valid address\n");

	got_fault = false;
	prefetch_i(NULL);
	prefetch_r(NULL);
	prefetch_w(NULL);
	ksft_test_result(!got_fault, "Zicbop prefetch.* on NULL\n");

	rc = sigaction(SIGBUS, &dfl, NULL);
	assert(rc == 0);
	rc = sigaction(SIGSEGV, &dfl, NULL);
	assert(rc == 0);
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

	got_fault = false;
	cbo_clean(&mem[block_size]);
	ksft_test_result(!got_fault, "cbo.clean\n");

	got_fault = false;
	cbo_flush(&mem[block_size]);
	ksft_test_result(!got_fault, "cbo.flush\n");
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

	got_fault = false;
	cbo_zero(&mem[block_size]);
	ksft_test_result(!got_fault, "cbo.zero\n");

	if (got_fault || !is_power_of_2(block_size)) {
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

		switch (cbo) {
		case RISCV_HWPROBE_EXT_ZICBOZ:
			cbostr = "Zicboz";
			break;
		case RISCV_HWPROBE_EXT_ZICBOM:
			cbostr = "Zicbom";
			break;
		case RISCV_HWPROBE_EXT_ZICBOP:
			cbostr = "Zicbop";
			break;
		default:
			ksft_exit_fail_msg("Internal error: invalid cbo %llu\n", cbo);
		}

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
	TEST_ZICBOP,
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
	[TEST_ZICBOP]		= { .nr_tests = 3, test_zicbop },
};

static const struct option long_opts[] = {
	{"zicbom-raises-sigill", no_argument, 0, 'm'},
	{"zicboz-raises-sigill", no_argument, 0, 'z'},
	{0, 0, 0, 0}
};

int main(int argc, char **argv)
{
	struct sigaction act = {
		.sa_sigaction = &fault_handler,
		.sa_flags = SA_SIGINFO,
	};
	struct riscv_hwprobe pair;
	unsigned int plan = 0;
	cpu_set_t cpus;
	long rc;
	int i, opt, long_index;

	long_index = 0;

	while ((opt = getopt_long(argc, argv, "mz", long_opts, &long_index)) != -1) {
		switch (opt) {
		case 'm':
			tests[TEST_NO_ZICBOM].enabled = true;
			tests[TEST_NO_CBO_INVAL].enabled = true;
			rc = sigaction(SIGILL, &act, NULL);
			assert(rc == 0);
			break;
		case 'z':
			tests[TEST_NO_ZICBOZ].enabled = true;
			tests[TEST_NO_CBO_INVAL].enabled = true;
			rc = sigaction(SIGILL, &act, NULL);
			assert(rc == 0);
			break;
		case '?':
			fprintf(stderr,
				"Usage: %s [--zicbom-raises-sigill|-m] [--zicboz-raises-sigill|-z]\n",
				argv[0]);
			exit(1);
		default:
			break;
		}
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

	if (pair.value & RISCV_HWPROBE_EXT_ZICBOP)
		tests[TEST_ZICBOP].enabled = true;
	else
		check_no_zicbo_cpus(&cpus, RISCV_HWPROBE_EXT_ZICBOP);

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
