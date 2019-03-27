/*-
 * Copyright (c) 2014 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#include <sys/param.h>

#include <bitstring.h>
#include <stdio.h>

#include <atf-c.h>

typedef void (testfunc_t)(bitstr_t *bstr, int nbits, const char *memloc);

static void
bitstring_run_stack_test(testfunc_t *test, int nbits)
{
	bitstr_t bit_decl(bitstr, nbits);

	test(bitstr, nbits, "stack");
}

static void
bitstring_run_heap_test(testfunc_t *test, int nbits)
{
	bitstr_t *bitstr = bit_alloc(nbits);

	test(bitstr, nbits, "heap");
}

static void
bitstring_test_runner(testfunc_t *test)
{
	const int bitstr_sizes[] = {
		0,
		1,
		_BITSTR_BITS - 1,
		_BITSTR_BITS,
		_BITSTR_BITS + 1,
		2 * _BITSTR_BITS - 1,
		2 * _BITSTR_BITS,
		1023,
		1024
	};

	for (unsigned long i = 0; i < nitems(bitstr_sizes); i++) {
		bitstring_run_stack_test(test, bitstr_sizes[i]);
		bitstring_run_heap_test(test, bitstr_sizes[i]);
	}
}

#define	BITSTRING_TC_DEFINE(name)				\
ATF_TC_WITHOUT_HEAD(name);					\
static testfunc_t name ## _test;				\
								\
ATF_TC_BODY(name, tc)						\
{								\
	bitstring_test_runner(name ## _test);			\
}								\
								\
static void							\
name ## _test(bitstr_t *bitstr, int nbits, const char *memloc)

#define	BITSTRING_TC_ADD(tp, name)				\
do {								\
	ATF_TP_ADD_TC(tp, name);				\
} while (0)

ATF_TC_WITHOUT_HEAD(bitstr_in_struct);
ATF_TC_BODY(bitstr_in_struct, tc)
{
	struct bitstr_containing_struct {
		bitstr_t bit_decl(bitstr, 8);
	} test_struct;

	bit_nclear(test_struct.bitstr, 0, 8);
}

ATF_TC_WITHOUT_HEAD(bitstr_size);
ATF_TC_BODY(bitstr_size, tc)
{
	size_t sob = sizeof(bitstr_t);

	ATF_CHECK_EQ(0, bitstr_size(0));
	ATF_CHECK_EQ(sob, bitstr_size(1));
	ATF_CHECK_EQ(sob, bitstr_size(sob * 8));
	ATF_CHECK_EQ(2 * sob, bitstr_size(sob * 8 + 1));
}

BITSTRING_TC_DEFINE(bit_set)
/* bitstr_t *bitstr, int nbits, const char *memloc */
{
	memset(bitstr, 0, bitstr_size(nbits));
	
	for (int i = 0; i < nbits; i++) {
		bit_set(bitstr, i);

		for (int j = 0; j < nbits; j++) {
			ATF_REQUIRE_MSG(bit_test(bitstr, j) == (j == i) ? 1 : 0,
			    "bit_set_%d_%s: Failed on bit %d",
			    nbits, memloc, i);
		}

		bit_clear(bitstr, i);
	}
}

BITSTRING_TC_DEFINE(bit_clear)
/* bitstr_t *bitstr, int nbits, const char *memloc */
{
	int i, j;

	memset(bitstr, 0xFF, bitstr_size(nbits));
	for (i = 0; i < nbits; i++) {
		bit_clear(bitstr, i);

		for (j = 0; j < nbits; j++) {
			ATF_REQUIRE_MSG(bit_test(bitstr, j) == (j == i) ? 0 : 1,
			    "bit_clear_%d_%s: Failed on bit %d",
			    nbits, memloc, i);
		}

		bit_set(bitstr, i);
	}
}

