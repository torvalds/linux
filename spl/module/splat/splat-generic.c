/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting LAyer Tests (SPLAT) Generic Tests.
\*****************************************************************************/

#include <sys/sunddi.h>
#include <linux/math64_compat.h>
#include "splat-internal.h"

#define SPLAT_GENERIC_NAME		"generic"
#define SPLAT_GENERIC_DESC		"Kernel Generic Tests"

#define SPLAT_GENERIC_TEST1_ID		0x0d01
#define SPLAT_GENERIC_TEST1_NAME	"ddi_strtoul"
#define SPLAT_GENERIC_TEST1_DESC	"ddi_strtoul Test"

#define SPLAT_GENERIC_TEST2_ID		0x0d02
#define SPLAT_GENERIC_TEST2_NAME	"ddi_strtol"
#define SPLAT_GENERIC_TEST2_DESC	"ddi_strtol Test"

#define SPLAT_GENERIC_TEST3_ID		0x0d03
#define SPLAT_GENERIC_TEST3_NAME	"ddi_strtoull"
#define SPLAT_GENERIC_TEST3_DESC	"ddi_strtoull Test"

#define SPLAT_GENERIC_TEST4_ID		0x0d04
#define SPLAT_GENERIC_TEST4_NAME	"ddi_strtoll"
#define SPLAT_GENERIC_TEST4_DESC	"ddi_strtoll Test"

# define SPLAT_GENERIC_TEST5_ID		0x0d05
# define SPLAT_GENERIC_TEST5_NAME	"udivdi3"
# define SPLAT_GENERIC_TEST5_DESC	"Unsigned Div-64 Test"

# define SPLAT_GENERIC_TEST6_ID		0x0d06
# define SPLAT_GENERIC_TEST6_NAME	"divdi3"
# define SPLAT_GENERIC_TEST6_DESC	"Signed Div-64 Test"

#define STR_POS				"123456789"
#define STR_NEG				"-123456789"
#define STR_BASE			"0xabcdef"
#define STR_RANGE_MAX			"10000000000000000"
#define STR_RANGE_MIN			"-10000000000000000"
#define STR_INVAL1			"12345U"
#define STR_INVAL2			"invald"

#define VAL_POS				123456789
#define VAL_NEG				-123456789
#define VAL_BASE			0xabcdef
#define VAL_INVAL1			12345U

#define define_generic_msg_strtox(type, valtype)			\
static void								\
generic_msg_strto##type(struct file *file, char *msg, int rc, int *err, \
			const char *s, valtype d, char *endptr)		\
{									\
	splat_vprint(file, SPLAT_GENERIC_TEST1_NAME,			\
		     "%s (%d) %s: %s == %lld, 0x%p\n",			\
		     rc ? "Fail" : "Pass", *err, msg, s,		\
		     (unsigned long long)d, endptr);			\
	*err = rc;							\
}

define_generic_msg_strtox(ul, unsigned long);
define_generic_msg_strtox(l, long);
define_generic_msg_strtox(ull, unsigned long long);
define_generic_msg_strtox(ll, long long);

