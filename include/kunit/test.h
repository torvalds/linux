/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Base unit test (KUnit) API.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#ifndef _KUNIT_TEST_H
#define _KUNIT_TEST_H

#include <kunit/assert.h>
#include <kunit/try-catch.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>

struct kunit_resource;

typedef int (*kunit_resource_init_t)(struct kunit_resource *, void *);
typedef void (*kunit_resource_free_t)(struct kunit_resource *);

/**
 * struct kunit_resource - represents a *test managed resource*
 * @allocation: for the user to store arbitrary data.
 * @free: a user supplied function to free the resource. Populated by
 * kunit_alloc_resource().
 *
 * Represents a *test managed resource*, a resource which will automatically be
 * cleaned up at the end of a test case.
 *
 * Example:
 *
 * .. code-block:: c
 *
 *	struct kunit_kmalloc_params {
 *		size_t size;
 *		gfp_t gfp;
 *	};
 *
 *	static int kunit_kmalloc_init(struct kunit_resource *res, void *context)
 *	{
 *		struct kunit_kmalloc_params *params = context;
 *		res->allocation = kmalloc(params->size, params->gfp);
 *
 *		if (!res->allocation)
 *			return -ENOMEM;
 *
 *		return 0;
 *	}
 *
 *	static void kunit_kmalloc_free(struct kunit_resource *res)
 *	{
 *		kfree(res->allocation);
 *	}
 *
 *	void *kunit_kmalloc(struct kunit *test, size_t size, gfp_t gfp)
 *	{
 *		struct kunit_kmalloc_params params;
 *		struct kunit_resource *res;
 *
 *		params.size = size;
 *		params.gfp = gfp;
 *
 *		res = kunit_alloc_resource(test, kunit_kmalloc_init,
 *			kunit_kmalloc_free, &params);
 *		if (res)
 *			return res->allocation;
 *
 *		return NULL;
 *	}
 */
struct kunit_resource {
	void *allocation;
	kunit_resource_free_t free;

	/* private: internal use only. */
	struct list_head node;
};

struct kunit;

/**
 * struct kunit_case - represents an individual test case.
 *
 * @run_case: the function representing the actual test case.
 * @name:     the name of the test case.
 *
 * A test case is a function with the signature,
 * ``void (*)(struct kunit *)``
 * that makes expectations and assertions (see KUNIT_EXPECT_TRUE() and
 * KUNIT_ASSERT_TRUE()) about code under test. Each test case is associated
 * with a &struct kunit_suite and will be run after the suite's init
 * function and followed by the suite's exit function.
 *
 * A test case should be static and should only be created with the
 * KUNIT_CASE() macro; additionally, every array of test cases should be
 * terminated with an empty test case.
 *
 * Example:
 *
 * .. code-block:: c
 *
 *	void add_test_basic(struct kunit *test)
 *	{
 *		KUNIT_EXPECT_EQ(test, 1, add(1, 0));
 *		KUNIT_EXPECT_EQ(test, 2, add(1, 1));
 *		KUNIT_EXPECT_EQ(test, 0, add(-1, 1));
 *		KUNIT_EXPECT_EQ(test, INT_MAX, add(0, INT_MAX));
 *		KUNIT_EXPECT_EQ(test, -1, add(INT_MAX, INT_MIN));
 *	}
 *
 *	static struct kunit_case example_test_cases[] = {
 *		KUNIT_CASE(add_test_basic),
 *		{}
 *	};
 *
 */
struct kunit_case {
	void (*run_case)(struct kunit *test);
	const char *name;

	/* private: internal use only. */
	bool success;
};

/**
 * KUNIT_CASE - A helper for creating a &struct kunit_case
 *
 * @test_name: a reference to a test case function.
 *
 * Takes a symbol for a function representing a test case and creates a
 * &struct kunit_case object from it. See the documentation for
 * &struct kunit_case for an example on how to use it.
 */
#define KUNIT_CASE(test_name) { .run_case = test_name, .name = #test_name }

/**
 * struct kunit_suite - describes a related collection of &struct kunit_case
 *
 * @name:	the name of the test. Purely informational.
 * @init:	called before every test case.
 * @exit:	called after every test case.
 * @test_cases:	a null terminated array of test cases.
 *
 * A kunit_suite is a collection of related &struct kunit_case s, such that
 * @init is called before every test case and @exit is called after every
 * test case, similar to the notion of a *test fixture* or a *test class*
 * in other unit testing frameworks like JUnit or Googletest.
 *
 * Every &struct kunit_case must be associated with a kunit_suite for KUnit
 * to run it.
 */
struct kunit_suite {
	const char name[256];
	int (*init)(struct kunit *test);
	void (*exit)(struct kunit *test);
	struct kunit_case *test_cases;
};

/**
 * struct kunit - represents a running instance of a test.
 *
 * @priv: for user to store arbitrary data. Commonly used to pass data
 *	  created in the init function (see &struct kunit_suite).
 *
 * Used to store information about the current context under which the test
 * is running. Most of this data is private and should only be accessed
 * indirectly via public functions; the one exception is @priv which can be
 * used by the test writer to store arbitrary data.
 */
struct kunit {
	void *priv;

	/* private: internal use only. */
	const char *name; /* Read only after initialization! */
	struct kunit_try_catch try_catch;
	/*
	 * success starts as true, and may only be set to false during a
	 * test case; thus, it is safe to update this across multiple
	 * threads using WRITE_ONCE; however, as a consequence, it may only
	 * be read after the test case finishes once all threads associated
	 * with the test case have terminated.
	 */
	bool success; /* Read only after test_case finishes! */
	spinlock_t lock; /* Guards all mutable test state. */
	/*
	 * Because resources is a list that may be updated multiple times (with
	 * new resources) from any thread associated with a test case, we must
	 * protect it with some type of lock.
	 */
	struct list_head resources; /* Protected by lock. */
};

