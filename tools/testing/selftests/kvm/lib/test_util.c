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

const struct vm_mem_backing_src_alias backing_src_aliases[] = {
	{"anonymous", VM_MEM_SRC_ANONYMOUS,},
	{"anonymous_thp", VM_MEM_SRC_ANONYMOUS_THP,},
	{"anonymous_hugetlb", VM_MEM_SRC_ANONYMOUS_HUGETLB,},
};

void backing_src_help(void)
{
	int i;

	printf("Available backing src types:\n");
	for (i = 0; i < ARRAY_SIZE(backing_src_aliases); i++)
		printf("\t%s\n", backing_src_aliases[i].name);
}

enum vm_mem_backing_src_type parse_backing_src_type(const char *type_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(backing_src_aliases); i++)
		if (!strcmp(type_name, backing_src_aliases[i].name))
			return backing_src_aliases[i].type;

	backing_src_help();
	TEST_FAIL("Unknown backing src type: %s", type_name);
	return -1;
}
