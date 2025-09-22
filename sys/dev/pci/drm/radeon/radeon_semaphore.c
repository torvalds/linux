/*
 * Copyright 2011 Christian König.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Christian König <deathsimple@vodafone.de>
 */

#include "radeon.h"
#include "radeon_trace.h"

int radeon_semaphore_create(struct radeon_device *rdev,
			    struct radeon_semaphore **semaphore)
{
	int r;

	*semaphore = kmalloc(sizeof(struct radeon_semaphore), GFP_KERNEL);
	if (*semaphore == NULL) {
		return -ENOMEM;
	}
	r = radeon_sa_bo_new(&rdev->ring_tmp_bo,
			     &(*semaphore)->sa_bo, 8, 8);
	if (r) {
		kfree(*semaphore);
		*semaphore = NULL;
		return r;
	}
	(*semaphore)->waiters = 0;
	(*semaphore)->gpu_addr = radeon_sa_bo_gpu_addr((*semaphore)->sa_bo);

	*((uint64_t *)radeon_sa_bo_cpu_addr((*semaphore)->sa_bo)) = 0;

	return 0;
}

bool radeon_semaphore_emit_signal(struct radeon_device *rdev, int ridx,
				  struct radeon_semaphore *semaphore)
{
	struct radeon_ring *ring = &rdev->ring[ridx];

	trace_radeon_semaphore_signale(ridx, semaphore);

	if (radeon_semaphore_ring_emit(rdev, ridx, ring, semaphore, false)) {
		--semaphore->waiters;

		/* for debugging lockup only, used by sysfs debug files */
		ring->last_semaphore_signal_addr = semaphore->gpu_addr;
		return true;
	}
	return false;
}

bool radeon_semaphore_emit_wait(struct radeon_device *rdev, int ridx,
				struct radeon_semaphore *semaphore)
{
	struct radeon_ring *ring = &rdev->ring[ridx];

	trace_radeon_semaphore_wait(ridx, semaphore);

	if (radeon_semaphore_ring_emit(rdev, ridx, ring, semaphore, true)) {
		++semaphore->waiters;

		/* for debugging lockup only, used by sysfs debug files */
		ring->last_semaphore_wait_addr = semaphore->gpu_addr;
		return true;
	}
	return false;
}

void radeon_semaphore_free(struct radeon_device *rdev,
			   struct radeon_semaphore **semaphore,
			   struct radeon_fence *fence)
{
	if (semaphore == NULL || *semaphore == NULL) {
		return;
	}
	if ((*semaphore)->waiters > 0) {
		dev_err(rdev->dev, "semaphore %p has more waiters than signalers,"
			" hardware lockup imminent!\n", *semaphore);
	}
	radeon_sa_bo_free(&(*semaphore)->sa_bo, fence);
	kfree(*semaphore);
	*semaphore = NULL;
}
