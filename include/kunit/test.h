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
#include <linux/kref.h>

struct kunit_resource;

typedef int (*kunit_resource_init_t)(struct kunit_resource *, void *);
typedef void (*kunit_resource_free_t)(struct kunit_resource *);

/**
 * struct kunit_resource - represents a *test managed resource*
 * @data: for the user to store arbitrary data.
 * @name: optional name
 * @free: a user supplied function to free the resource. Populated by
 * kunit_resource_alloc().
 *
 * Represents a *test managed resource*, a resource which will automatically be
 * cleaned up at the end of a test case.
 *
 * Resources are reference counted so if a resource is retrieved via
 * kunit_alloc_and_get_resource() or kunit_find_resource(), we need
 * to call kunit_put_resource() to reduce the resource reference count
 * when finished with it.  Note that kunit_alloc_resource() does not require a
 * kunit_resource_put() because it does not retrieve the resource itself.
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
 *		res->data = kmalloc(params->size, params->gfp);
 *
 *		if (!res->data)
 *			return -ENOMEM;
 *
 *		return 0;
 *	}
 *
 *	static void kunit_kmalloc_free(struct kunit_resource *res)
 *	{
 *		kfree(res->data);
 *	}
 *
 *	void *kunit_kmalloc(struct kunit *test, size_t size, gfp_t gfp)
 *	{
 *		struct kunit_kmalloc_params params;
 *
 *		params.size = size;
 *		params.gfp = gfp;
 *
 *		return kunit_alloc_resource(test, kunit_kmalloc_init,
 *			kunit_kmalloc_free, &params);
 *	}
 *
 * Resources can also be named, with lookup/removal done on a name
 * basis also.  kunit_add_named_resource(), kunit_find_named_resource()
 * and kunit_destroy_named_resource().  Resource names must be
 * unique within the test instance.
 */
struct kunit_resource {
	void *data;
	const char *name;
	kunit_resource_free_t free;

	/* private: internal use only. */
	struct kref refcount;
	struct list_head node;
};

struct kunit;

/* Size of log associated with test. */
#define KUNIT_LOG_SIZE	512

/* Maximum size of parameter description string. */
#define KUNIT_PARAM_DESC_SIZE 128

/* Maximum size of a status comment. */
#define KUNIT_STATUS_COMMENT_SIZE 256

/*
 * TAP specifies subtest stream indentation of 4 spaces, 8 spaces for a
 * sub-subtest.  See the "Subtests" section in
 * https://node-tap.org/tap-protocol/
 */
#define KUNIT_SUBTEST_INDENT		"    "
#define KUNIT_SUBSUBTEST_INDENT		"        "

/**
 * enum kunit_status - Type of result for a test or test suite
 * @KUNIT_SUCCESS: Denotes the test suite has not failed nor been skipped
 * @KUNIT_FAILURE: Denotes the test has failed.
 * @KUNIT_SKIPPED: Denotes the test has been skipped.
 */
enum kunit_status {
	KUNIT_SUCCESS,
	KUNIT_FAILURE,
	KUNIT_SKIPPED,
};

/**
 * struct kunit_case - represents an individual test case.
 *
 * @run_case: the function representing the actual test case.
 * @name:     the name of the test case.
 * @generate_params: the generator function for parameterized tests.
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
	const void* (*generate_params)(const void *prev, char *desc);

	/* private: internal use only. */
	enum kunit_status status;
	char *log;
};

static inline char *kunit_status_to_ok_not_ok(enum kunit_status status)
{
	switch (status) {
	case KUNIT_SKIPPED:
	case KUNIT_SUCCESS:
		return "ok";
	case KUNIT_FAILURE:
		return "not ok";
	}
	return "invalid";
}

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
 * KUNIT_CASE_PARAM - A helper for creation a parameterized &struct kunit_case
 *
 * @test_name: a reference to a test case function.
 * @gen_params: a reference to a parameter generator function.
 *
 * The generator function::
 *
 *	const void* gen_params(const void *prev, char *desc)
 *
 * is used to lazily generate a series of arbitrarily typed values that fit into
 * a void*. The argument @prev is the previously returned value, which should be
 * used to derive the next value; @prev is set to NULL on the initial generator
 * call. When no more values are available, the generator must return NULL.
 * Optionally write a string into @desc (size of KUNIT_PARAM_DESC_SIZE)
 * describing the parameter.
 */
