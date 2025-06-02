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
#include <linux/xarray.h>
#include <linux/workqueue.h>

#define MAX_WAIT_SCHED_ENTITY_Q_EMPTY msecs_to_jiffies(1000)

/**
 * DRM_SCHED_FENCE_DONT_PIPELINE - Prevent dependency pipelining
 *
 * Setting this flag on a scheduler fence prevents pipelining of jobs depending
 * on this fence. In other words we always insert a full CPU round trip before
 * dependent jobs are pushed to the hw queue.
 */
#define DRM_SCHED_FENCE_DONT_PIPELINE	DMA_FENCE_FLAG_USER_BITS

/**
 * DRM_SCHED_FENCE_FLAG_HAS_DEADLINE_BIT - A fence deadline hint has been set
 *
 * Because we could have a deadline hint can be set before the backing hw
 * fence is created, we need to keep track of whether a deadline has already
 * been set.
 */
#define DRM_SCHED_FENCE_FLAG_HAS_DEADLINE_BIT	(DMA_FENCE_FLAG_USER_BITS + 1)

enum dma_resv_usage;
struct dma_resv;
struct drm_gem_object;

struct drm_gpu_scheduler;
struct drm_sched_rq;

struct drm_file;

/* These are often used as an (initial) index
 * to an array, and as such should start at 0.
 */
enum drm_sched_priority {
	DRM_SCHED_PRIORITY_KERNEL,
	DRM_SCHED_PRIORITY_HIGH,
	DRM_SCHED_PRIORITY_NORMAL,
	DRM_SCHED_PRIORITY_LOW,

	DRM_SCHED_PRIORITY_COUNT
};

/**
 * struct drm_sched_entity - A wrapper around a job queue (typically
 * attached to the DRM file_priv).
 *
 * Entities will emit jobs in order to their corresponding hardware
 * ring, and the scheduler will alternate between entities based on
 * scheduling policy.
 */
struct drm_sched_entity {
	/**
	 * @list:
	 *
	 * Used to append this struct to the list of entities in the runqueue
	 * @rq under &drm_sched_rq.entities.
	 *
	 * Protected by &drm_sched_rq.lock of @rq.
	 */
	struct list_head		list;

	/**
	 * @lock:
	 *
	 * Lock protecting the run-queue (@rq) to which this entity belongs,
	 * @priority and the list of schedulers (@sched_list, @num_sched_list).
	 */
	spinlock_t			lock;

	/**
	 * @rq:
	 *
	 * Runqueue on which this entity is currently scheduled.
	 *
	 * FIXME: Locking is very unclear for this. Writers are protected by
	 * @lock, but readers are generally lockless and seem to just race with
	 * not even a READ_ONCE.
	 */
	struct drm_sched_rq		*rq;

	/**
	 * @sched_list:
	 *
	 * A list of schedulers (struct drm_gpu_scheduler).  Jobs from this entity can
	 * be scheduled on any scheduler on this list.
	 *
	 * This can be modified by calling drm_sched_entity_modify_sched().
	 * Locking is entirely up to the driver, see the above function for more
	 * details.
	 *
	 * This will be set to NULL if &num_sched_list equals 1 and @rq has been
	 * set already.
	 *
	 * FIXME: This means priority changes through
	 * drm_sched_entity_set_priority() will be lost henceforth in this case.
	 */
	struct drm_gpu_scheduler        **sched_list;

	/**
	 * @num_sched_list:
	 *
	 * Number of drm_gpu_schedulers in the @sched_list.
	 */
	unsigned int                    num_sched_list;

	/**
	 * @priority:
	 *
	 * Priority of the entity. This can be modified by calling
	 * drm_sched_entity_set_priority(). Protected by @lock.
	 */
	enum drm_sched_priority         priority;

	/**
	 * @job_queue: the list of jobs of this entity.
	 */
	struct spsc_queue		job_queue;

	/**
	 * @fence_seq:
	 *
	 * A linearly increasing seqno incremented with each new
	 * &drm_sched_fence which is part of the entity.
	 *
	 * FIXME: Callers of drm_sched_job_arm() need to ensure correct locking,
	 * this doesn't need to be atomic.
	 */
	atomic_t			fence_seq;

	/**
	 * @fence_context:
	 *
	 * A unique context for all the fences which belong to this entity.  The
	 * &drm_sched_fence.scheduled uses the fence_context but
	 * &drm_sched_fence.finished uses fence_context + 1.
	 */
	uint64_t			fence_context;

	/**
	 * @dependency:
	 *
	 * The dependency fence of the job which is on the top of the job queue.
	 */
	struct dma_fence		*dependency;

