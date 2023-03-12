// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <inttypes.h>

#include "../kselftest.h"

#ifndef __x86_64__
# error This test is 64-bit only
#endif

/* LAM modes, these definitions were copied from kernel code */
#define LAM_NONE                0
#define LAM_U57_BITS            6

#define LAM_U57_MASK            (0x3fULL << 57)
/* arch prctl for LAM */
#define ARCH_GET_UNTAG_MASK     0x4001
#define ARCH_ENABLE_TAGGED_ADDR 0x4002
#define ARCH_GET_MAX_TAG_BITS   0x4003

/* Specified test function bits */
#define FUNC_MALLOC             0x1
#define FUNC_BITS               0x2
#define FUNC_MMAP               0x4
#define FUNC_SYSCALL            0x8

#define TEST_MASK               0xf

#define LOW_ADDR                (0x1UL << 30)
#define HIGH_ADDR               (0x3UL << 48)

#define MALLOC_LEN              32

#define PAGE_SIZE               (4 << 10)

struct testcases {
	unsigned int later;
	int expected; /* 2: SIGSEGV Error; 1: other errors */
	unsigned long lam;
	uint64_t addr;
	int (*test_func)(struct testcases *test);
	const char *msg;
};

int tests_cnt;
jmp_buf segv_env;

static void segv_handler(int sig)
{
	ksft_print_msg("Get segmentation fault(%d).", sig);

	siglongjmp(segv_env, 1);
}

static inline int cpu_has_lam(void)
{
	unsigned int cpuinfo[4];

	__cpuid_count(0x7, 1, cpuinfo[0], cpuinfo[1], cpuinfo[2], cpuinfo[3]);

	return (cpuinfo[0] & (1 << 26));
}

/* Check 5-level page table feature in CPUID.(EAX=07H, ECX=00H):ECX.[bit 16] */
static inline int cpu_has_la57(void)
{
	unsigned int cpuinfo[4];

	__cpuid_count(0x7, 0, cpuinfo[0], cpuinfo[1], cpuinfo[2], cpuinfo[3]);

	return (cpuinfo[2] & (1 << 16));
}

/*
 * Set tagged address and read back untag mask.
 * check if the untagged mask is expected.
 *
 * @return:
 * 0: Set LAM mode successfully
 * others: failed to set LAM
 */
static int set_lam(unsigned long lam)
{
	int ret = 0;
	uint64_t ptr = 0;

	if (lam != LAM_U57_BITS && lam != LAM_NONE)
		return -1;

	/* Skip check return */
	syscall(SYS_arch_prctl, ARCH_ENABLE_TAGGED_ADDR, lam);

	/* Get untagged mask */
	syscall(SYS_arch_prctl, ARCH_GET_UNTAG_MASK, &ptr);

	/* Check mask returned is expected */
	if (lam == LAM_U57_BITS)
		ret = (ptr != ~(LAM_U57_MASK));
	else if (lam == LAM_NONE)
		ret = (ptr != -1ULL);

	return ret;
}

static unsigned long get_default_tag_bits(void)
{
	pid_t pid;
	int lam = LAM_NONE;
	int ret = 0;

	pid = fork();
	if (pid < 0) {
		perror("Fork failed.");
	} else if (pid == 0) {
		/* Set LAM mode in child process */
		if (set_lam(LAM_U57_BITS) == 0)
			lam = LAM_U57_BITS;
		else
			lam = LAM_NONE;
		exit(lam);
	} else {
		wait(&ret);
		lam = WEXITSTATUS(ret);
	}

	return lam;
}

/* According to LAM mode, set metadata in high bits */
static uint64_t set_metadata(uint64_t src, unsigned long lam)
{
	uint64_t metadata;

	srand(time(NULL));

	switch (lam) {
	case LAM_U57_BITS: /* Set metadata in bits 62:57 */
		/* Get a random non-zero value as metadata */
		metadata = (rand() % ((1UL << LAM_U57_BITS) - 1) + 1) << 57;
		metadata |= (src & ~(LAM_U57_MASK));
		break;
	default:
		metadata = src;
		break;
	}

	return metadata;
}

