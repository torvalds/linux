/*
 *  sync allocation tests
 *  Copyright 2015-2016 Collabora Ltd.
 *
 *  Based on the implementation from the Android Open Source Project,
 *
 *  Copyright 2012 Google, Inc
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 *  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 *  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *  OTHER DEALINGS IN THE SOFTWARE.
 */

#include "sync.h"
#include "sw_sync.h"
#include "synctest.h"

int test_alloc_timeline(void)
{
	int timeline, valid;

	timeline = sw_sync_timeline_create();
	valid = sw_sync_timeline_is_valid(timeline);
	ASSERT(valid, "Failure allocating timeline\n");

	sw_sync_timeline_destroy(timeline);
	return 0;
}

int test_alloc_fence(void)
{
	int timeline, fence, valid;

	timeline = sw_sync_timeline_create();
	valid = sw_sync_timeline_is_valid(timeline);
	ASSERT(valid, "Failure allocating timeline\n");

	fence = sw_sync_fence_create(timeline, "allocFence", 1);
	valid = sw_sync_fence_is_valid(fence);
	ASSERT(valid, "Failure allocating fence\n");

	sw_sync_fence_destroy(fence);
	sw_sync_timeline_destroy(timeline);
	return 0;
}

int test_alloc_fence_negative(void)
{
	int fence, timeline;

	timeline = sw_sync_timeline_create();
	ASSERT(timeline > 0, "Failure allocating timeline\n");

	fence = sw_sync_fence_create(-1, "fence", 1);
	ASSERT(fence < 0, "Success allocating negative fence\n");

	sw_sync_fence_destroy(fence);
	sw_sync_timeline_destroy(timeline);
	return 0;
}
