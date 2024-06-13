// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit resource API for test managed resources (allocations, etc.).
 *
 * Copyright (C) 2022, Google LLC.
 * Author: Daniel Latypov <dlatypov@google.com>
 */

#include <kunit/resource.h>
#include <kunit/test.h>
#include <linux/kref.h>

/*
 * Used for static resources and when a kunit_resource * has been created by
 * kunit_alloc_resource().  When an init function is supplied, @data is passed
 * into the init function; otherwise, we simply set the resource data field to
 * the data value passed in. Doesn't initialize res->should_kfree.
 */
int __kunit_add_resource(struct kunit *test,
			 kunit_resource_init_t init,
			 kunit_resource_free_t free,
			 struct kunit_resource *res,
			 void *data)
{
	int ret = 0;
	unsigned long flags;

	res->free = free;
	kref_init(&res->refcount);

	if (init) {
		ret = init(res, data);
		if (ret)
			return ret;
	} else {
		res->data = data;
	}

	spin_lock_irqsave(&test->lock, flags);
	list_add_tail(&res->node, &test->resources);
	/* refcount for list is established by kref_init() */
	spin_unlock_irqrestore(&test->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(__kunit_add_resource);

void kunit_remove_resource(struct kunit *test, struct kunit_resource *res)
{
	unsigned long flags;
	bool was_linked;

	spin_lock_irqsave(&test->lock, flags);
	was_linked = !list_empty(&res->node);
	list_del_init(&res->node);
	spin_unlock_irqrestore(&test->lock, flags);

	if (was_linked)
		kunit_put_resource(res);
}
EXPORT_SYMBOL_GPL(kunit_remove_resource);

int kunit_destroy_resource(struct kunit *test, kunit_resource_match_t match,
			   void *match_data)
{
	struct kunit_resource *res = kunit_find_resource(test, match,
							 match_data);

	if (!res)
		return -ENOENT;

	kunit_remove_resource(test, res);

	/* We have a reference also via _find(); drop it. */
	kunit_put_resource(res);

	return 0;
}
EXPORT_SYMBOL_GPL(kunit_destroy_resource);