#define define_splat_generic_test_strtox(type, valtype)			\
static int								\
splat_generic_test_strto##type(struct file *file, void *arg)		\
{									\
	int rc, rc1, rc2, rc3, rc4, rc5, rc6, rc7;			\
	char str[20], *endptr;						\
	valtype r;							\
									\
	/* Positive value: expect success */				\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	rc1 = ddi_strto##type(STR_POS, &endptr, 10, &r);		\
	if (rc1 == 0 && r == VAL_POS && endptr && *endptr == '\0')	\
		rc = 0;							\
									\
	generic_msg_strto##type(file, "positive", rc , &rc1,		\
				STR_POS, r, endptr);			\
									\
	/* Negative value: expect success */				\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	strcpy(str, STR_NEG);						\
	rc2 = ddi_strto##type(str, &endptr, 10, &r);			\
	if (#type[0] == 'u') {						\
		if (rc2 == 0 && r == 0 && endptr == str)		\
			rc = 0;						\
	} else {							\
		if (rc2 == 0 && r == VAL_NEG &&				\
		    endptr && *endptr == '\0')				\
			rc = 0;						\
	}								\
									\
	generic_msg_strto##type(file, "negative", rc, &rc2,		\
				STR_NEG, r, endptr);			\
									\
	/* Non decimal base: expect sucess */				\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	rc3 = ddi_strto##type(STR_BASE, &endptr, 0, &r);		\
	if (rc3 == 0 && r == VAL_BASE && endptr && *endptr == '\0')	\
		rc = 0;							\
									\
	generic_msg_strto##type(file, "base", rc, &rc3,			\
				STR_BASE, r, endptr);			\
									\
	/* Max out of range: failure expected, r unchanged */		\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	rc4 = ddi_strto##type(STR_RANGE_MAX, &endptr, 16, &r);		\
	if (rc4 == ERANGE && r == 0 && endptr == NULL)			\
		rc = 0;							\
									\
	generic_msg_strto##type(file, "max", rc, &rc4,			\
				STR_RANGE_MAX, r, endptr);		\
									\
	/* Min out of range: failure expected, r unchanged */		\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	strcpy(str, STR_RANGE_MIN);					\
	rc5 = ddi_strto##type(str, &endptr, 16, &r);			\
	if (#type[0] == 'u') {						\
		if (rc5 == 0 && r == 0 && endptr == str)		\
			rc = 0;						\
	} else {							\
		if (rc5 == ERANGE && r == 0 && endptr == NULL)		\
			rc = 0;						\
	}								\
									\
	generic_msg_strto##type(file, "min", rc, &rc5,			\
				STR_RANGE_MIN, r, endptr);		\
									\
	/* Invalid string: success expected, endptr == 'U' */		\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	rc6 = ddi_strto##type(STR_INVAL1, &endptr, 10, &r);		\
	if (rc6 == 0 && r == VAL_INVAL1 && endptr && *endptr == 'U')	\
		rc = 0;							\
									\
	generic_msg_strto##type(file, "invalid", rc, &rc6,		\
				STR_INVAL1, r, endptr);			\
									\
	/* Invalid string: failure expected, endptr == str */		\
	r = 0;								\
	rc = 1;								\
	endptr = NULL;							\
	strcpy(str, STR_INVAL2);					\
	rc7 = ddi_strto##type(str, &endptr, 10, &r);			\
	if (rc7 == 0 && r == 0 && endptr == str)			\
		rc = 0;							\
									\
	generic_msg_strto##type(file, "invalid", rc, &rc7,		\
				STR_INVAL2, r, endptr);			\
									\
        return (rc1 || rc2 || rc3 || rc4 || rc5 || rc6 || rc7) ?	\
		-EINVAL : 0;						\
}

define_splat_generic_test_strtox(ul, unsigned long);
define_splat_generic_test_strtox(l, long);
define_splat_generic_test_strtox(ull, unsigned long long);
define_splat_generic_test_strtox(ll, long long);

/*
 * The entries in the table are used in all combinations and the
 * return value is checked to ensure it is range.  On 32-bit
 * systems __udivdi3 will be invoked for the 64-bit division.
 * On 64-bit system the native 64-bit divide will be used so
 * __udivdi3 isn't used but we might as well stil run the test.
 */
static int
splat_generic_test_udivdi3(struct file *file, void *arg)
{
	const uint64_t tabu[] = {
	    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	    10, 11, 12, 13, 14, 15, 16, 1000, 2003,
	    32765, 32766, 32767, 32768, 32769, 32760,
	    65533, 65534, 65535, 65536, 65537, 65538,
	    0x7ffffffeULL, 0x7fffffffULL, 0x80000000ULL, 0x80000001ULL,
	    0x7000000000000000ULL, 0x7000000080000000ULL, 0x7000000080000001ULL,
	    0x7fffffffffffffffULL, 0x7fffffff8fffffffULL, 0x7fffffff8ffffff1ULL,
	    0x7fffffff00000000ULL, 0x7fffffff80000000ULL, 0x7fffffff00000001ULL,
	    0x8000000000000000ULL, 0x8000000080000000ULL, 0x8000000080000001ULL,
	    0xc000000000000000ULL, 0xc000000080000000ULL, 0xc000000080000001ULL,
	    0xfffffffffffffffdULL, 0xfffffffffffffffeULL, 0xffffffffffffffffULL,
	};
	uint64_t uu, vu, qu, ru;
	int n, i, j, errors = 0;

	splat_vprint(file, SPLAT_GENERIC_TEST5_NAME, "%s",
	    "Testing unsigned 64-bit division.\n");
	n = sizeof(tabu) / sizeof(tabu[0]);
	for (i = 0; i < n; i++) {
		for (j = 1; j < n; j++) {
			uu = tabu[i];
			vu = tabu[j];
			qu = uu / vu; /* __udivdi3 */
			ru = uu - qu * vu;
			if (qu > uu || ru >= vu) {
				splat_vprint(file, SPLAT_GENERIC_TEST5_NAME,
				    "%016llx/%016llx != %016llx rem %016llx\n",
				    uu, vu, qu, ru);
				errors++;
			}
		}
	}

	if (errors) {
		splat_vprint(file, SPLAT_GENERIC_TEST5_NAME,
		    "Failed %d/%d tests\n", errors, n * (n - 1));
		return -ERANGE;
	}

	splat_vprint(file, SPLAT_GENERIC_TEST5_NAME,
	    "Passed all %d tests\n", n * (n - 1));

	return 0;
}

/*
 * The entries the table are used in all combinations, with + and - signs
 * preceding them.  The return value is checked to ensure it is range.
 * On 32-bit systems __divdi3 will be invoked for the 64-bit division.
 * On 64-bit system the native 64-bit divide will be used so __divdi3
 *  isn't used but we might as well stil run the test.
 */