#define KUNIT_CASE_PARAM(test_name, gen_params)			\
		{ .run_case = test_name, .name = #test_name,	\
		  .generate_params = gen_params }

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

	/* private: internal use only */
	char status_comment[KUNIT_STATUS_COMMENT_SIZE];
	struct dentry *debugfs;
	char *log;
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
	char *log; /* Points at case log after initialization */
	struct kunit_try_catch try_catch;
	/* param_value is the current parameter value for a test case. */
	const void *param_value;
	/* param_index stores the index of the parameter in parameterized tests. */
	int param_index;
	/*
	 * success starts as true, and may only be set to false during a
	 * test case; thus, it is safe to update this across multiple
	 * threads using WRITE_ONCE; however, as a consequence, it may only
	 * be read after the test case finishes once all threads associated
	 * with the test case have terminated.
	 */
	spinlock_t lock; /* Guards all mutable test state. */
	enum kunit_status status; /* Read only after test_case finishes! */
	/*
	 * Because resources is a list that may be updated multiple times (with
	 * new resources) from any thread associated with a test case, we must
	 * protect it with some type of lock.
	 */
	struct list_head resources; /* Protected by lock. */

	char status_comment[KUNIT_STATUS_COMMENT_SIZE];
};

static inline void kunit_set_failure(struct kunit *test)
{
	WRITE_ONCE(test->status, KUNIT_FAILURE);
}

void kunit_init_test(struct kunit *test, const char *name, char *log);

int kunit_run_tests(struct kunit_suite *suite);

size_t kunit_suite_num_test_cases(struct kunit_suite *suite);

unsigned int kunit_test_case_num(struct kunit_suite *suite,
				 struct kunit_case *test_case);

int __kunit_test_suites_init(struct kunit_suite * const * const suites);

void __kunit_test_suites_exit(struct kunit_suite **suites);

#if IS_BUILTIN(CONFIG_KUNIT)
int kunit_run_all_tests(void);
#else
static inline int kunit_run_all_tests(void)
{
	return 0;
}
#endif /* IS_BUILTIN(CONFIG_KUNIT) */

#ifdef MODULE
/**
 * kunit_test_suites_for_module() - used to register one or more
 *			 &struct kunit_suite with KUnit.
 *
 * @__suites: a statically allocated list of &struct kunit_suite.
 *
 * Registers @__suites with the test framework. See &struct kunit_suite for
 * more information.
 *
 * If a test suite is built-in, module_init() gets translated into
 * an initcall which we don't want as the idea is that for builtins
 * the executor will manage execution.  So ensure we do not define
 * module_{init|exit} functions for the builtin case when registering
 * suites via kunit_test_suites() below.
 */
#define kunit_test_suites_for_module(__suites)				\
	static int __init kunit_test_suites_init(void)			\
	{								\
		return __kunit_test_suites_init(__suites);		\
	}								\
	module_init(kunit_test_suites_init);				\
									\
	static void __exit kunit_test_suites_exit(void)			\
	{								\
		return __kunit_test_suites_exit(__suites);		\
	}								\
	module_exit(kunit_test_suites_exit)
#else
#define kunit_test_suites_for_module(__suites)
#endif /* MODULE */

#define __kunit_test_suites(unique_array, unique_suites, ...)		       \
	static struct kunit_suite *unique_array[] = { __VA_ARGS__, NULL };     \
	kunit_test_suites_for_module(unique_array);			       \
	static struct kunit_suite **unique_suites			       \
	__used __section(".kunit_test_suites") = unique_array