	/**
	 * @cb:
	 *
	 * Callback for the dependency fence above.
	 */
	struct dma_fence_cb		cb;

	/**
	 * @guilty:
	 *
	 * Points to entities' guilty.
	 */
	atomic_t			*guilty;

	/**
	 * @last_scheduled:
	 *
	 * Points to the finished fence of the last scheduled job. Only written
	 * by the scheduler thread, can be accessed locklessly from
	 * drm_sched_job_arm() if the queue is empty.
	 */
	struct dma_fence __rcu		*last_scheduled;

	/**
	 * @last_user: last group leader pushing a job into the entity.
	 */
	struct task_struct		*last_user;

	/**
	 * @stopped:
	 *
	 * Marks the enity as removed from rq and destined for
	 * termination. This is set by calling drm_sched_entity_flush() and by
	 * drm_sched_fini().
	 */
	bool 				stopped;

	/**
	 * @entity_idle:
	 *
	 * Signals when entity is not in use, used to sequence entity cleanup in
	 * drm_sched_entity_fini().
	 */
	struct completion		entity_idle;

	/**
	 * @oldest_job_waiting:
	 *
	 * Marks earliest job waiting in SW queue
	 */
	ktime_t				oldest_job_waiting;

	/**
	 * @rb_tree_node:
	 *
	 * The node used to insert this entity into time based priority queue
	 */
	struct rb_node			rb_tree_node;

};

/**
 * struct drm_sched_rq - queue of entities to be scheduled.
 *
 * @sched: the scheduler to which this rq belongs to.
 * @lock: protects @entities, @rb_tree_root and @current_entity.
 * @current_entity: the entity which is to be scheduled.
 * @entities: list of the entities to be scheduled.
 * @rb_tree_root: root of time based priority queue of entities for FIFO scheduling
 *
 * Run queue is a set of entities scheduling command submissions for
 * one specific ring. It implements the scheduling policy that selects
 * the next entity to emit commands from.
 */
struct drm_sched_rq {
	struct drm_gpu_scheduler	*sched;

	spinlock_t			lock;
	/* Following members are protected by the @lock: */
	struct drm_sched_entity		*current_entity;
	struct list_head		entities;
	struct rb_root_cached		rb_tree_root;
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
	 * @deadline: deadline set on &drm_sched_fence.finished which
	 * potentially needs to be propagated to &drm_sched_fence.parent
	 */
	ktime_t				deadline;

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
 * @credits: the number of credits this job contributes to the scheduler
 * @work: Helper to reschedule job kill to different context.
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
	u64				id;

	/**
	 * @submit_ts:
	 *
	 * When the job was pushed into the entity queue.
	 */
	ktime_t                         submit_ts;

	/**
	 * @sched:
	 *
	 * The scheduler this job is or will be scheduled on. Gets set by
	 * drm_sched_job_arm(). Valid until drm_sched_backend_ops.free_job()
	 * has finished.
	 */
	struct drm_gpu_scheduler	*sched;

	struct drm_sched_fence		*s_fence;
	struct drm_sched_entity         *entity;

	enum drm_sched_priority		s_priority;
	u32				credits;
	/** @last_dependency: tracks @dependencies as they signal */
	unsigned int			last_dependency;
	atomic_t			karma;

	struct spsc_node		queue_node;
	struct list_head		list;

	/*
	 * work is used only after finish_cb has been used and will not be
	 * accessed anymore.
	 */
	union {
		struct dma_fence_cb	finish_cb;
		struct work_struct	work;
	};

	struct dma_fence_cb		cb;

	/**
	 * @dependencies:
	 *
	 * Contains the dependencies as struct dma_fence for this job, see
	 * drm_sched_job_add_dependency() and
	 * drm_sched_job_add_implicit_dependencies().
	 */
	struct xarray			dependencies;
};

enum drm_gpu_sched_stat {
	DRM_GPU_SCHED_STAT_NONE, /* Reserve 0 */
	DRM_GPU_SCHED_STAT_NOMINAL,
	DRM_GPU_SCHED_STAT_ENODEV,
};

/**
 * struct drm_sched_backend_ops - Define the backend operations
 *	called by the scheduler
 *
 * These functions should be implemented in the driver side.
 */
