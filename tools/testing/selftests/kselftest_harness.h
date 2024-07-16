/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 *
 * kselftest_harness.h: simple C unit test helper.
 *
 * See documentation in Documentation/dev-tools/kselftest.rst
 *
 * API inspired by code.google.com/p/googletest
 */

/**
 * DOC: example
 *
 * .. code-block:: c
 *
 *    #include "../kselftest_harness.h"
 *
 *    TEST(standalone_test) {
 *      do_some_stuff;
 *      EXPECT_GT(10, stuff) {
 *         stuff_state_t state;
 *         enumerate_stuff_state(&state);
 *         TH_LOG("expectation failed with state: %s", state.msg);
 *      }
 *      more_stuff;
 *      ASSERT_NE(some_stuff, NULL) TH_LOG("how did it happen?!");
 *      last_stuff;
 *      EXPECT_EQ(0, last_stuff);
 *    }
 *
 *    FIXTURE(my_fixture) {
 *      mytype_t *data;
 *      int awesomeness_level;
 *    };
 *    FIXTURE_SETUP(my_fixture) {
 *      self->data = mytype_new();
 *      ASSERT_NE(NULL, self->data);
 *    }
 *    FIXTURE_TEARDOWN(my_fixture) {
 *      mytype_free(self->data);
 *    }
 *    TEST_F(my_fixture, data_is_good) {
 *      EXPECT_EQ(1, is_my_data_good(self->data));
 *    }
 *
 *    TEST_HARNESS_MAIN
 */

#ifndef __KSELFTEST_HARNESS_H
#define __KSELFTEST_HARNESS_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <asm/types.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <setjmp.h>
#include <syscall.h>
#include <linux/sched.h>

#include "kselftest.h"

#define TEST_TIMEOUT_DEFAULT 30

/* Utilities exposed to the test definitions */
#ifndef TH_LOG_STREAM
#  define TH_LOG_STREAM stderr
#endif

#ifndef TH_LOG_ENABLED
#  define TH_LOG_ENABLED 1
#endif

/* Wait for the child process to end but without sharing memory mapping. */
static inline pid_t clone3_vfork(void)
{
	struct clone_args args = {
		.flags = CLONE_VFORK,
		.exit_signal = SIGCHLD,
	};

	return syscall(__NR_clone3, &args, sizeof(args));
}

/**
 * TH_LOG()
 *
 * @fmt: format string
 * @...: optional arguments
 *
 * .. code-block:: c
 *
 *     TH_LOG(format, ...)
 *
 * Optional debug logging function available for use in tests.
 * Logging may be enabled or disabled by defining TH_LOG_ENABLED.
 * E.g., #define TH_LOG_ENABLED 1
 *
 * If no definition is provided, logging is enabled by default.
 */
#define TH_LOG(fmt, ...) do { \
	if (TH_LOG_ENABLED) \
		__TH_LOG(fmt, ##__VA_ARGS__); \
} while (0)

