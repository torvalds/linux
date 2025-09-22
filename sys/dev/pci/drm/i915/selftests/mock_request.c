/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "gem/selftests/igt_gem_utils.h"
#include "gt/mock_engine.h"

#include "mock_request.h"

struct i915_request *
mock_request(struct intel_context *ce, unsigned long delay)
{
	struct i915_request *request;

	/* NB the i915->requests slab cache is enlarged to fit mock_request */
	request = intel_context_create_request(ce);
	if (IS_ERR(request))
		return request;

	request->mock.delay = delay;
	return request;
}

bool mock_cancel_request(struct i915_request *request)
{
	struct mock_engine *engine =
		container_of(request->engine, typeof(*engine), base);
	bool was_queued;

	spin_lock_irq(&engine->hw_lock);
	was_queued = !list_empty(&request->mock.link);
	list_del_init(&request->mock.link);
	spin_unlock_irq(&engine->hw_lock);

	if (was_queued)
		i915_request_unsubmit(request);

	return was_queued;
}
