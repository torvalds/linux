/*	$OpenBSD: rde_aspa_test.c,v 1.6 2025/02/21 06:10:59 anton Exp $ */

/*
 * Copyright (c) 2022 Claudio Jeker <claudio@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <err.h>
#include <stdio.h>

#include "rde_aspa.c"

static struct aspath	*build_aspath(const uint32_t *, uint32_t, int);
static const char	*print_aspath(const uint32_t *, uint32_t);

static void	 reverse_state(struct aspa_state *, struct aspa_state *);
static void	 print_state(struct aspa_state *, struct aspa_state *);

struct aspa_test_set {
	uint32_t	customeras;
	const uint32_t	*providers;
	uint32_t	pascnt;
};

struct cp_test {
	uint32_t	customeras;
	uint32_t	provideras;
	uint8_t		expected_result;
};

struct aspath_test {
	const uint32_t		*aspath;
	uint32_t		 aspathcnt;
	struct aspa_state	 state;
};

struct aspa_test {
	const uint32_t	*aspath;
	uint32_t	 aspathcnt;
	enum role	 role;
	uint8_t		 expected_result;
};

struct aspa_test_set testset[] = {
	/* test vectors from  github.com/benmaddison/aspa-fuzz */
	{ 1, (const uint32_t []){ 4, 5, 6 }, 3 },
	{ 2, (const uint32_t []){ 10, 11 }, 2 },
	{ 3, (const uint32_t []){ 1, 13, 14 }, 3 },
	{ 4, (const uint32_t []){ 16, 24 }, 2 },
	{ 5, (const uint32_t []){ 1, 17, 25 }, 3 },
	{ 8, (const uint32_t []){ 0 }, 1 },
	{ 9, (const uint32_t []){ 2 }, 1 },
	{ 10, (const uint32_t []){ 0 }, 1 },
	{ 11, (const uint32_t []){ 2 }, 1 },
	{ 12, (const uint32_t []){ 3 }, 1 },
	{ 13, (const uint32_t []){ 0 }, 1 },
	{ 14, (const uint32_t []){ 3, 25 }, 2 },
	{ 15, (const uint32_t []){ 4 }, 1 },
	{ 16, (const uint32_t []){ 4 }, 1 },
	{ 17, (const uint32_t []){ 5 }, 1 },
	{ 18, (const uint32_t []){ 6 }, 1 },
	{ 20, (const uint32_t []){ 19 }, 1 },
	{ 21, (const uint32_t []){ 0 }, 1 },
	{ 23, (const uint32_t []){ 22 }, 1 },
	{ 24, (const uint32_t []){ 0 }, 1 },
	{ 25, (const uint32_t []){ 0 }, 1 },
	{ 26, (const uint32_t []){ 5 }, 1 },
	{ 27, (const uint32_t []){ 14 }, 1 },
	/* tests to simulate slides-110-sidrops-sriram-aspa-alg-accuracy-01 */
	{ 101, (const uint32_t []){ 102 }, 1 },
	{ 102, (const uint32_t []){ 103, 104, 105 }, 3 },
	{ 103, (const uint32_t []){ 111, 112, 203 }, 3 },
	/* 104 no ASPA */
	{ 105, (const uint32_t []){ 0 }, 1 },

	/* 111 no ASPA */
	{ 112, (const uint32_t []){ 0 }, 1 },
	{ 113, (const uint32_t []){ 104, 105, 204, 205 }, 4 },

	{ 121, (const uint32_t []){ 131, 132, 133 }, 3 },
	{ 123, (const uint32_t []){ 0 }, 1 },
	{ 131, (const uint32_t []){ 121, 122, 123 }, 3 },
	{ 133, (const uint32_t []){ 0 }, 1 },
	

	{ 201, (const uint32_t []){ 202 }, 1 },
	{ 202, (const uint32_t []){ 203, 204, 205 }, 3 },
	{ 203, (const uint32_t []){ 103, 111, 112 }, 3 },
	/* 204 no ASPA */
	{ 205, (const uint32_t []){ 0 }, 1 },

	/* extra test for big table test */
	{ 65000, (const uint32_t []){
	    3, 5, 10, 15, 20, 21, 22, 23, 24, 25, 
	    30, 35, 40, 45, 50, 51, 52, 53, 54, 55, 
	    60, 65, 70, 75, 80, 81, 82, 83, 87, 90 }, 30 },
	{ 196618, (const uint32_t []){ 1, 2, 3, 4 }, 4 },
};

