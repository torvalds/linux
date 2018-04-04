/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _DRM_GPU_SCHEDULER_H_
#define _DRM_GPU_SCHEDULER_H_

#include <drm/spsc_queue.h>
#include <linux/dma-fence.h>

struct drm_gpu_scheduler;
struct drm_sched_rq;

enum drm_sched_priority {
	DRM_SCHED_PRIORITY_MIN,
	DRM_SCHED_PRIORITY_LOW = DRM_SCHED_PRIORITY_MIN,
	DRM_SCHED_PRIORITY_NORMAL,
	DRM_SCHED_PRIORITY_HIGH_SW,
	DRM_SCHED_PRIORITY_HIGH_HW,
	DRM_SCHED_PRIORITY_KERNEL,
	DRM_SCHED_PRIORITY_MAX,
	DRM_SCHED_PRIORITY_INVALID = -1,
	DRM_SCHED_PRIORITY_UNSET = -2
};

/**
 * drm_sched_entity - A wrapper around a job queue (typically attached
 * to the DRM file_priv).
 *
 * Entities will emit jobs in order to their corresponding hardware
 * ring, and the scheduler will alternate between entities based on
 * scheduling policy.
*/
struct drm_sched_entity {
	struct list_head		list;
	struct drm_sched_rq		*rq;
	spinlock_t			rq_lock;
	struct drm_gpu_scheduler	*sched;

	spinlock_t			queue_lock;
	struct spsc_queue		job_queue;

	atomic_t			fence_seq;
	uint64_t			fence_context;

	struct dma_fence		*dependency;
	struct dma_fence_cb		cb;
	atomic_t			*guilty; /* points to ctx's guilty */
};

/**
 * Run queue is a set of entities scheduling command submissions for
 * one specific ring. It implements the scheduling policy that selects
 * the next entity to emit commands from.
*/
struct drm_sched_rq {
	spinlock_t			lock;
	struct list_head		entities;
	struct drm_sched_entity		*current_entity;
};

struct drm_sched_fence {
	struct dma_fence		scheduled;

	/* This fence is what will be signaled by the scheduler when
	 * the job is completed.
	 *
	 * When setting up an out fence for the job, you should use
	 * this, since it's available immediately upon
	 * drm_sched_job_init(), and the fence returned by the driver
	 * from run_job() won't be created until the dependencies have
	 * resolved.
	 */
	struct dma_fence		finished;

	struct dma_fence_cb		cb;
	struct dma_fence		*parent;
	struct drm_gpu_scheduler	*sched;
	spinlock_t			lock;
	void				*owner;
};

struct drm_sched_fence *to_drm_sched_fence(struct dma_fence *f);

/**
 * drm_sched_job - A job to be run by an entity.
 *
 * A job is created by the driver using drm_sched_job_init(), and
 * should call drm_sched_entity_push_job() once it wants the scheduler
 * to schedule the job.
 */
struct drm_sched_job {
	struct spsc_node		queue_node;
	struct drm_gpu_scheduler	*sched;
	struct drm_sched_fence		*s_fence;
	struct dma_fence_cb		finish_cb;
	struct work_struct		finish_work;
	struct list_head		node;
	struct delayed_work		work_tdr;
	uint64_t			id;
	atomic_t			karma;
	enum drm_sched_priority		s_priority;
};

static inline bool drm_sched_invalidate_job(struct drm_sched_job *s_job,
					    int threshold)
{
	return (s_job && atomic_inc_return(&s_job->karma) > threshold);
}

/**
 * Define the backend operations called by the scheduler,
 * these functions should be implemented in driver side
*/
struct drm_sched_backend_ops {
	/* Called when the scheduler is considering scheduling this
	 * job next, to get another struct dma_fence for this job to
	 * block on.  Once it returns NULL, run_job() may be called.
	 */
	struct dma_fence *(*dependency)(struct drm_sched_job *sched_job,
					struct drm_sched_entity *s_entity);

	/* Called to execute the job once all of the dependencies have
	 * been resolved.  This may be called multiple times, if
	 * timedout_job() has happened and drm_sched_job_recovery()
	 * decides to try it again.
	 */
	struct dma_fence *(*run_job)(struct drm_sched_job *sched_job);

	/* Called when a job has taken too long to execute, to trigger
	 * GPU recovery.
	 */
	void (*timedout_job)(struct drm_sched_job *sched_job);

	/* Called once the job's finished fence has been signaled and
	 * it's time to clean it up.
	 */
	void (*free_job)(struct drm_sched_job *sched_job);
};

/**
 * One scheduler is implemented for each hardware ring
*/
struct drm_gpu_scheduler {
	const struct drm_sched_backend_ops	*ops;
	uint32_t			hw_submission_limit;
	long				timeout;
	const char			*name;
	struct drm_sched_rq		sched_rq[DRM_SCHED_PRIORITY_MAX];
	wait_queue_head_t		wake_up_worker;
	wait_queue_head_t		job_scheduled;
	atomic_t			hw_rq_count;
	atomic64_t			job_id_count;
	struct task_struct		*thread;
	struct list_head		ring_mirror_list;
	spinlock_t			job_list_lock;
	int				hang_limit;
};

int drm_sched_init(struct drm_gpu_scheduler *sched,
		   const struct drm_sched_backend_ops *ops,
		   uint32_t hw_submission, unsigned hang_limit, long timeout,
		   const char *name);
void drm_sched_fini(struct drm_gpu_scheduler *sched);

int drm_sched_entity_init(struct drm_gpu_scheduler *sched,
			  struct drm_sched_entity *entity,
			  struct drm_sched_rq *rq,
			  uint32_t jobs, atomic_t *guilty);
void drm_sched_entity_fini(struct drm_gpu_scheduler *sched,
			   struct drm_sched_entity *entity);
void drm_sched_entity_push_job(struct drm_sched_job *sched_job,
			       struct drm_sched_entity *entity);
void drm_sched_entity_set_rq(struct drm_sched_entity *entity,
			     struct drm_sched_rq *rq);

struct drm_sched_fence *drm_sched_fence_create(
	struct drm_sched_entity *s_entity, void *owner);
void drm_sched_fence_scheduled(struct drm_sched_fence *fence);
void drm_sched_fence_finished(struct drm_sched_fence *fence);
int drm_sched_job_init(struct drm_sched_job *job,
		       struct drm_gpu_scheduler *sched,
		       struct drm_sched_entity *entity,
		       void *owner);
void drm_sched_hw_job_reset(struct drm_gpu_scheduler *sched,
			    struct drm_sched_job *job);
void drm_sched_job_recovery(struct drm_gpu_scheduler *sched);
bool drm_sched_dependency_optimized(struct dma_fence* fence,
				    struct drm_sched_entity *entity);
void drm_sched_job_kickout(struct drm_sched_job *s_job);

#endif
