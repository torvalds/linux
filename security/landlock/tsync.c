// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Cross-thread ruleset enforcement
 *
 * Copyright © 2025 Google LLC
 */

#include <linux/atomic.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/overflow.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/task_work.h>

#include "cred.h"
#include "tsync.h"

/*
 * Shared state between multiple threads which are enforcing Landlock rulesets
 * in lockstep with each other.
 */
struct tsync_shared_context {
	/* The old and tentative new creds of the calling thread. */
	const struct cred *old_cred;
	const struct cred *new_cred;

	/* True if sibling tasks need to set the no_new_privs flag. */
	bool set_no_new_privs;

	/* An error encountered in preparation step, or 0. */
	atomic_t preparation_error;

	/*
	 * Barrier after preparation step in restrict_one_thread.
	 * The calling thread waits for completion.
	 *
	 * Re-initialized on every round of looking for newly spawned threads.
	 */
	atomic_t num_preparing;
	struct completion all_prepared;

	/* Sibling threads wait for completion. */
	struct completion ready_to_commit;

	/*
	 * Barrier after commit step (used by syscall impl to wait for
	 * completion).
	 */
	atomic_t num_unfinished;
	struct completion all_finished;
};

struct tsync_work {
	struct callback_head work;
	struct task_struct *task;
	struct tsync_shared_context *shared_ctx;
};

/*
 * restrict_one_thread - update a thread's Landlock domain in lockstep with the
 * other threads in the same process
 *
 * When this is run, the same function gets run in all other threads in the same
 * process (except for the calling thread which called landlock_restrict_self).
 * The concurrently running invocations of restrict_one_thread coordinate
 * through the shared ctx object to do their work in lockstep to implement
 * all-or-nothing semantics for enforcing the new Landlock domain.
 *
 * Afterwards, depending on the presence of an error, all threads either commit
 * or abort the prepared credentials.  The commit operation can not fail any
 * more.
 */
static void restrict_one_thread(struct tsync_shared_context *ctx)
{
	int err;
	struct cred *cred = NULL;

	if (current_cred() == ctx->old_cred) {
		/*
		 * Switch out old_cred with new_cred, if possible.
		 *
		 * In the common case, where all threads initially point to the same
		 * struct cred, this optimization avoids creating separate redundant
		 * credentials objects for each, which would all have the same contents.
		 *
		 * Note: We are intentionally dropping the const qualifier here, because
		 * it is required by commit_creds() and abort_creds().
		 */
		cred = (struct cred *)get_cred(ctx->new_cred);
	} else {
		/* Else, prepare new creds and populate them. */
		cred = prepare_creds();

		if (!cred) {
			atomic_set(&ctx->preparation_error, -ENOMEM);

			/*
			 * Even on error, we need to adhere to the protocol and coordinate
			 * with concurrently running invocations.
			 */
			if (atomic_dec_return(&ctx->num_preparing) == 0)
				complete_all(&ctx->all_prepared);

			goto out;
		}

		landlock_cred_copy(landlock_cred(cred),
				   landlock_cred(ctx->new_cred));
	}

	/*
	 * Barrier: Wait until all threads are done preparing.
	 * After this point, we can have no more failures.
	 */
	if (atomic_dec_return(&ctx->num_preparing) == 0)
		complete_all(&ctx->all_prepared);

	/*
	 * Wait for signal from calling thread that it's safe to read the
	 * preparation error now and we are ready to commit (or abort).
	 */
	wait_for_completion(&ctx->ready_to_commit);

	/* Abort the commit if any of the other threads had an error. */
	err = atomic_read(&ctx->preparation_error);
	if (err) {
		abort_creds(cred);
		goto out;
	}

	/*
	 * Make sure that all sibling tasks fulfill the no_new_privs prerequisite.
	 * (This is in line with Seccomp's SECCOMP_FILTER_FLAG_TSYNC logic in
	 * kernel/seccomp.c)
	 */
	if (ctx->set_no_new_privs)
		task_set_no_new_privs(current);

	commit_creds(cred);

out:
	/* Notify the calling thread once all threads are done */
	if (atomic_dec_return(&ctx->num_unfinished) == 0)
		complete_all(&ctx->all_finished);
}