BITSTRING_TC_DEFINE(bit_ffs)
/* bitstr_t *bitstr, int nbits, const char *memloc */
{
	int i;
	int found_set_bit;

	memset(bitstr, 0, bitstr_size(nbits));
	bit_ffs(bitstr, nbits, &found_set_bit);
	ATF_REQUIRE_MSG(found_set_bit == -1,
	    "bit_ffs_%d_%s: Failed all clear bits.", nbits, memloc);

	for (i = 0; i < nbits; i++) {
		memset(bitstr, 0xFF, bitstr_size(nbits));
		if (i > 0)
			bit_nclear(bitstr, 0, i - 1);

		bit_ffs(bitstr, nbits, &found_set_bit);
		ATF_REQUIRE_MSG(found_set_bit == i,
		    "bit_ffs_%d_%s: Failed on bit %d, Result %d",
		    nbits, memloc, i, found_set_bit);
	}
}

BITSTRING_TC_DEFINE(bit_ffc)
/* bitstr_t *bitstr, int nbits, const char *memloc */
{
	int i;
	int found_clear_bit;

	memset(bitstr, 0xFF, bitstr_size(nbits));
	bit_ffc(bitstr, nbits, &found_clear_bit);
	ATF_REQUIRE_MSG(found_clear_bit == -1,
	    "bit_ffc_%d_%s: Failed all set bits.", nbits, memloc);

	for (i = 0; i < nbits; i++) {
		memset(bitstr, 0, bitstr_size(nbits));
		if (i > 0)
			bit_nset(bitstr, 0, i - 1);

		bit_ffc(bitstr, nbits, &found_clear_bit);
		ATF_REQUIRE_MSG(found_clear_bit == i,
		    "bit_ffc_%d_%s: Failed on bit %d, Result %d",
		    nbits, memloc, i, found_clear_bit);
	}
}

BITSTRING_TC_DEFINE(bit_ffs_at)
/* bitstr_t *bitstr, int nbits, const char *memloc */
{
	int i;
	int found_set_bit;

	memset(bitstr, 0xFF, bitstr_size(nbits));
	for (i = 0; i < nbits; i++) {
		bit_ffs_at(bitstr, i, nbits, &found_set_bit);
		ATF_REQUIRE_MSG(found_set_bit == i,
		    "bit_ffs_at_%d_%s: Failed on bit %d, Result %d",
		    nbits, memloc, i, found_set_bit);
	}

	memset(bitstr, 0, bitstr_size(nbits));
	for (i = 0; i < nbits; i++) {
		bit_ffs_at(bitstr, i, nbits, &found_set_bit);
		ATF_REQUIRE_MSG(found_set_bit == -1,
		    "bit_ffs_at_%d_%s: Failed on bit %d, Result %d",
		    nbits, memloc, i, found_set_bit);
	}

	memset(bitstr, 0x55, bitstr_size(nbits));
	for (i = 0; i < nbits; i++) {
		bit_ffs_at(bitstr, i, nbits, &found_set_bit);
		if (i == nbits - 1 && (nbits & 1) == 0) {
			ATF_REQUIRE_MSG(found_set_bit == -1,
			    "bit_ffs_at_%d_%s: Failed on bit %d, Result %d",
			    nbits, memloc, i, found_set_bit);
		} else {
			ATF_REQUIRE_MSG(found_set_bit == i + (i & 1),
			    "bit_ffs_at_%d_%s: Failed on bit %d, Result %d",
			    nbits, memloc, i, found_set_bit);
		}
	}

	memset(bitstr, 0xAA, bitstr_size(nbits));
	for (i = 0; i < nbits; i++) {
		bit_ffs_at(bitstr, i, nbits, &found_set_bit);
		if (i == nbits - 1 && (nbits & 1) != 0) {
			ATF_REQUIRE_MSG(found_set_bit == -1,
			    "bit_ffs_at_%d_%s: Failed on bit %d, Result %d",
			    nbits, memloc, i, found_set_bit);
		} else {
			ATF_REQUIRE_MSG(
			    found_set_bit == i + ((i & 1) ? 0 : 1),
			    "bit_ffs_at_%d_%s: Failed on bit %d, Result %d",
			    nbits, memloc, i, found_set_bit);
		}
	}
}

