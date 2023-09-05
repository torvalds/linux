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

struct kunit_action_ctx {
	struct kunit_resource res;
	kunit_action_t *func;
	void *ctx;
};

static void __kunit_action_free(struct kunit_resource *res)
{
	struct kunit_action_ctx *action_ctx = container_of(res, struct kunit_action_ctx, res);

	action_ctx->func(action_ctx->ctx);
}


int kunit_add_action(struct kunit *test, void (*action)(void *), void *ctx)
{
	struct kunit_action_ctx *action_ctx;

	KUNIT_ASSERT_NOT_NULL_MSG(test, action, "Tried to action a NULL function!");

	action_ctx = kzalloc(sizeof(*action_ctx), GFP_KERNEL);
	if (!action_ctx)
		return -ENOMEM;

	action_ctx->func = action;
	action_ctx->ctx = ctx;

	action_ctx->res.should_kfree = true;
	/* As init is NULL, this cannot fail. */
	__kunit_add_resource(test, NULL, __kunit_action_free, &action_ctx->res, action_ctx);

	return 0;
}
EXPORT_SYMBOL_GPL(kunit_add_action);

int kunit_add_action_or_reset(struct kunit *test, void (*action)(void *),
			      void *ctx)
{
	int res = kunit_add_action(test, action, ctx);

	if (res)
		action(ctx);
	return res;
}
EXPORT_SYMBOL_GPL(kunit_add_action_or_reset);

static bool __kunit_action_match(struct kunit *test,
				struct kunit_resource *res, void *match_data)
{
	struct kunit_action_ctx *match_ctx = (struct kunit_action_ctx *)match_data;
	struct kunit_action_ctx *res_ctx = container_of(res, struct kunit_action_ctx, res);

	/* Make sure this is a free function. */
	if (res->free != __kunit_action_free)
		return false;

	/* Both the function and context data should match. */
	return (match_ctx->func == res_ctx->func) && (match_ctx->ctx == res_ctx->ctx);
}

void kunit_remove_action(struct kunit *test,
			kunit_action_t *action,
			void *ctx)
{
	struct kunit_action_ctx match_ctx;
	struct kunit_resource *res;

	match_ctx.func = action;
	match_ctx.ctx = ctx;

	res = kunit_find_resource(test, __kunit_action_match, &match_ctx);
	if (res) {
		/* Remove the free function so we don't run the action. */
		res->free = NULL;
		kunit_remove_resource(test, res);
		kunit_put_resource(res);
	}
}
EXPORT_SYMBOL_GPL(kunit_remove_action);

void kunit_release_action(struct kunit *test,
			 kunit_action_t *action,
			 void *ctx)
{
	struct kunit_action_ctx match_ctx;
	struct kunit_resource *res;

	match_ctx.func = action;
	match_ctx.ctx = ctx;

	res = kunit_find_resource(test, __kunit_action_match, &match_ctx);
	if (res) {
		kunit_remove_resource(test, res);
		/* We have to put() this here, else free won't be called. */
		kunit_put_resource(res);
	}
}
EXPORT_SYMBOL_GPL(kunit_release_action);
