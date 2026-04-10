// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *   KUnit tests of SMB1 maperror
 *
 *   Copyright (C) 2026 KylinSoft Co., Ltd. All rights reserved.
 *   Author(s): Youling Tang <tangyouling@kylinos.cn>
 *              ChenXiaoSong <chenxiaosong@kylinos.cn>
 *
 */

#include <kunit/test.h>
#include "smb1proto.h"
#include "nterr.h"
#include "smberr.h"

#define DEFINE_CHECK_SEARCH_FUNC(__struct_name, __field,		\
				 __array, __num)			\
static void check_search_ ## __array(struct kunit *test)		\
{									\
	unsigned int i;							\
	const struct __struct_name *expect, *result;			\
									\
	for (i = 0; i < __num; i++) {					\
		expect = &__array ## _test[i];				\
		result = search_ ## __array ## _test(expect->__field);	\
		KUNIT_ASSERT_NOT_NULL(test, result);			\
		test_cmp_ ## __struct_name(test, expect, result);	\
	}								\
}

static void
test_cmp_ntstatus_to_dos_err(struct kunit *test,
			     const struct ntstatus_to_dos_err *expect,
			     const struct ntstatus_to_dos_err *result)
{
	KUNIT_EXPECT_EQ(test, expect->dos_class, result->dos_class);
	KUNIT_EXPECT_EQ(test, expect->dos_code, result->dos_code);
	KUNIT_EXPECT_EQ(test, expect->ntstatus, result->ntstatus);
	KUNIT_EXPECT_STREQ(test, expect->nt_errstr, result->nt_errstr);
}

static void
test_cmp_smb_to_posix_error(struct kunit *test,
			    const struct smb_to_posix_error *expect,
			    const struct smb_to_posix_error *result)
{
	KUNIT_EXPECT_EQ(test, expect->smb_err, result->smb_err);
	KUNIT_EXPECT_EQ(test, expect->posix_code, result->posix_code);
}

/* check_search_ntstatus_to_dos_map */
DEFINE_CHECK_SEARCH_FUNC(ntstatus_to_dos_err, ntstatus, ntstatus_to_dos_map,
			 ntstatus_to_dos_num);
/* check_search_mapping_table_ERRDOS */
DEFINE_CHECK_SEARCH_FUNC(smb_to_posix_error, smb_err, mapping_table_ERRDOS,
			 mapping_table_ERRDOS_num);
/* check_search_mapping_table_ERRSRV */
DEFINE_CHECK_SEARCH_FUNC(smb_to_posix_error, smb_err, mapping_table_ERRSRV,
			 mapping_table_ERRSRV_num);

static struct kunit_case maperror_test_cases[] = {
	KUNIT_CASE(check_search_ntstatus_to_dos_map),
	KUNIT_CASE(check_search_mapping_table_ERRDOS),
	KUNIT_CASE(check_search_mapping_table_ERRSRV),
	{}
};

static struct kunit_suite maperror_suite = {
	.name = "smb1_maperror",
	.test_cases = maperror_test_cases,
};

kunit_test_suite(maperror_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests of SMB1 maperror");