/**
 * kunit_test_suites() - used to register one or more &struct kunit_suite
 *			 with KUnit.
 *
 * @__suites: a statically allocated list of &struct kunit_suite.
 *
 * Registers @suites with the test framework. See &struct kunit_suite for
 * more information.
 *
 * When builtin,  KUnit tests are all run via executor; this is done
 * by placing the array of struct kunit_suite * in the .kunit_test_suites
 * ELF section.
 *
 * An alternative is to build the tests as a module.  Because modules do not
 * support multiple initcall()s, we need to initialize an array of suites for a
 * module.
 *
 */
#define kunit_test_suites(__suites...)						\
	__kunit_test_suites(__UNIQUE_ID(array),				\
			    __UNIQUE_ID(suites),			\
			    ##__suites)

#define kunit_test_suite(suite)	kunit_test_suites(&suite)

#define kunit_suite_for_each_test_case(suite, test_case)		\
	for (test_case = suite->test_cases; test_case->run_case; test_case++)

enum kunit_status kunit_suite_has_succeeded(struct kunit_suite *suite);

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
 * kunit_get_resource() - Hold resource for use.  Should not need to be used
 *			  by most users as we automatically get resources
 *			  retrieved by kunit_find_resource*().
 * @res: resource
 */
static inline void kunit_get_resource(struct kunit_resource *res)
{
	kref_get(&res->refcount);
}

/*
 * Called when refcount reaches zero via kunit_put_resources();
 * should not be called directly.
 */
static inline void kunit_release_resource(struct kref *kref)
{
	struct kunit_resource *res = container_of(kref, struct kunit_resource,
						  refcount);

	/* If free function is defined, resource was dynamically allocated. */
	if (res->free) {
		res->free(res);
		kfree(res);
	}
}

/**
 * kunit_put_resource() - When caller is done with retrieved resource,
 *			  kunit_put_resource() should be called to drop
 *			  reference count.  The resource list maintains
 *			  a reference count on resources, so if no users
 *			  are utilizing a resource and it is removed from
 *			  the resource list, it will be freed via the
 *			  associated free function (if any).  Only
 *			  needs to be used if we alloc_and_get() or
 *			  find() resource.
 * @res: resource
 */
static inline void kunit_put_resource(struct kunit_resource *res)
{
	kref_put(&res->refcount, kunit_release_resource);
}

/**
 * kunit_add_resource() - Add a *test managed resource*.
 * @test: The test context object.
 * @init: a user-supplied function to initialize the result (if needed).  If
 *        none is supplied, the resource data value is simply set to @data.
 *	  If an init function is supplied, @data is passed to it instead.
 * @free: a user-supplied function to free the resource (if needed).
 * @res: The resource.
 * @data: value to pass to init function or set in resource data field.
 */
int kunit_add_resource(struct kunit *test,
		       kunit_resource_init_t init,
		       kunit_resource_free_t free,
		       struct kunit_resource *res,
		       void *data);

/**
 * kunit_add_named_resource() - Add a named *test managed resource*.
 * @test: The test context object.
 * @init: a user-supplied function to initialize the resource data, if needed.
 * @free: a user-supplied function to free the resource data, if needed.
 * @res: The resource.
 * @name: name to be set for resource.
 * @data: value to pass to init function or set in resource data field.
 */
int kunit_add_named_resource(struct kunit *test,
			     kunit_resource_init_t init,
			     kunit_resource_free_t free,
			     struct kunit_resource *res,
			     const char *name,
			     void *data);

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
 * Note: KUnit needs to allocate memory for a kunit_resource object. You must
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

	res = kzalloc(sizeof(*res), internal_gfp);
	if (!res)
		return NULL;

	if (!kunit_add_resource(test, init, free, res, context))
		return res->data;

	return NULL;
}

typedef bool (*kunit_resource_match_t)(struct kunit *test,
				       struct kunit_resource *res,
				       void *match_data);

/**
 * kunit_resource_instance_match() - Match a resource with the same instance.
 * @test: Test case to which the resource belongs.
 * @res: The resource.
 * @match_data: The resource pointer to match against.
 *
 * An instance of kunit_resource_match_t that matches a resource whose
 * allocation matches @match_data.
 */
