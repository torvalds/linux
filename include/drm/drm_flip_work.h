/*
 * Copyright (C) 2013 Red Hat
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef DRM_FLIP_WORK_H
#define DRM_FLIP_WORK_H

#include <linux/kfifo.h>
#include <linux/workqueue.h>

/**
 * DOC: flip utils
 *
 * Util to queue up work to run from work-queue context after flip/vblank.
 * Typically this can be used to defer unref of framebuffer's, cursor
 * bo's, etc until after vblank.  The APIs are all safe (and lockless)
 * for up to one producer and once consumer at a time.  The single-consumer
 * aspect is ensured by committing the queued work to a single work-queue.
 */

struct drm_flip_work;

/*
 * drm_flip_func_t - callback function
 *
 * @work: the flip work
 * @val: value queued via drm_flip_work_queue()
 *
 * Callback function to be called for each of the  queue'd work items after
 * drm_flip_work_commit() is called.
 */
typedef void (*drm_flip_func_t)(struct drm_flip_work *work, void *val);

/**
 * struct drm_flip_work - flip work queue
 * @name: debug name
 * @pending: number of queued but not committed items
 * @count: number of committed items
 * @func: callback fxn called for each committed item
 * @worker: worker which calls @func
 * @fifo: queue of committed items
 */
struct drm_flip_work {
	const char *name;
	atomic_t pending, count;
	drm_flip_func_t func;
	struct work_struct worker;
	DECLARE_KFIFO_PTR(fifo, void *);
};

void drm_flip_work_queue(struct drm_flip_work *work, void *val);
void drm_flip_work_commit(struct drm_flip_work *work,
		struct workqueue_struct *wq);
int drm_flip_work_init(struct drm_flip_work *work, int size,
		const char *name, drm_flip_func_t func);
void drm_flip_work_cleanup(struct drm_flip_work *work);

#endif  /* DRM_FLIP_WORK_H */
