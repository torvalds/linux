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

#include "kselftest.h"

#define TEST_TIMEOUT_DEFAULT 30

/* Utilities exposed to the test definitions */
#ifndef TH_LOG_STREAM
#  define TH_LOG_STREAM stderr
#endif

#ifndef TH_LOG_ENABLED
#  define TH_LOG_ENABLED 1
#endif

/**
 * TH_LOG(fmt, ...)
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
 *
 * If there is no way to print an error message for the process running the
 * test (e.g. not allowed to write to stderr), it is still possible to get the
 * ASSERT_* number for which the test failed.  This behavior can be enabled by
 * writing `_metadata->no_print = true;` before the check sequence that is
 * unable to print.  When an error occur, instead of printing an error message
 * and calling `abort(3)`, the test process call `_exit(2)` with the assert
 * number as argument, which is then printed by the parent process.
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
 * SKIP(statement, fmt, ...)
 *
 * @statement: statement to run after reporting SKIP
 * @fmt: format string
 * @...: optional arguments
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
	_metadata->passed = 1; \
	_metadata->skip = 1; \
	_metadata->trigger = 0; \
	statement; \
} while (0)

/**
 * TEST(test_name) - Defines the test function and creates the registration
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
 * TEST_SIGNAL(test_name, signal)
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
		test_name(_metadata); \
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
 * FIXTURE_DATA(datatype_name) - Wraps the struct name so we have one less
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
 * FIXTURE(fixture_name) - Called once per fixture to setup the data and
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
 * FIXTURE_SETUP(fixture_name) - Prepares the setup function for the fixture.
 * *_metadata* is included so that EXPECT_* and ASSERT_* work correctly.
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
 * FIXTURE_TEARDOWN(fixture_name)
 * *_metadata* is included so that EXPECT_* and ASSERT_* work correctly.
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
	void fixture_name##_teardown( \
		struct __test_metadata __attribute__((unused)) *_metadata, \
		FIXTURE_DATA(fixture_name) __attribute__((unused)) *self)

/**
 * FIXTURE_VARIANT(fixture_name) - Optionally called once per fixture
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
 * Defines type of constant parameters provided to FIXTURE_SETUP() and TEST_F()
 * as *variant*. Variants allow the same tests to be run with different
 * arguments.
 */
#define FIXTURE_VARIANT(fixture_name) struct _fixture_variant_##fixture_name

/**
 * FIXTURE_VARIANT_ADD(fixture_name, variant_name) - Called once per fixture
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
	extern FIXTURE_VARIANT(fixture_name) \
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
	FIXTURE_VARIANT(fixture_name) \
		_##fixture_name##_##variant_name##_variant =

/**
 * TEST_F(fixture_name, test_name) - Emits test registration and helpers for
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
 * Warning: use of ASSERT_* here will skip TEARDOWN.
 */
/* TODO(wad) register fixtures on dedicated test lists. */
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
		FIXTURE_DATA(fixture_name) self; \
		memset(&self, 0, sizeof(FIXTURE_DATA(fixture_name))); \
		fixture_name##_setup(_metadata, &self, variant->data); \
		/* Let setup failure terminate early. */ \
		if (!_metadata->passed) \
			return; \
		fixture_name##_##test_name(_metadata, &self, variant->data); \
		fixture_name##_teardown(_metadata, &self); \
	} \
	static struct __test_metadata \
		      _##fixture_name##_##test_name##_object = { \
		.name = #test_name, \
		.fn = &wrapper_##fixture_name##_##test_name, \
		.fixture = &_##fixture_name##_fixture_object, \
		.termsig = signal, \
		.timeout = tmout, \
	 }; \
	static void __attribute__((constructor)) \
			_register_##fixture_name##_##test_name(void) \
	{ \
		__register_test(&_##fixture_name##_##test_name##_object); \
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

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof(a[0]))

/* Support an optional handler after and ASSERT_* or EXPECT_*.  The approach is
 * not thread-safe, but it should be fine in most sane test scenarios.
 *
 * Using __bail(), which optionally abort()s, is the easiest way to early
 * return while still providing an optional block to the API consumer.
 */
#define OPTIONAL_HANDLER(_assert) \
	for (; _metadata->trigger; _metadata->trigger = \
			__bail(_assert, _metadata->no_print, _metadata->step))

#define __INC_STEP(_metadata) \
	/* Keep "step" below 255 (which is used for "SKIP" reporting). */	\
	if (_metadata->passed && _metadata->step < 253) \
		_metadata->step++;

#define is_signed_type(var)       (!!(((__typeof__(var))(-1)) < (__typeof__(var))1))

#define __EXPECT(_expected, _expected_str, _seen, _seen_str, _t, _assert) do { \
	/* Avoid multiple evaluation of the cases */ \
	__typeof__(_expected) __exp = (_expected); \
	__typeof__(_seen) __seen = (_seen); \
	if (_assert) __INC_STEP(_metadata); \
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
		_metadata->passed = 0; \
		/* Ensure the optional handler is triggered */ \
		_metadata->trigger = 1; \
	} \
} while (0); OPTIONAL_HANDLER(_assert)