/*
 * Set metadata in user pointer, compare new pointer with original pointer.
 * both pointers should point to the same address.
 *
 * @return:
 * 0: value on the pointer with metadate and value on original are same
 * 1: not same.
 */
static int handle_lam_test(void *src, unsigned int lam)
{
	char *ptr;

	strcpy((char *)src, "USER POINTER");

	ptr = (char *)set_metadata((uint64_t)src, lam);
	if (src == ptr)
		return 0;

	/* Copy a string into the pointer with metadata */
	strcpy((char *)ptr, "METADATA POINTER");

	return (!!strcmp((char *)src, (char *)ptr));
}


int handle_max_bits(struct testcases *test)
{
	unsigned long exp_bits = get_default_tag_bits();
	unsigned long bits = 0;

	if (exp_bits != LAM_NONE)
		exp_bits = LAM_U57_BITS;

	/* Get LAM max tag bits */
	if (syscall(SYS_arch_prctl, ARCH_GET_MAX_TAG_BITS, &bits) == -1)
		return 1;

	return (exp_bits != bits);
}

/*
 * Test lam feature through dereference pointer get from malloc.
 * @return 0: Pass test. 1: Get failure during test 2: Get SIGSEGV
 */
static int handle_malloc(struct testcases *test)
{
	char *ptr = NULL;
	int ret = 0;

	if (test->later == 0 && test->lam != 0)
		if (set_lam(test->lam) == -1)
			return 1;

	ptr = (char *)malloc(MALLOC_LEN);
	if (ptr == NULL) {
		perror("malloc() failure\n");
		return 1;
	}

	/* Set signal handler */
	if (sigsetjmp(segv_env, 1) == 0) {
		signal(SIGSEGV, segv_handler);
		ret = handle_lam_test(ptr, test->lam);
	} else {
		ret = 2;
	}

	if (test->later != 0 && test->lam != 0)
		if (set_lam(test->lam) == -1 && ret == 0)
			ret = 1;

	free(ptr);

	return ret;
}

static int handle_mmap(struct testcases *test)
{
	void *ptr;
	unsigned int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
	int ret = 0;

	if (test->later == 0 && test->lam != 0)
		if (set_lam(test->lam) != 0)
			return 1;

	ptr = mmap((void *)test->addr, PAGE_SIZE, PROT_READ | PROT_WRITE,
		   flags, -1, 0);
	if (ptr == MAP_FAILED) {
		if (test->addr == HIGH_ADDR)
			if (!cpu_has_la57())
				return 3; /* unsupport LA57 */
		return 1;
	}

	if (test->later != 0 && test->lam != 0)
		if (set_lam(test->lam) != 0)
			ret = 1;

	if (ret == 0) {
		if (sigsetjmp(segv_env, 1) == 0) {
			signal(SIGSEGV, segv_handler);
			ret = handle_lam_test(ptr, test->lam);
		} else {
			ret = 2;
		}
	}

	munmap(ptr, PAGE_SIZE);
	return ret;
}

static int handle_syscall(struct testcases *test)
{
	struct utsname unme, *pu;
	int ret = 0;

	if (test->later == 0 && test->lam != 0)
		if (set_lam(test->lam) != 0)
			return 1;

	if (sigsetjmp(segv_env, 1) == 0) {
		signal(SIGSEGV, segv_handler);
		pu = (struct utsname *)set_metadata((uint64_t)&unme, test->lam);
		ret = uname(pu);
		if (ret < 0)
			ret = 1;
	} else {
		ret = 2;
	}

	if (test->later != 0 && test->lam != 0)
		if (set_lam(test->lam) != -1 && ret == 0)
			ret = 1;

	return ret;
}

static int fork_test(struct testcases *test)
{
	int ret, child_ret;
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		perror("Fork failed.");
		ret = 1;
	} else if (pid == 0) {
		ret = test->test_func(test);
		exit(ret);
	} else {
		wait(&child_ret);
		ret = WEXITSTATUS(child_ret);
	}

	return ret;
}