struct drm_sched_backend_ops {
	/**
	 * @prepare_job:
	 *
	 * Called when the scheduler is considering scheduling this job next, to
	 * get another struct dma_fence for this job to block on.  Once it
	 * returns NULL, run_job() may be called.
	 *
	 * Can be NULL if no additional preparation to the dependencies are
	 * necessary. Skipped when jobs are killed instead of run.
	 */
	struct dma_fence *(*prepare_job)(struct drm_sched_job *sched_job,
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
	 * This method is called in a workqueue context.
	 *
	 * Drivers typically issue a reset to recover from GPU hangs, and this
	 * procedure usually follows the following workflow:
	 *
	 * 1. Stop the scheduler using drm_sched_stop(). This will park the
	 *    scheduler thread and cancel the timeout work, guaranteeing that
	 *    nothing is queued while we reset the hardware queue
	 * 2. Try to gracefully stop non-faulty jobs (optional)
	 * 3. Issue a GPU reset (driver-specific)
	 * 4. Re-submit jobs using drm_sched_resubmit_jobs()
	 * 5. Restart the scheduler using drm_sched_start(). At that point, new
	 *    jobs can be queued, and the scheduler thread is unblocked
	 *
	 * Note that some GPUs have distinct hardware queues but need to reset
	 * the GPU globally, which requires extra synchronization between the
	 * timeout handler of the different &drm_gpu_scheduler. One way to
	 * achieve this synchronization is to create an ordered workqueue
	 * (using alloc_ordered_workqueue()) at the driver level, and pass this
	 * queue to drm_sched_init(), to guarantee that timeout handlers are
	 * executed sequentially. The above workflow needs to be slightly
	 * adjusted in that case:
	 *
	 * 1. Stop all schedulers impacted by the reset using drm_sched_stop()
	 * 2. Try to gracefully stop non-faulty jobs on all queues impacted by
	 *    the reset (optional)
	 * 3. Issue a GPU reset on all faulty queues (driver-specific)
	 * 4. Re-submit jobs on all schedulers impacted by the reset using
	 *    drm_sched_resubmit_jobs()
	 * 5. Restart all schedulers that were stopped in step #1 using
	 *    drm_sched_start()
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
 * struct drm_gpu_scheduler - scheduler instance-specific data
 *
 * @ops: backend operations provided by the driver.
 * @credit_limit: the credit limit of this scheduler
 * @credit_count: the current credit count of this scheduler
 * @timeout: the time after which a job is removed from the scheduler.
 * @name: name of the ring for which this scheduler is being used.
 * @num_rqs: Number of run-queues. This is at most DRM_SCHED_PRIORITY_COUNT,
 *           as there's usually one run-queue per priority, but could be less.
 * @sched_rq: An allocated array of run-queues of size @num_rqs;
 * @job_scheduled: once @drm_sched_entity_do_release is called the scheduler
 *                 waits on this wait queue until all the scheduled jobs are
 *                 finished.
 * @job_id_count: used to assign unique id to the each job.
 * @submit_wq: workqueue used to queue @work_run_job and @work_free_job
 * @timeout_wq: workqueue used to queue @work_tdr
 * @work_run_job: work which calls run_job op of each scheduler.
 * @work_free_job: work which calls free_job op of each scheduler.
 * @work_tdr: schedules a delayed call to @drm_sched_job_timedout after the
 *            timeout interval is over.
 * @pending_list: the list of jobs which are currently in the job queue.
 * @job_list_lock: lock to protect the pending_list.
 * @hang_limit: once the hangs by a job crosses this limit then it is marked
 *              guilty and it will no longer be considered for scheduling.
 * @score: score to help loadbalancer pick a idle sched
 * @_score: score used when the driver doesn't provide one
 * @ready: marks if the underlying HW is ready to work
 * @free_guilty: A hit to time out handler to free the guilty job.
 * @pause_submit: pause queuing of @work_run_job on @submit_wq
 * @own_submit_wq: scheduler owns allocation of @submit_wq
 * @dev: system &struct device
 *
 * One scheduler is implemented for each hardware ring.
 */
struct drm_gpu_scheduler {
	const struct drm_sched_backend_ops	*ops;
	u32				credit_limit;
	atomic_t			credit_count;
	long				timeout;
	const char			*name;
	u32                             num_rqs;
	struct drm_sched_rq             **sched_rq;
	wait_queue_head_t		job_scheduled;
	atomic64_t			job_id_count;
	struct workqueue_struct		*submit_wq;
	struct workqueue_struct		*timeout_wq;
	struct work_struct		work_run_job;
	struct work_struct		work_free_job;
	struct delayed_work		work_tdr;
	struct list_head		pending_list;
	spinlock_t			job_list_lock;
	int				hang_limit;
	atomic_t                        *score;
	atomic_t                        _score;
	bool				ready;
	bool				free_guilty;
	bool				pause_submit;
	bool				own_submit_wq;
	struct device			*dev;
};

/**
 * struct drm_sched_init_args - parameters for initializing a DRM GPU scheduler
 *
 * @ops: backend operations provided by the driver
 * @submit_wq: workqueue to use for submission. If NULL, an ordered wq is
 *	       allocated and used.
 * @num_rqs: Number of run-queues. This may be at most DRM_SCHED_PRIORITY_COUNT,
 *	     as there's usually one run-queue per priority, but may be less.
 * @credit_limit: the number of credits this scheduler can hold from all jobs
 * @hang_limit: number of times to allow a job to hang before dropping it.
 *		This mechanism is DEPRECATED. Set it to 0.
 * @timeout: timeout value in jiffies for submitted jobs.
 * @timeout_wq: workqueue to use for timeout work. If NULL, the system_wq is used.
 * @score: score atomic shared with other schedulers. May be NULL.
 * @name: name (typically the driver's name). Used for debugging
 * @dev: associated device. Used for debugging
 */
struct drm_sched_init_args {
	const struct drm_sched_backend_ops *ops;
	struct workqueue_struct *submit_wq;
	struct workqueue_struct *timeout_wq;
	u32 num_rqs;
	u32 credit_limit;
	unsigned int hang_limit;
	long timeout;
	atomic_t *score;
	const char *name;
	struct device *dev;
};

/* Scheduler operations */

int drm_sched_init(struct drm_gpu_scheduler *sched,
		   const struct drm_sched_init_args *args);

void drm_sched_fini(struct drm_gpu_scheduler *sched);

unsigned long drm_sched_suspend_timeout(struct drm_gpu_scheduler *sched);
void drm_sched_resume_timeout(struct drm_gpu_scheduler *sched,
			      unsigned long remaining);
void drm_sched_tdr_queue_imm(struct drm_gpu_scheduler *sched);
bool drm_sched_wqueue_ready(struct drm_gpu_scheduler *sched);
void drm_sched_wqueue_stop(struct drm_gpu_scheduler *sched);
void drm_sched_wqueue_start(struct drm_gpu_scheduler *sched);
void drm_sched_stop(struct drm_gpu_scheduler *sched, struct drm_sched_job *bad);
void drm_sched_start(struct drm_gpu_scheduler *sched, int errno);
void drm_sched_resubmit_jobs(struct drm_gpu_scheduler *sched);
void drm_sched_fault(struct drm_gpu_scheduler *sched);

struct drm_gpu_scheduler *
drm_sched_pick_best(struct drm_gpu_scheduler **sched_list,
		    unsigned int num_sched_list);

/* Jobs */

int drm_sched_job_init(struct drm_sched_job *job,
		       struct drm_sched_entity *entity,
		       u32 credits, void *owner);
void drm_sched_job_arm(struct drm_sched_job *job);
void drm_sched_entity_push_job(struct drm_sched_job *sched_job);
int drm_sched_job_add_dependency(struct drm_sched_job *job,
				 struct dma_fence *fence);
int drm_sched_job_add_syncobj_dependency(struct drm_sched_job *job,
					 struct drm_file *file,
					 u32 handle,
					 u32 point);
int drm_sched_job_add_resv_dependencies(struct drm_sched_job *job,
					struct dma_resv *resv,
					enum dma_resv_usage usage);
int drm_sched_job_add_implicit_dependencies(struct drm_sched_job *job,
					    struct drm_gem_object *obj,
					    bool write);
bool drm_sched_job_has_dependency(struct drm_sched_job *job,
				  struct dma_fence *fence);
void drm_sched_job_cleanup(struct drm_sched_job *job);
void drm_sched_increase_karma(struct drm_sched_job *bad);

static inline bool drm_sched_invalidate_job(struct drm_sched_job *s_job,
					    int threshold)
{
	return s_job && atomic_inc_return(&s_job->karma) > threshold;
}

/* Entities */

int drm_sched_entity_init(struct drm_sched_entity *entity,
			  enum drm_sched_priority priority,
			  struct drm_gpu_scheduler **sched_list,
			  unsigned int num_sched_list,
			  atomic_t *guilty);
long drm_sched_entity_flush(struct drm_sched_entity *entity, long timeout);
void drm_sched_entity_fini(struct drm_sched_entity *entity);
void drm_sched_entity_destroy(struct drm_sched_entity *entity);
void drm_sched_entity_set_priority(struct drm_sched_entity *entity,
				   enum drm_sched_priority priority);
int drm_sched_entity_error(struct drm_sched_entity *entity);
void drm_sched_entity_modify_sched(struct drm_sched_entity *entity,
				   struct drm_gpu_scheduler **sched_list,
				   unsigned int num_sched_list);

#endif