BITSTRING_TC_DEFINE(bit_ffc_at)
/* bitstr_t *bitstr, int nbits, const char *memloc */
{
	int i, found_clear_bit;

	memset(bitstr, 0, bitstr_size(nbits));
	for (i = 0; i < nbits; i++) {
		bit_ffc_at(bitstr, i, nbits, &found_clear_bit);
		ATF_REQUIRE_MSG(found_clear_bit == i,
		    "bit_ffc_at_%d_%s: Failed on bit %d, Result %d",
		    nbits, memloc, i, found_clear_bit);
	}

	memset(bitstr, 0xFF, bitstr_size(nbits));
	for (i = 0; i < nbits; i++) {
		bit_ffc_at(bitstr, i, nbits, &found_clear_bit);
		ATF_REQUIRE_MSG(found_clear_bit == -1,
		    "bit_ffc_at_%d_%s: Failed on bit %d, Result %d",
		    nbits, memloc, i, found_clear_bit);
	}

	memset(bitstr, 0x55, bitstr_size(nbits));
	for (i = 0; i < nbits; i++) {
		bit_ffc_at(bitstr, i, nbits, &found_clear_bit);
		if (i == nbits - 1 && (nbits & 1) != 0) {
			ATF_REQUIRE_MSG(found_clear_bit == -1,
			    "bit_ffc_at_%d_%s: Failed on bit %d, Result %d",
			    nbits, memloc, i, found_clear_bit);
		} else {
			ATF_REQUIRE_MSG(
			    found_clear_bit == i + ((i & 1) ? 0 : 1),
			    "bit_ffc_at_%d_%s: Failed on bit %d, Result %d",
			    nbits, memloc, i, found_clear_bit);
		}
	}

	memset(bitstr, 0xAA, bitstr_size(nbits));
	for (i = 0; i < nbits; i++) {
		bit_ffc_at(bitstr, i, nbits, &found_clear_bit);
		if (i == nbits - 1 && (nbits & 1) == 0) {
			ATF_REQUIRE_MSG(found_clear_bit == -1,
			    "bit_ffc_at_%d_%s: Failed on bit %d, Result %d",
			    nbits, memloc, i, found_clear_bit);
		} else {
			ATF_REQUIRE_MSG(found_clear_bit == i + (i & 1),
			    "bit_ffc_at_%d_%s: Failed on bit %d, Result %d",
			    nbits, memloc, i, found_clear_bit);
		}
	}
}

BITSTRING_TC_DEFINE(bit_nclear)
/* bitstr_t *bitstr, int nbits, const char *memloc */
{
	int i, j;
	int found_set_bit;
	int found_clear_bit;

	for (i = 0; i < nbits; i++) {
		for (j = i; j < nbits; j++) {
			memset(bitstr, 0xFF, bitstr_size(nbits));
			bit_nclear(bitstr, i, j);

			bit_ffc(bitstr, nbits, &found_clear_bit);
			ATF_REQUIRE_MSG(
			    found_clear_bit == i,
			    "bit_nclear_%d_%d_%d%s: Failed with result %d",
			    nbits, i, j, memloc, found_clear_bit);

			bit_ffs_at(bitstr, i, nbits, &found_set_bit);
			ATF_REQUIRE_MSG(
			    (j + 1 < nbits) ? found_set_bit == j + 1 : -1,
			    "bit_nset_%d_%d_%d%s: Failed with result %d",
			    nbits, i, j, memloc, found_set_bit);
		}
	}
}