/*
 * restrict_one_thread_callback - task_work callback for restricting a thread
 *
 * Calls restrict_one_thread with the struct landlock_shared_tsync_context.
 */
static void restrict_one_thread_callback(struct callback_head *work)
{
	struct tsync_work *ctx = container_of(work, struct tsync_work, work);

	restrict_one_thread(ctx->shared_ctx);
}

/*
 * struct tsync_works - a growable array of per-task contexts
 *
 * The zero-initialized struct represents the empty array.
 */
struct tsync_works {
	struct tsync_work **works;
	size_t size;
	size_t capacity;
};

/*
 * tsync_works_provide - provides a preallocated tsync_work for the given task
 *
 * This also stores a task pointer in the context and increments the reference
 * count of the task.
 *
 * This function may fail in the case where we did not preallocate sufficient
 * capacity.  This can legitimately happen if new threads get started after we
 * grew the capacity.
 *
 * Returns:
 *   A pointer to the preallocated context struct, with task filled in.
 *
 *   NULL, if we ran out of preallocated context structs.
 */
static struct tsync_work *tsync_works_provide(struct tsync_works *s,
					      struct task_struct *task)
{
	struct tsync_work *ctx;

	if (s->size >= s->capacity)
		return NULL;

	ctx = s->works[s->size];
	s->size++;

	ctx->task = get_task_struct(task);
	return ctx;
}

/*
 * tsync_works_grow_by - preallocates space for n more contexts in s
 *
 * On a successful return, the subsequent n calls to tsync_works_provide() are
 * guaranteed to succeed.  (size + n <= capacity)
 *
 * Returns:
 *   -ENOMEM if the (re)allocation fails

 *   0       if the allocation succeeds, partially succeeds, or no reallocation
 *           was needed
 */
static int tsync_works_grow_by(struct tsync_works *s, size_t n, gfp_t flags)
{
	size_t i;
	size_t new_capacity;
	struct tsync_work **works;
	struct tsync_work *work;

	if (check_add_overflow(s->size, n, &new_capacity))
		return -EOVERFLOW;

	/* No need to reallocate if s already has sufficient capacity. */
	if (new_capacity <= s->capacity)
		return 0;

	works = krealloc_array(s->works, new_capacity, sizeof(s->works[0]),
			       flags);
	if (!works)
		return -ENOMEM;

	s->works = works;

	for (i = s->capacity; i < new_capacity; i++) {
		work = kzalloc(sizeof(*work), flags);
		if (!work) {
			/*
			 * Leave the object in a consistent state,
			 * but return an error.
			 */
			s->capacity = i;
			return -ENOMEM;
		}
		s->works[i] = work;
	}
	s->capacity = new_capacity;
	return 0;
}

/*
 * tsync_works_contains - checks for presence of task in s
 */
static bool tsync_works_contains_task(const struct tsync_works *s,
				      struct task_struct *task)
{
	size_t i;

	for (i = 0; i < s->size; i++)
		if (s->works[i]->task == task)
			return true;
	return false;
}

/*
 * tsync_works_release - frees memory held by s and drops all task references
 *
 * This does not free s itself, only the data structures held by it.
 */
static void tsync_works_release(struct tsync_works *s)
{
	size_t i;

	for (i = 0; i < s->size; i++) {
		if (!s->works[i]->task)
			continue;

		put_task_struct(s->works[i]->task);
	}

	for (i = 0; i < s->capacity; i++)
		kfree(s->works[i]);
	kfree(s->works);
	s->works = NULL;
	s->size = 0;
	s->capacity = 0;
}

/*
 * count_additional_threads - counts the sibling threads that are not in works
 */
static size_t count_additional_threads(const struct tsync_works *works)
{
	struct task_struct *thread, *caller;
	size_t n = 0;

	caller = current;

	guard(rcu)();

	for_each_thread(caller, thread) {
		/* Skip current, since it is initiating the sync. */
		if (thread == caller)
			continue;

		/* Skip exited threads. */
		if (thread->flags & PF_EXITING)
			continue;

		/* Skip threads that we have already seen. */
		if (tsync_works_contains_task(works, thread))
			continue;

		n++;
	}
	return n;
}