void kunit_init_test(struct kunit *test, const char *name);

int kunit_run_tests(struct kunit_suite *suite);

/**
 * kunit_test_suites() - used to register one or more &struct kunit_suite
 *			 with KUnit.
 *
 * @suites: a statically allocated list of &struct kunit_suite.
 *
 * Registers @suites with the test framework. See &struct kunit_suite for
 * more information.
 *
 * When builtin,  KUnit tests are all run as late_initcalls; this means
 * that they cannot test anything where tests must run at a different init
 * phase. One significant restriction resulting from this is that KUnit
 * cannot reliably test anything that is initialize in the late_init phase;
 * another is that KUnit is useless to test things that need to be run in
 * an earlier init phase.
 *
 * An alternative is to build the tests as a module.  Because modules
 * do not support multiple late_initcall()s, we need to initialize an
 * array of suites for a module.
 *
 * TODO(brendanhiggins@google.com): Don't run all KUnit tests as
 * late_initcalls.  I have some future work planned to dispatch all KUnit
 * tests from the same place, and at the very least to do so after
 * everything else is definitely initialized.
 */
#define kunit_test_suites(...)						\
	static struct kunit_suite *suites[] = { __VA_ARGS__, NULL};	\
	static int kunit_test_suites_init(void)				\
	{								\
		unsigned int i;						\
		for (i = 0; suites[i] != NULL; i++)			\
			kunit_run_tests(suites[i]);			\
		return 0;						\
	}								\
	late_initcall(kunit_test_suites_init);				\
	static void __exit kunit_test_suites_exit(void)			\
	{								\
		return;							\
	}								\
	module_exit(kunit_test_suites_exit)

#define kunit_test_suite(suite)	kunit_test_suites(&suite)

/*
 * Like kunit_alloc_resource() below, but returns the struct kunit_resource
 * object that contains the allocation. This is mostly for testing purposes.
 */
struct kunit_resource *kunit_alloc_and_get_resource(struct kunit *test,
						    kunit_resource_init_t init,
						    kunit_resource_free_t free,
						    gfp_t internal_gfp,
						    void *context);

/**
 * kunit_alloc_resource() - Allocates a *test managed resource*.
 * @test: The test context object.
 * @init: a user supplied function to initialize the resource.
 * @free: a user supplied function to free the resource.
 * @internal_gfp: gfp to use for internal allocations, if unsure, use GFP_KERNEL
 * @context: for the user to pass in arbitrary data to the init function.
 *
 * Allocates a *test managed resource*, a resource which will automatically be
 * cleaned up at the end of a test case. See &struct kunit_resource for an
 * example.
 *
 * NOTE: KUnit needs to allocate memory for each kunit_resource object. You must
 * specify an @internal_gfp that is compatible with the use context of your
 * resource.
 */
static inline void *kunit_alloc_resource(struct kunit *test,
					 kunit_resource_init_t init,
					 kunit_resource_free_t free,
					 gfp_t internal_gfp,
					 void *context)
{
	struct kunit_resource *res;

	res = kunit_alloc_and_get_resource(test, init, free, internal_gfp,
					   context);

	if (res)
		return res->allocation;

	return NULL;
}

typedef bool (*kunit_resource_match_t)(struct kunit *test,
				       const void *res,
				       void *match_data);

/**
 * kunit_resource_instance_match() - Match a resource with the same instance.
 * @test: Test case to which the resource belongs.
 * @res: The data stored in kunit_resource->allocation.
 * @match_data: The resource pointer to match against.
 *
 * An instance of kunit_resource_match_t that matches a resource whose
 * allocation matches @match_data.
 */
static inline bool kunit_resource_instance_match(struct kunit *test,
						 const void *res,
						 void *match_data)
{
	return res == match_data;
}

/**
 * kunit_resource_destroy() - Find a kunit_resource and destroy it.
 * @test: Test case to which the resource belongs.
 * @match: Match function. Returns whether a given resource matches @match_data.
 * @free: Must match free on the kunit_resource to free.
 * @match_data: Data passed into @match.
 *
 * Free the latest kunit_resource of @test for which @free matches the
 * kunit_resource_free_t associated with the resource and for which @match
 * returns true.
 *
 * RETURNS:
 * 0 if kunit_resource is found and freed, -ENOENT if not found.
 */
int kunit_resource_destroy(struct kunit *test,
			   kunit_resource_match_t match,
			   kunit_resource_free_t free,
			   void *match_data);

/**
 * kunit_kmalloc() - Like kmalloc() except the allocation is *test managed*.
 * @test: The test context object.
 * @size: The size in bytes of the desired memory.
 * @gfp: flags passed to underlying kmalloc().
 *
 * Just like `kmalloc(...)`, except the allocation is managed by the test case
 * and is automatically cleaned up after the test case concludes. See &struct
 * kunit_resource for more information.
 */
void *kunit_kmalloc(struct kunit *test, size_t size, gfp_t gfp);

/**
 * kunit_kfree() - Like kfree except for allocations managed by KUnit.
 * @test: The test case to which the resource belongs.
 * @ptr: The memory allocation to free.
 */
void kunit_kfree(struct kunit *test, const void *ptr);

/**
 * kunit_kzalloc() - Just like kunit_kmalloc(), but zeroes the allocation.
 * @test: The test context object.
 * @size: The size in bytes of the desired memory.
 * @gfp: flags passed to underlying kmalloc().
 *
 * See kzalloc() and kunit_kmalloc() for more information.
 */
static inline void *kunit_kzalloc(struct kunit *test, size_t size, gfp_t gfp)
{
	return kunit_kmalloc(test, size, gfp | __GFP_ZERO);
}

void kunit_cleanup(struct kunit *test);