BITSTRING_TC_DEFINE(bit_nset)
/* bitstr_t *bitstr, int nbits, const char *memloc */
{
	int i, j;
	int found_set_bit;
	int found_clear_bit;

	for (i = 0; i < nbits; i++) {
		for (j = i; j < nbits; j++) {
			memset(bitstr, 0, bitstr_size(nbits));
			bit_nset(bitstr, i, j);

			bit_ffs(bitstr, nbits, &found_set_bit);
			ATF_REQUIRE_MSG(
			    found_set_bit == i,
			    "bit_nset_%d_%d_%d%s: Failed with result %d",
			    nbits, i, j, memloc, found_set_bit);

			bit_ffc_at(bitstr, i, nbits, &found_clear_bit);
			ATF_REQUIRE_MSG(
			    (j + 1 < nbits) ? found_clear_bit == j + 1 : -1,
			    "bit_nset_%d_%d_%d%s: Failed with result %d",
			    nbits, i, j, memloc, found_clear_bit);
		}
	}
}

BITSTRING_TC_DEFINE(bit_count)
/* bitstr_t *bitstr, int nbits, const char *memloc */
{
	int result, s, e, expected;

	/* Empty bitstr */
	memset(bitstr, 0, bitstr_size(nbits));
	bit_count(bitstr, 0, nbits, &result);
	ATF_CHECK_MSG(0 == result,
			"bit_count_%d_%s_%s: Failed with result %d",
			nbits, "clear", memloc, result);

	/* Full bitstr */
	memset(bitstr, 0xFF, bitstr_size(nbits));
	bit_count(bitstr, 0, nbits, &result);
	ATF_CHECK_MSG(nbits == result,
			"bit_count_%d_%s_%s: Failed with result %d",
			nbits, "set", memloc, result);

	/* Invalid _start value */
	memset(bitstr, 0xFF, bitstr_size(nbits));
	bit_count(bitstr, nbits, nbits, &result);
	ATF_CHECK_MSG(0 == result,
			"bit_count_%d_%s_%s: Failed with result %d",
			nbits, "invalid_start", memloc, result);
	
	/* Alternating bitstr, starts with 0 */
	memset(bitstr, 0xAA, bitstr_size(nbits));
	bit_count(bitstr, 0, nbits, &result);
	ATF_CHECK_MSG(nbits / 2 == result,
			"bit_count_%d_%s_%d_%s: Failed with result %d",
			nbits, "alternating", 0, memloc, result);

	/* Alternating bitstr, starts with 1 */
	memset(bitstr, 0x55, bitstr_size(nbits));
	bit_count(bitstr, 0, nbits, &result);
	ATF_CHECK_MSG((nbits + 1) / 2 == result,
			"bit_count_%d_%s_%d_%s: Failed with result %d",
			nbits, "alternating", 1, memloc, result);

	/* Varying start location */
	memset(bitstr, 0xAA, bitstr_size(nbits));
	for (s = 0; s < nbits; s++) {
		expected = s % 2 == 0 ? (nbits - s) / 2 : (nbits - s + 1) / 2;
		bit_count(bitstr, s, nbits, &result);
		ATF_CHECK_MSG(expected == result,
				"bit_count_%d_%s_%d_%s: Failed with result %d",
				nbits, "vary_start", s, memloc, result);
	}

	/* Varying end location */
	memset(bitstr, 0xAA, bitstr_size(nbits));
	for (e = 0; e < nbits; e++) {
		bit_count(bitstr, 0, e, &result);
		ATF_CHECK_MSG(e / 2 == result,
				"bit_count_%d_%s_%d_%s: Failed with result %d",
				nbits, "vary_end", e, memloc, result);
	}

}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, bitstr_in_struct);
	ATF_TP_ADD_TC(tp, bitstr_size);
	BITSTRING_TC_ADD(tp, bit_set);
	BITSTRING_TC_ADD(tp, bit_clear);
	BITSTRING_TC_ADD(tp, bit_ffs);
	BITSTRING_TC_ADD(tp, bit_ffc);
	BITSTRING_TC_ADD(tp, bit_ffs_at);
	BITSTRING_TC_ADD(tp, bit_ffc_at);
	BITSTRING_TC_ADD(tp, bit_nclear);
	BITSTRING_TC_ADD(tp, bit_nset);
	BITSTRING_TC_ADD(tp, bit_count);

	return (atf_no_error());
}