static void run_test(struct testcases *test, int count)
{
	int i, ret = 0;

	for (i = 0; i < count; i++) {
		struct testcases *t = test + i;

		/* fork a process to run test case */
		tests_cnt++;
		ret = fork_test(t);

		/* return 3 is not support LA57, the case should be skipped */
		if (ret == 3) {
			ksft_test_result_skip(t->msg);
			continue;
		}

		if (ret != 0)
			ret = (t->expected == ret);
		else
			ret = !(t->expected);

		ksft_test_result(ret, t->msg);
	}
}

static struct testcases malloc_cases[] = {
	{
		.later = 0,
		.lam = LAM_U57_BITS,
		.test_func = handle_malloc,
		.msg = "MALLOC: LAM_U57. Dereferencing pointer with metadata\n",
	},
	{
		.later = 1,
		.expected = 2,
		.lam = LAM_U57_BITS,
		.test_func = handle_malloc,
		.msg = "MALLOC:[Negative] Disable LAM. Dereferencing pointer with metadata.\n",
	},
};

static struct testcases bits_cases[] = {
	{
		.test_func = handle_max_bits,
		.msg = "BITS: Check default tag bits\n",
	},
};

static struct testcases syscall_cases[] = {
	{
		.later = 0,
		.lam = LAM_U57_BITS,
		.test_func = handle_syscall,
		.msg = "SYSCALL: LAM_U57. syscall with metadata\n",
	},
	{
		.later = 1,
		.expected = 1,
		.lam = LAM_U57_BITS,
		.test_func = handle_syscall,
		.msg = "SYSCALL:[Negative] Disable LAM. Dereferencing pointer with metadata.\n",
	},
};

static struct testcases mmap_cases[] = {
	{
		.later = 1,
		.expected = 0,
		.lam = LAM_U57_BITS,
		.addr = HIGH_ADDR,
		.test_func = handle_mmap,
		.msg = "MMAP: First mmap high address, then set LAM_U57.\n",
	},
	{
		.later = 0,
		.expected = 0,
		.lam = LAM_U57_BITS,
		.addr = HIGH_ADDR,
		.test_func = handle_mmap,
		.msg = "MMAP: First LAM_U57, then High address.\n",
	},
	{
		.later = 0,
		.expected = 0,
		.lam = LAM_U57_BITS,
		.addr = LOW_ADDR,
		.test_func = handle_mmap,
		.msg = "MMAP: First LAM_U57, then Low address.\n",
	},
};

static void cmd_help(void)
{
	printf("usage: lam [-h] [-t test list]\n");
	printf("\t-t test list: run tests specified in the test list, default:0x%x\n", TEST_MASK);
	printf("\t\t0x1:malloc; 0x2:max_bits; 0x4:mmap; 0x8:syscall.\n");
	printf("\t-h: help\n");
}

int main(int argc, char **argv)
{
	int c = 0;
	unsigned int tests = TEST_MASK;

	tests_cnt = 0;

	if (!cpu_has_lam()) {
		ksft_print_msg("Unsupported LAM feature!\n");
		return -1;
	}

	while ((c = getopt(argc, argv, "ht:")) != -1) {
		switch (c) {
		case 't':
			tests = strtoul(optarg, NULL, 16);
			if (!(tests & TEST_MASK)) {
				ksft_print_msg("Invalid argument!\n");
				return -1;
			}
			break;
		case 'h':
			cmd_help();
			return 0;
		default:
			ksft_print_msg("Invalid argument\n");
			return -1;
		}
	}

	if (tests & FUNC_MALLOC)
		run_test(malloc_cases, ARRAY_SIZE(malloc_cases));

	if (tests & FUNC_BITS)
		run_test(bits_cases, ARRAY_SIZE(bits_cases));

	if (tests & FUNC_MMAP)
		run_test(mmap_cases, ARRAY_SIZE(mmap_cases));

	if (tests & FUNC_SYSCALL)
		run_test(syscall_cases, ARRAY_SIZE(syscall_cases));

	ksft_set_plan(tests_cnt);

	return ksft_exit_pass();
}
