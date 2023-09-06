/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2013, Michael Ellerman, IBM Corp.
 */

#ifndef _SELFTESTS_POWERPC_SUBUNIT_H
#define _SELFTESTS_POWERPC_SUBUNIT_H

static inline void test_start(const char *name)
{
	printf("test: %s\n", name);
}

static inline void test_failure_detail(const char *name, const char *detail)
{
	printf("failure: %s [%s]\n", name, detail);
}

static inline void test_failure(const char *name)
{
	printf("failure: %s\n", name);
}

static inline void test_error(const char *name)
{
	printf("error: %s\n", name);
}

static inline void test_skip(const char *name)
{
	printf("skip: %s\n", name);
}

static inline void test_success(const char *name)
{
	printf("success: %s\n", name);
}

static inline void test_finish(const char *name, int status)
{
	if (status)
		test_failure(name);
	else
		test_success(name);
}

static inline void test_set_git_version(const char *value)
{
	printf("tags: git_version:%s\n", value);
}

#endif /* _SELFTESTS_POWERPC_SUBUNIT_H */
