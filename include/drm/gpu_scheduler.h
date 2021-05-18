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
#include <linux/completion.h>

#define MAX_WAIT_SCHED_ENTITY_Q_EMPTY msecs_to_jiffies(1000)

struct drm_gpu_scheduler;
struct drm_sched_rq;

/* These are often used as an (initial) index
 * to an array, and as such should start at 0.
 */
enum drm_sched_priority {
	DRM_SCHED_PRIORITY_MIN,
	DRM_SCHED_PRIORITY_NORMAL,
	DRM_SCHED_PRIORITY_HIGH,
	DRM_SCHED_PRIORITY_KERNEL,

	DRM_SCHED_PRIORITY_COUNT,
	DRM_SCHED_PRIORITY_UNSET = -2
};

/**
 * struct drm_sched_entity - A wrapper around a job queue (typically
 * attached to the DRM file_priv).
 *
 * @list: used to append this struct to the list of entities in the
 *        runqueue.
 * @rq: runqueue on which this entity is currently scheduled.
 * @sched_list: A list of schedulers (drm_gpu_schedulers).
 *              Jobs from this entity can be scheduled on any scheduler
 *              on this list.
 * @num_sched_list: number of drm_gpu_schedulers in the sched_list.
 * @priority: priority of the entity
 * @rq_lock: lock to modify the runqueue to which this entity belongs.
 * @job_queue: the list of jobs of this entity.
 * @fence_seq: a linearly increasing seqno incremented with each
 *             new &drm_sched_fence which is part of the entity.
 * @fence_context: a unique context for all the fences which belong
 *                 to this entity.
 *                 The &drm_sched_fence.scheduled uses the
 *                 fence_context but &drm_sched_fence.finished uses
 *                 fence_context + 1.
 * @dependency: the dependency fence of the job which is on the top
 *              of the job queue.
 * @cb: callback for the dependency fence above.
 * @guilty: points to ctx's guilty.
 * @fini_status: contains the exit status in case the process was signalled.
 * @last_scheduled: points to the finished fence of the last scheduled job.
 * @last_user: last group leader pushing a job into the entity.
 * @stopped: Marks the enity as removed from rq and destined for termination.
 * @entity_idle: Signals when enityt is not in use
 *
 * Entities will emit jobs in order to their corresponding hardware
 * ring, and the scheduler will alternate between entities based on
 * scheduling policy.
 */
struct drm_sched_entity {
	struct list_head		list;
	struct drm_sched_rq		*rq;
	struct drm_gpu_scheduler        **sched_list;
	unsigned int                    num_sched_list;
	enum drm_sched_priority         priority;
	spinlock_t			rq_lock;

	struct spsc_queue		job_queue;

	atomic_t			fence_seq;
	uint64_t			fence_context;

	struct dma_fence		*dependency;
	struct dma_fence_cb		cb;
	atomic_t			*guilty;
	struct dma_fence                *last_scheduled;
	struct task_struct		*last_user;
	bool 				stopped;
	struct completion		entity_idle;
};

/**
 * struct drm_sched_rq - queue of entities to be scheduled.
 *
 * @lock: to modify the entities list.
 * @sched: the scheduler to which this rq belongs to.
 * @entities: list of the entities to be scheduled.
 * @current_entity: the entity which is to be scheduled.
 *
 * Run queue is a set of entities scheduling command submissions for
 * one specific ring. It implements the scheduling policy that selects
 * the next entity to emit commands from.
 */
struct drm_sched_rq {
	spinlock_t			lock;
	struct drm_gpu_scheduler	*sched;
	struct list_head		entities;
	struct drm_sched_entity		*current_entity;
};

/**
 * struct drm_sched_fence - fences corresponding to the scheduling of a job.
 */
struct drm_sched_fence {
        /**
         * @scheduled: this fence is what will be signaled by the scheduler
         * when the job is scheduled.
         */
	struct dma_fence		scheduled;

        /**
         * @finished: this fence is what will be signaled by the scheduler
         * when the job is completed.
         *
         * When setting up an out fence for the job, you should use
         * this, since it's available immediately upon
         * drm_sched_job_init(), and the fence returned by the driver
         * from run_job() won't be created until the dependencies have
         * resolved.
         */
	struct dma_fence		finished;

        /**
         * @parent: the fence returned by &drm_sched_backend_ops.run_job
         * when scheduling the job on hardware. We signal the
         * &drm_sched_fence.finished fence once parent is signalled.
         */
	struct dma_fence		*parent;
        /**
         * @sched: the scheduler instance to which the job having this struct
         * belongs to.
         */
	struct drm_gpu_scheduler	*sched;
        /**
         * @lock: the lock used by the scheduled and the finished fences.
         */
	spinlock_t			lock;
        /**
         * @owner: job owner for debugging
         */
	void				*owner;
};

