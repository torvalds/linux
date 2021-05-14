// SPDX-License-Identifier: GPL-2.0-only
/*
 * tools/testing/selftests/kvm/lib/test_util.c
 *
 * Copyright (C) 2020, Google LLC.
 */

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <linux/mman.h>
#include "linux/kernel.h"

#include "test_util.h"

/*
 * Parses "[0-9]+[kmgt]?".
 */
size_t parse_size(const char *size)
{
	size_t base;
	char *scale;
	int shift = 0;

	TEST_ASSERT(size && isdigit(size[0]), "Need at least one digit in '%s'", size);

	base = strtoull(size, &scale, 0);

	TEST_ASSERT(base != ULLONG_MAX, "Overflow parsing size!");

	switch (tolower(*scale)) {
	case 't':
		shift = 40;
		break;
	case 'g':
		shift = 30;
		break;
	case 'm':
		shift = 20;
		break;
	case 'k':
		shift = 10;
		break;
	case 'b':
	case '\0':
		shift = 0;
		break;
	default:
		TEST_ASSERT(false, "Unknown size letter %c", *scale);
	}

	TEST_ASSERT((base << shift) >> shift == base, "Overflow scaling size!");

	return base << shift;
}

int64_t timespec_to_ns(struct timespec ts)
{
	return (int64_t)ts.tv_nsec + 1000000000LL * (int64_t)ts.tv_sec;
}

struct timespec timespec_add_ns(struct timespec ts, int64_t ns)
{
	struct timespec res;

	res.tv_nsec = ts.tv_nsec + ns;
	res.tv_sec = ts.tv_sec + res.tv_nsec / 1000000000LL;
	res.tv_nsec %= 1000000000LL;

	return res;
}

struct timespec timespec_add(struct timespec ts1, struct timespec ts2)
{
	int64_t ns1 = timespec_to_ns(ts1);
	int64_t ns2 = timespec_to_ns(ts2);
	return timespec_add_ns((struct timespec){0}, ns1 + ns2);
}

struct timespec timespec_sub(struct timespec ts1, struct timespec ts2)
{
	int64_t ns1 = timespec_to_ns(ts1);
	int64_t ns2 = timespec_to_ns(ts2);
	return timespec_add_ns((struct timespec){0}, ns1 - ns2);
}

struct timespec timespec_elapsed(struct timespec start)
{
	struct timespec end;

	clock_gettime(CLOCK_MONOTONIC, &end);
	return timespec_sub(end, start);
}

struct timespec timespec_div(struct timespec ts, int divisor)
{
	int64_t ns = timespec_to_ns(ts) / divisor;

	return timespec_add_ns((struct timespec){0}, ns);
}

void print_skip(const char *fmt, ...)
{
	va_list ap;

	assert(fmt);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	puts(", skipping test");
}

bool thp_configured(void)
{
	int ret;
	struct stat statbuf;

	ret = stat("/sys/kernel/mm/transparent_hugepage", &statbuf);
	TEST_ASSERT(ret == 0 || (ret == -1 && errno == ENOENT),
		    "Error in stating /sys/kernel/mm/transparent_hugepage");

	return ret == 0;
}

size_t get_trans_hugepagesz(void)
{
	size_t size;
	FILE *f;

	TEST_ASSERT(thp_configured(), "THP is not configured in host kernel");

	f = fopen("/sys/kernel/mm/transparent_hugepage/hpage_pmd_size", "r");
	TEST_ASSERT(f != NULL, "Error in opening transparent_hugepage/hpage_pmd_size");

	fscanf(f, "%ld", &size);
	fclose(f);

	return size;
}

size_t get_def_hugetlb_pagesz(void)
{
	char buf[64];
	const char *tag = "Hugepagesize:";
	FILE *f;

	f = fopen("/proc/meminfo", "r");
	TEST_ASSERT(f != NULL, "Error in opening /proc/meminfo");

	while (fgets(buf, sizeof(buf), f) != NULL) {
		if (strstr(buf, tag) == buf) {
			fclose(f);
			return strtoull(buf + strlen(tag), NULL, 10) << 10;
		}
	}

	if (feof(f))
		TEST_FAIL("HUGETLB is not configured in host kernel");
	else
		TEST_FAIL("Error in reading /proc/meminfo");

	fclose(f);
	return 0;
}

