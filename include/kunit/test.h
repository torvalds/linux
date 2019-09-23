/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Base unit test (KUnit) API.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#ifndef _KUNIT_TEST_H
#define _KUNIT_TEST_H

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
 * ``void (*)(struct kunit *)`` that makes expectations (see
 * KUNIT_EXPECT_TRUE()) about code under test. Each test case is associated
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
 * kunit_test_suite() - used to register a &struct kunit_suite with KUnit.
 *
 * @suite: a statically allocated &struct kunit_suite.
 *
 * Registers @suite with the test framework. See &struct kunit_suite for
 * more information.
 *
 * NOTE: Currently KUnit tests are all run as late_initcalls; this means
 * that they cannot test anything where tests must run at a different init
 * phase. One significant restriction resulting from this is that KUnit
 * cannot reliably test anything that is initialize in the late_init phase;
 * another is that KUnit is useless to test things that need to be run in
 * an earlier init phase.
 *
 * TODO(brendanhiggins@google.com): Don't run all KUnit tests as
 * late_initcalls.  I have some future work planned to dispatch all KUnit
 * tests from the same place, and at the very least to do so after
 * everything else is definitely initialized.
 */
#define kunit_test_suite(suite)						       \
	static int kunit_suite_init##suite(void)			       \
	{								       \
		return kunit_run_tests(&suite);				       \
	}								       \
	late_initcall(kunit_suite_init##suite)

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

void __printf(3, 4) kunit_printk(const char *level,
				 const struct kunit *test,
				 const char *fmt, ...);

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

#endif /* _KUNIT_TEST_H */