/*
 * schedule_task_work - adds task_work for all eligible sibling threads
 *                      which have not been scheduled yet
 *
 * For each added task_work, atomically increments shared_ctx->num_preparing and
 * shared_ctx->num_unfinished.
 *
 * Returns:
 *     true, if at least one eligible sibling thread was found
 */
static bool schedule_task_work(struct tsync_works *works,
			       struct tsync_shared_context *shared_ctx)
{
	int err;
	struct task_struct *thread, *caller;
	struct tsync_work *ctx;
	bool found_more_threads = false;

	caller = current;

	guard(rcu)();

	for_each_thread(caller, thread) {
		/* Skip current, since it is initiating the sync. */
		if (thread == caller)
			continue;

		/* Skip exited threads. */
		if (thread->flags & PF_EXITING)
			continue;

		/* Skip threads that we already looked at. */
		if (tsync_works_contains_task(works, thread))
			continue;

		/*
		 * We found a sibling thread that is not doing its task_work yet, and
		 * which might spawn new threads before our task work runs, so we need
		 * at least one more round in the outer loop.
		 */
		found_more_threads = true;

		ctx = tsync_works_provide(works, thread);
		if (!ctx) {
			/*
			 * We ran out of preallocated contexts -- we need to try again with
			 * this thread at a later time!
			 * found_more_threads is already true at this point.
			 */
			break;
		}

		ctx->shared_ctx = shared_ctx;

		atomic_inc(&shared_ctx->num_preparing);
		atomic_inc(&shared_ctx->num_unfinished);

		init_task_work(&ctx->work, restrict_one_thread_callback);
		err = task_work_add(thread, &ctx->work, TWA_SIGNAL);
		if (err) {
			/*
			 * task_work_add() only fails if the task is about to exit.  We
			 * checked that earlier, but it can happen as a race.  Resume
			 * without setting an error, as the task is probably gone in the
			 * next loop iteration.  For consistency, remove the task from ctx
			 * so that it does not look like we handed it a task_work.
			 */
			put_task_struct(ctx->task);
			ctx->task = NULL;

			atomic_dec(&shared_ctx->num_preparing);
			atomic_dec(&shared_ctx->num_unfinished);
		}
	}

	return found_more_threads;
}

/*
 * cancel_tsync_works - cancel all task works where it is possible
 *
 * Task works can be canceled as long as they are still queued and have not
 * started running.  If they get canceled, we decrement
 * shared_ctx->num_preparing and shared_ctx->num_unfished and mark the two
 * completions if needed, as if the task was never scheduled.
 */
static void cancel_tsync_works(struct tsync_works *works,
			       struct tsync_shared_context *shared_ctx)
{
	int i;

	for (i = 0; i < works->size; i++) {
		if (!task_work_cancel(works->works[i]->task,
				      &works->works[i]->work))
			continue;

		/* After dequeueing, act as if the task work had executed. */

		if (atomic_dec_return(&shared_ctx->num_preparing) == 0)
			complete_all(&shared_ctx->all_prepared);

		if (atomic_dec_return(&shared_ctx->num_unfinished) == 0)
			complete_all(&shared_ctx->all_finished);
	}
}

/*
 * restrict_sibling_threads - enables a Landlock policy for all sibling threads
 */