struct cp_test cp_testset[] = {
	{ 6, 1, UNKNOWN },
	{ 42, 1, UNKNOWN },

	{ 1, 2, NOT_PROVIDER },
	{ 1, 3, NOT_PROVIDER },
	{ 1, 7, NOT_PROVIDER },
	{ 5, 2, NOT_PROVIDER },
	{ 5, 16, NOT_PROVIDER },
	{ 5, 18, NOT_PROVIDER },
	{ 5, 24, NOT_PROVIDER },
	{ 5, 26, NOT_PROVIDER },
	{ 8, 2, NOT_PROVIDER },
	{ 9, 5, NOT_PROVIDER },
	{ 27, 13, NOT_PROVIDER },
	{ 27, 15, NOT_PROVIDER },

	{ 1, 4, PROVIDER },
	{ 1, 5, PROVIDER },
	{ 1, 6, PROVIDER },
	{ 2, 10, PROVIDER },
	{ 2, 11, PROVIDER },
	{ 9, 2, PROVIDER },
	{ 27, 14, PROVIDER },

	{ 196618, 1, PROVIDER },
	{ 196618, 2, PROVIDER },
	{ 196618, 3, PROVIDER },
	{ 196618, 4, PROVIDER },
	{ 196618, 5, NOT_PROVIDER },

	/* big provider set test */
	{ 65000, 1, NOT_PROVIDER },
	{ 65000, 2, NOT_PROVIDER },
	{ 65000, 3, PROVIDER },
	{ 65000, 4, NOT_PROVIDER },
	{ 65000, 5, PROVIDER },
	{ 65000, 15, PROVIDER },
	{ 65000, 19, NOT_PROVIDER },
	{ 65000, 20, PROVIDER },
	{ 65000, 21, PROVIDER },
	{ 65000, 22, PROVIDER },
	{ 65000, 23, PROVIDER },
	{ 65000, 24, PROVIDER },
	{ 65000, 25, PROVIDER },
	{ 65000, 26, NOT_PROVIDER },
	{ 65000, 85, NOT_PROVIDER },
	{ 65000, 86, NOT_PROVIDER },
	{ 65000, 87, PROVIDER },
	{ 65000, 88, NOT_PROVIDER },
	{ 65000, 89, NOT_PROVIDER },
	{ 65000, 90, PROVIDER },
	{ 65000, 91, NOT_PROVIDER },
	{ 65000, 92, NOT_PROVIDER },
	{ 65000, 6666, NOT_PROVIDER },
};

struct aspath_test	aspath_testset[] = {
	{ (const uint32_t []) { 1 }, 1, { 1, 1, 0, 0, 1, 0, 0 } },
	{ (const uint32_t []) { 7 }, 1, { 1, 1, 0, 0, 1, 0, 0 } },
	{ (const uint32_t []) { 8 }, 1, { 1, 1, 0, 0, 1, 0, 0 } },

	{ (const uint32_t []) { 1, 1 }, 2, { 1, 1, 0, 0, 1, 0, 0 } },
	{ (const uint32_t []) { 7, 7 }, 2, { 1, 1, 0, 0, 1, 0, 0 } },
	{ (const uint32_t []) { 8, 8 }, 2, { 1, 1, 0, 0, 1, 0, 0 } },

	{ (const uint32_t []) { 1, 1, 1 }, 3, { 1, 1, 0, 0, 1, 0, 0 } },
	{ (const uint32_t []) { 7, 7, 7 }, 3, { 1, 1, 0, 0, 1, 0, 0 } },
	{ (const uint32_t []) { 8, 8, 8 }, 3, { 1, 1, 0, 0, 1, 0, 0 } },

	{ (const uint32_t []) { 1, 5 }, 2, { 2, 1, 0, 0, 2, 0, 0 } },
	{ (const uint32_t []) { 1, 1, 5, 5 }, 4, { 2, 1, 0, 0, 2, 0, 0 } },
	{ (const uint32_t []) { 1, 5, 17 }, 3, { 3, 1, 0, 0, 3, 0, 0 } },