#define __EXPECT_STR(_expected, _seen, _t, _assert) do { \
	const char *__exp = (_expected); \
	const char *__seen = (_seen); \
	if (_assert) __INC_STEP(_metadata); \
	if (!(strcmp(__exp, __seen) _t 0))  { \
		__TH_LOG("Expected '%s' %s '%s'.", __exp, #_t, __seen); \
		_metadata->passed = 0; \
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
	int passed;
	int skip;	/* did SKIP get used? */
	int trigger; /* extra handler after the evaluation */
	int timeout;	/* seconds to wait for test timeout */
	bool timed_out;	/* did this test timeout instead of exiting? */
	__u8 step;
	bool no_print; /* manual trigger when TH_LOG_STREAM is not available */
	struct __test_results *results;
	struct __test_metadata *prev, *next;
};

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

static inline int __bail(int for_realz, bool no_print, __u8 step)
{
	if (for_realz) {
		if (no_print)
			_exit(step);
		abort();
	}
	return 0;
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
		t->passed = 0;
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
		t->passed = 0;
		fprintf(TH_LOG_STREAM,
			"# %s: unable to uninstall SIGALRM handler\n",
			t->name);
		return;
	}
	__active_test = NULL;

	if (t->timed_out) {
		t->passed = 0;
		fprintf(TH_LOG_STREAM,
			"# %s: Test terminated by timeout\n", t->name);
	} else if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) == 255) {
			/* SKIP */
			t->passed = 1;
			t->skip = 1;
		} else if (t->termsig != -1) {
			t->passed = 0;
			fprintf(TH_LOG_STREAM,
				"# %s: Test exited normally instead of by signal (code: %d)\n",
				t->name,
				WEXITSTATUS(status));
		} else {
			switch (WEXITSTATUS(status)) {
			/* Success */
			case 0:
				t->passed = 1;
				break;
			/* Other failure, assume step report. */
			default:
				t->passed = 0;
				fprintf(TH_LOG_STREAM,
					"# %s: Test failed at step #%d\n",
					t->name,
					WEXITSTATUS(status));
			}
		}
	} else if (WIFSIGNALED(status)) {
		t->passed = 0;
		if (WTERMSIG(status) == SIGABRT) {
			fprintf(TH_LOG_STREAM,
				"# %s: Test terminated by assertion\n",
				t->name);
		} else if (WTERMSIG(status) == t->termsig) {
			t->passed = 1;
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

void __run_test(struct __fixture_metadata *f,
		struct __fixture_variant_metadata *variant,
		struct __test_metadata *t)
{
	/* reset test struct */
	t->passed = 1;
	t->skip = 0;
	t->trigger = 0;
	t->step = 1;
	t->no_print = 0;
	memset(t->results->reason, 0, sizeof(t->results->reason));

	ksft_print_msg(" RUN           %s%s%s.%s ...\n",
	       f->name, variant->name[0] ? "." : "", variant->name, t->name);

	/* Make sure output buffers are flushed before fork */
	fflush(stdout);
	fflush(stderr);

	t->pid = fork();
	if (t->pid < 0) {
		ksft_print_msg("ERROR SPAWNING TEST CHILD\n");
		t->passed = 0;
	} else if (t->pid == 0) {
		setpgrp();
		t->fn(t, variant);
		if (t->skip)
			_exit(255);
		/* Pass is exit 0 */
		if (t->passed)
			_exit(0);
		/* Something else happened, report the step. */
		_exit(t->step);
	} else {
		__wait_for_test(t);
	}
	ksft_print_msg("         %4s  %s%s%s.%s\n", t->passed ? "OK" : "FAIL",
	       f->name, variant->name[0] ? "." : "", variant->name, t->name);

	if (t->skip)
		ksft_test_result_skip("%s\n", t->results->reason[0] ?
					t->results->reason : "unknown");
	else
		ksft_test_result(t->passed, "%s%s%s.%s\n",
			f->name, variant->name[0] ? "." : "", variant->name, t->name);
}

static int test_harness_run(int __attribute__((unused)) argc,
			    char __attribute__((unused)) **argv)
{
	struct __fixture_variant_metadata no_variant = { .name = "", };
	struct __fixture_variant_metadata *v;
	struct __fixture_metadata *f;
	struct __test_results *results;
	struct __test_metadata *t;
	int ret = 0;
	unsigned int case_count = 0, test_count = 0;
	unsigned int count = 0;
	unsigned int pass_count = 0;

	for (f = __fixture_list; f; f = f->next) {
		for (v = f->variant ?: &no_variant; v; v = v->next) {
			case_count++;
			for (t = f->tests; t; t = t->next)
				test_count++;
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
				count++;
				t->results = results;
				__run_test(f, v, t);
				t->results = NULL;
				if (t->passed)
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