int landlock_restrict_sibling_threads(const struct cred *old_cred,
				      const struct cred *new_cred)
{
	int err;
	struct tsync_shared_context shared_ctx;
	struct tsync_works works = {};
	size_t newly_discovered_threads;
	bool found_more_threads;

	atomic_set(&shared_ctx.preparation_error, 0);
	init_completion(&shared_ctx.all_prepared);
	init_completion(&shared_ctx.ready_to_commit);
	atomic_set(&shared_ctx.num_unfinished, 1);
	init_completion(&shared_ctx.all_finished);
	shared_ctx.old_cred = old_cred;
	shared_ctx.new_cred = new_cred;
	shared_ctx.set_no_new_privs = task_no_new_privs(current);

	/*
	 * We schedule a pseudo-signal task_work for each of the calling task's
	 * sibling threads.  In the task work, each thread:
	 *
	 * 1) runs prepare_creds() and writes back the error to
	 *    shared_ctx.preparation_error, if needed.
	 *
	 * 2) signals that it's done with prepare_creds() to the calling task.
	 *    (completion "all_prepared").
	 *
	 * 3) waits for the completion "ready_to_commit".  This is sent by the
	 *    calling task after ensuring that all sibling threads have done
	 *    with the "preparation" stage.
	 *
	 *    After this barrier is reached, it's safe to read
	 *    shared_ctx.preparation_error.
	 *
	 * 4) reads shared_ctx.preparation_error and then either does commit_creds()
	 *    or abort_creds().
	 *
	 * 5) signals that it's done altogether (barrier synchronization
	 *    "all_finished")
	 *
	 * Unlike seccomp, which modifies sibling tasks directly, we do not need to
	 * acquire the cred_guard_mutex and sighand->siglock:
	 *
	 * - As in our case, all threads are themselves exchanging their own struct
	 *   cred through the credentials API, no locks are needed for that.
	 * - Our for_each_thread() loops are protected by RCU.
	 * - We do not acquire a lock to keep the list of sibling threads stable
	 *   between our for_each_thread loops.  If the list of available sibling
	 *   threads changes between these for_each_thread loops, we make up for
	 *   that by continuing to look for threads until they are all discovered
	 *   and have entered their task_work, where they are unable to spawn new
	 *   threads.
	 */
	do {
		/* In RCU read-lock, count the threads we need. */
		newly_discovered_threads = count_additional_threads(&works);

		if (newly_discovered_threads == 0)
			break; /* done */

		err = tsync_works_grow_by(&works, newly_discovered_threads,
					  GFP_KERNEL_ACCOUNT);
		if (err) {
			atomic_set(&shared_ctx.preparation_error, err);
			break;
		}

		/*
		 * The "all_prepared" barrier is used locally to the loop body, this use
		 * of for_each_thread().  We can reset it on each loop iteration because
		 * all previous loop iterations are done with it already.
		 *
		 * num_preparing is initialized to 1 so that the counter can not go to 0
		 * and mark the completion as done before all task works are registered.
		 * We decrement it at the end of the loop body.
		 */
		atomic_set(&shared_ctx.num_preparing, 1);
		reinit_completion(&shared_ctx.all_prepared);

		/*
		 * In RCU read-lock, schedule task work on newly discovered sibling
		 * tasks.
		 */
		found_more_threads = schedule_task_work(&works, &shared_ctx);

		/*
		 * Decrement num_preparing for current, to undo that we initialized it
		 * to 1 a few lines above.
		 */
		if (atomic_dec_return(&shared_ctx.num_preparing) > 0) {
			if (wait_for_completion_interruptible(
				    &shared_ctx.all_prepared)) {
				/* In case of interruption, we need to retry the system call. */
				atomic_set(&shared_ctx.preparation_error,
					   -ERESTARTNOINTR);

				/*
				 * Cancel task works for tasks that did not start running yet,
				 * and decrement all_prepared and num_unfinished accordingly.
				 */
				cancel_tsync_works(&works, &shared_ctx);

				/*
				 * The remaining task works have started running, so waiting for
				 * their completion will finish.
				 */
				wait_for_completion(&shared_ctx.all_prepared);
			}
		}
	} while (found_more_threads &&
		 !atomic_read(&shared_ctx.preparation_error));

	/*
	 * We now have all sibling threads blocking and in "prepared" state in the
	 * task work. Ask all threads to commit.
	 */
	complete_all(&shared_ctx.ready_to_commit);

	/*
	 * Decrement num_unfinished for current, to undo that we initialized it to 1
	 * at the beginning.
	 */
	if (atomic_dec_return(&shared_ctx.num_unfinished) > 0)
		wait_for_completion(&shared_ctx.all_finished);

	tsync_works_release(&works);

	return atomic_read(&shared_ctx.preparation_error);
}