	{ (const uint32_t []) { 1, 4 }, 2, { 2, 2, 0, 1, 2, 0, 0 } },
	{ (const uint32_t []) { 1, 6 }, 2, { 2, 2, 1, 0, 2, 0, 0 } },
	{ (const uint32_t []) { 1, 17 }, 2, { 2, 2, 0, 1, 1, 0, 2 } },

	{ (const uint32_t []) { 42, 43, 44 }, 3, { 3, 3, 2, 0, 1, 2, 0 } },

	{ (const uint32_t []) { 42, 1, 5, 17, 44 }, 5,
	     { 5, 5, 4, 1, 1, 2, 5 } },

	/* 1 ?> 6 -? 11 -- 12 -- 13 ?- 19 <? 20 */
	{ (const uint32_t []) { 1, 6, 11, 12, 13, 19, 20 }, 7,
	     { 7, 6, 5, 4, 2, 3, 4 } },
};

/*
 * For simplicity the relation between is described as 123 LR 124 where:
 * R: ? if ASPA(123) is empty
 *    > if 124 is a provider of 123
 *    - otherwise (124 is not part of the provider list)
 * L: ? if ASPA(124) is empty
 *    > if 123 is a provider of of 124
 *    - otherwise (123 is not part of the provider list)
 *
 * e.g. 1 -> 2 (2 is provider of 1 but 1 is not for 2)
 *      1 ?> 2 (2 is provider of 1 but 2 has no ASPA set defined)
 */
struct aspa_test	aspa_testset[] = {
	/* empty ASPATH are invalid by default */
	{ (const uint32_t []) { }, 0, ROLE_CUSTOMER, ASPA_INVALID },
	{ (const uint32_t []) { }, 0, ROLE_PROVIDER, ASPA_INVALID },
	{ (const uint32_t []) { }, 0, ROLE_RS, ASPA_INVALID },
	{ (const uint32_t []) { }, 0, ROLE_RS_CLIENT, ASPA_INVALID },
	{ (const uint32_t []) { }, 0, ROLE_PEER, ASPA_INVALID },

	{ (const uint32_t []) { 2 }, 1, ROLE_RS_CLIENT, ASPA_VALID },
	{ (const uint32_t []) { 2 }, 1, ROLE_PEER, ASPA_VALID },