const struct vm_mem_backing_src_alias *vm_mem_backing_src_alias(uint32_t i)
{
	static const struct vm_mem_backing_src_alias aliases[] = {
		[VM_MEM_SRC_ANONYMOUS] = {
			.name = "anonymous",
			.flag = 0,
		},
		[VM_MEM_SRC_ANONYMOUS_THP] = {
			.name = "anonymous_thp",
			.flag = 0,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB] = {
			.name = "anonymous_hugetlb",
			.flag = MAP_HUGETLB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_16KB] = {
			.name = "anonymous_hugetlb_16kb",
			.flag = MAP_HUGETLB | MAP_HUGE_16KB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_64KB] = {
			.name = "anonymous_hugetlb_64kb",
			.flag = MAP_HUGETLB | MAP_HUGE_64KB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_512KB] = {
			.name = "anonymous_hugetlb_512kb",
			.flag = MAP_HUGETLB | MAP_HUGE_512KB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_1MB] = {
			.name = "anonymous_hugetlb_1mb",
			.flag = MAP_HUGETLB | MAP_HUGE_1MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_2MB] = {
			.name = "anonymous_hugetlb_2mb",
			.flag = MAP_HUGETLB | MAP_HUGE_2MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_8MB] = {
			.name = "anonymous_hugetlb_8mb",
			.flag = MAP_HUGETLB | MAP_HUGE_8MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_16MB] = {
			.name = "anonymous_hugetlb_16mb",
			.flag = MAP_HUGETLB | MAP_HUGE_16MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_32MB] = {
			.name = "anonymous_hugetlb_32mb",
			.flag = MAP_HUGETLB | MAP_HUGE_32MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_256MB] = {
			.name = "anonymous_hugetlb_256mb",
			.flag = MAP_HUGETLB | MAP_HUGE_256MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_512MB] = {
			.name = "anonymous_hugetlb_512mb",
			.flag = MAP_HUGETLB | MAP_HUGE_512MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_1GB] = {
			.name = "anonymous_hugetlb_1gb",
			.flag = MAP_HUGETLB | MAP_HUGE_1GB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_2GB] = {
			.name = "anonymous_hugetlb_2gb",
			.flag = MAP_HUGETLB | MAP_HUGE_2GB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_16GB] = {
			.name = "anonymous_hugetlb_16gb",
			.flag = MAP_HUGETLB | MAP_HUGE_16GB,
		},
	};
	_Static_assert(ARRAY_SIZE(aliases) == NUM_SRC_TYPES,
		       "Missing new backing src types?");

	TEST_ASSERT(i < NUM_SRC_TYPES, "Backing src type ID %d too big", i);

	return &aliases[i];
}

#define MAP_HUGE_PAGE_SIZE(x) (1ULL << ((x >> MAP_HUGE_SHIFT) & MAP_HUGE_MASK))

size_t get_backing_src_pagesz(uint32_t i)
{
	uint32_t flag = vm_mem_backing_src_alias(i)->flag;

	switch (i) {
	case VM_MEM_SRC_ANONYMOUS:
		return getpagesize();
	case VM_MEM_SRC_ANONYMOUS_THP:
		return get_trans_hugepagesz();
	case VM_MEM_SRC_ANONYMOUS_HUGETLB:
		return get_def_hugetlb_pagesz();
	default:
		return MAP_HUGE_PAGE_SIZE(flag);
	}
}

void backing_src_help(void)
{
	int i;

	printf("Available backing src types:\n");
	for (i = 0; i < NUM_SRC_TYPES; i++)
		printf("\t%s\n", vm_mem_backing_src_alias(i)->name);
}

enum vm_mem_backing_src_type parse_backing_src_type(const char *type_name)
{
	int i;

	for (i = 0; i < NUM_SRC_TYPES; i++)
		if (!strcmp(type_name, vm_mem_backing_src_alias(i)->name))
			return i;

	backing_src_help();
	TEST_FAIL("Unknown backing src type: %s", type_name);
	return -1;
}
