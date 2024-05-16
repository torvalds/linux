// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Ventana Micro Systems Inc.
 *
 * Test the RISCV_HWPROBE_WHICH_CPUS flag of hwprobe. Also provides a command
 * line interface to get the cpu list for arbitrary hwprobe pairs.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>
#include <assert.h>

#include "hwprobe.h"
#include "../../kselftest.h"

static void help(void)
{
	printf("\n"
	       "which-cpus: [-h] [<key=value> [<key=value> ...]]\n\n"
	       "   Without parameters, tests the RISCV_HWPROBE_WHICH_CPUS flag of hwprobe.\n"
	       "   With parameters, where each parameter is a hwprobe pair written as\n"
	       "   <key=value>, outputs the cpulist for cpus which all match the given set\n"
	       "   of pairs.  'key' and 'value' should be in numeric form, e.g. 4=0x3b\n");
}

static void print_cpulist(cpu_set_t *cpus)
{
	int start = 0, end = 0;

	if (!CPU_COUNT(cpus)) {
		printf("cpus: None\n");
		return;
	}

	printf("cpus:");
	for (int i = 0, c = 0; i < CPU_COUNT(cpus); i++, c++) {
		if (start != end && !CPU_ISSET(c, cpus))
			printf("-%d", end);

		while (!CPU_ISSET(c, cpus))
			++c;

		if (i != 0 && c == end + 1) {
			end = c;
			continue;
		}

		printf("%c%d", i == 0 ? ' ' : ',', c);
		start = end = c;
	}
	if (start != end)
		printf("-%d", end);
	printf("\n");
}

static void do_which_cpus(int argc, char **argv, cpu_set_t *cpus)
{
	struct riscv_hwprobe *pairs;
	int nr_pairs = argc - 1;
	char *start, *end;
	int rc;

	pairs = malloc(nr_pairs * sizeof(struct riscv_hwprobe));
	assert(pairs);

	for (int i = 0; i < nr_pairs; i++) {
		start = argv[i + 1];
		pairs[i].key = strtol(start, &end, 0);
		assert(end != start && *end == '=');
		start = end + 1;
		pairs[i].value = strtoul(start, &end, 0);
		assert(end != start && *end == '\0');
	}

	rc = riscv_hwprobe(pairs, nr_pairs, sizeof(cpu_set_t), (unsigned long *)cpus, RISCV_HWPROBE_WHICH_CPUS);
	assert(rc == 0);
	print_cpulist(cpus);
	free(pairs);
}

int main(int argc, char **argv)
{
	struct riscv_hwprobe pairs[2];
	cpu_set_t cpus_aff, cpus;
	__u64 ext0_all;
	long rc;

	rc = sched_getaffinity(0, sizeof(cpu_set_t), &cpus_aff);
	assert(rc == 0);

	if (argc > 1) {
		if (!strcmp(argv[1], "-h"))
			help();
		else
			do_which_cpus(argc, argv, &cpus_aff);
		return 0;
	}

	ksft_print_header();
	ksft_set_plan(7);

	pairs[0] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_BASE_BEHAVIOR, };
	rc = riscv_hwprobe(pairs, 1, 0, NULL, 0);
	assert(rc == 0 && pairs[0].key == RISCV_HWPROBE_KEY_BASE_BEHAVIOR &&
	       pairs[0].value == RISCV_HWPROBE_BASE_BEHAVIOR_IMA);

	pairs[0] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_IMA_EXT_0, };
	rc = riscv_hwprobe(pairs, 1, 0, NULL, 0);
	assert(rc == 0 && pairs[0].key == RISCV_HWPROBE_KEY_IMA_EXT_0);
	ext0_all = pairs[0].value;

	pairs[0] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_BASE_BEHAVIOR, .value = RISCV_HWPROBE_BASE_BEHAVIOR_IMA, };
	CPU_ZERO(&cpus);
	rc = riscv_hwprobe(pairs, 1, 0, (unsigned long *)&cpus, RISCV_HWPROBE_WHICH_CPUS);
	ksft_test_result(rc == -EINVAL, "no cpusetsize\n");

	pairs[0] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_BASE_BEHAVIOR, .value = RISCV_HWPROBE_BASE_BEHAVIOR_IMA, };
	rc = riscv_hwprobe(pairs, 1, sizeof(cpu_set_t), NULL, RISCV_HWPROBE_WHICH_CPUS);
	ksft_test_result(rc == -EINVAL, "NULL cpus\n");

	pairs[0] = (struct riscv_hwprobe){ .key = 0xbadc0de, };
	CPU_ZERO(&cpus);
	rc = riscv_hwprobe(pairs, 1, sizeof(cpu_set_t), (unsigned long *)&cpus, RISCV_HWPROBE_WHICH_CPUS);
	ksft_test_result(rc == 0 && CPU_COUNT(&cpus) == 0, "unknown key\n");

	pairs[0] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_BASE_BEHAVIOR, .value = RISCV_HWPROBE_BASE_BEHAVIOR_IMA, };
	pairs[1] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_BASE_BEHAVIOR, .value = RISCV_HWPROBE_BASE_BEHAVIOR_IMA, };
	CPU_ZERO(&cpus);
	rc = riscv_hwprobe(pairs, 2, sizeof(cpu_set_t), (unsigned long *)&cpus, RISCV_HWPROBE_WHICH_CPUS);
	ksft_test_result(rc == 0, "duplicate keys\n");

	pairs[0] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_BASE_BEHAVIOR, .value = RISCV_HWPROBE_BASE_BEHAVIOR_IMA, };
	pairs[1] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_IMA_EXT_0, .value = ext0_all, };
	CPU_ZERO(&cpus);
	rc = riscv_hwprobe(pairs, 2, sizeof(cpu_set_t), (unsigned long *)&cpus, RISCV_HWPROBE_WHICH_CPUS);
	ksft_test_result(rc == 0 && CPU_COUNT(&cpus) == sysconf(_SC_NPROCESSORS_ONLN), "set all cpus\n");

	pairs[0] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_BASE_BEHAVIOR, .value = RISCV_HWPROBE_BASE_BEHAVIOR_IMA, };
	pairs[1] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_IMA_EXT_0, .value = ext0_all, };
	memcpy(&cpus, &cpus_aff, sizeof(cpu_set_t));
	rc = riscv_hwprobe(pairs, 2, sizeof(cpu_set_t), (unsigned long *)&cpus, RISCV_HWPROBE_WHICH_CPUS);
	ksft_test_result(rc == 0 && CPU_EQUAL(&cpus, &cpus_aff), "set all affinity cpus\n");

	pairs[0] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_BASE_BEHAVIOR, .value = RISCV_HWPROBE_BASE_BEHAVIOR_IMA, };
	pairs[1] = (struct riscv_hwprobe){ .key = RISCV_HWPROBE_KEY_IMA_EXT_0, .value = ~ext0_all, };
	memcpy(&cpus, &cpus_aff, sizeof(cpu_set_t));
	rc = riscv_hwprobe(pairs, 2, sizeof(cpu_set_t), (unsigned long *)&cpus, RISCV_HWPROBE_WHICH_CPUS);
	ksft_test_result(rc == 0 && CPU_COUNT(&cpus) == 0, "clear all cpus\n");

	ksft_finished();
}
