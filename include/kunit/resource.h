/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KUnit resource API for test managed resources (allocations, etc.).
 *
 * Copyright (C) 2022, Google LLC.
 * Author: Daniel Latypov <dlatypov@google.com>
 */

#ifndef _KUNIT_RESOURCE_H
#define _KUNIT_RESOURCE_H

#include <kunit/test.h>

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

struct kunit_resource;

typedef int (*kunit_resource_init_t)(struct kunit_resource *, void *);
typedef void (*kunit_resource_free_t)(struct kunit_resource *);

/**
 * struct kunit_resource - represents a *test managed resource*
 * @data: for the user to store arbitrary data.
 * @name: optional name
 * @free: a user supplied function to free the resource.
 *
 * Represents a *test managed resource*, a resource which will automatically be
 * cleaned up at the end of a test case. This cleanup is performed by the 'free'
 * function. The struct kunit_resource itself is freed automatically with
 * kfree() if it was allocated by KUnit (e.g., by kunit_alloc_resource()), but
 * must be freed by the user otherwise.
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
	bool should_kfree;
};

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
 * Called when refcount reaches zero via kunit_put_resource();
 * should not be called directly.
 */
static inline void kunit_release_resource(struct kref *kref)
{
	struct kunit_resource *res = container_of(kref, struct kunit_resource,
						  refcount);

	if (res->free)
		res->free(res);

	/* 'res' is valid here, as if should_kfree is set, res->free may not free
	 * 'res' itself, just res->data
	 */
	if (res->should_kfree)
		kfree(res);
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
 * __kunit_add_resource() - Internal helper to add a resource.
 *
 * res->should_kfree is not initialised.
 * @test: The test context object.
 * @init: a user-supplied function to initialize the result (if needed).  If
 *        none is supplied, the resource data value is simply set to @data.
 *	  If an init function is supplied, @data is passed to it instead.
 * @free: a user-supplied function to free the resource (if needed).
 * @res: The resource.
 * @data: value to pass to init function or set in resource data field.
 */
int __kunit_add_resource(struct kunit *test,
			 kunit_resource_init_t init,
			 kunit_resource_free_t free,
			 struct kunit_resource *res,
			 void *data);

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
static inline int kunit_add_resource(struct kunit *test,
				     kunit_resource_init_t init,
				     kunit_resource_free_t free,
				     struct kunit_resource *res,
				     void *data)
{
	res->should_kfree = false;
	return __kunit_add_resource(test, init, free, res, data);
}

static inline struct kunit_resource *
kunit_find_named_resource(struct kunit *test, const char *name);

/**
 * kunit_add_named_resource() - Add a named *test managed resource*.
 * @test: The test context object.
 * @init: a user-supplied function to initialize the resource data, if needed.
 * @free: a user-supplied function to free the resource data, if needed.
 * @res: The resource.
 * @name: name to be set for resource.
 * @data: value to pass to init function or set in resource data field.
 */
static inline int kunit_add_named_resource(struct kunit *test,
					   kunit_resource_init_t init,
					   kunit_resource_free_t free,
					   struct kunit_resource *res,
					   const char *name,
					   void *data)
{
	struct kunit_resource *existing;

	if (!name)
		return -EINVAL;

	existing = kunit_find_named_resource(test, name);
	if (existing) {
		kunit_put_resource(existing);
		return -EEXIST;
	}

	res->name = name;
	res->should_kfree = false;

	return __kunit_add_resource(test, init, free, res, data);
}

/**
 * kunit_alloc_and_get_resource() - Allocates and returns a *test managed resource*.
 * @test: The test context object.
 * @init: a user supplied function to initialize the resource.
 * @free: a user supplied function to free the resource (if needed).
 * @internal_gfp: gfp to use for internal allocations, if unsure, use GFP_KERNEL
 * @context: for the user to pass in arbitrary data to the init function.
 *
 * Allocates a *test managed resource*, a resource which will automatically be
 * cleaned up at the end of a test case. See &struct kunit_resource for an
 * example.
 *
 * This is effectively identical to kunit_alloc_resource, but returns the
 * struct kunit_resource pointer, not just the 'data' pointer. It therefore
 * also increments the resource's refcount, so kunit_put_resource() should be
 * called when you've finished with it.
 *
 * Note: KUnit needs to allocate memory for a kunit_resource object. You must
 * specify an @internal_gfp that is compatible with the use context of your
 * resource.
 */
static inline struct kunit_resource *
kunit_alloc_and_get_resource(struct kunit *test,
			     kunit_resource_init_t init,
			     kunit_resource_free_t free,
			     gfp_t internal_gfp,
			     void *context)
{
	struct kunit_resource *res;
	int ret;

	res = kzalloc(sizeof(*res), internal_gfp);
	if (!res)
		return NULL;

	res->should_kfree = true;

	ret = __kunit_add_resource(test, init, free, res, context);
	if (!ret) {
		/*
		 * bump refcount for get; kunit_resource_put() should be called
		 * when done.
		 */
		kunit_get_resource(res);
		return res;
	}
	return NULL;
}

/**
 * kunit_alloc_resource() - Allocates a *test managed resource*.
 * @test: The test context object.
 * @init: a user supplied function to initialize the resource.
 * @free: a user supplied function to free the resource (if needed).
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

	res->should_kfree = true;
	if (!__kunit_add_resource(test, init, free, res, context))
		return res->data;

	return NULL;
}

typedef bool (*kunit_resource_match_t)(struct kunit *test,
				       struct kunit_resource *res,
				       void *match_data);

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

#endif /* _KUNIT_RESOURCE_H */
