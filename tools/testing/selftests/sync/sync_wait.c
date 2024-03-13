/*
 *  sync fence wait tests
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

int test_fence_multi_timeline_wait(void)
{
	int timelineA, timelineB, timelineC;
	int fenceA, fenceB, fenceC, merged;
	int valid, active, signaled, ret;

	timelineA = sw_sync_timeline_create();
	timelineB = sw_sync_timeline_create();
	timelineC = sw_sync_timeline_create();

	fenceA = sw_sync_fence_create(timelineA, "fenceA", 5);
	fenceB = sw_sync_fence_create(timelineB, "fenceB", 5);
	fenceC = sw_sync_fence_create(timelineC, "fenceC", 5);

	merged = sync_merge("mergeFence", fenceB, fenceA);
	merged = sync_merge("mergeFence", fenceC, merged);

	valid = sw_sync_fence_is_valid(merged);
	ASSERT(valid, "Failure merging fence from various timelines\n");

	/* Confirm fence isn't signaled */
	active = sync_fence_count_with_status(merged, FENCE_STATUS_ACTIVE);
	ASSERT(active == 3, "Fence signaled too early!\n");

	ret = sync_wait(merged, 0);
	ASSERT(ret == 0,
	       "Failure waiting on fence until timeout\n");

	ret = sw_sync_timeline_inc(timelineA, 5);
	active = sync_fence_count_with_status(merged, FENCE_STATUS_ACTIVE);
	signaled = sync_fence_count_with_status(merged, FENCE_STATUS_SIGNALED);
	ASSERT(active == 2 && signaled == 1,
	       "Fence did not signal properly!\n");

	ret = sw_sync_timeline_inc(timelineB, 5);
	active = sync_fence_count_with_status(merged, FENCE_STATUS_ACTIVE);
	signaled = sync_fence_count_with_status(merged, FENCE_STATUS_SIGNALED);
	ASSERT(active == 1 && signaled == 2,
	       "Fence did not signal properly!\n");

	ret = sw_sync_timeline_inc(timelineC, 5);
	active = sync_fence_count_with_status(merged, FENCE_STATUS_ACTIVE);
	signaled = sync_fence_count_with_status(merged, FENCE_STATUS_SIGNALED);
	ASSERT(active == 0 && signaled == 3,
	       "Fence did not signal properly!\n");

	/* confirm you can successfully wait */
	ret = sync_wait(merged, 100);
	ASSERT(ret > 0, "Failure waiting on signaled fence\n");

	sw_sync_fence_destroy(merged);
	sw_sync_fence_destroy(fenceC);
	sw_sync_fence_destroy(fenceB);
	sw_sync_fence_destroy(fenceA);
	sw_sync_timeline_destroy(timelineC);
	sw_sync_timeline_destroy(timelineB);
	sw_sync_timeline_destroy(timelineA);

	return 0;
}
