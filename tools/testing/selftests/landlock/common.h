/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Landlock test helpers
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2019-2020 ANSSI
 * Copyright © 2021 Microsoft Corporation
 */

#include <errno.h>
#include <linux/landlock.h>
#include <sys/capability.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../kselftest_harness.h"

/*
 * TEST_F_FORK() is useful when a test drop privileges but the corresponding
 * FIXTURE_TEARDOWN() requires them (e.g. to remove files from a directory
 * where write actions are denied).  For convenience, FIXTURE_TEARDOWN() is
 * also called when the test failed, but not when FIXTURE_SETUP() failed.  For
 * this to be possible, we must not call abort() but instead exit smoothly
 * (hence the step print).
 */
/* clang-format off */
#define TEST_F_FORK(fixture_name, test_name) \
	static void fixture_name##_##test_name##_child( \
		struct __test_metadata *_metadata, \
		FIXTURE_DATA(fixture_name) *self, \
		const FIXTURE_VARIANT(fixture_name) *variant); \
	TEST_F(fixture_name, test_name) \
	{ \
		int status; \
		const pid_t child = fork(); \
		if (child < 0) \
			abort(); \
		if (child == 0) { \
			_metadata->no_print = 1; \
			fixture_name##_##test_name##_child(_metadata, self, variant); \
			if (_metadata->skip) \
				_exit(255); \
			if (_metadata->passed) \
				_exit(0); \
			_exit(_metadata->step); \
		} \
		if (child != waitpid(child, &status, 0)) \
			abort(); \
		if (WIFSIGNALED(status) || !WIFEXITED(status)) { \
			_metadata->passed = 0; \
			_metadata->step = 1; \
			return; \
		} \
		switch (WEXITSTATUS(status)) { \
		case 0: \
			_metadata->passed = 1; \
			break; \
		case 255: \
			_metadata->passed = 1; \
			_metadata->skip = 1; \
			break; \
		default: \
			_metadata->passed = 0; \
			_metadata->step = WEXITSTATUS(status); \
			break; \
		} \
	} \
	static void fixture_name##_##test_name##_child( \
		struct __test_metadata __attribute__((unused)) *_metadata, \
		FIXTURE_DATA(fixture_name) __attribute__((unused)) *self, \
		const FIXTURE_VARIANT(fixture_name) \
			__attribute__((unused)) *variant)
/* clang-format on */

#ifndef landlock_create_ruleset
static inline int
landlock_create_ruleset(const struct landlock_ruleset_attr *const attr,
			const size_t size, const __u32 flags)
{
	return syscall(__NR_landlock_create_ruleset, attr, size, flags);
}
#endif

#ifndef landlock_add_rule
static inline int landlock_add_rule(const int ruleset_fd,
				    const enum landlock_rule_type rule_type,
				    const void *const rule_attr,
				    const __u32 flags)
{
	return syscall(__NR_landlock_add_rule, ruleset_fd, rule_type, rule_attr,
		       flags);
}
#endif

#ifndef landlock_restrict_self
static inline int landlock_restrict_self(const int ruleset_fd,
					 const __u32 flags)
{
	return syscall(__NR_landlock_restrict_self, ruleset_fd, flags);
}
#endif

static void _init_caps(struct __test_metadata *const _metadata, bool drop_all)
{
	cap_t cap_p;
	/* Only these three capabilities are useful for the tests. */
	const cap_value_t caps[] = {
		CAP_DAC_OVERRIDE,
		CAP_MKNOD,
		CAP_SYS_ADMIN,
		CAP_SYS_CHROOT,
	};

	cap_p = cap_get_proc();
	EXPECT_NE(NULL, cap_p)
	{
		TH_LOG("Failed to cap_get_proc: %s", strerror(errno));
	}
	EXPECT_NE(-1, cap_clear(cap_p))
	{
		TH_LOG("Failed to cap_clear: %s", strerror(errno));
	}
	if (!drop_all) {
		EXPECT_NE(-1, cap_set_flag(cap_p, CAP_PERMITTED,
					   ARRAY_SIZE(caps), caps, CAP_SET))
		{
			TH_LOG("Failed to cap_set_flag: %s", strerror(errno));
		}
	}
	EXPECT_NE(-1, cap_set_proc(cap_p))
	{
		TH_LOG("Failed to cap_set_proc: %s", strerror(errno));
	}
	EXPECT_NE(-1, cap_free(cap_p))
	{
		TH_LOG("Failed to cap_free: %s", strerror(errno));
	}
}

/* We cannot put such helpers in a library because of kselftest_harness.h . */
__attribute__((__unused__)) static void
disable_caps(struct __test_metadata *const _metadata)
{
	_init_caps(_metadata, false);
}

__attribute__((__unused__)) static void
drop_caps(struct __test_metadata *const _metadata)
{
	_init_caps(_metadata, true);
}

static void _effective_cap(struct __test_metadata *const _metadata,
			   const cap_value_t caps, const cap_flag_value_t value)
{
	cap_t cap_p;

	cap_p = cap_get_proc();
	EXPECT_NE(NULL, cap_p)
	{
		TH_LOG("Failed to cap_get_proc: %s", strerror(errno));
	}
	EXPECT_NE(-1, cap_set_flag(cap_p, CAP_EFFECTIVE, 1, &caps, value))
	{
		TH_LOG("Failed to cap_set_flag: %s", strerror(errno));
	}
	EXPECT_NE(-1, cap_set_proc(cap_p))
	{
		TH_LOG("Failed to cap_set_proc: %s", strerror(errno));
	}
	EXPECT_NE(-1, cap_free(cap_p))
	{
		TH_LOG("Failed to cap_free: %s", strerror(errno));
	}
}

__attribute__((__unused__)) static void
set_cap(struct __test_metadata *const _metadata, const cap_value_t caps)
{
	_effective_cap(_metadata, caps, CAP_SET);
}

__attribute__((__unused__)) static void
clear_cap(struct __test_metadata *const _metadata, const cap_value_t caps)
{
	_effective_cap(_metadata, caps, CAP_CLEAR);
}