static inline bool kunit_resource_instance_match(struct kunit *test,
						 struct kunit_resource *res,
						 void *match_data)
{
	return res->data == match_data;
}

/**
 * kunit_resource_name_match() - Match a resource with the same name.
 * @test: Test case to which the resource belongs.
 * @res: The resource.
 * @match_name: The name to match against.
 */
static inline bool kunit_resource_name_match(struct kunit *test,
					     struct kunit_resource *res,
					     void *match_name)
{
	return res->name && strcmp(res->name, match_name) == 0;
}

/**
 * kunit_find_resource() - Find a resource using match function/data.
 * @test: Test case to which the resource belongs.
 * @match: match function to be applied to resources/match data.
 * @match_data: data to be used in matching.
 */
static inline struct kunit_resource *
kunit_find_resource(struct kunit *test,
		    kunit_resource_match_t match,
		    void *match_data)
{
	struct kunit_resource *res, *found = NULL;
	unsigned long flags;

	spin_lock_irqsave(&test->lock, flags);

	list_for_each_entry_reverse(res, &test->resources, node) {
		if (match(test, res, (void *)match_data)) {
			found = res;
			kunit_get_resource(found);
			break;
		}
	}

	spin_unlock_irqrestore(&test->lock, flags);

	return found;
}

/**
 * kunit_find_named_resource() - Find a resource using match name.
 * @test: Test case to which the resource belongs.
 * @name: match name.
 */
static inline struct kunit_resource *
kunit_find_named_resource(struct kunit *test,
			  const char *name)
{
	return kunit_find_resource(test, kunit_resource_name_match,
				   (void *)name);
}

/**
 * kunit_destroy_resource() - Find a kunit_resource and destroy it.
 * @test: Test case to which the resource belongs.
 * @match: Match function. Returns whether a given resource matches @match_data.
 * @match_data: Data passed into @match.
 *
 * RETURNS:
 * 0 if kunit_resource is found and freed, -ENOENT if not found.
 */
int kunit_destroy_resource(struct kunit *test,
			   kunit_resource_match_t match,
			   void *match_data);

static inline int kunit_destroy_named_resource(struct kunit *test,
					       const char *name)
{
	return kunit_destroy_resource(test, kunit_resource_name_match,
				      (void *)name);
}

/**
 * kunit_remove_resource() - remove resource from resource list associated with
 *			     test.
 * @test: The test context object.
 * @res: The resource to be removed.
 *
 * Note that the resource will not be immediately freed since it is likely
 * the caller has a reference to it via alloc_and_get() or find();
 * in this case a final call to kunit_put_resource() is required.
 */
void kunit_remove_resource(struct kunit *test, struct kunit_resource *res);

/**
 * kunit_kmalloc_array() - Like kmalloc_array() except the allocation is *test managed*.
 * @test: The test context object.
 * @n: number of elements.
 * @size: The size in bytes of the desired memory.
 * @gfp: flags passed to underlying kmalloc().
 *
 * Just like `kmalloc_array(...)`, except the allocation is managed by the test case
 * and is automatically cleaned up after the test case concludes. See &struct
 * kunit_resource for more information.
 */
void *kunit_kmalloc_array(struct kunit *test, size_t n, size_t size, gfp_t gfp);

/**
 * kunit_kmalloc() - Like kmalloc() except the allocation is *test managed*.
 * @test: The test context object.
 * @size: The size in bytes of the desired memory.
 * @gfp: flags passed to underlying kmalloc().
 *
 * See kmalloc() and kunit_kmalloc_array() for more information.
 */
static inline void *kunit_kmalloc(struct kunit *test, size_t size, gfp_t gfp)
{
	return kunit_kmalloc_array(test, 1, size, gfp);
}

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
 * See kzalloc() and kunit_kmalloc_array() for more information.
 */
static inline void *kunit_kzalloc(struct kunit *test, size_t size, gfp_t gfp)
{
	return kunit_kmalloc(test, size, gfp | __GFP_ZERO);
}

