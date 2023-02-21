// SPDX-License-Identifier: GPL-2.0-only
/*
 * vdso_test_getcpu.c: Sample code to test parse_vdso.c and vDSO getcpu()
 *
 * Copyright (c) 2020 Arm Ltd
 */

#include <stdint.h>
#include <elf.h>
#include <stdio.h>
#include <sys/auxv.h>
#include <sys/time.h>

#include "../kselftest.h"
#include "parse_vdso.h"

#if defined(__riscv)
const char *version = "LINUX_4.15";
#else
const char *version = "LINUX_2.6";
#endif
const char *name = "__vdso_getcpu";

struct getcpu_cache;
typedef long (*getcpu_t)(unsigned int *, unsigned int *,
			 struct getcpu_cache *);

int main(int argc, char **argv)
{
	unsigned long sysinfo_ehdr;
	unsigned int cpu, node;
	getcpu_t get_cpu;
	long ret;

	sysinfo_ehdr = getauxval(AT_SYSINFO_EHDR);
	if (!sysinfo_ehdr) {
		printf("AT_SYSINFO_EHDR is not present!\n");
		return KSFT_SKIP;
	}

	vdso_init_from_sysinfo_ehdr(getauxval(AT_SYSINFO_EHDR));

	get_cpu = (getcpu_t)vdso_sym(version, name);
	if (!get_cpu) {
		printf("Could not find %s\n", name);
		return KSFT_SKIP;
	}

	ret = get_cpu(&cpu, &node, 0);
	if (ret == 0) {
		printf("Running on CPU %u node %u\n", cpu, node);
	} else {
		printf("%s failed\n", name);
		return KSFT_FAIL;
	}

	return 0;
}