/* Unconditional logger for internal use. */
#define __TH_LOG(fmt, ...) \
		fprintf(TH_LOG_STREAM, "# %s:%d:%s:" fmt "\n", \
			__FILE__, __LINE__, _metadata->name, ##__VA_ARGS__)

/**
 * SKIP()
 *
 * @statement: statement to run after reporting SKIP
 * @fmt: format string
 * @...: optional arguments
 *
 * .. code-block:: c
 *
 *     SKIP(statement, fmt, ...);
 *
 * This forces a "pass" after reporting why something is being skipped
 * and runs "statement", which is usually "return" or "goto skip".
 */
#define SKIP(statement, fmt, ...) do { \
	snprintf(_metadata->results->reason, \
		 sizeof(_metadata->results->reason), fmt, ##__VA_ARGS__); \
	if (TH_LOG_ENABLED) { \
		fprintf(TH_LOG_STREAM, "#      SKIP      %s\n", \
			_metadata->results->reason); \
	} \
	_metadata->exit_code = KSFT_SKIP; \
	_metadata->trigger = 0; \
	statement; \
} while (0)

/**
 * TEST() - Defines the test function and creates the registration
 * stub
 *
 * @test_name: test name
 *
 * .. code-block:: c
 *
 *     TEST(name) { implementation }
 *
 * Defines a test by name.
 * Names must be unique and tests must not be run in parallel.  The
 * implementation containing block is a function and scoping should be treated
 * as such.  Returning early may be performed with a bare "return;" statement.
 *
 * EXPECT_* and ASSERT_* are valid in a TEST() { } context.
 */
#define TEST(test_name) __TEST_IMPL(test_name, -1)

/**
 * TEST_SIGNAL()
 *
 * @test_name: test name
 * @signal: signal number
 *
 * .. code-block:: c
 *
 *     TEST_SIGNAL(name, signal) { implementation }
 *
 * Defines a test by name and the expected term signal.
 * Names must be unique and tests must not be run in parallel.  The
 * implementation containing block is a function and scoping should be treated
 * as such.  Returning early may be performed with a bare "return;" statement.
 *
 * EXPECT_* and ASSERT_* are valid in a TEST() { } context.
 */
#define TEST_SIGNAL(test_name, signal) __TEST_IMPL(test_name, signal)

#define __TEST_IMPL(test_name, _signal) \
	static void test_name(struct __test_metadata *_metadata); \
	static inline void wrapper_##test_name( \
		struct __test_metadata *_metadata, \
		struct __fixture_variant_metadata *variant) \
	{ \
		_metadata->setup_completed = true; \
		if (setjmp(_metadata->env) == 0) \
			test_name(_metadata); \
		__test_check_assert(_metadata); \
	} \
	static struct __test_metadata _##test_name##_object = \
		{ .name = #test_name, \
		  .fn = &wrapper_##test_name, \
		  .fixture = &_fixture_global, \
		  .termsig = _signal, \
		  .timeout = TEST_TIMEOUT_DEFAULT, }; \
	static void __attribute__((constructor)) _register_##test_name(void) \
	{ \
		__register_test(&_##test_name##_object); \
	} \
	static void test_name( \
		struct __test_metadata __attribute__((unused)) *_metadata)

/**
 * FIXTURE_DATA() - Wraps the struct name so we have one less
 * argument to pass around
 *
 * @datatype_name: datatype name
 *
 * .. code-block:: c
 *
 *     FIXTURE_DATA(datatype_name)
 *
 * Almost always, you want just FIXTURE() instead (see below).
 * This call may be used when the type of the fixture data
 * is needed.  In general, this should not be needed unless
 * the *self* is being passed to a helper directly.
 */
#define FIXTURE_DATA(datatype_name) struct _test_data_##datatype_name

/**
 * FIXTURE() - Called once per fixture to setup the data and
 * register
 *
 * @fixture_name: fixture name
 *
 * .. code-block:: c
 *
 *     FIXTURE(fixture_name) {
 *       type property1;
 *       ...
 *     };
 *
 * Defines the data provided to TEST_F()-defined tests as *self*.  It should be
 * populated and cleaned up using FIXTURE_SETUP() and FIXTURE_TEARDOWN().
 */
#define FIXTURE(fixture_name) \
	FIXTURE_VARIANT(fixture_name); \
	static struct __fixture_metadata _##fixture_name##_fixture_object = \
		{ .name =  #fixture_name, }; \
	static void __attribute__((constructor)) \
	_register_##fixture_name##_data(void) \
	{ \
		__register_fixture(&_##fixture_name##_fixture_object); \
	} \
	FIXTURE_DATA(fixture_name)

/**
 * FIXTURE_SETUP() - Prepares the setup function for the fixture.
 * *_metadata* is included so that EXPECT_*, ASSERT_* etc. work correctly.
 *
 * @fixture_name: fixture name
 *
 * .. code-block:: c
 *
 *     FIXTURE_SETUP(fixture_name) { implementation }
 *
 * Populates the required "setup" function for a fixture.  An instance of the
 * datatype defined with FIXTURE_DATA() will be exposed as *self* for the
 * implementation.
 *
 * ASSERT_* are valid for use in this context and will prempt the execution
 * of any dependent fixture tests.
 *
 * A bare "return;" statement may be used to return early.
 */
#define FIXTURE_SETUP(fixture_name) \
	void fixture_name##_setup( \
		struct __test_metadata __attribute__((unused)) *_metadata, \
		FIXTURE_DATA(fixture_name) __attribute__((unused)) *self, \
		const FIXTURE_VARIANT(fixture_name) \
			__attribute__((unused)) *variant)

/**
 * FIXTURE_TEARDOWN()
 * *_metadata* is included so that EXPECT_*, ASSERT_* etc. work correctly.
 *
 * @fixture_name: fixture name
 *
 * .. code-block:: c
 *
 *     FIXTURE_TEARDOWN(fixture_name) { implementation }
 *
 * Populates the required "teardown" function for a fixture.  An instance of the
 * datatype defined with FIXTURE_DATA() will be exposed as *self* for the
 * implementation to clean up.
 *
 * A bare "return;" statement may be used to return early.
 */
#define FIXTURE_TEARDOWN(fixture_name) \
	static const bool fixture_name##_teardown_parent; \
	__FIXTURE_TEARDOWN(fixture_name)

/**
 * FIXTURE_TEARDOWN_PARENT()
 * *_metadata* is included so that EXPECT_*, ASSERT_* etc. work correctly.
 *
 * @fixture_name: fixture name
 *
 * .. code-block:: c
 *
 *     FIXTURE_TEARDOWN_PARENT(fixture_name) { implementation }
 *
 * Same as FIXTURE_TEARDOWN() but run this code in a parent process.  This
 * enables the test process to drop its privileges without impacting the
 * related FIXTURE_TEARDOWN_PARENT() (e.g. to remove files from a directory
 * where write access was dropped).
 *
 * To make it possible for the parent process to use *self*, share (MAP_SHARED)
 * the fixture data between all forked processes.
 */
#define FIXTURE_TEARDOWN_PARENT(fixture_name) \
	static const bool fixture_name##_teardown_parent = true; \
	__FIXTURE_TEARDOWN(fixture_name)

#define __FIXTURE_TEARDOWN(fixture_name) \
	void fixture_name##_teardown( \
		struct __test_metadata __attribute__((unused)) *_metadata, \
		FIXTURE_DATA(fixture_name) __attribute__((unused)) *self, \
		const FIXTURE_VARIANT(fixture_name) \
			__attribute__((unused)) *variant)

/**
 * FIXTURE_VARIANT() - Optionally called once per fixture
 * to declare fixture variant
 *
 * @fixture_name: fixture name
 *
 * .. code-block:: c
 *
 *     FIXTURE_VARIANT(fixture_name) {
 *       type property1;
 *       ...
 *     };
 *
 * Defines type of constant parameters provided to FIXTURE_SETUP(), TEST_F() and
 * FIXTURE_TEARDOWN as *variant*. Variants allow the same tests to be run with
 * different arguments.
 */
#define FIXTURE_VARIANT(fixture_name) struct _fixture_variant_##fixture_name

/**
 * FIXTURE_VARIANT_ADD() - Called once per fixture
 * variant to setup and register the data
 *
 * @fixture_name: fixture name
 * @variant_name: name of the parameter set
 *
 * .. code-block:: c
 *
 *     FIXTURE_VARIANT_ADD(fixture_name, variant_name) {
 *       .property1 = val1,
 *       ...
 *     };
 *
 * Defines a variant of the test fixture, provided to FIXTURE_SETUP() and
 * TEST_F() as *variant*. Tests of each fixture will be run once for each
 * variant.
 */
#define FIXTURE_VARIANT_ADD(fixture_name, variant_name) \
	extern const FIXTURE_VARIANT(fixture_name) \
		_##fixture_name##_##variant_name##_variant; \
	static struct __fixture_variant_metadata \
		_##fixture_name##_##variant_name##_object = \
		{ .name = #variant_name, \
		  .data = &_##fixture_name##_##variant_name##_variant}; \
	static void __attribute__((constructor)) \
		_register_##fixture_name##_##variant_name(void) \
	{ \
		__register_fixture_variant(&_##fixture_name##_fixture_object, \
			&_##fixture_name##_##variant_name##_object);	\
	} \
	const FIXTURE_VARIANT(fixture_name) \
		_##fixture_name##_##variant_name##_variant =

/**
 * TEST_F() - Emits test registration and helpers for
 * fixture-based test cases
 *
 * @fixture_name: fixture name
 * @test_name: test name
 *
 * .. code-block:: c
 *
 *     TEST_F(fixture, name) { implementation }
 *
 * Defines a test that depends on a fixture (e.g., is part of a test case).
 * Very similar to TEST() except that *self* is the setup instance of fixture's
 * datatype exposed for use by the implementation.
 *
 * The _metadata object is shared (MAP_SHARED) with all the potential forked
 * processes, which enables them to use EXCEPT_*() and ASSERT_*().
 *
 * The *self* object is only shared with the potential forked processes if
 * FIXTURE_TEARDOWN_PARENT() is used instead of FIXTURE_TEARDOWN().
 */
#define TEST_F(fixture_name, test_name) \
	__TEST_F_IMPL(fixture_name, test_name, -1, TEST_TIMEOUT_DEFAULT)

#define TEST_F_SIGNAL(fixture_name, test_name, signal) \
	__TEST_F_IMPL(fixture_name, test_name, signal, TEST_TIMEOUT_DEFAULT)

#define TEST_F_TIMEOUT(fixture_name, test_name, timeout) \
	__TEST_F_IMPL(fixture_name, test_name, -1, timeout)

#define __TEST_F_IMPL(fixture_name, test_name, signal, tmout) \
	static void fixture_name##_##test_name( \
		struct __test_metadata *_metadata, \
		FIXTURE_DATA(fixture_name) *self, \
		const FIXTURE_VARIANT(fixture_name) *variant); \
	static inline void wrapper_##fixture_name##_##test_name( \
		struct __test_metadata *_metadata, \
		struct __fixture_variant_metadata *variant) \
	{ \
		/* fixture data is alloced, setup, and torn down per call. */ \
		FIXTURE_DATA(fixture_name) self_private, *self = NULL; \
		pid_t child = 1; \
		int status = 0; \
		/* Makes sure there is only one teardown, even when child forks again. */ \
		bool *teardown = mmap(NULL, sizeof(*teardown), \
			PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0); \
		*teardown = false; \
		if (sizeof(*self) > 0) { \
			if (fixture_name##_teardown_parent) { \
				self = mmap(NULL, sizeof(*self), PROT_READ | PROT_WRITE, \
					MAP_SHARED | MAP_ANONYMOUS, -1, 0); \
			} else { \
				memset(&self_private, 0, sizeof(self_private)); \
				self = &self_private; \
			} \
		} \
		if (setjmp(_metadata->env) == 0) { \
			/* _metadata and potentially self are shared with all forks. */ \
			child = clone3_vfork(); \
			if (child == 0) { \
				fixture_name##_setup(_metadata, self, variant->data); \
				/* Let setup failure terminate early. */ \
				if (_metadata->exit_code) \
					_exit(0); \
				_metadata->setup_completed = true; \
				fixture_name##_##test_name(_metadata, self, variant->data); \
			} else if (child < 0 || child != waitpid(child, &status, 0)) { \
				ksft_print_msg("ERROR SPAWNING TEST GRANDCHILD\n"); \
				_metadata->exit_code = KSFT_FAIL; \
			} \
		} \
		if (child == 0) { \
			if (_metadata->setup_completed && !fixture_name##_teardown_parent && \
					__sync_bool_compare_and_swap(teardown, false, true)) \
				fixture_name##_teardown(_metadata, self, variant->data); \
			_exit(0); \
		} \
		if (_metadata->setup_completed && fixture_name##_teardown_parent && \
				__sync_bool_compare_and_swap(teardown, false, true)) \
			fixture_name##_teardown(_metadata, self, variant->data); \
		munmap(teardown, sizeof(*teardown)); \
		if (self && fixture_name##_teardown_parent) \
			munmap(self, sizeof(*self)); \
		if (WIFEXITED(status)) { \
			if (WEXITSTATUS(status)) \
				_metadata->exit_code = WEXITSTATUS(status); \
		} else if (WIFSIGNALED(status)) { \
			/* Forward signal to __wait_for_test(). */ \
			kill(getpid(), WTERMSIG(status)); \
		} \
		__test_check_assert(_metadata); \
	} \
	static struct __test_metadata *_##fixture_name##_##test_name##_object; \
	static void __attribute__((constructor)) \
			_register_##fixture_name##_##test_name(void) \
	{ \
		struct __test_metadata *object = mmap(NULL, sizeof(*object), \
			PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0); \
		object->name = #test_name; \
		object->fn = &wrapper_##fixture_name##_##test_name; \
		object->fixture = &_##fixture_name##_fixture_object; \
		object->termsig = signal; \
		object->timeout = tmout; \
		_##fixture_name##_##test_name##_object = object; \
		__register_test(object); \
	} \
	static void fixture_name##_##test_name( \
		struct __test_metadata __attribute__((unused)) *_metadata, \
		FIXTURE_DATA(fixture_name) __attribute__((unused)) *self, \
		const FIXTURE_VARIANT(fixture_name) \
			__attribute__((unused)) *variant)

/**
 * TEST_HARNESS_MAIN - Simple wrapper to run the test harness
 *
 * .. code-block:: c
 *
 *     TEST_HARNESS_MAIN
 *
 * Use once to append a main() to the test file.
 */
#define TEST_HARNESS_MAIN \
	static void __attribute__((constructor)) \
	__constructor_order_last(void) \
	{ \
		if (!__constructor_order) \
			__constructor_order = _CONSTRUCTOR_ORDER_BACKWARD; \
	} \
	int main(int argc, char **argv) { \
		return test_harness_run(argc, argv); \
	}

/**
 * DOC: operators
 *
 * Operators for use in TEST() and TEST_F().
 * ASSERT_* calls will stop test execution immediately.
 * EXPECT_* calls will emit a failure warning, note it, and continue.
 */

/**
 * ASSERT_EQ()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * ASSERT_EQ(expected, measured): expected == measured
 */
#define ASSERT_EQ(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, ==, 1)

/**
 * ASSERT_NE()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * ASSERT_NE(expected, measured): expected != measured
 */
#define ASSERT_NE(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, !=, 1)

/**
 * ASSERT_LT()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * ASSERT_LT(expected, measured): expected < measured
 */
#define ASSERT_LT(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, <, 1)

/**
 * ASSERT_LE()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * ASSERT_LE(expected, measured): expected <= measured
 */
#define ASSERT_LE(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, <=, 1)

/**
 * ASSERT_GT()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * ASSERT_GT(expected, measured): expected > measured
 */
#define ASSERT_GT(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, >, 1)

/**
 * ASSERT_GE()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * ASSERT_GE(expected, measured): expected >= measured
 */
#define ASSERT_GE(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, >=, 1)

/**
 * ASSERT_NULL()
 *
 * @seen: measured value
 *
 * ASSERT_NULL(measured): NULL == measured
 */
#define ASSERT_NULL(seen) \
	__EXPECT(NULL, "NULL", seen, #seen, ==, 1)

/**
 * ASSERT_TRUE()
 *
 * @seen: measured value
 *
 * ASSERT_TRUE(measured): measured != 0
 */
#define ASSERT_TRUE(seen) \
	__EXPECT(0, "0", seen, #seen, !=, 1)

/**
 * ASSERT_FALSE()
 *
 * @seen: measured value
 *
 * ASSERT_FALSE(measured): measured == 0
 */
#define ASSERT_FALSE(seen) \
	__EXPECT(0, "0", seen, #seen, ==, 1)

/**
 * ASSERT_STREQ()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * ASSERT_STREQ(expected, measured): !strcmp(expected, measured)
 */
#define ASSERT_STREQ(expected, seen) \
	__EXPECT_STR(expected, seen, ==, 1)

/**
 * ASSERT_STRNE()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * ASSERT_STRNE(expected, measured): strcmp(expected, measured)
 */
#define ASSERT_STRNE(expected, seen) \
	__EXPECT_STR(expected, seen, !=, 1)

/**
 * EXPECT_EQ()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * EXPECT_EQ(expected, measured): expected == measured
 */
#define EXPECT_EQ(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, ==, 0)

/**
 * EXPECT_NE()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * EXPECT_NE(expected, measured): expected != measured
 */
#define EXPECT_NE(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, !=, 0)

/**
 * EXPECT_LT()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * EXPECT_LT(expected, measured): expected < measured
 */
#define EXPECT_LT(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, <, 0)

/**
 * EXPECT_LE()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * EXPECT_LE(expected, measured): expected <= measured
 */
#define EXPECT_LE(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, <=, 0)

/**
 * EXPECT_GT()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * EXPECT_GT(expected, measured): expected > measured
 */
#define EXPECT_GT(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, >, 0)

/**
 * EXPECT_GE()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * EXPECT_GE(expected, measured): expected >= measured
 */
#define EXPECT_GE(expected, seen) \
	__EXPECT(expected, #expected, seen, #seen, >=, 0)

/**
 * EXPECT_NULL()
 *
 * @seen: measured value
 *
 * EXPECT_NULL(measured): NULL == measured
 */
#define EXPECT_NULL(seen) \
	__EXPECT(NULL, "NULL", seen, #seen, ==, 0)

/**
 * EXPECT_TRUE()
 *
 * @seen: measured value
 *
 * EXPECT_TRUE(measured): 0 != measured
 */
#define EXPECT_TRUE(seen) \
	__EXPECT(0, "0", seen, #seen, !=, 0)

/**
 * EXPECT_FALSE()
 *
 * @seen: measured value
 *
 * EXPECT_FALSE(measured): 0 == measured
 */
#define EXPECT_FALSE(seen) \
	__EXPECT(0, "0", seen, #seen, ==, 0)

/**
 * EXPECT_STREQ()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * EXPECT_STREQ(expected, measured): !strcmp(expected, measured)
 */
#define EXPECT_STREQ(expected, seen) \
	__EXPECT_STR(expected, seen, ==, 0)

/**
 * EXPECT_STRNE()
 *
 * @expected: expected value
 * @seen: measured value
 *
 * EXPECT_STRNE(expected, measured): strcmp(expected, measured)
 */
#define EXPECT_STRNE(expected, seen) \
	__EXPECT_STR(expected, seen, !=, 0)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)	(sizeof(a) / sizeof(a[0]))
#endif

/* Support an optional handler after and ASSERT_* or EXPECT_*.  The approach is
 * not thread-safe, but it should be fine in most sane test scenarios.
 *
 * Using __bail(), which optionally abort()s, is the easiest way to early
 * return while still providing an optional block to the API consumer.
 */
#define OPTIONAL_HANDLER(_assert) \
	for (; _metadata->trigger; _metadata->trigger = \
			__bail(_assert, _metadata))

#define is_signed_type(var)       (!!(((__typeof__(var))(-1)) < (__typeof__(var))1))

#define __EXPECT(_expected, _expected_str, _seen, _seen_str, _t, _assert) do { \
	/* Avoid multiple evaluation of the cases */ \
	__typeof__(_expected) __exp = (_expected); \
	__typeof__(_seen) __seen = (_seen); \
	if (!(__exp _t __seen)) { \
		/* Report with actual signedness to avoid weird output. */ \
		switch (is_signed_type(__exp) * 2 + is_signed_type(__seen)) { \
		case 0: { \
			unsigned long long __exp_print = (uintptr_t)__exp; \
			unsigned long long __seen_print = (uintptr_t)__seen; \
			__TH_LOG("Expected %s (%llu) %s %s (%llu)", \
				 _expected_str, __exp_print, #_t, \
				 _seen_str, __seen_print); \
			break; \
			} \
		case 1: { \
			unsigned long long __exp_print = (uintptr_t)__exp; \
			long long __seen_print = (intptr_t)__seen; \
			__TH_LOG("Expected %s (%llu) %s %s (%lld)", \
				 _expected_str, __exp_print, #_t, \
				 _seen_str, __seen_print); \
			break; \
			} \
		case 2: { \
			long long __exp_print = (intptr_t)__exp; \
			unsigned long long __seen_print = (uintptr_t)__seen; \
			__TH_LOG("Expected %s (%lld) %s %s (%llu)", \
				 _expected_str, __exp_print, #_t, \
				 _seen_str, __seen_print); \
			break; \
			} \
		case 3: { \
			long long __exp_print = (intptr_t)__exp; \
			long long __seen_print = (intptr_t)__seen; \
			__TH_LOG("Expected %s (%lld) %s %s (%lld)", \
				 _expected_str, __exp_print, #_t, \
				 _seen_str, __seen_print); \
			break; \
			} \
		} \
		_metadata->exit_code = KSFT_FAIL; \
		/* Ensure the optional handler is triggered */ \
		_metadata->trigger = 1; \
	} \
} while (0); OPTIONAL_HANDLER(_assert)

#define __EXPECT_STR(_expected, _seen, _t, _assert) do { \
	const char *__exp = (_expected); \
	const char *__seen = (_seen); \
	if (!(strcmp(__exp, __seen) _t 0))  { \
		__TH_LOG("Expected '%s' %s '%s'.", __exp, #_t, __seen); \
		_metadata->exit_code = KSFT_FAIL; \
		_metadata->trigger = 1; \
	} \
} while (0); OPTIONAL_HANDLER(_assert)

/* List helpers */
#define __LIST_APPEND(head, item) \
{ \
	/* Circular linked list where only prev is circular. */ \
	if (head == NULL) { \
		head = item; \
		item->next = NULL; \
		item->prev = item; \
		return;	\
	} \
	if (__constructor_order == _CONSTRUCTOR_ORDER_FORWARD) { \
		item->next = NULL; \
		item->prev = head->prev; \
		item->prev->next = item; \
		head->prev = item; \
	} else { \
		item->next = head; \
		item->next->prev = item; \
		item->prev = item; \
		head = item; \
	} \
}

struct __test_results {
	char reason[1024];	/* Reason for test result */
};

struct __test_metadata;
struct __fixture_variant_metadata;

/* Contains all the information about a fixture. */
struct __fixture_metadata {
	const char *name;
	struct __test_metadata *tests;
	struct __fixture_variant_metadata *variant;
	struct __fixture_metadata *prev, *next;
} _fixture_global __attribute__((unused)) = {
	.name = "global",
	.prev = &_fixture_global,
};

struct __test_xfail {
	struct __fixture_metadata *fixture;
	struct __fixture_variant_metadata *variant;
	struct __test_metadata *test;
	struct __test_xfail *prev, *next;
};

/**
 * XFAIL_ADD() - mark variant + test case combination as expected to fail
 * @fixture_name: name of the fixture
 * @variant_name: name of the variant
 * @test_name: name of the test case
 *
 * Mark a combination of variant + test case for a given fixture as expected
 * to fail. Tests marked this way will report XPASS / XFAIL return codes,
 * instead of PASS / FAIL,and use respective counters.
 */
#define XFAIL_ADD(fixture_name, variant_name, test_name) \
	static struct __test_xfail \
		_##fixture_name##_##variant_name##_##test_name##_xfail = \
	{ \
		.fixture = &_##fixture_name##_fixture_object, \
		.variant = &_##fixture_name##_##variant_name##_object, \
	}; \
	static void __attribute__((constructor)) \
		_register_##fixture_name##_##variant_name##_##test_name##_xfail(void) \
	{ \
		_##fixture_name##_##variant_name##_##test_name##_xfail.test = \
			_##fixture_name##_##test_name##_object; \
		__register_xfail(&_##fixture_name##_##variant_name##_##test_name##_xfail); \
	}

static struct __fixture_metadata *__fixture_list = &_fixture_global;
static int __constructor_order;

#define _CONSTRUCTOR_ORDER_FORWARD   1
#define _CONSTRUCTOR_ORDER_BACKWARD -1

static inline void __register_fixture(struct __fixture_metadata *f)
{
	__LIST_APPEND(__fixture_list, f);
}

struct __fixture_variant_metadata {
	const char *name;
	const void *data;
	struct __test_xfail *xfails;
	struct __fixture_variant_metadata *prev, *next;
};

static inline void
__register_fixture_variant(struct __fixture_metadata *f,
			   struct __fixture_variant_metadata *variant)
{
	__LIST_APPEND(f->variant, variant);
}

/* Contains all the information for test execution and status checking. */
struct __test_metadata {
	const char *name;
	void (*fn)(struct __test_metadata *,
		   struct __fixture_variant_metadata *);
	pid_t pid;	/* pid of test when being run */
	struct __fixture_metadata *fixture;
	int termsig;
	int exit_code;
	int trigger; /* extra handler after the evaluation */
	int timeout;	/* seconds to wait for test timeout */
	bool timed_out;	/* did this test timeout instead of exiting? */
	bool aborted;	/* stopped test due to failed ASSERT */
	bool setup_completed; /* did setup finish? */
	jmp_buf env;	/* for exiting out of test early */
	struct __test_results *results;
	struct __test_metadata *prev, *next;
};

static inline bool __test_passed(struct __test_metadata *metadata)
{
	return metadata->exit_code != KSFT_FAIL &&
	       metadata->exit_code <= KSFT_SKIP;
}

/*
 * Since constructors are called in reverse order, reverse the test
 * list so tests are run in source declaration order.
 * https://gcc.gnu.org/onlinedocs/gccint/Initialization.html
 * However, it seems not all toolchains do this correctly, so use
 * __constructor_order to detect which direction is called first
 * and adjust list building logic to get things running in the right
 * direction.
 */
static inline void __register_test(struct __test_metadata *t)
{
	__LIST_APPEND(t->fixture->tests, t);
}

static inline void __register_xfail(struct __test_xfail *xf)
{
	__LIST_APPEND(xf->variant->xfails, xf);
}

static inline int __bail(int for_realz, struct __test_metadata *t)
{
	/* if this is ASSERT, return immediately. */
	if (for_realz) {
		t->aborted = true;
		longjmp(t->env, 1);
	}
	/* otherwise, end the for loop and continue. */
	return 0;
}

static inline void __test_check_assert(struct __test_metadata *t)
{
	if (t->aborted)
		abort();
}

struct __test_metadata *__active_test;
static void __timeout_handler(int sig, siginfo_t *info, void *ucontext)
{
	struct __test_metadata *t = __active_test;

	/* Sanity check handler execution environment. */
	if (!t) {
		fprintf(TH_LOG_STREAM,
			"# no active test in SIGALRM handler!?\n");
		abort();
	}
	if (sig != SIGALRM || sig != info->si_signo) {
		fprintf(TH_LOG_STREAM,
			"# %s: SIGALRM handler caught signal %d!?\n",
			t->name, sig != SIGALRM ? sig : info->si_signo);
		abort();
	}

	t->timed_out = true;
	// signal process group
	kill(-(t->pid), SIGKILL);
}

void __wait_for_test(struct __test_metadata *t)
{
	struct sigaction action = {
		.sa_sigaction = __timeout_handler,
		.sa_flags = SA_SIGINFO,
	};
	struct sigaction saved_action;
	int status;

	if (sigaction(SIGALRM, &action, &saved_action)) {
		t->exit_code = KSFT_FAIL;
		fprintf(TH_LOG_STREAM,
			"# %s: unable to install SIGALRM handler\n",
			t->name);
		return;
	}
	__active_test = t;
	t->timed_out = false;
	alarm(t->timeout);
	waitpid(t->pid, &status, 0);
	alarm(0);
	if (sigaction(SIGALRM, &saved_action, NULL)) {
		t->exit_code = KSFT_FAIL;
		fprintf(TH_LOG_STREAM,
			"# %s: unable to uninstall SIGALRM handler\n",
			t->name);
		return;
	}
	__active_test = NULL;

	if (t->timed_out) {
		t->exit_code = KSFT_FAIL;
		fprintf(TH_LOG_STREAM,
			"# %s: Test terminated by timeout\n", t->name);
	} else if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) == KSFT_SKIP ||
		    WEXITSTATUS(status) == KSFT_XPASS ||
		    WEXITSTATUS(status) == KSFT_XFAIL) {
			t->exit_code = WEXITSTATUS(status);
		} else if (t->termsig != -1) {
			t->exit_code = KSFT_FAIL;
			fprintf(TH_LOG_STREAM,
				"# %s: Test exited normally instead of by signal (code: %d)\n",
				t->name,
				WEXITSTATUS(status));
		} else {
			switch (WEXITSTATUS(status)) {
			/* Success */
			case KSFT_PASS:
				t->exit_code = KSFT_PASS;
				break;
			/* Failure */
			default:
				t->exit_code = KSFT_FAIL;
				fprintf(TH_LOG_STREAM,
					"# %s: Test failed\n",
					t->name);
			}
		}
	} else if (WIFSIGNALED(status)) {
		t->exit_code = KSFT_FAIL;
		if (WTERMSIG(status) == SIGABRT) {
			fprintf(TH_LOG_STREAM,
				"# %s: Test terminated by assertion\n",
				t->name);
		} else if (WTERMSIG(status) == t->termsig) {
			t->exit_code = KSFT_PASS;
		} else {
			fprintf(TH_LOG_STREAM,
				"# %s: Test terminated unexpectedly by signal %d\n",
				t->name,
				WTERMSIG(status));
		}
	} else {
		fprintf(TH_LOG_STREAM,
			"# %s: Test ended in some other way [%u]\n",
			t->name,
			status);
	}
}

static void test_harness_list_tests(void)
{
	struct __fixture_variant_metadata *v;
	struct __fixture_metadata *f;
	struct __test_metadata *t;

	for (f = __fixture_list; f; f = f->next) {
		v = f->variant;
		t = f->tests;

		if (f == __fixture_list)
			fprintf(stderr, "%-20s %-25s %s\n",
				"# FIXTURE", "VARIANT", "TEST");
		else
			fprintf(stderr, "--------------------------------------------------------------------------------\n");

		do {
			fprintf(stderr, "%-20s %-25s %s\n",
				t == f->tests ? f->name : "",
				v ? v->name : "",
				t ? t->name : "");

			v = v ? v->next : NULL;
			t = t ? t->next : NULL;
		} while (v || t);
	}
}

static int test_harness_argv_check(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "hlF:f:V:v:t:T:r:")) != -1) {
		switch (opt) {
		case 'f':
		case 'F':
		case 'v':
		case 'V':
		case 't':
		case 'T':
		case 'r':
			break;
		case 'l':
			test_harness_list_tests();
			return KSFT_SKIP;
		case 'h':
		default:
			fprintf(stderr,
				"Usage: %s [-h|-l] [-t|-T|-v|-V|-f|-F|-r name]\n"
				"\t-h       print help\n"
				"\t-l       list all tests\n"
				"\n"
				"\t-t name  include test\n"
				"\t-T name  exclude test\n"
				"\t-v name  include variant\n"
				"\t-V name  exclude variant\n"
				"\t-f name  include fixture\n"
				"\t-F name  exclude fixture\n"
				"\t-r name  run specified test\n"
				"\n"
				"Test filter options can be specified "
				"multiple times. The filtering stops\n"
				"at the first match. For example to "
				"include all tests from variant 'bla'\n"
				"but not test 'foo' specify '-T foo -v bla'.\n"
				"", argv[0]);
			return opt == 'h' ? KSFT_SKIP : KSFT_FAIL;
		}
	}

	return KSFT_PASS;
}

static bool test_enabled(int argc, char **argv,
			 struct __fixture_metadata *f,
			 struct __fixture_variant_metadata *v,
			 struct __test_metadata *t)
{
	unsigned int flen = 0, vlen = 0, tlen = 0;
	bool has_positive = false;
	int opt;

	optind = 1;
	while ((opt = getopt(argc, argv, "F:f:V:v:t:T:r:")) != -1) {
		has_positive |= islower(opt);

		switch (tolower(opt)) {
		case 't':
			if (!strcmp(t->name, optarg))
				return islower(opt);
			break;
		case 'f':
			if (!strcmp(f->name, optarg))
				return islower(opt);
			break;
		case 'v':
			if (!strcmp(v->name, optarg))
				return islower(opt);
			break;
		case 'r':
			if (!tlen) {
				flen = strlen(f->name);
				vlen = strlen(v->name);
				tlen = strlen(t->name);
			}
			if (strlen(optarg) == flen + 1 + vlen + !!vlen + tlen &&
			    !strncmp(f->name, &optarg[0], flen) &&
			    !strncmp(v->name, &optarg[flen + 1], vlen) &&
			    !strncmp(t->name, &optarg[flen + 1 + vlen + !!vlen], tlen))
				return true;
			break;
		}
	}

	/*
	 * If there are no positive tests then we assume user just wants
	 * exclusions and everything else is a pass.
	 */
	return !has_positive;
}

void __run_test(struct __fixture_metadata *f,
		struct __fixture_variant_metadata *variant,
		struct __test_metadata *t)
{
	struct __test_xfail *xfail;
	char test_name[1024];
	const char *diagnostic;

	/* reset test struct */
	t->exit_code = KSFT_PASS;
	t->trigger = 0;
	t->aborted = false;
	t->setup_completed = false;
	memset(t->env, 0, sizeof(t->env));
	memset(t->results->reason, 0, sizeof(t->results->reason));

	snprintf(test_name, sizeof(test_name), "%s%s%s.%s",
		 f->name, variant->name[0] ? "." : "", variant->name, t->name);

	ksft_print_msg(" RUN           %s ...\n", test_name);

	/* Make sure output buffers are flushed before fork */
	fflush(stdout);
	fflush(stderr);

	t->pid = clone3_vfork();
	if (t->pid < 0) {
		ksft_print_msg("ERROR SPAWNING TEST CHILD\n");
		t->exit_code = KSFT_FAIL;
	} else if (t->pid == 0) {
		setpgrp();
		t->fn(t, variant);
		_exit(t->exit_code);
	} else {
		__wait_for_test(t);
	}
	ksft_print_msg("         %4s  %s\n",
		       __test_passed(t) ? "OK" : "FAIL", test_name);

	/* Check if we're expecting this test to fail */
	for (xfail = variant->xfails; xfail; xfail = xfail->next)
		if (xfail->test == t)
			break;
	if (xfail)
		t->exit_code = __test_passed(t) ? KSFT_XPASS : KSFT_XFAIL;

	if (t->results->reason[0])
		diagnostic = t->results->reason;
	else if (t->exit_code == KSFT_PASS || t->exit_code == KSFT_FAIL)
		diagnostic = NULL;
	else
		diagnostic = "unknown";

	ksft_test_result_code(t->exit_code, test_name,
			      diagnostic ? "%s" : NULL, diagnostic);
}

static int test_harness_run(int argc, char **argv)
{
	struct __fixture_variant_metadata no_variant = { .name = "", };
	struct __fixture_variant_metadata *v;
	struct __fixture_metadata *f;
	struct __test_results *results;
	struct __test_metadata *t;
	int ret;
	unsigned int case_count = 0, test_count = 0;
	unsigned int count = 0;
	unsigned int pass_count = 0;

	ret = test_harness_argv_check(argc, argv);
	if (ret != KSFT_PASS)
		return ret;

	for (f = __fixture_list; f; f = f->next) {
		for (v = f->variant ?: &no_variant; v; v = v->next) {
			unsigned int old_tests = test_count;

			for (t = f->tests; t; t = t->next)
				if (test_enabled(argc, argv, f, v, t))
					test_count++;

			if (old_tests != test_count)
				case_count++;
		}
	}

	results = mmap(NULL, sizeof(*results), PROT_READ | PROT_WRITE,
		       MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	ksft_print_header();
	ksft_set_plan(test_count);
	ksft_print_msg("Starting %u tests from %u test cases.\n",
	       test_count, case_count);
	for (f = __fixture_list; f; f = f->next) {
		for (v = f->variant ?: &no_variant; v; v = v->next) {
			for (t = f->tests; t; t = t->next) {
				if (!test_enabled(argc, argv, f, v, t))
					continue;
				count++;
				t->results = results;
				__run_test(f, v, t);
				t->results = NULL;
				if (__test_passed(t))
					pass_count++;
				else
					ret = 1;
			}
		}
	}
	munmap(results, sizeof(*results));

	ksft_print_msg("%s: %u / %u tests passed.\n", ret ? "FAILED" : "PASSED",
			pass_count, count);
	ksft_exit(ret == 0);

	/* unreachable */
	return KSFT_FAIL;
}

static void __attribute__((constructor)) __constructor_order_first(void)
{
	if (!__constructor_order)
		__constructor_order = _CONSTRUCTOR_ORDER_FORWARD;
}

#endif  /* __KSELFTEST_HARNESS_H */