static int
splat_generic_test_divdi3(struct file *file, void *arg)
{
	const int64_t tabs[] = {
	    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	    10, 11, 12, 13, 14, 15, 16, 1000, 2003,
	    32765, 32766, 32767, 32768, 32769, 32760,
	    65533, 65534, 65535, 65536, 65537, 65538,
	    0x7ffffffeLL, 0x7fffffffLL, 0x80000000LL, 0x80000001LL,
	    0x7000000000000000LL, 0x7000000080000000LL, 0x7000000080000001LL,
	    0x7fffffffffffffffLL, 0x7fffffff8fffffffLL, 0x7fffffff8ffffff1LL,
	    0x7fffffff00000000LL, 0x7fffffff80000000LL, 0x7fffffff00000001LL,
	    0x0123456789abcdefLL, 0x00000000abcdef01LL, 0x0000000012345678LL,
#if BITS_PER_LONG == 32
	    0x8000000000000000LL, 0x8000000080000000LL, 0x8000000080000001LL,
#endif
	};
	int64_t u, v, q, r;
	int n, i, j, k, errors = 0;

	splat_vprint(file, SPLAT_GENERIC_TEST6_NAME, "%s",
	    "Testing signed 64-bit division.\n");
	n = sizeof(tabs) / sizeof(tabs[0]);
	for (i = 0; i < n; i++) {
		for (j = 1; j < n; j++) {
			for (k = 0; k <= 3; k++) {
				u = (k & 1)  ? -tabs[i] : tabs[i];
				v = (k >= 2) ? -tabs[j] : tabs[j];

				q = u / v; /* __divdi3 */
				r = u - q * v;
				if (abs64(q) >  abs64(u) ||
				    abs64(r) >= abs64(v) ||
				    (r != 0 && (r ^ u) < 0)) {
					splat_vprint(file,
					    SPLAT_GENERIC_TEST6_NAME,
					    "%016llx/%016llx != %016llx "
					    "rem %016llx\n", u, v, q, r);
					errors++;
				}
			}
		}
	}

	if (errors) {
		splat_vprint(file, SPLAT_GENERIC_TEST6_NAME,
		    "Failed %d/%d tests\n", errors, n * (n - 1));
		return -ERANGE;
	}

	splat_vprint(file, SPLAT_GENERIC_TEST6_NAME,
	    "Passed all %d tests\n", n * (n - 1));

	return 0;
}

splat_subsystem_t *
splat_generic_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_GENERIC_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_GENERIC_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_GENERIC;

        SPLAT_TEST_INIT(sub, SPLAT_GENERIC_TEST1_NAME, SPLAT_GENERIC_TEST1_DESC,
	                SPLAT_GENERIC_TEST1_ID, splat_generic_test_strtoul);
        SPLAT_TEST_INIT(sub, SPLAT_GENERIC_TEST2_NAME, SPLAT_GENERIC_TEST2_DESC,
	                SPLAT_GENERIC_TEST2_ID, splat_generic_test_strtol);
        SPLAT_TEST_INIT(sub, SPLAT_GENERIC_TEST3_NAME, SPLAT_GENERIC_TEST3_DESC,
	                SPLAT_GENERIC_TEST3_ID, splat_generic_test_strtoull);
        SPLAT_TEST_INIT(sub, SPLAT_GENERIC_TEST4_NAME, SPLAT_GENERIC_TEST4_DESC,
	                SPLAT_GENERIC_TEST4_ID, splat_generic_test_strtoll);
        SPLAT_TEST_INIT(sub, SPLAT_GENERIC_TEST5_NAME, SPLAT_GENERIC_TEST5_DESC,
	                SPLAT_GENERIC_TEST5_ID, splat_generic_test_udivdi3);
        SPLAT_TEST_INIT(sub, SPLAT_GENERIC_TEST6_NAME, SPLAT_GENERIC_TEST6_DESC,
	                SPLAT_GENERIC_TEST6_ID, splat_generic_test_divdi3);

        return sub;
}

void
splat_generic_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);

        SPLAT_TEST_FINI(sub, SPLAT_GENERIC_TEST6_ID);
        SPLAT_TEST_FINI(sub, SPLAT_GENERIC_TEST5_ID);
        SPLAT_TEST_FINI(sub, SPLAT_GENERIC_TEST4_ID);
        SPLAT_TEST_FINI(sub, SPLAT_GENERIC_TEST3_ID);
        SPLAT_TEST_FINI(sub, SPLAT_GENERIC_TEST2_ID);
        SPLAT_TEST_FINI(sub, SPLAT_GENERIC_TEST1_ID);

        kfree(sub);
}

int
splat_generic_id(void)
{
        return SPLAT_SUBSYSTEM_GENERIC;
}