	{ (const uint32_t []) { 3 }, 1, ROLE_PROVIDER, ASPA_VALID },
	{ (const uint32_t []) { 4 }, 1, ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 5 }, 1, ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 6 }, 1, ROLE_CUSTOMER, ASPA_VALID },

	{ (const uint32_t []) { 7 }, 1, ROLE_PROVIDER, ASPA_VALID },
	{ (const uint32_t []) { 7 }, 1, ROLE_PEER, ASPA_VALID },
	{ (const uint32_t []) { 7 }, 1, ROLE_RS_CLIENT, ASPA_VALID },

	{ (const uint32_t []) { 2, 8 }, 2, ROLE_PEER, ASPA_INVALID },
	{ (const uint32_t []) { 2, 8 }, 2, ROLE_RS_CLIENT, ASPA_INVALID },

	{ (const uint32_t []) { 2, 9 }, 2, ROLE_PEER, ASPA_VALID },
	{ (const uint32_t []) { 2, 9 }, 2, ROLE_RS_CLIENT, ASPA_VALID },

	{ (const uint32_t []) { 2, 10 }, 2, ROLE_PEER, ASPA_INVALID },
	{ (const uint32_t []) { 2, 10 }, 2, ROLE_RS_CLIENT, ASPA_INVALID },

	{ (const uint32_t []) { 2, 11 }, 2, ROLE_PEER, ASPA_VALID },
	{ (const uint32_t []) { 2, 11 }, 2, ROLE_RS_CLIENT, ASPA_VALID },

	{ (const uint32_t []) { 3, 8 }, 2, ROLE_PROVIDER, ASPA_INVALID },
	{ (const uint32_t []) { 3, 12 }, 2, ROLE_PROVIDER, ASPA_VALID },
	{ (const uint32_t []) { 3, 13 }, 2, ROLE_PROVIDER, ASPA_INVALID },
	{ (const uint32_t []) { 3, 14 }, 2, ROLE_PROVIDER, ASPA_VALID },

	{ (const uint32_t []) { 4, 8 }, 2, ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 4, 15 }, 2, ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 4, 16 }, 2, ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 4, 24 }, 2, ROLE_CUSTOMER, ASPA_VALID },

	{ (const uint32_t []) { 5, 8 }, 2, ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 5, 17 }, 2, ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 5, 25 }, 2, ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 5, 26 }, 2, ROLE_CUSTOMER, ASPA_VALID },

	{ (const uint32_t []) { 6, 18 }, 2, ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 6, 19 }, 2, ROLE_CUSTOMER, ASPA_VALID },

	{ (const uint32_t []) { 7, 19 }, 2, ROLE_PROVIDER, ASPA_UNKNOWN },
	{ (const uint32_t []) { 7, 19 }, 2, ROLE_PEER, ASPA_UNKNOWN },
	{ (const uint32_t []) { 7, 19 }, 2, ROLE_RS_CLIENT, ASPA_UNKNOWN },
	{ (const uint32_t []) { 7, 21 }, 2, ROLE_PROVIDER, ASPA_INVALID },
	{ (const uint32_t []) { 7, 21 }, 2, ROLE_PEER, ASPA_INVALID },
	{ (const uint32_t []) { 7, 21 }, 2, ROLE_RS_CLIENT, ASPA_INVALID },

	{ (const uint32_t []) { 6, 19, 20 }, 3, ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 20, 19, 6 }, 3, ROLE_CUSTOMER, ASPA_VALID },

	{ (const uint32_t []) { 3, 14, 25 }, 3, ROLE_PROVIDER, ASPA_INVALID },
	{ (const uint32_t []) { 3, 14, 19 }, 3, ROLE_PROVIDER, ASPA_UNKNOWN },
	{ (const uint32_t []) { 3, 14, 19 }, 3, ROLE_PEER, ASPA_UNKNOWN },
	{ (const uint32_t []) { 3, 14, 19 }, 3, ROLE_RS_CLIENT, ASPA_UNKNOWN },
	{ (const uint32_t []) { 3, 14, 21 }, 3, ROLE_PROVIDER, ASPA_INVALID },
	{ (const uint32_t []) { 3, 14, 21 }, 3, ROLE_PEER, ASPA_INVALID },
	{ (const uint32_t []) { 3, 14, 21 }, 3, ROLE_RS_CLIENT, ASPA_INVALID },
	{ (const uint32_t []) { 3, 14, 27 }, 3, ROLE_PROVIDER, ASPA_VALID },
	{ (const uint32_t []) { 3, 14, 27 }, 3, ROLE_PEER, ASPA_VALID },
	{ (const uint32_t []) { 3, 14, 27 }, 3, ROLE_RS_CLIENT, ASPA_VALID },
	
	{ (const uint32_t []) { 7, 19, 22, 21 }, 4, ROLE_PROVIDER,
	    ASPA_INVALID },
	{ (const uint32_t []) { 7, 19, 22, 21 }, 4, ROLE_PEER, ASPA_INVALID },
	{ (const uint32_t []) { 7, 19, 22, 21 }, 4, ROLE_RS_CLIENT,
	    ASPA_INVALID },

	{ (const uint32_t []) { 6, 19, 22, 23 }, 4, ROLE_CUSTOMER,
	    ASPA_UNKNOWN },

	{ (const uint32_t []) { 1, 5, 17, 13, 3, 14, 27 }, 7, ROLE_CUSTOMER,
	    ASPA_VALID },
	{ (const uint32_t []) { 27, 14, 3, 13, 17, 5, 1 }, 7, ROLE_CUSTOMER,
	    ASPA_VALID },

	{ (const uint32_t []) { 27, 14, 3, 6, 7, 19, 17, 5, 1 }, 9,
	    ROLE_CUSTOMER, ASPA_INVALID },
	{ (const uint32_t []) { 27, 14, 3, 7, 19, 6, 1, 5, 17 }, 9,
	    ROLE_CUSTOMER, ASPA_UNKNOWN },

	/* check L < K (ramps overlap) */
	{ (const uint32_t []) { 201, 202, 203, 103, 102, 101 }, 6,
	    ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 101, 102, 103, 203, 202, 201 }, 6,
	    ROLE_CUSTOMER, ASPA_VALID },

	/* check L == K (ramps touch) 203 ?> 111 <? 103 */
	{ (const uint32_t []) { 201, 202, 203, 111, 103, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 101, 102, 103, 111, 203, 202, 201 }, 7,
	    ROLE_CUSTOMER, ASPA_VALID },
	/* check L == K (ramps touch) 203 -> 111 <- 103 */
	{ (const uint32_t []) { 201, 202, 203, 112, 103, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_VALID },
	{ (const uint32_t []) { 101, 102, 103, 112, 203, 202, 201 }, 7,
	    ROLE_CUSTOMER, ASPA_VALID },

	/* check L - K == 1 (204 ?? 104) */
	{ (const uint32_t []) { 201, 202, 204, 104, 102, 101 }, 6,
	    ROLE_CUSTOMER, ASPA_VALID },
	/* check L - K == 1 (204 -? 105) */
	{ (const uint32_t []) { 201, 202, 204, 105, 102, 101 }, 6,
	    ROLE_CUSTOMER, ASPA_VALID },
	/* check L - K == 1 (205 ?- 104) */
	{ (const uint32_t []) { 201, 202, 205, 104, 102, 101 }, 6,
	    ROLE_CUSTOMER, ASPA_VALID },
	/* check L - K == 1 (205 -- 105) */
	{ (const uint32_t []) { 201, 202, 205, 105, 102, 101 }, 6,
	    ROLE_CUSTOMER, ASPA_VALID },

	/* check L - K == 2 invalid cases (205 ?- 111 -? 105) */
	{ (const uint32_t []) { 201, 202, 205, 111, 105, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_INVALID },
	/* check L - K == 2 invalid cases (205 -- 112 -- 105) */
	{ (const uint32_t []) { 201, 202, 205, 112, 105, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_INVALID },
	/* check L - K == 2 invalid cases (205 <- 113 -> 105) */
	{ (const uint32_t []) { 201, 202, 205, 113, 105, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_INVALID },

	/* check L - K == 2 unknown cases (205 ?- 111 ?? 104) */
	{ (const uint32_t []) { 201, 202, 205, 111, 104, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_UNKNOWN },
	/* check L - K == 2 unknown cases (204 ?? 111 -? 105) */
	{ (const uint32_t []) { 201, 202, 204, 111, 105, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_UNKNOWN },
	/* check L - K == 2 unknown cases (204 ?? 111 ?? 104) */
	{ (const uint32_t []) { 201, 202, 204, 111, 104, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_UNKNOWN },
	/* check L - K == 2 unknown cases (205 -- 112 ?- 104) */
	{ (const uint32_t []) { 201, 202, 205, 112, 104, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_UNKNOWN },
	/* check L - K == 2 unknown cases (204 -? 112 -- 105) */
	{ (const uint32_t []) { 201, 202, 204, 112, 105, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_UNKNOWN },
	/* check L - K == 2 unknown cases (204 -? 112 ?- 104) */
	{ (const uint32_t []) { 201, 202, 204, 112, 104, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_UNKNOWN },
	/* check L - K == 2 unknown cases (205 <- 113 ?> 104) */
	{ (const uint32_t []) { 201, 202, 205, 113, 104, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_UNKNOWN },
	/* check L - K == 2 unknown cases (204 <? 113 -> 105) */
	{ (const uint32_t []) { 201, 202, 204, 113, 105, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_UNKNOWN },
	/* check L - K == 2 unknown cases (204 <? 113 ?> 104) */
	{ (const uint32_t []) { 201, 202, 204, 113, 104, 102, 101 }, 7,
	    ROLE_CUSTOMER, ASPA_UNKNOWN },
};

static struct rde_aspa *
load_test_set(struct aspa_test_set *testv, uint32_t numentries)
{
	struct rde_aspa *aspa;
	size_t data_size = 0;
	uint32_t i;

	for (i = 0; i < numentries; i++)
		data_size += testv[i].pascnt * sizeof(uint32_t);

	aspa = aspa_table_prep(numentries, data_size);

	for (i = numentries; i > 0; i--)
		aspa_add_set(aspa, testv[i - 1].customeras,
		    testv[i - 1].providers, testv[i - 1].pascnt);

	return aspa;
}

static uint8_t
vstate_for_role(struct rde_aspa_state *vstate, enum role role)
{
	if (role != ROLE_CUSTOMER) {
		return (vstate->onlyup);
	} else {
		return (vstate->downup);
	}
}

int
main(int argc, char **argv)
{
	struct rde_aspa *aspa;
	size_t num_cp = sizeof(cp_testset) / sizeof(cp_testset[0]);
	size_t num_aspath = sizeof(aspath_testset) / sizeof(aspath_testset[0]);
	size_t num_aspa = sizeof(aspa_testset) / sizeof(aspa_testset[0]);
	size_t i;
	int cp_failed = 0, aspath_failed = 0, aspa_failed = 0;

	/* first test, loading empty aspa table works. */
	aspa = load_test_set(NULL, 0);
	assert(aspa == NULL);
	aspa_table_free(aspa);

	aspa = load_test_set(testset, sizeof(testset) / sizeof(testset[0]));
	assert(aspa != NULL);

	printf("testing aspa_cp_lookup: ");
	for (i = 0; i < num_cp; i++) {
		uint8_t r;
		r = aspa_cp_lookup(aspa, cp_testset[i].customeras,
		    cp_testset[i].provideras);

		if (cp_testset[i].expected_result != r) {
			printf("failed: cp_testset[%zu]: "
			    "cas %u pas %u -> %d got %d\n", i, 
			    cp_testset[i].customeras,
			    cp_testset[i].provideras,
			    cp_testset[i].expected_result,
			    r);
			cp_failed = 1;
		}
	}
	if (!cp_failed)
		printf("OK\n");

	printf("testing aspa_check_aspath: ");
	for (i = 0; i < num_aspath; i++) {
		struct aspa_state st, revst;
		struct aspath *a;

		memset(&st, 0, sizeof(st));
		a = build_aspath(aspath_testset[i].aspath,
		    aspath_testset[i].aspathcnt, 0);
		if (aspa_check_aspath(aspa, a, &st) == -1) {
			printf("failed: aspath_testset[%zu]: "
			    "aspath %s got -1\n", i,
			    print_aspath(aspath_testset[i].aspath,
			    aspath_testset[i].aspathcnt));
			aspath_failed = 1;
		}

		if (memcmp(&aspath_testset[i].state, &st, sizeof(st))) {
			printf("failed: aspath_testset[%zu]: aspath %s "
			    "bad state", i,
			    print_aspath(aspath_testset[i].aspath,
			    aspath_testset[i].aspathcnt));
			print_state(&aspath_testset[i].state, &st);
			printf("\n");
			aspath_failed = 1;
		}
		free(a);

		memset(&st, 0, sizeof(st));
		a = build_aspath(aspath_testset[i].aspath,
		    aspath_testset[i].aspathcnt, 1);
		if (aspa_check_aspath(aspa, a, &st) == -1) {
			printf("failed: reverse aspath_testset[%zu]: "
			    "aspath %s got -1\n", i,
			    print_aspath(aspath_testset[i].aspath,
			    aspath_testset[i].aspathcnt));
			aspath_failed = 1;
		}

		reverse_state(&aspath_testset[i].state, &revst);
		if (memcmp(&revst, &st, sizeof(st))) {
			printf("failed: reverse aspath_testset[%zu]: aspath %s "
			    "bad state", i,
			    print_aspath(aspath_testset[i].aspath,
			    aspath_testset[i].aspathcnt));
			print_state(&revst, &st);
			printf("\n");
			aspath_failed = 1;
		}
		free(a);
	}
	if (!aspath_failed)
		printf("OK\n");

	printf("testing aspa_validation: ");
	for (i = 0; i < num_aspa; i++) {
		struct aspath *a;
		struct rde_aspa_state vstate;
		uint8_t rv;

		a = build_aspath(aspa_testset[i].aspath,
		    aspa_testset[i].aspathcnt, 0);
		aspa_validation(aspa, a, &vstate);

		rv = vstate_for_role(&vstate, aspa_testset[i].role);

		if (aspa_testset[i].expected_result != rv) {
			printf("failed: aspa_testset[%zu]: aspath %s role %d "
			    "want %d got %d", i,
			    print_aspath(aspa_testset[i].aspath,
			    aspa_testset[i].aspathcnt),
			    aspa_testset[i].role,
			    aspa_testset[i].expected_result,
			    rv);
			aspa_failed = 1;
		}

		free(a);
	}
	if (!aspa_failed)
		printf("OK\n");

	aspa_table_free(aspa);

	return cp_failed | aspath_failed | aspa_failed;
}

__dead void
fatalx(const char *emsg, ...)
{
	va_list ap;
	va_start(ap, emsg);
	verrx(2, emsg, ap);
}

__dead void
fatal(const char *emsg, ...)
{
	va_list ap;
	va_start(ap, emsg);
	verr(2, emsg, ap);
}

uint32_t
aspath_extract(const void *seg, int pos)
{
	const u_char	*ptr = seg;
	uint32_t	 as;

	/* minimal pos check, return 0 since that is an invalid ASN */
	if (pos < 0 || pos >= ptr[1])
		return (0);
	ptr += 2 + sizeof(uint32_t) * pos;
	memcpy(&as, ptr, sizeof(uint32_t));
	return (ntohl(as));
}

static struct aspath *
build_aspath(const uint32_t *asns, uint32_t asncnt, int rev)
{
	struct aspath *aspath;
	uint32_t i, idx, as;
	uint16_t len;

	/* don't mess around with multi segment ASPATHs */
	if (asncnt >= 255)
		errx(1, "asncnt too big");

	if (asncnt == 0)
		len = 0;
	else
		len = 2 + sizeof(uint32_t) * asncnt;
	aspath = malloc(ASPATH_HEADER_SIZE + len);
	 if (aspath == NULL)
		err(1, NULL);

	aspath->len = len;
	aspath->ascnt = asncnt; /* lie but nothing cares */
	aspath->source_as = 0;

	if (len == 0)
		return aspath;

	if (rev)
		aspath->source_as = asns[asncnt - 1];
	else
		aspath->source_as = asns[0];

	aspath->data[0] = AS_SEQUENCE;
	aspath->data[1] = asncnt;
	for (i = 0; i < asncnt; i++) {
		if (rev)
			idx = asncnt - 1 - i;
		else
			idx = i;
		as = htonl(asns[idx]);
		memcpy(aspath->data + 2 + sizeof(as) * i, &as,
		   sizeof(uint32_t));
	}

	return aspath;
}

static const char *
print_aspath(const uint32_t *asns, uint32_t asncnt)
{
	static char buf[1024];
	char b[16];
	uint32_t i;

	strlcpy(buf, "", sizeof(buf));
	for (i = 0; i < asncnt; i++) {
		snprintf(b, sizeof(b), "%d", asns[i]);
		if (i > 0)
			strlcat(buf, " ", sizeof(buf));
		strlcat(buf, b, sizeof(buf));
	}
	return buf;
}

static void
reverse_state(struct aspa_state *in, struct aspa_state *rev)
{
	memset(rev, 0, sizeof(*rev));
	rev->nhops = in->nhops;
	rev->nup_p = in->nhops + 1 - in->ndown_p;
	if (in->ndown_u != 0)
		rev->nup_u = in->nhops + 1 - in->ndown_u;
	if (in->ndown_np != 0)
		rev->nup_np = in->nhops + 1 - in->ndown_np;
	rev->ndown_p = in->nhops + 1 - in->nup_p;
	if (in->nup_u != 0)
		rev->ndown_u = in->nhops + 1 - in->nup_u;
	if (in->nup_np != 0)
		rev->ndown_np = in->nhops + 1 - in->nup_np;
}

static void
print_state(struct aspa_state *a, struct aspa_state *b)
{
	if (a->nhops != b->nhops)
		printf(" nhops %d != %d", a->nhops, b->nhops);
	if (a->nup_p != b->nup_p)
		printf(" nup_p %d != %d", a->nup_p, b->nup_p);
	if (a->nup_u != b->nup_u)
		printf(" nup_u %d != %d", a->nup_u, b->nup_u);
	if (a->nup_np != b->nup_np)
		printf(" nup_np %d != %d", a->nup_np, b->nup_np);
	if (a->ndown_p != b->ndown_p)
		printf(" ndown_p %d != %d", a->ndown_p, b->ndown_p);
	if (a->ndown_u != b->ndown_u)
		printf(" ndown_u %d != %d", a->ndown_u, b->ndown_u);
	if (a->ndown_np != b->ndown_np)
		printf(" ndown_np %d != %d", a->ndown_np, b->ndown_np);
}