/**
 * kunit_kcalloc() - Just like kunit_kmalloc_array(), but zeroes the allocation.
 * @test: The test context object.
 * @n: number of elements.
 * @size: The size in bytes of the desired memory.
 * @gfp: flags passed to underlying kmalloc().
 *
 * See kcalloc() and kunit_kmalloc_array() for more information.
 */
static inline void *kunit_kcalloc(struct kunit *test, size_t n, size_t size, gfp_t gfp)
{
	return kunit_kmalloc_array(test, n, size, gfp | __GFP_ZERO);
}

void kunit_cleanup(struct kunit *test);

void __printf(2, 3) kunit_log_append(char *log, const char *fmt, ...);

/**
 * kunit_mark_skipped() - Marks @test_or_suite as skipped
 *
 * @test_or_suite: The test context object.
 * @fmt:  A printk() style format string.
 *
 * Marks the test as skipped. @fmt is given output as the test status
 * comment, typically the reason the test was skipped.
 *
 * Test execution continues after kunit_mark_skipped() is called.
 */
#define kunit_mark_skipped(test_or_suite, fmt, ...)			\
	do {								\
		WRITE_ONCE((test_or_suite)->status, KUNIT_SKIPPED);	\
		scnprintf((test_or_suite)->status_comment,		\
			  KUNIT_STATUS_COMMENT_SIZE,			\
			  fmt, ##__VA_ARGS__);				\
	} while (0)

/**
 * kunit_skip() - Marks @test_or_suite as skipped
 *
 * @test_or_suite: The test context object.
 * @fmt:  A printk() style format string.
 *
 * Skips the test. @fmt is given output as the test status
 * comment, typically the reason the test was skipped.
 *
 * Test execution is halted after kunit_skip() is called.
 */
#define kunit_skip(test_or_suite, fmt, ...)				\
	do {								\
		kunit_mark_skipped((test_or_suite), fmt, ##__VA_ARGS__);\
		kunit_try_catch_throw(&((test_or_suite)->try_catch));	\
	} while (0)

/*
 * printk and log to per-test or per-suite log buffer.  Logging only done
 * if CONFIG_KUNIT_DEBUGFS is 'y'; if it is 'n', no log is allocated/used.
 */
#define kunit_log(lvl, test_or_suite, fmt, ...)				\
	do {								\
		printk(lvl fmt, ##__VA_ARGS__);				\
		kunit_log_append((test_or_suite)->log,	fmt "\n",	\
				 ##__VA_ARGS__);			\
	} while (0)

#define kunit_printk(lvl, test, fmt, ...)				\
	kunit_log(lvl, test, KUNIT_SUBTEST_INDENT "# %s: " fmt,		\
		  (test)->name,	##__VA_ARGS__)

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
	const char *__left = (left);					       \
	const char *__right = (right);				       \
									       \
	KUNIT_ASSERTION(test,						       \
			strcmp(__left, __right) op 0,			       \
			kunit_binary_str_assert,			       \
			KUNIT_INIT_BINARY_STR_ASSERT_STRUCT(test,	       \
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

/**
 * KUNIT_ARRAY_PARAM() - Define test parameter generator from an array.
 * @name:  prefix for the test parameter generator function.
 * @array: array of test parameters.
 * @get_desc: function to convert param to description; NULL to use default
 *
 * Define function @name_gen_params which uses @array to generate parameters.
 */
#define KUNIT_ARRAY_PARAM(name, array, get_desc)						\
	static const void *name##_gen_params(const void *prev, char *desc)			\
	{											\
		typeof((array)[0]) *__next = prev ? ((typeof(__next)) prev) + 1 : (array);	\
		if (__next - (array) < ARRAY_SIZE((array))) {					\
			void (*__get_desc)(typeof(__next), char *) = get_desc;			\
			if (__get_desc)								\
				__get_desc(__next, desc);					\
			return __next;								\
		}										\
		return NULL;									\
	}

#endif /* _KUNIT_TEST_H */
