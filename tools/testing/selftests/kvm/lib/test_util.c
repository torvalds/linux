// SPDX-License-Identifier: GPL-2.0-only
/*
 * tools/testing/selftests/kvm/lib/test_util.c
 *
 * Copyright (C) 2020, Google LLC.
 */
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
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