struct drm_sched_fence *to_drm_sched_fence(struct dma_fence *f);

/**
 * struct drm_sched_job - A job to be run by an entity.
 *
 * @queue_node: used to append this struct to the queue of jobs in an entity.
 * @list: a job participates in a "pending" and "done" lists.
 * @sched: the scheduler instance on which this job is scheduled.
 * @s_fence: contains the fences for the scheduling of job.
 * @finish_cb: the callback for the finished fence.
 * @id: a unique id assigned to each job scheduled on the scheduler.
 * @karma: increment on every hang caused by this job. If this exceeds the hang
 *         limit of the scheduler then the job is marked guilty and will not
 *         be scheduled further.
 * @s_priority: the priority of the job.
 * @entity: the entity to which this job belongs.
 * @cb: the callback for the parent fence in s_fence.
 *
 * A job is created by the driver using drm_sched_job_init(), and
 * should call drm_sched_entity_push_job() once it wants the scheduler
 * to schedule the job.
 */
struct drm_sched_job {
	struct spsc_node		queue_node;
	struct list_head		list;
	struct drm_gpu_scheduler	*sched;
	struct drm_sched_fence		*s_fence;
	struct dma_fence_cb		finish_cb;
	uint64_t			id;
	atomic_t			karma;
	enum drm_sched_priority		s_priority;
	struct drm_sched_entity         *entity;
	struct dma_fence_cb		cb;
};

static inline bool drm_sched_invalidate_job(struct drm_sched_job *s_job,
					    int threshold)
{
	return s_job && atomic_inc_return(&s_job->karma) > threshold;
}

enum drm_gpu_sched_stat {
	DRM_GPU_SCHED_STAT_NONE, /* Reserve 0 */
	DRM_GPU_SCHED_STAT_NOMINAL,
	DRM_GPU_SCHED_STAT_ENODEV,
};

/**
 * struct drm_sched_backend_ops
 *
 * Define the backend operations called by the scheduler,
 * these functions should be implemented in driver side.
 */
struct drm_sched_backend_ops {
	/**
         * @dependency: Called when the scheduler is considering scheduling
         * this job next, to get another struct dma_fence for this job to
	 * block on.  Once it returns NULL, run_job() may be called.
	 */
	struct dma_fence *(*dependency)(struct drm_sched_job *sched_job,
					struct drm_sched_entity *s_entity);

	/**
         * @run_job: Called to execute the job once all of the dependencies
         * have been resolved.  This may be called multiple times, if
	 * timedout_job() has happened and drm_sched_job_recovery()
	 * decides to try it again.
	 */
	struct dma_fence *(*run_job)(struct drm_sched_job *sched_job);

	/**
	 * @timedout_job: Called when a job has taken too long to execute,
	 * to trigger GPU recovery.
	 *
	 * Return DRM_GPU_SCHED_STAT_NOMINAL, when all is normal,
	 * and the underlying driver has started or completed recovery.
	 *
	 * Return DRM_GPU_SCHED_STAT_ENODEV, if the device is no longer
	 * available, i.e. has been unplugged.
	 */
	enum drm_gpu_sched_stat (*timedout_job)(struct drm_sched_job *sched_job);

	/**
         * @free_job: Called once the job's finished fence has been signaled
         * and it's time to clean it up.
	 */
	void (*free_job)(struct drm_sched_job *sched_job);
};

/**
 * struct drm_gpu_scheduler
 *
 * @ops: backend operations provided by the driver.
 * @hw_submission_limit: the max size of the hardware queue.
 * @timeout: the time after which a job is removed from the scheduler.
 * @name: name of the ring for which this scheduler is being used.
 * @sched_rq: priority wise array of run queues.
 * @wake_up_worker: the wait queue on which the scheduler sleeps until a job
 *                  is ready to be scheduled.
 * @job_scheduled: once @drm_sched_entity_do_release is called the scheduler
 *                 waits on this wait queue until all the scheduled jobs are
 *                 finished.
 * @hw_rq_count: the number of jobs currently in the hardware queue.
 * @job_id_count: used to assign unique id to the each job.
 * @work_tdr: schedules a delayed call to @drm_sched_job_timedout after the
 *            timeout interval is over.
 * @thread: the kthread on which the scheduler which run.
 * @pending_list: the list of jobs which are currently in the job queue.
 * @job_list_lock: lock to protect the pending_list.
 * @hang_limit: once the hangs by a job crosses this limit then it is marked
 *              guilty and it will be considered for scheduling further.
 * @score: score to help loadbalancer pick a idle sched
 * @_score: score used when the driver doesn't provide one
 * @ready: marks if the underlying HW is ready to work
 * @free_guilty: A hit to time out handler to free the guilty job.
 *
 * One scheduler is implemented for each hardware ring.
 */