#define kunit_printk(lvl, test, fmt, ...) \
	printk(lvl "\t# %s: " fmt, (test)->name, ##__VA_ARGS__)

/**
 * kunit_info() - Prints an INFO level message associated with @test.
 *
 * @test: The test context object.
 * @fmt:  A printk() style format string.
 *
 * Prints an info level message associated with the test suite being run.
 * Takes a variable number of format parameters just like printk().
 */
#define kunit_info(test, fmt, ...) \
	kunit_printk(KERN_INFO, test, fmt, ##__VA_ARGS__)

/**
 * kunit_warn() - Prints a WARN level message associated with @test.
 *
 * @test: The test context object.
 * @fmt:  A printk() style format string.
 *
 * Prints a warning level message.
 */
#define kunit_warn(test, fmt, ...) \
	kunit_printk(KERN_WARNING, test, fmt, ##__VA_ARGS__)

/**
 * kunit_err() - Prints an ERROR level message associated with @test.
 *
 * @test: The test context object.
 * @fmt:  A printk() style format string.
 *
 * Prints an error level message.
 */
#define kunit_err(test, fmt, ...) \
	kunit_printk(KERN_ERR, test, fmt, ##__VA_ARGS__)

/**
 * KUNIT_SUCCEED() - A no-op expectation. Only exists for code clarity.
 * @test: The test context object.
 *
 * The opposite of KUNIT_FAIL(), it is an expectation that cannot fail. In other
 * words, it does nothing and only exists for code clarity. See
 * KUNIT_EXPECT_TRUE() for more information.
 */
#define KUNIT_SUCCEED(test) do {} while (0)

void kunit_do_assertion(struct kunit *test,
			struct kunit_assert *assert,
			bool pass,
			const char *fmt, ...);

#define KUNIT_ASSERTION(test, pass, assert_class, INITIALIZER, fmt, ...) do {  \
	struct assert_class __assertion = INITIALIZER;			       \
	kunit_do_assertion(test,					       \
			   &__assertion.assert,				       \
			   pass,					       \
			   fmt,						       \
			   ##__VA_ARGS__);				       \
} while (0)


#define KUNIT_FAIL_ASSERTION(test, assert_type, fmt, ...)		       \
	KUNIT_ASSERTION(test,						       \
			false,						       \
			kunit_fail_assert,				       \
			KUNIT_INIT_FAIL_ASSERT_STRUCT(test, assert_type),      \
			fmt,						       \
			##__VA_ARGS__)

/**
 * KUNIT_FAIL() - Always causes a test to fail when evaluated.
 * @test: The test context object.
 * @fmt: an informational message to be printed when the assertion is made.
 * @...: string format arguments.
 *
 * The opposite of KUNIT_SUCCEED(), it is an expectation that always fails. In
 * other words, it always results in a failed expectation, and consequently
 * always causes the test case to fail when evaluated. See KUNIT_EXPECT_TRUE()
 * for more information.
 */
#define KUNIT_FAIL(test, fmt, ...)					       \
	KUNIT_FAIL_ASSERTION(test,					       \
			     KUNIT_EXPECTATION,				       \
			     fmt,					       \
			     ##__VA_ARGS__)

#define KUNIT_UNARY_ASSERTION(test,					       \
			      assert_type,				       \
			      condition,				       \
			      expected_true,				       \
			      fmt,					       \
			      ...)					       \
	KUNIT_ASSERTION(test,						       \
			!!(condition) == !!expected_true,		       \
			kunit_unary_assert,				       \
			KUNIT_INIT_UNARY_ASSERT_STRUCT(test,		       \
						       assert_type,	       \
						       #condition,	       \
						       expected_true),	       \
			fmt,						       \
			##__VA_ARGS__)

#define KUNIT_TRUE_MSG_ASSERTION(test, assert_type, condition, fmt, ...)       \
	KUNIT_UNARY_ASSERTION(test,					       \
			      assert_type,				       \
			      condition,				       \
			      true,					       \
			      fmt,					       \
			      ##__VA_ARGS__)

#define KUNIT_TRUE_ASSERTION(test, assert_type, condition) \
	KUNIT_TRUE_MSG_ASSERTION(test, assert_type, condition, NULL)

#define KUNIT_FALSE_MSG_ASSERTION(test, assert_type, condition, fmt, ...)      \
	KUNIT_UNARY_ASSERTION(test,					       \
			      assert_type,				       \
			      condition,				       \
			      false,					       \
			      fmt,					       \
			      ##__VA_ARGS__)

#define KUNIT_FALSE_ASSERTION(test, assert_type, condition) \
	KUNIT_FALSE_MSG_ASSERTION(test, assert_type, condition, NULL)

/*
 * A factory macro for defining the assertions and expectations for the basic
 * comparisons defined for the built in types.
 *
 * Unfortunately, there is no common type that all types can be promoted to for
 * which all the binary operators behave the same way as for the actual types
 * (for example, there is no type that long long and unsigned long long can
 * both be cast to where the comparison result is preserved for all values). So
 * the best we can do is do the comparison in the original types and then coerce
 * everything to long long for printing; this way, the comparison behaves
 * correctly and the printed out value usually makes sense without
 * interpretation, but can always be interpreted to figure out the actual
 * value.
 */
#define KUNIT_BASE_BINARY_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left,				       \
				    op,					       \
				    right,				       \
				    fmt,				       \
				    ...)				       \
do {									       \
	typeof(left) __left = (left);					       \
	typeof(right) __right = (right);				       \
	((void)__typecheck(__left, __right));				       \
									       \
	KUNIT_ASSERTION(test,						       \
			__left op __right,				       \
			assert_class,					       \
			ASSERT_CLASS_INIT(test,				       \
					  assert_type,			       \
					  #op,				       \
					  #left,			       \
					  __left,			       \
					  #right,			       \
					  __right),			       \
			fmt,						       \
			##__VA_ARGS__);					       \
} while (0)

#define KUNIT_BASE_EQ_MSG_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ...)				       \
	KUNIT_BASE_BINARY_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left, ==, right,			       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BASE_NE_MSG_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ...)				       \
	KUNIT_BASE_BINARY_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left, !=, right,			       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BASE_LT_MSG_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ...)				       \
	KUNIT_BASE_BINARY_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left, <, right,			       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BASE_LE_MSG_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ...)				       \
	KUNIT_BASE_BINARY_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left, <=, right,			       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BASE_GT_MSG_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ...)				       \
	KUNIT_BASE_BINARY_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left, >, right,			       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BASE_GE_MSG_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ...)				       \
	KUNIT_BASE_BINARY_ASSERTION(test,				       \
				    assert_class,			       \
				    ASSERT_CLASS_INIT,			       \
				    assert_type,			       \
				    left, >=, right,			       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_EQ_MSG_ASSERTION(test, assert_type, left, right, fmt, ...)\
	KUNIT_BASE_EQ_MSG_ASSERTION(test,				       \
				    kunit_binary_assert,		       \
				    KUNIT_INIT_BINARY_ASSERT_STRUCT,	       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_EQ_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_EQ_MSG_ASSERTION(test,				       \
				      assert_type,			       \
				      left,				       \
				      right,				       \
				      NULL)

#define KUNIT_BINARY_PTR_EQ_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ...)				       \
	KUNIT_BASE_EQ_MSG_ASSERTION(test,				       \
				    kunit_binary_ptr_assert,		       \
				    KUNIT_INIT_BINARY_PTR_ASSERT_STRUCT,       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_PTR_EQ_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_PTR_EQ_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  NULL)

#define KUNIT_BINARY_NE_MSG_ASSERTION(test, assert_type, left, right, fmt, ...)\
	KUNIT_BASE_NE_MSG_ASSERTION(test,				       \
				    kunit_binary_assert,		       \
				    KUNIT_INIT_BINARY_ASSERT_STRUCT,	       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_NE_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_NE_MSG_ASSERTION(test,				       \
				      assert_type,			       \
				      left,				       \
				      right,				       \
				      NULL)

#define KUNIT_BINARY_PTR_NE_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ...)				       \
	KUNIT_BASE_NE_MSG_ASSERTION(test,				       \
				    kunit_binary_ptr_assert,		       \
				    KUNIT_INIT_BINARY_PTR_ASSERT_STRUCT,       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_PTR_NE_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_PTR_NE_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  NULL)

#define KUNIT_BINARY_LT_MSG_ASSERTION(test, assert_type, left, right, fmt, ...)\
	KUNIT_BASE_LT_MSG_ASSERTION(test,				       \
				    kunit_binary_assert,		       \
				    KUNIT_INIT_BINARY_ASSERT_STRUCT,	       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_LT_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_LT_MSG_ASSERTION(test,				       \
				      assert_type,			       \
				      left,				       \
				      right,				       \
				      NULL)

#define KUNIT_BINARY_PTR_LT_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ...)				       \
	KUNIT_BASE_LT_MSG_ASSERTION(test,				       \
				    kunit_binary_ptr_assert,		       \
				    KUNIT_INIT_BINARY_PTR_ASSERT_STRUCT,       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_PTR_LT_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_PTR_LT_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  NULL)

#define KUNIT_BINARY_LE_MSG_ASSERTION(test, assert_type, left, right, fmt, ...)\
	KUNIT_BASE_LE_MSG_ASSERTION(test,				       \
				    kunit_binary_assert,		       \
				    KUNIT_INIT_BINARY_ASSERT_STRUCT,	       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_LE_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_LE_MSG_ASSERTION(test,				       \
				      assert_type,			       \
				      left,				       \
				      right,				       \
				      NULL)

#define KUNIT_BINARY_PTR_LE_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ...)				       \
	KUNIT_BASE_LE_MSG_ASSERTION(test,				       \
				    kunit_binary_ptr_assert,		       \
				    KUNIT_INIT_BINARY_PTR_ASSERT_STRUCT,       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_PTR_LE_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_PTR_LE_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  NULL)

#define KUNIT_BINARY_GT_MSG_ASSERTION(test, assert_type, left, right, fmt, ...)\
	KUNIT_BASE_GT_MSG_ASSERTION(test,				       \
				    kunit_binary_assert,		       \
				    KUNIT_INIT_BINARY_ASSERT_STRUCT,	       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_GT_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_GT_MSG_ASSERTION(test,				       \
				      assert_type,			       \
				      left,				       \
				      right,				       \
				      NULL)

#define KUNIT_BINARY_PTR_GT_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ...)				       \
	KUNIT_BASE_GT_MSG_ASSERTION(test,				       \
				    kunit_binary_ptr_assert,		       \
				    KUNIT_INIT_BINARY_PTR_ASSERT_STRUCT,       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_PTR_GT_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_PTR_GT_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  NULL)

#define KUNIT_BINARY_GE_MSG_ASSERTION(test, assert_type, left, right, fmt, ...)\
	KUNIT_BASE_GE_MSG_ASSERTION(test,				       \
				    kunit_binary_assert,		       \
				    KUNIT_INIT_BINARY_ASSERT_STRUCT,	       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_GE_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_GE_MSG_ASSERTION(test,				       \
				      assert_type,			       \
				      left,				       \
				      right,				       \
				      NULL)

#define KUNIT_BINARY_PTR_GE_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ...)				       \
	KUNIT_BASE_GE_MSG_ASSERTION(test,				       \
				    kunit_binary_ptr_assert,		       \
				    KUNIT_INIT_BINARY_PTR_ASSERT_STRUCT,       \
				    assert_type,			       \
				    left,				       \
				    right,				       \
				    fmt,				       \
				    ##__VA_ARGS__)

#define KUNIT_BINARY_PTR_GE_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_PTR_GE_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  NULL)

#define KUNIT_BINARY_STR_ASSERTION(test,				       \
				   assert_type,				       \
				   left,				       \
				   op,					       \
				   right,				       \
				   fmt,					       \
				   ...)					       \
do {									       \
	typeof(left) __left = (left);					       \
	typeof(right) __right = (right);				       \
									       \
	KUNIT_ASSERTION(test,						       \
			strcmp(__left, __right) op 0,			       \
			kunit_binary_str_assert,			       \
			KUNIT_INIT_BINARY_ASSERT_STRUCT(test,		       \
							assert_type,	       \
							#op,		       \
							#left,		       \
							__left,		       \
							#right,		       \
							__right),	       \
			fmt,						       \
			##__VA_ARGS__);					       \
} while (0)

#define KUNIT_BINARY_STR_EQ_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ...)				       \
	KUNIT_BINARY_STR_ASSERTION(test,				       \
				   assert_type,				       \
				   left, ==, right,			       \
				   fmt,					       \
				   ##__VA_ARGS__)

#define KUNIT_BINARY_STR_EQ_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_STR_EQ_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  NULL)

#define KUNIT_BINARY_STR_NE_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ...)				       \
	KUNIT_BINARY_STR_ASSERTION(test,				       \
				   assert_type,				       \
				   left, !=, right,			       \
				   fmt,					       \
				   ##__VA_ARGS__)

#define KUNIT_BINARY_STR_NE_ASSERTION(test, assert_type, left, right)	       \
	KUNIT_BINARY_STR_NE_MSG_ASSERTION(test,				       \
					  assert_type,			       \
					  left,				       \
					  right,			       \
					  NULL)

#define KUNIT_PTR_NOT_ERR_OR_NULL_MSG_ASSERTION(test,			       \
						assert_type,		       \
						ptr,			       \
						fmt,			       \
						...)			       \
do {									       \
	typeof(ptr) __ptr = (ptr);					       \
									       \
	KUNIT_ASSERTION(test,						       \
			!IS_ERR_OR_NULL(__ptr),				       \
			kunit_ptr_not_err_assert,			       \
			KUNIT_INIT_PTR_NOT_ERR_STRUCT(test,		       \
						      assert_type,	       \
						      #ptr,		       \
						      __ptr),		       \
			fmt,						       \
			##__VA_ARGS__);					       \
} while (0)

#define KUNIT_PTR_NOT_ERR_OR_NULL_ASSERTION(test, assert_type, ptr)	       \
	KUNIT_PTR_NOT_ERR_OR_NULL_MSG_ASSERTION(test,			       \
						assert_type,		       \
						ptr,			       \
						NULL)

/**
 * KUNIT_EXPECT_TRUE() - Causes a test failure when the expression is not true.
 * @test: The test context object.
 * @condition: an arbitrary boolean expression. The test fails when this does
 * not evaluate to true.
 *
 * This and expectations of the form `KUNIT_EXPECT_*` will cause the test case
 * to fail when the specified condition is not met; however, it will not prevent
 * the test case from continuing to run; this is otherwise known as an
 * *expectation failure*.
 */
#define KUNIT_EXPECT_TRUE(test, condition) \
	KUNIT_TRUE_ASSERTION(test, KUNIT_EXPECTATION, condition)

#define KUNIT_EXPECT_TRUE_MSG(test, condition, fmt, ...)		       \
	KUNIT_TRUE_MSG_ASSERTION(test,					       \
				 KUNIT_EXPECTATION,			       \
				 condition,				       \
				 fmt,					       \
				 ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_FALSE() - Makes a test failure when the expression is not false.
 * @test: The test context object.
 * @condition: an arbitrary boolean expression. The test fails when this does
 * not evaluate to false.
 *
 * Sets an expectation that @condition evaluates to false. See
 * KUNIT_EXPECT_TRUE() for more information.
 */
#define KUNIT_EXPECT_FALSE(test, condition) \
	KUNIT_FALSE_ASSERTION(test, KUNIT_EXPECTATION, condition)

#define KUNIT_EXPECT_FALSE_MSG(test, condition, fmt, ...)		       \
	KUNIT_FALSE_MSG_ASSERTION(test,					       \
				  KUNIT_EXPECTATION,			       \
				  condition,				       \
				  fmt,					       \
				  ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_EQ() - Sets an expectation that @left and @right are equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an expectation that the values that @left and @right evaluate to are
 * equal. This is semantically equivalent to
 * KUNIT_EXPECT_TRUE(@test, (@left) == (@right)). See KUNIT_EXPECT_TRUE() for
 * more information.
 */
#define KUNIT_EXPECT_EQ(test, left, right) \
	KUNIT_BINARY_EQ_ASSERTION(test, KUNIT_EXPECTATION, left, right)

#define KUNIT_EXPECT_EQ_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_EQ_MSG_ASSERTION(test,				       \
				      KUNIT_EXPECTATION,		       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_PTR_EQ() - Expects that pointers @left and @right are equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a pointer.
 * @right: an arbitrary expression that evaluates to a pointer.
 *
 * Sets an expectation that the values that @left and @right evaluate to are
 * equal. This is semantically equivalent to
 * KUNIT_EXPECT_TRUE(@test, (@left) == (@right)). See KUNIT_EXPECT_TRUE() for
 * more information.
 */
#define KUNIT_EXPECT_PTR_EQ(test, left, right)				       \
	KUNIT_BINARY_PTR_EQ_ASSERTION(test,				       \
				      KUNIT_EXPECTATION,		       \
				      left,				       \
				      right)

#define KUNIT_EXPECT_PTR_EQ_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_PTR_EQ_MSG_ASSERTION(test,				       \
					  KUNIT_EXPECTATION,		       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_NE() - An expectation that @left and @right are not equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an expectation that the values that @left and @right evaluate to are not
 * equal. This is semantically equivalent to
 * KUNIT_EXPECT_TRUE(@test, (@left) != (@right)). See KUNIT_EXPECT_TRUE() for
 * more information.
 */
#define KUNIT_EXPECT_NE(test, left, right) \
	KUNIT_BINARY_NE_ASSERTION(test, KUNIT_EXPECTATION, left, right)

#define KUNIT_EXPECT_NE_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_NE_MSG_ASSERTION(test,				       \
				      KUNIT_EXPECTATION,		       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_PTR_NE() - Expects that pointers @left and @right are not equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a pointer.
 * @right: an arbitrary expression that evaluates to a pointer.
 *
 * Sets an expectation that the values that @left and @right evaluate to are not
 * equal. This is semantically equivalent to
 * KUNIT_EXPECT_TRUE(@test, (@left) != (@right)). See KUNIT_EXPECT_TRUE() for
 * more information.
 */
#define KUNIT_EXPECT_PTR_NE(test, left, right)				       \
	KUNIT_BINARY_PTR_NE_ASSERTION(test,				       \
				      KUNIT_EXPECTATION,		       \
				      left,				       \
				      right)

#define KUNIT_EXPECT_PTR_NE_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_PTR_NE_MSG_ASSERTION(test,				       \
					  KUNIT_EXPECTATION,		       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_LT() - An expectation that @left is less than @right.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an expectation that the value that @left evaluates to is less than the
 * value that @right evaluates to. This is semantically equivalent to
 * KUNIT_EXPECT_TRUE(@test, (@left) < (@right)). See KUNIT_EXPECT_TRUE() for
 * more information.
 */
#define KUNIT_EXPECT_LT(test, left, right) \
	KUNIT_BINARY_LT_ASSERTION(test, KUNIT_EXPECTATION, left, right)

#define KUNIT_EXPECT_LT_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_LT_MSG_ASSERTION(test,				       \
				      KUNIT_EXPECTATION,		       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_LE() - Expects that @left is less than or equal to @right.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an expectation that the value that @left evaluates to is less than or
 * equal to the value that @right evaluates to. Semantically this is equivalent
 * to KUNIT_EXPECT_TRUE(@test, (@left) <= (@right)). See KUNIT_EXPECT_TRUE() for
 * more information.
 */
#define KUNIT_EXPECT_LE(test, left, right) \
	KUNIT_BINARY_LE_ASSERTION(test, KUNIT_EXPECTATION, left, right)

#define KUNIT_EXPECT_LE_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_LE_MSG_ASSERTION(test,				       \
				      KUNIT_EXPECTATION,		       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_GT() - An expectation that @left is greater than @right.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an expectation that the value that @left evaluates to is greater than
 * the value that @right evaluates to. This is semantically equivalent to
 * KUNIT_EXPECT_TRUE(@test, (@left) > (@right)). See KUNIT_EXPECT_TRUE() for
 * more information.
 */
#define KUNIT_EXPECT_GT(test, left, right) \
	KUNIT_BINARY_GT_ASSERTION(test, KUNIT_EXPECTATION, left, right)

#define KUNIT_EXPECT_GT_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_GT_MSG_ASSERTION(test,				       \
				      KUNIT_EXPECTATION,		       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_GE() - Expects that @left is greater than or equal to @right.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an expectation that the value that @left evaluates to is greater than
 * the value that @right evaluates to. This is semantically equivalent to
 * KUNIT_EXPECT_TRUE(@test, (@left) >= (@right)). See KUNIT_EXPECT_TRUE() for
 * more information.
 */
#define KUNIT_EXPECT_GE(test, left, right) \
	KUNIT_BINARY_GE_ASSERTION(test, KUNIT_EXPECTATION, left, right)

#define KUNIT_EXPECT_GE_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_GE_MSG_ASSERTION(test,				       \
				      KUNIT_EXPECTATION,		       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_STREQ() - Expects that strings @left and @right are equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a null terminated string.
 * @right: an arbitrary expression that evaluates to a null terminated string.
 *
 * Sets an expectation that the values that @left and @right evaluate to are
 * equal. This is semantically equivalent to
 * KUNIT_EXPECT_TRUE(@test, !strcmp((@left), (@right))). See KUNIT_EXPECT_TRUE()
 * for more information.
 */
#define KUNIT_EXPECT_STREQ(test, left, right) \
	KUNIT_BINARY_STR_EQ_ASSERTION(test, KUNIT_EXPECTATION, left, right)

#define KUNIT_EXPECT_STREQ_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_STR_EQ_MSG_ASSERTION(test,				       \
					  KUNIT_EXPECTATION,		       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_STRNEQ() - Expects that strings @left and @right are not equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a null terminated string.
 * @right: an arbitrary expression that evaluates to a null terminated string.
 *
 * Sets an expectation that the values that @left and @right evaluate to are
 * not equal. This is semantically equivalent to
 * KUNIT_EXPECT_TRUE(@test, strcmp((@left), (@right))). See KUNIT_EXPECT_TRUE()
 * for more information.
 */
#define KUNIT_EXPECT_STRNEQ(test, left, right) \
	KUNIT_BINARY_STR_NE_ASSERTION(test, KUNIT_EXPECTATION, left, right)

#define KUNIT_EXPECT_STRNEQ_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_STR_NE_MSG_ASSERTION(test,				       \
					  KUNIT_EXPECTATION,		       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ##__VA_ARGS__)

/**
 * KUNIT_EXPECT_NOT_ERR_OR_NULL() - Expects that @ptr is not null and not err.
 * @test: The test context object.
 * @ptr: an arbitrary pointer.
 *
 * Sets an expectation that the value that @ptr evaluates to is not null and not
 * an errno stored in a pointer. This is semantically equivalent to
 * KUNIT_EXPECT_TRUE(@test, !IS_ERR_OR_NULL(@ptr)). See KUNIT_EXPECT_TRUE() for
 * more information.
 */
#define KUNIT_EXPECT_NOT_ERR_OR_NULL(test, ptr) \
	KUNIT_PTR_NOT_ERR_OR_NULL_ASSERTION(test, KUNIT_EXPECTATION, ptr)

#define KUNIT_EXPECT_NOT_ERR_OR_NULL_MSG(test, ptr, fmt, ...)		       \
	KUNIT_PTR_NOT_ERR_OR_NULL_MSG_ASSERTION(test,			       \
						KUNIT_EXPECTATION,	       \
						ptr,			       \
						fmt,			       \
						##__VA_ARGS__)

#define KUNIT_ASSERT_FAILURE(test, fmt, ...) \
	KUNIT_FAIL_ASSERTION(test, KUNIT_ASSERTION, fmt, ##__VA_ARGS__)

/**
 * KUNIT_ASSERT_TRUE() - Sets an assertion that @condition is true.
 * @test: The test context object.
 * @condition: an arbitrary boolean expression. The test fails and aborts when
 * this does not evaluate to true.
 *
 * This and assertions of the form `KUNIT_ASSERT_*` will cause the test case to
 * fail *and immediately abort* when the specified condition is not met. Unlike
 * an expectation failure, it will prevent the test case from continuing to run;
 * this is otherwise known as an *assertion failure*.
 */
#define KUNIT_ASSERT_TRUE(test, condition) \
	KUNIT_TRUE_ASSERTION(test, KUNIT_ASSERTION, condition)

#define KUNIT_ASSERT_TRUE_MSG(test, condition, fmt, ...)		       \
	KUNIT_TRUE_MSG_ASSERTION(test,					       \
				 KUNIT_ASSERTION,			       \
				 condition,				       \
				 fmt,					       \
				 ##__VA_ARGS__)

/**
 * KUNIT_ASSERT_FALSE() - Sets an assertion that @condition is false.
 * @test: The test context object.
 * @condition: an arbitrary boolean expression.
 *
 * Sets an assertion that the value that @condition evaluates to is false. This
 * is the same as KUNIT_EXPECT_FALSE(), except it causes an assertion failure
 * (see KUNIT_ASSERT_TRUE()) when the assertion is not met.
 */
#define KUNIT_ASSERT_FALSE(test, condition) \
	KUNIT_FALSE_ASSERTION(test, KUNIT_ASSERTION, condition)

#define KUNIT_ASSERT_FALSE_MSG(test, condition, fmt, ...)		       \
	KUNIT_FALSE_MSG_ASSERTION(test,					       \
				  KUNIT_ASSERTION,			       \
				  condition,				       \
				  fmt,					       \
				  ##__VA_ARGS__)

/**
 * KUNIT_ASSERT_EQ() - Sets an assertion that @left and @right are equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an assertion that the values that @left and @right evaluate to are
 * equal. This is the same as KUNIT_EXPECT_EQ(), except it causes an assertion
 * failure (see KUNIT_ASSERT_TRUE()) when the assertion is not met.
 */
#define KUNIT_ASSERT_EQ(test, left, right) \
	KUNIT_BINARY_EQ_ASSERTION(test, KUNIT_ASSERTION, left, right)

#define KUNIT_ASSERT_EQ_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_EQ_MSG_ASSERTION(test,				       \
				      KUNIT_ASSERTION,			       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)

/**
 * KUNIT_ASSERT_PTR_EQ() - Asserts that pointers @left and @right are equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a pointer.
 * @right: an arbitrary expression that evaluates to a pointer.
 *
 * Sets an assertion that the values that @left and @right evaluate to are
 * equal. This is the same as KUNIT_EXPECT_EQ(), except it causes an assertion
 * failure (see KUNIT_ASSERT_TRUE()) when the assertion is not met.
 */
#define KUNIT_ASSERT_PTR_EQ(test, left, right) \
	KUNIT_BINARY_PTR_EQ_ASSERTION(test, KUNIT_ASSERTION, left, right)

#define KUNIT_ASSERT_PTR_EQ_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_PTR_EQ_MSG_ASSERTION(test,				       \
					  KUNIT_ASSERTION,		       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ##__VA_ARGS__)

/**
 * KUNIT_ASSERT_NE() - An assertion that @left and @right are not equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an assertion that the values that @left and @right evaluate to are not
 * equal. This is the same as KUNIT_EXPECT_NE(), except it causes an assertion
 * failure (see KUNIT_ASSERT_TRUE()) when the assertion is not met.
 */
#define KUNIT_ASSERT_NE(test, left, right) \
	KUNIT_BINARY_NE_ASSERTION(test, KUNIT_ASSERTION, left, right)

#define KUNIT_ASSERT_NE_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_NE_MSG_ASSERTION(test,				       \
				      KUNIT_ASSERTION,			       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)

/**
 * KUNIT_ASSERT_PTR_NE() - Asserts that pointers @left and @right are not equal.
 * KUNIT_ASSERT_PTR_EQ() - Asserts that pointers @left and @right are equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a pointer.
 * @right: an arbitrary expression that evaluates to a pointer.
 *
 * Sets an assertion that the values that @left and @right evaluate to are not
 * equal. This is the same as KUNIT_EXPECT_NE(), except it causes an assertion
 * failure (see KUNIT_ASSERT_TRUE()) when the assertion is not met.
 */
#define KUNIT_ASSERT_PTR_NE(test, left, right) \
	KUNIT_BINARY_PTR_NE_ASSERTION(test, KUNIT_ASSERTION, left, right)

#define KUNIT_ASSERT_PTR_NE_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_PTR_NE_MSG_ASSERTION(test,				       \
					  KUNIT_ASSERTION,		       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ##__VA_ARGS__)
/**
 * KUNIT_ASSERT_LT() - An assertion that @left is less than @right.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an assertion that the value that @left evaluates to is less than the
 * value that @right evaluates to. This is the same as KUNIT_EXPECT_LT(), except
 * it causes an assertion failure (see KUNIT_ASSERT_TRUE()) when the assertion
 * is not met.
 */
#define KUNIT_ASSERT_LT(test, left, right) \
	KUNIT_BINARY_LT_ASSERTION(test, KUNIT_ASSERTION, left, right)

#define KUNIT_ASSERT_LT_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_LT_MSG_ASSERTION(test,				       \
				      KUNIT_ASSERTION,			       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)
/**
 * KUNIT_ASSERT_LE() - An assertion that @left is less than or equal to @right.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an assertion that the value that @left evaluates to is less than or
 * equal to the value that @right evaluates to. This is the same as
 * KUNIT_EXPECT_LE(), except it causes an assertion failure (see
 * KUNIT_ASSERT_TRUE()) when the assertion is not met.
 */
#define KUNIT_ASSERT_LE(test, left, right) \
	KUNIT_BINARY_LE_ASSERTION(test, KUNIT_ASSERTION, left, right)

#define KUNIT_ASSERT_LE_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_LE_MSG_ASSERTION(test,				       \
				      KUNIT_ASSERTION,			       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)

/**
 * KUNIT_ASSERT_GT() - An assertion that @left is greater than @right.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an assertion that the value that @left evaluates to is greater than the
 * value that @right evaluates to. This is the same as KUNIT_EXPECT_GT(), except
 * it causes an assertion failure (see KUNIT_ASSERT_TRUE()) when the assertion
 * is not met.
 */
#define KUNIT_ASSERT_GT(test, left, right) \
	KUNIT_BINARY_GT_ASSERTION(test, KUNIT_ASSERTION, left, right)

#define KUNIT_ASSERT_GT_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_GT_MSG_ASSERTION(test,				       \
				      KUNIT_ASSERTION,			       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)

/**
 * KUNIT_ASSERT_GE() - Assertion that @left is greater than or equal to @right.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a primitive C type.
 * @right: an arbitrary expression that evaluates to a primitive C type.
 *
 * Sets an assertion that the value that @left evaluates to is greater than the
 * value that @right evaluates to. This is the same as KUNIT_EXPECT_GE(), except
 * it causes an assertion failure (see KUNIT_ASSERT_TRUE()) when the assertion
 * is not met.
 */
#define KUNIT_ASSERT_GE(test, left, right) \
	KUNIT_BINARY_GE_ASSERTION(test, KUNIT_ASSERTION, left, right)

#define KUNIT_ASSERT_GE_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_GE_MSG_ASSERTION(test,				       \
				      KUNIT_ASSERTION,			       \
				      left,				       \
				      right,				       \
				      fmt,				       \
				      ##__VA_ARGS__)

/**
 * KUNIT_ASSERT_STREQ() - An assertion that strings @left and @right are equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a null terminated string.
 * @right: an arbitrary expression that evaluates to a null terminated string.
 *
 * Sets an assertion that the values that @left and @right evaluate to are
 * equal. This is the same as KUNIT_EXPECT_STREQ(), except it causes an
 * assertion failure (see KUNIT_ASSERT_TRUE()) when the assertion is not met.
 */
#define KUNIT_ASSERT_STREQ(test, left, right) \
	KUNIT_BINARY_STR_EQ_ASSERTION(test, KUNIT_ASSERTION, left, right)

#define KUNIT_ASSERT_STREQ_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_STR_EQ_MSG_ASSERTION(test,				       \
					  KUNIT_ASSERTION,		       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ##__VA_ARGS__)

/**
 * KUNIT_ASSERT_STRNEQ() - Expects that strings @left and @right are not equal.
 * @test: The test context object.
 * @left: an arbitrary expression that evaluates to a null terminated string.
 * @right: an arbitrary expression that evaluates to a null terminated string.
 *
 * Sets an expectation that the values that @left and @right evaluate to are
 * not equal. This is semantically equivalent to
 * KUNIT_ASSERT_TRUE(@test, strcmp((@left), (@right))). See KUNIT_ASSERT_TRUE()
 * for more information.
 */
#define KUNIT_ASSERT_STRNEQ(test, left, right) \
	KUNIT_BINARY_STR_NE_ASSERTION(test, KUNIT_ASSERTION, left, right)

#define KUNIT_ASSERT_STRNEQ_MSG(test, left, right, fmt, ...)		       \
	KUNIT_BINARY_STR_NE_MSG_ASSERTION(test,				       \
					  KUNIT_ASSERTION,		       \
					  left,				       \
					  right,			       \
					  fmt,				       \
					  ##__VA_ARGS__)

/**
 * KUNIT_ASSERT_NOT_ERR_OR_NULL() - Assertion that @ptr is not null and not err.
 * @test: The test context object.
 * @ptr: an arbitrary pointer.
 *
 * Sets an assertion that the value that @ptr evaluates to is not null and not
 * an errno stored in a pointer. This is the same as
 * KUNIT_EXPECT_NOT_ERR_OR_NULL(), except it causes an assertion failure (see
 * KUNIT_ASSERT_TRUE()) when the assertion is not met.
 */
#define KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr) \
	KUNIT_PTR_NOT_ERR_OR_NULL_ASSERTION(test, KUNIT_ASSERTION, ptr)

#define KUNIT_ASSERT_NOT_ERR_OR_NULL_MSG(test, ptr, fmt, ...)		       \
	KUNIT_PTR_NOT_ERR_OR_NULL_MSG_ASSERTION(test,			       \
						KUNIT_ASSERTION,	       \
						ptr,			       \
						fmt,			       \
						##__VA_ARGS__)

#endif /* _KUNIT_TEST_H */
