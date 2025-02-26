// SPDX-License-Identifier: GPL-2.0-only
/*
 * vdso_test_gettimeofday.c: Sample code to test parse_vdso.c and
 *                           vDSO gettimeofday()
 * Copyright (c) 2014 Andy Lutomirski
 *
 * Compile with:
 * gcc -std=gnu99 vdso_test_gettimeofday.c parse_vdso_gettimeofday.c
 *
 * Tested on x86, 32-bit and 64-bit.  It may work on other architectures, too.
 */

#include <stdio.h>
#ifndef NOLIBC
#include <sys/auxv.h>
#include <sys/time.h>
#endif

#include "../kselftest.h"
#include "parse_vdso.h"
#include "vdso_config.h"
#include "vdso_call.h"

int main(int argc, char **argv)
{
	const char *version = versions[VDSO_VERSION];
	const char **name = (const char **)&names[VDSO_NAMES];

	unsigned long sysinfo_ehdr = getauxval(AT_SYSINFO_EHDR);
	if (!sysinfo_ehdr) {
		printf("AT_SYSINFO_EHDR is not present!\n");
		return KSFT_SKIP;
	}

	vdso_init_from_sysinfo_ehdr(getauxval(AT_SYSINFO_EHDR));

	/* Find gettimeofday. */
	typedef long (*gtod_t)(struct timeval *tv, struct timezone *tz);
	gtod_t gtod = (gtod_t)vdso_sym(version, name[0]);

	if (!gtod) {
		printf("Could not find %s\n", name[0]);
		return KSFT_SKIP;
	}

	struct timeval tv;
	long ret = VDSO_CALL(gtod, 2, &tv, 0);

	if (ret == 0) {
		printf("The time is %lld.%06lld\n",
		       (long long)tv.tv_sec, (long long)tv.tv_usec);
	} else {
		printf("%s failed\n", name[0]);
		return KSFT_FAIL;
	}

	return 0;
}