struct drm_gpu_scheduler {
	const struct drm_sched_backend_ops	*ops;
	uint32_t			hw_submission_limit;
	long				timeout;
	const char			*name;
	struct drm_sched_rq		sched_rq[DRM_SCHED_PRIORITY_COUNT];
	wait_queue_head_t		wake_up_worker;
	wait_queue_head_t		job_scheduled;
	atomic_t			hw_rq_count;
	atomic64_t			job_id_count;
	struct delayed_work		work_tdr;
	struct task_struct		*thread;
	struct list_head		pending_list;
	spinlock_t			job_list_lock;
	int				hang_limit;
	atomic_t                        *score;
	atomic_t                        _score;
	bool				ready;
	bool				free_guilty;
};

int drm_sched_init(struct drm_gpu_scheduler *sched,
		   const struct drm_sched_backend_ops *ops,
		   uint32_t hw_submission, unsigned hang_limit, long timeout,
		   atomic_t *score, const char *name);

void drm_sched_fini(struct drm_gpu_scheduler *sched);
int drm_sched_job_init(struct drm_sched_job *job,
		       struct drm_sched_entity *entity,
		       void *owner);
void drm_sched_entity_modify_sched(struct drm_sched_entity *entity,
				    struct drm_gpu_scheduler **sched_list,
                                   unsigned int num_sched_list);

void drm_sched_job_cleanup(struct drm_sched_job *job);
void drm_sched_wakeup(struct drm_gpu_scheduler *sched);
void drm_sched_stop(struct drm_gpu_scheduler *sched, struct drm_sched_job *bad);
void drm_sched_start(struct drm_gpu_scheduler *sched, bool full_recovery);
void drm_sched_resubmit_jobs(struct drm_gpu_scheduler *sched);
void drm_sched_resubmit_jobs_ext(struct drm_gpu_scheduler *sched, int max);
void drm_sched_increase_karma(struct drm_sched_job *bad);
void drm_sched_reset_karma(struct drm_sched_job *bad);
void drm_sched_increase_karma_ext(struct drm_sched_job *bad, int type);
bool drm_sched_dependency_optimized(struct dma_fence* fence,
				    struct drm_sched_entity *entity);
void drm_sched_fault(struct drm_gpu_scheduler *sched);
void drm_sched_job_kickout(struct drm_sched_job *s_job);

void drm_sched_rq_add_entity(struct drm_sched_rq *rq,
			     struct drm_sched_entity *entity);
void drm_sched_rq_remove_entity(struct drm_sched_rq *rq,
				struct drm_sched_entity *entity);

int drm_sched_entity_init(struct drm_sched_entity *entity,
			  enum drm_sched_priority priority,
			  struct drm_gpu_scheduler **sched_list,
			  unsigned int num_sched_list,
			  atomic_t *guilty);
long drm_sched_entity_flush(struct drm_sched_entity *entity, long timeout);
void drm_sched_entity_fini(struct drm_sched_entity *entity);
void drm_sched_entity_destroy(struct drm_sched_entity *entity);
void drm_sched_entity_select_rq(struct drm_sched_entity *entity);
struct drm_sched_job *drm_sched_entity_pop_job(struct drm_sched_entity *entity);
void drm_sched_entity_push_job(struct drm_sched_job *sched_job,
			       struct drm_sched_entity *entity);
void drm_sched_entity_set_priority(struct drm_sched_entity *entity,
				   enum drm_sched_priority priority);
bool drm_sched_entity_is_ready(struct drm_sched_entity *entity);

struct drm_sched_fence *drm_sched_fence_create(
	struct drm_sched_entity *s_entity, void *owner);
void drm_sched_fence_scheduled(struct drm_sched_fence *fence);
void drm_sched_fence_finished(struct drm_sched_fence *fence);

unsigned long drm_sched_suspend_timeout(struct drm_gpu_scheduler *sched);
void drm_sched_resume_timeout(struct drm_gpu_scheduler *sched,
		                unsigned long remaining);
struct drm_gpu_scheduler *
drm_sched_pick_best(struct drm_gpu_scheduler **sched_list,
		     unsigned int num_sched_list);

#endif
