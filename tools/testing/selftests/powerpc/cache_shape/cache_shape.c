/*
 * Copyright 2017, Michael Ellerman, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

#ifndef AT_L1I_CACHESIZE
#define AT_L1I_CACHESIZE	40
#define AT_L1I_CACHEGEOMETRY	41
#define AT_L1D_CACHESIZE	42
#define AT_L1D_CACHEGEOMETRY	43
#define AT_L2_CACHESIZE		44
#define AT_L2_CACHEGEOMETRY	45
#define AT_L3_CACHESIZE		46
#define AT_L3_CACHEGEOMETRY	47
#endif

static void print_size(const char *label, uint32_t val)
{
	printf("%s cache size: %#10x %10dB %10dK\n", label, val, val, val / 1024);
}

static void print_geo(const char *label, uint32_t val)
{
	uint16_t assoc;

	printf("%s line size:  %#10x       ", label, val & 0xFFFF);

	assoc = val >> 16;
	if (assoc)
		printf("%u-way", assoc);
	else
		printf("fully");

	printf(" associative\n");
}

static int test_cache_shape()
{
	static char buffer[4096];
	ElfW(auxv_t) *p;
	int found;

	FAIL_IF(read_auxv(buffer, sizeof(buffer)));

	found = 0;

	p = find_auxv_entry(AT_L1I_CACHESIZE, buffer);
	if (p) {
		found++;
		print_size("L1I ", (uint32_t)p->a_un.a_val);
	}

	p = find_auxv_entry(AT_L1I_CACHEGEOMETRY, buffer);
	if (p) {
		found++;
		print_geo("L1I ", (uint32_t)p->a_un.a_val);
	}

	p = find_auxv_entry(AT_L1D_CACHESIZE, buffer);
	if (p) {
		found++;
		print_size("L1D ", (uint32_t)p->a_un.a_val);
	}

	p = find_auxv_entry(AT_L1D_CACHEGEOMETRY, buffer);
	if (p) {
		found++;
		print_geo("L1D ", (uint32_t)p->a_un.a_val);
	}

	p = find_auxv_entry(AT_L2_CACHESIZE, buffer);
	if (p) {
		found++;
		print_size("L2  ", (uint32_t)p->a_un.a_val);
	}

	p = find_auxv_entry(AT_L2_CACHEGEOMETRY, buffer);
	if (p) {
		found++;
		print_geo("L2  ", (uint32_t)p->a_un.a_val);
	}

	p = find_auxv_entry(AT_L3_CACHESIZE, buffer);
	if (p) {
		found++;
		print_size("L3  ", (uint32_t)p->a_un.a_val);
	}

	p = find_auxv_entry(AT_L3_CACHEGEOMETRY, buffer);
	if (p) {
		found++;
		print_geo("L3  ", (uint32_t)p->a_un.a_val);
	}

	/* If we found none we're probably on a system where they don't exist */
	SKIP_IF(found == 0);

	/* But if we found any, we expect to find them all */
	FAIL_IF(found != 8);

	return 0;
}

int main(void)
{
	return test_harness(test_cache_shape, "cache_shape");
}
