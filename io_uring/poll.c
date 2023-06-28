// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/hashtable.h>
#include <linux/io_uring.h>

#include <trace/events/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "refs.h"
#include "opdef.h"
#include "kbuf.h"
#include "poll.h"
#include "cancel.h"

struct io_poll_update {
	struct file			*file;
	u64				old_user_data;
	u64				new_user_data;
	__poll_t			events;
	bool				update_events;
	bool				update_user_data;
};

struct io_poll_table {
	struct poll_table_struct pt;
	struct io_kiocb *req;
	int nr_entries;
	int error;
	bool owning;
	/* output value, set only if arm poll returns >0 */
	__poll_t result_mask;
};

#define IO_POLL_CANCEL_FLAG	BIT(31)
#define IO_POLL_RETRY_FLAG	BIT(30)
#define IO_POLL_REF_MASK	GENMASK(29, 0)

/*
 * We usually have 1-2 refs taken, 128 is more than enough and we want to
 * maximise the margin between this amount and the moment when it overflows.
 */
#define IO_POLL_REF_BIAS	128

#define IO_WQE_F_DOUBLE		1

static inline struct io_kiocb *wqe_to_req(struct wait_queue_entry *wqe)
{
	unsigned long priv = (unsigned long)wqe->private;

	return (struct io_kiocb *)(priv & ~IO_WQE_F_DOUBLE);
}

static inline bool wqe_is_double(struct wait_queue_entry *wqe)
{
	unsigned long priv = (unsigned long)wqe->private;

	return priv & IO_WQE_F_DOUBLE;
}

static bool io_poll_get_ownership_slowpath(struct io_kiocb *req)
{
	int v;

	/*
	 * poll_refs are already elevated and we don't have much hope for
	 * grabbing the ownership. Instead of incrementing set a retry flag
	 * to notify the loop that there might have been some change.
	 */
	v = atomic_fetch_or(IO_POLL_RETRY_FLAG, &req->poll_refs);
	if (v & IO_POLL_REF_MASK)
		return false;
	return !(atomic_fetch_inc(&req->poll_refs) & IO_POLL_REF_MASK);
}

/*
 * If refs part of ->poll_refs (see IO_POLL_REF_MASK) is 0, it's free. We can
 * bump it and acquire ownership. It's disallowed to modify requests while not
 * owning it, that prevents from races for enqueueing task_work's and b/w
 * arming poll and wakeups.
 */
static inline bool io_poll_get_ownership(struct io_kiocb *req)
{
	if (unlikely(atomic_read(&req->poll_refs) >= IO_POLL_REF_BIAS))
		return io_poll_get_ownership_slowpath(req);
	return !(atomic_fetch_inc(&req->poll_refs) & IO_POLL_REF_MASK);
}

static void io_poll_mark_cancelled(struct io_kiocb *req)
{
	atomic_or(IO_POLL_CANCEL_FLAG, &req->poll_refs);
}

static struct io_poll *io_poll_get_double(struct io_kiocb *req)
{
	/* pure poll stashes this in ->async_data, poll driven retry elsewhere */
	if (req->opcode == IORING_OP_POLL_ADD)
		return req->async_data;
	return req->apoll->double_poll;
}

static struct io_poll *io_poll_get_single(struct io_kiocb *req)
{
	if (req->opcode == IORING_OP_POLL_ADD)
		return io_kiocb_to_cmd(req, struct io_poll);
	return &req->apoll->poll;
}

static void io_poll_req_insert(struct io_kiocb *req)
{
	struct io_hash_table *table = &req->ctx->cancel_table;
	u32 index = hash_long(req->cqe.user_data, table->hash_bits);
	struct io_hash_bucket *hb = &table->hbs[index];

	spin_lock(&hb->lock);
	hlist_add_head(&req->hash_node, &hb->list);
	spin_unlock(&hb->lock);
}

static void io_poll_req_delete(struct io_kiocb *req, struct io_ring_ctx *ctx)
{
	struct io_hash_table *table = &req->ctx->cancel_table;
	u32 index = hash_long(req->cqe.user_data, table->hash_bits);
	spinlock_t *lock = &table->hbs[index].lock;

	spin_lock(lock);
	hash_del(&req->hash_node);
	spin_unlock(lock);
}

static void io_poll_req_insert_locked(struct io_kiocb *req)
{
	struct io_hash_table *table = &req->ctx->cancel_table_locked;
	u32 index = hash_long(req->cqe.user_data, table->hash_bits);

	lockdep_assert_held(&req->ctx->uring_lock);

	hlist_add_head(&req->hash_node, &table->hbs[index].list);
}

static void io_poll_tw_hash_eject(struct io_kiocb *req, bool *locked)
{
	struct io_ring_ctx *ctx = req->ctx;

	if (req->flags & REQ_F_HASH_LOCKED) {
		/*
		 * ->cancel_table_locked is protected by ->uring_lock in
		 * contrast to per bucket spinlocks. Likely, tctx_task_work()
		 * already grabbed the mutex for us, but there is a chance it
		 * failed.
		 */
		io_tw_lock(ctx, locked);
		hash_del(&req->hash_node);
		req->flags &= ~REQ_F_HASH_LOCKED;
	} else {
		io_poll_req_delete(req, ctx);
	}
}

static void io_init_poll_iocb(struct io_poll *poll, __poll_t events,
			      wait_queue_func_t wake_func)
{
	poll->head = NULL;
#define IO_POLL_UNMASK	(EPOLLERR|EPOLLHUP|EPOLLNVAL|EPOLLRDHUP)
	/* mask in events that we always want/need */
	poll->events = events | IO_POLL_UNMASK;
	INIT_LIST_HEAD(&poll->wait.entry);
	init_waitqueue_func_entry(&poll->wait, wake_func);
}

static inline void io_poll_remove_entry(struct io_poll *poll)
{
	struct wait_queue_head *head = smp_load_acquire(&poll->head);

	if (head) {
		spin_lock_irq(&head->lock);
		list_del_init(&poll->wait.entry);
		poll->head = NULL;
		spin_unlock_irq(&head->lock);
	}
}

static void io_poll_remove_entries(struct io_kiocb *req)
{
	/*
	 * Nothing to do if neither of those flags are set. Avoid dipping
	 * into the poll/apoll/double cachelines if we can.
	 */
	if (!(req->flags & (REQ_F_SINGLE_POLL | REQ_F_DOUBLE_POLL)))
		return;

	/*
	 * While we hold the waitqueue lock and the waitqueue is nonempty,
	 * wake_up_pollfree() will wait for us.  However, taking the waitqueue
	 * lock in the first place can race with the waitqueue being freed.
	 *
	 * We solve this as eventpoll does: by taking advantage of the fact that
	 * all users of wake_up_pollfree() will RCU-delay the actual free.  If
	 * we enter rcu_read_lock() and see that the pointer to the queue is
	 * non-NULL, we can then lock it without the memory being freed out from
	 * under us.
	 *
	 * Keep holding rcu_read_lock() as long as we hold the queue lock, in
	 * case the caller deletes the entry from the queue, leaving it empty.
	 * In that case, only RCU prevents the queue memory from being freed.
	 */
	rcu_read_lock();
	if (req->flags & REQ_F_SINGLE_POLL)
		io_poll_remove_entry(io_poll_get_single(req));
	if (req->flags & REQ_F_DOUBLE_POLL)
		io_poll_remove_entry(io_poll_get_double(req));
	rcu_read_unlock();
}

enum {
	IOU_POLL_DONE = 0,
	IOU_POLL_NO_ACTION = 1,
	IOU_POLL_REMOVE_POLL_USE_RES = 2,
	IOU_POLL_REISSUE = 3,
};

/*
 * All poll tw should go through this. Checks for poll events, manages
 * references, does rewait, etc.
 *
 * Returns a negative error on failure. IOU_POLL_NO_ACTION when no action
 * require, which is either spurious wakeup or multishot CQE is served.
 * IOU_POLL_DONE when it's done with the request, then the mask is stored in
 * req->cqe.res. IOU_POLL_REMOVE_POLL_USE_RES indicates to remove multishot
 * poll and that the result is stored in req->cqe.
 */
static int io_poll_check_events(struct io_kiocb *req, bool *locked)
{
	struct io_ring_ctx *ctx = req->ctx;
	int v;

	/* req->task == current here, checking PF_EXITING is safe */
	if (unlikely(req->task->flags & PF_EXITING))
		return -ECANCELED;

	do {
		v = atomic_read(&req->poll_refs);

		/* tw handler should be the owner, and so have some references */
		if (WARN_ON_ONCE(!(v & IO_POLL_REF_MASK)))
			return IOU_POLL_DONE;
		if (v & IO_POLL_CANCEL_FLAG)
			return -ECANCELED;
		/*
		 * cqe.res contains only events of the first wake up
		 * and all others are be lost. Redo vfs_poll() to get
		 * up to date state.
		 */
		if ((v & IO_POLL_REF_MASK) != 1)
			req->cqe.res = 0;
		if (v & IO_POLL_RETRY_FLAG) {
			req->cqe.res = 0;
			/*
			 * We won't find new events that came in between
			 * vfs_poll and the ref put unless we clear the flag
			 * in advance.
			 */
			atomic_andnot(IO_POLL_RETRY_FLAG, &req->poll_refs);
			v &= ~IO_POLL_RETRY_FLAG;
		}

		/* the mask was stashed in __io_poll_execute */
		if (!req->cqe.res) {
			struct poll_table_struct pt = { ._key = req->apoll_events };
			req->cqe.res = vfs_poll(req->file, &pt) & req->apoll_events;
			/*
			 * We got woken with a mask, but someone else got to
			 * it first. The above vfs_poll() doesn't add us back
			 * to the waitqueue, so if we get nothing back, we
			 * should be safe and attempt a reissue.
			 */
			if (unlikely(!req->cqe.res)) {
				/* Multishot armed need not reissue */
				if (!(req->apoll_events & EPOLLONESHOT))
					continue;
				return IOU_POLL_REISSUE;
			}
		}
		if (req->apoll_events & EPOLLONESHOT)
			return IOU_POLL_DONE;
		if (io_is_uring_fops(req->file))
			return IOU_POLL_DONE;

		/* multishot, just fill a CQE and proceed */
		if (!(req->flags & REQ_F_APOLL_MULTISHOT)) {
			__poll_t mask = mangle_poll(req->cqe.res &
						    req->apoll_events);

			if (!io_post_aux_cqe(ctx, req->cqe.user_data,
					     mask, IORING_CQE_F_MORE, false)) {
				io_req_set_res(req, mask, 0);
				return IOU_POLL_REMOVE_POLL_USE_RES;
			}
		} else {
			int ret = io_poll_issue(req, locked);
			if (ret == IOU_STOP_MULTISHOT)
				return IOU_POLL_REMOVE_POLL_USE_RES;
			if (ret < 0)
				return ret;
		}

		/* force the next iteration to vfs_poll() */
		req->cqe.res = 0;

		/*
		 * Release all references, retry if someone tried to restart
		 * task_work while we were executing it.
		 */
	} while (atomic_sub_return(v & IO_POLL_REF_MASK, &req->poll_refs) &
					IO_POLL_REF_MASK);

	return IOU_POLL_NO_ACTION;
}

static void io_poll_task_func(struct io_kiocb *req, bool *locked)
{
	int ret;

	ret = io_poll_check_events(req, locked);
	if (ret == IOU_POLL_NO_ACTION)
		return;

	if (ret == IOU_POLL_DONE) {
		struct io_poll *poll = io_kiocb_to_cmd(req, struct io_poll);
		req->cqe.res = mangle_poll(req->cqe.res & poll->events);
	} else if (ret == IOU_POLL_REISSUE) {
		io_poll_remove_entries(req);
		io_poll_tw_hash_eject(req, locked);
		io_req_task_submit(req, locked);
		return;
	} else if (ret != IOU_POLL_REMOVE_POLL_USE_RES) {
		req->cqe.res = ret;
		req_set_fail(req);
	}

	io_poll_remove_entries(req);
	io_poll_tw_hash_eject(req, locked);

	io_req_set_res(req, req->cqe.res, 0);
	io_req_task_complete(req, locked);
}

static void io_apoll_task_func(struct io_kiocb *req, bool *locked)
{
	int ret;

	ret = io_poll_check_events(req, locked);
	if (ret == IOU_POLL_NO_ACTION)
		return;

	io_poll_remove_entries(req);
	io_poll_tw_hash_eject(req, locked);

	if (ret == IOU_POLL_REMOVE_POLL_USE_RES)
		io_req_complete_post(req);
	else if (ret == IOU_POLL_DONE || ret == IOU_POLL_REISSUE)
		io_req_task_submit(req, locked);
	else
		io_req_complete_failed(req, ret);
}

static void __io_poll_execute(struct io_kiocb *req, int mask)
{
	io_req_set_res(req, mask, 0);
	/*
	 * This is useful for poll that is armed on behalf of another
	 * request, and where the wakeup path could be on a different
	 * CPU. We want to avoid pulling in req->apoll->events for that
	 * case.
	 */
	if (req->opcode == IORING_OP_POLL_ADD)
		req->io_task_work.func = io_poll_task_func;
	else
		req->io_task_work.func = io_apoll_task_func;

	trace_io_uring_task_add(req, mask);
	io_req_task_work_add(req);
}

static inline void io_poll_execute(struct io_kiocb *req, int res)
{
	if (io_poll_get_ownership(req))
		__io_poll_execute(req, res);
}

static void io_poll_cancel_req(struct io_kiocb *req)
{
	io_poll_mark_cancelled(req);
	/* kick tw, which should complete the request */
	io_poll_execute(req, 0);
}

#define IO_ASYNC_POLL_COMMON	(EPOLLONESHOT | EPOLLPRI)

static __cold int io_pollfree_wake(struct io_kiocb *req, struct io_poll *poll)
{
	io_poll_mark_cancelled(req);
	/* we have to kick tw in case it's not already */
	io_poll_execute(req, 0);

	/*
	 * If the waitqueue is being freed early but someone is already
	 * holds ownership over it, we have to tear down the request as
	 * best we can. That means immediately removing the request from
	 * its waitqueue and preventing all further accesses to the
	 * waitqueue via the request.
	 */
	list_del_init(&poll->wait.entry);

	/*
	 * Careful: this *must* be the last step, since as soon
	 * as req->head is NULL'ed out, the request can be
	 * completed and freed, since aio_poll_complete_work()
	 * will no longer need to take the waitqueue lock.
	 */
	smp_store_release(&poll->head, NULL);
	return 1;
}

static int io_poll_wake(struct wait_queue_entry *wait, unsigned mode, int sync,
			void *key)
{
	struct io_kiocb *req = wqe_to_req(wait);
	struct io_poll *poll = container_of(wait, struct io_poll, wait);
	__poll_t mask = key_to_poll(key);

	if (unlikely(mask & POLLFREE))
		return io_pollfree_wake(req, poll);

	/* for instances that support it check for an event match first */
	if (mask && !(mask & (poll->events & ~IO_ASYNC_POLL_COMMON)))
		return 0;

	if (io_poll_get_ownership(req)) {
		/*
		 * If we trigger a multishot poll off our own wakeup path,
		 * disable multishot as there is a circular dependency between
		 * CQ posting and triggering the event.
		 */
		if (mask & EPOLL_URING_WAKE)
			poll->events |= EPOLLONESHOT;

		/* optional, saves extra locking for removal in tw handler */
		if (mask && poll->events & EPOLLONESHOT) {
			list_del_init(&poll->wait.entry);
			poll->head = NULL;
			if (wqe_is_double(wait))
				req->flags &= ~REQ_F_DOUBLE_POLL;
			else
				req->flags &= ~REQ_F_SINGLE_POLL;
		}
		__io_poll_execute(req, mask);
	}
	return 1;
}

/* fails only when polling is already completing by the first entry */
static bool io_poll_double_prepare(struct io_kiocb *req)
{
	struct wait_queue_head *head;
	struct io_poll *poll = io_poll_get_single(req);

	/* head is RCU protected, see io_poll_remove_entries() comments */
	rcu_read_lock();
	head = smp_load_acquire(&poll->head);
	/*
	 * poll arm might not hold ownership and so race for req->flags with
	 * io_poll_wake(). There is only one poll entry queued, serialise with
	 * it by taking its head lock. As we're still arming the tw hanlder
	 * is not going to be run, so there are no races with it.
	 */
	if (head) {
		spin_lock_irq(&head->lock);
		req->flags |= REQ_F_DOUBLE_POLL;
		if (req->opcode == IORING_OP_POLL_ADD)
			req->flags |= REQ_F_ASYNC_DATA;
		spin_unlock_irq(&head->lock);
	}
	rcu_read_unlock();
	return !!head;
}

static void __io_queue_proc(struct io_poll *poll, struct io_poll_table *pt,
			    struct wait_queue_head *head,
			    struct io_poll **poll_ptr)
{
	struct io_kiocb *req = pt->req;
	unsigned long wqe_private = (unsigned long) req;

	/*
	 * The file being polled uses multiple waitqueues for poll handling
	 * (e.g. one for read, one for write). Setup a separate io_poll
	 * if this happens.
	 */
	if (unlikely(pt->nr_entries)) {
		struct io_poll *first = poll;

		/* double add on the same waitqueue head, ignore */
		if (first->head == head)
			return;
		/* already have a 2nd entry, fail a third attempt */
		if (*poll_ptr) {
			if ((*poll_ptr)->head == head)
				return;
			pt->error = -EINVAL;
			return;
		}

		poll = kmalloc(sizeof(*poll), GFP_ATOMIC);
		if (!poll) {
			pt->error = -ENOMEM;
			return;
		}

		/* mark as double wq entry */
		wqe_private |= IO_WQE_F_DOUBLE;
		io_init_poll_iocb(poll, first->events, first->wait.func);
		if (!io_poll_double_prepare(req)) {
			/* the request is completing, just back off */
			kfree(poll);
			return;
		}
		*poll_ptr = poll;
	} else {
		/* fine to modify, there is no poll queued to race with us */
		req->flags |= REQ_F_SINGLE_POLL;
	}

	pt->nr_entries++;
	poll->head = head;
	poll->wait.private = (void *) wqe_private;

	if (poll->events & EPOLLEXCLUSIVE)
		add_wait_queue_exclusive(head, &poll->wait);
	else
		add_wait_queue(head, &poll->wait);
}

static void io_poll_queue_proc(struct file *file, struct wait_queue_head *head,
			       struct poll_table_struct *p)
{
	struct io_poll_table *pt = container_of(p, struct io_poll_table, pt);
	struct io_poll *poll = io_kiocb_to_cmd(pt->req, struct io_poll);

	__io_queue_proc(poll, pt, head,
			(struct io_poll **) &pt->req->async_data);
}

static bool io_poll_can_finish_inline(struct io_kiocb *req,
				      struct io_poll_table *pt)
{
	return pt->owning || io_poll_get_ownership(req);
}

static void io_poll_add_hash(struct io_kiocb *req)
{
	if (req->flags & REQ_F_HASH_LOCKED)
		io_poll_req_insert_locked(req);
	else
		io_poll_req_insert(req);
}

/*
 * Returns 0 when it's handed over for polling. The caller owns the requests if
 * it returns non-zero, but otherwise should not touch it. Negative values
 * contain an error code. When the result is >0, the polling has completed
 * inline and ipt.result_mask is set to the mask.
 */
static int __io_arm_poll_handler(struct io_kiocb *req,
				 struct io_poll *poll,
				 struct io_poll_table *ipt, __poll_t mask,
				 unsigned issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;

	INIT_HLIST_NODE(&req->hash_node);
	req->work.cancel_seq = atomic_read(&ctx->cancel_seq);
	io_init_poll_iocb(poll, mask, io_poll_wake);
	poll->file = req->file;
	req->apoll_events = poll->events;

	ipt->pt._key = mask;
	ipt->req = req;
	ipt->error = 0;
	ipt->nr_entries = 0;
	/*
	 * Polling is either completed here or via task_work, so if we're in the
	 * task context we're naturally serialised with tw by merit of running
	 * the same task. When it's io-wq, take the ownership to prevent tw
	 * from running. However, when we're in the task context, skip taking
	 * it as an optimisation.
	 *
	 * Note: even though the request won't be completed/freed, without
	 * ownership we still can race with io_poll_wake().
	 * io_poll_can_finish_inline() tries to deal with that.
	 */
	ipt->owning = issue_flags & IO_URING_F_UNLOCKED;
	atomic_set(&req->poll_refs, (int)ipt->owning);

	/* io-wq doesn't hold uring_lock */
	if (issue_flags & IO_URING_F_UNLOCKED)
		req->flags &= ~REQ_F_HASH_LOCKED;

	mask = vfs_poll(req->file, &ipt->pt) & poll->events;

	if (unlikely(ipt->error || !ipt->nr_entries)) {
		io_poll_remove_entries(req);

		if (!io_poll_can_finish_inline(req, ipt)) {
			io_poll_mark_cancelled(req);
			return 0;
		} else if (mask && (poll->events & EPOLLET)) {
			ipt->result_mask = mask;
			return 1;
		}
		return ipt->error ?: -EINVAL;
	}

	if (mask &&
	   ((poll->events & (EPOLLET|EPOLLONESHOT)) == (EPOLLET|EPOLLONESHOT))) {
		if (!io_poll_can_finish_inline(req, ipt)) {
			io_poll_add_hash(req);
			return 0;
		}
		io_poll_remove_entries(req);
		ipt->result_mask = mask;
		/* no one else has access to the req, forget about the ref */
		return 1;
	}

	io_poll_add_hash(req);

	if (mask && (poll->events & EPOLLET) &&
	    io_poll_can_finish_inline(req, ipt)) {
		__io_poll_execute(req, mask);
		return 0;
	}

	if (ipt->owning) {
		/*
		 * Try to release ownership. If we see a change of state, e.g.
		 * poll was waken up, queue up a tw, it'll deal with it.
		 */
		if (atomic_cmpxchg(&req->poll_refs, 1, 0) != 1)
			__io_poll_execute(req, 0);
	}
	return 0;
}

static void io_async_queue_proc(struct file *file, struct wait_queue_head *head,
			       struct poll_table_struct *p)
{
	struct io_poll_table *pt = container_of(p, struct io_poll_table, pt);
	struct async_poll *apoll = pt->req->apoll;

	__io_queue_proc(&apoll->poll, pt, head, &apoll->double_poll);
}

/*
 * We can't reliably detect loops in repeated poll triggers and issue
 * subsequently failing. But rather than fail these immediately, allow a
 * certain amount of retries before we give up. Given that this condition
 * should _rarely_ trigger even once, we should be fine with a larger value.
 */
#define APOLL_MAX_RETRY		128

static struct async_poll *io_req_alloc_apoll(struct io_kiocb *req,
					     unsigned issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_cache_entry *entry;
	struct async_poll *apoll;

	if (req->flags & REQ_F_POLLED) {
		apoll = req->apoll;
		kfree(apoll->double_poll);
	} else if (!(issue_flags & IO_URING_F_UNLOCKED)) {
		entry = io_alloc_cache_get(&ctx->apoll_cache);
		if (entry == NULL)
			goto alloc_apoll;
		apoll = container_of(entry, struct async_poll, cache);
		apoll->poll.retries = APOLL_MAX_RETRY;
	} else {
alloc_apoll:
		apoll = kmalloc(sizeof(*apoll), GFP_ATOMIC);
		if (unlikely(!apoll))
			return NULL;
		apoll->poll.retries = APOLL_MAX_RETRY;
	}
	apoll->double_poll = NULL;
	req->apoll = apoll;
	if (unlikely(!--apoll->poll.retries))
		return NULL;
	return apoll;
}

int io_arm_poll_handler(struct io_kiocb *req, unsigned issue_flags)
{
	const struct io_op_def *def = &io_op_defs[req->opcode];
	struct async_poll *apoll;
	struct io_poll_table ipt;
	__poll_t mask = POLLPRI | POLLERR | EPOLLET;
	int ret;

	/*
	 * apoll requests already grab the mutex to complete in the tw handler,
	 * so removal from the mutex-backed hash is free, use it by default.
	 */
	req->flags |= REQ_F_HASH_LOCKED;

	if (!def->pollin && !def->pollout)
		return IO_APOLL_ABORTED;
	if (!file_can_poll(req->file))
		return IO_APOLL_ABORTED;
	if (!(req->flags & REQ_F_APOLL_MULTISHOT))
		mask |= EPOLLONESHOT;

	if (def->pollin) {
		mask |= EPOLLIN | EPOLLRDNORM;

		/* If reading from MSG_ERRQUEUE using recvmsg, ignore POLLIN */
		if (req->flags & REQ_F_CLEAR_POLLIN)
			mask &= ~EPOLLIN;
	} else {
		mask |= EPOLLOUT | EPOLLWRNORM;
	}
	if (def->poll_exclusive)
		mask |= EPOLLEXCLUSIVE;

	apoll = io_req_alloc_apoll(req, issue_flags);
	if (!apoll)
		return IO_APOLL_ABORTED;
	req->flags &= ~(REQ_F_SINGLE_POLL | REQ_F_DOUBLE_POLL);
	req->flags |= REQ_F_POLLED;
	ipt.pt._qproc = io_async_queue_proc;

	io_kbuf_recycle(req, issue_flags);

	ret = __io_arm_poll_handler(req, &apoll->poll, &ipt, mask, issue_flags);
	if (ret)
		return ret > 0 ? IO_APOLL_READY : IO_APOLL_ABORTED;
	trace_io_uring_poll_arm(req, mask, apoll->poll.events);
	return IO_APOLL_OK;
}

static __cold bool io_poll_remove_all_table(struct task_struct *tsk,
					    struct io_hash_table *table,
					    bool cancel_all)
{
	unsigned nr_buckets = 1U << table->hash_bits;
	struct hlist_node *tmp;
	struct io_kiocb *req;
	bool found = false;
	int i;

	for (i = 0; i < nr_buckets; i++) {
		struct io_hash_bucket *hb = &table->hbs[i];

		spin_lock(&hb->lock);
		hlist_for_each_entry_safe(req, tmp, &hb->list, hash_node) {
			if (io_match_task_safe(req, tsk, cancel_all)) {
				hlist_del_init(&req->hash_node);
				io_poll_cancel_req(req);
				found = true;
			}
		}
		spin_unlock(&hb->lock);
	}
	return found;
}

/*
 * Returns true if we found and killed one or more poll requests
 */
__cold bool io_poll_remove_all(struct io_ring_ctx *ctx, struct task_struct *tsk,
			       bool cancel_all)
	__must_hold(&ctx->uring_lock)
{
	bool ret;

	ret = io_poll_remove_all_table(tsk, &ctx->cancel_table, cancel_all);
	ret |= io_poll_remove_all_table(tsk, &ctx->cancel_table_locked, cancel_all);
	return ret;
}

static struct io_kiocb *io_poll_find(struct io_ring_ctx *ctx, bool poll_only,
				     struct io_cancel_data *cd,
				     struct io_hash_table *table,
				     struct io_hash_bucket **out_bucket)
{
	struct io_kiocb *req;
	u32 index = hash_long(cd->data, table->hash_bits);
	struct io_hash_bucket *hb = &table->hbs[index];

	*out_bucket = NULL;

	spin_lock(&hb->lock);
	hlist_for_each_entry(req, &hb->list, hash_node) {
		if (cd->data != req->cqe.user_data)
			continue;
		if (poll_only && req->opcode != IORING_OP_POLL_ADD)
			continue;
		if (cd->flags & IORING_ASYNC_CANCEL_ALL) {
			if (cd->seq == req->work.cancel_seq)
				continue;
			req->work.cancel_seq = cd->seq;
		}
		*out_bucket = hb;
		return req;
	}
	spin_unlock(&hb->lock);
	return NULL;
}

static struct io_kiocb *io_poll_file_find(struct io_ring_ctx *ctx,
					  struct io_cancel_data *cd,
					  struct io_hash_table *table,
					  struct io_hash_bucket **out_bucket)
{
	unsigned nr_buckets = 1U << table->hash_bits;
	struct io_kiocb *req;
	int i;

	*out_bucket = NULL;

	for (i = 0; i < nr_buckets; i++) {
		struct io_hash_bucket *hb = &table->hbs[i];

		spin_lock(&hb->lock);
		hlist_for_each_entry(req, &hb->list, hash_node) {
			if (!(cd->flags & IORING_ASYNC_CANCEL_ANY) &&
			    req->file != cd->file)
				continue;
			if (cd->seq == req->work.cancel_seq)
				continue;
			req->work.cancel_seq = cd->seq;
			*out_bucket = hb;
			return req;
		}
		spin_unlock(&hb->lock);
	}
	return NULL;
}

static int io_poll_disarm(struct io_kiocb *req)
{
	if (!req)
		return -ENOENT;
	if (!io_poll_get_ownership(req))
		return -EALREADY;
	io_poll_remove_entries(req);
	hash_del(&req->hash_node);
	return 0;
}

static int __io_poll_cancel(struct io_ring_ctx *ctx, struct io_cancel_data *cd,
			    struct io_hash_table *table)
{
	struct io_hash_bucket *bucket;
	struct io_kiocb *req;

	if (cd->flags & (IORING_ASYNC_CANCEL_FD|IORING_ASYNC_CANCEL_ANY))
		req = io_poll_file_find(ctx, cd, table, &bucket);
	else
		req = io_poll_find(ctx, false, cd, table, &bucket);

	if (req)
		io_poll_cancel_req(req);
	if (bucket)
		spin_unlock(&bucket->lock);
	return req ? 0 : -ENOENT;
}

int io_poll_cancel(struct io_ring_ctx *ctx, struct io_cancel_data *cd,
		   unsigned issue_flags)
{
	int ret;

	ret = __io_poll_cancel(ctx, cd, &ctx->cancel_table);
	if (ret != -ENOENT)
		return ret;

	io_ring_submit_lock(ctx, issue_flags);
	ret = __io_poll_cancel(ctx, cd, &ctx->cancel_table_locked);
	io_ring_submit_unlock(ctx, issue_flags);
	return ret;
}

static __poll_t io_poll_parse_events(const struct io_uring_sqe *sqe,
				     unsigned int flags)
{
	u32 events;

	events = READ_ONCE(sqe->poll32_events);
#ifdef __BIG_ENDIAN
	events = swahw32(events);
#endif
	if (!(flags & IORING_POLL_ADD_MULTI))
		events |= EPOLLONESHOT;
	if (!(flags & IORING_POLL_ADD_LEVEL))
		events |= EPOLLET;
	return demangle_poll(events) |
		(events & (EPOLLEXCLUSIVE|EPOLLONESHOT|EPOLLET));
}

int io_poll_remove_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_poll_update *upd = io_kiocb_to_cmd(req, struct io_poll_update);
	u32 flags;

	if (sqe->buf_index || sqe->splice_fd_in)
		return -EINVAL;
	flags = READ_ONCE(sqe->len);
	if (flags & ~(IORING_POLL_UPDATE_EVENTS | IORING_POLL_UPDATE_USER_DATA |
		      IORING_POLL_ADD_MULTI))
		return -EINVAL;
	/* meaningless without update */
	if (flags == IORING_POLL_ADD_MULTI)
		return -EINVAL;

	upd->old_user_data = READ_ONCE(sqe->addr);
	upd->update_events = flags & IORING_POLL_UPDATE_EVENTS;
	upd->update_user_data = flags & IORING_POLL_UPDATE_USER_DATA;

	upd->new_user_data = READ_ONCE(sqe->off);
	if (!upd->update_user_data && upd->new_user_data)
		return -EINVAL;
	if (upd->update_events)
		upd->events = io_poll_parse_events(sqe, flags);
	else if (sqe->poll32_events)
		return -EINVAL;

	return 0;
}

int io_poll_add_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_poll *poll = io_kiocb_to_cmd(req, struct io_poll);
	u32 flags;

	if (sqe->buf_index || sqe->off || sqe->addr)
		return -EINVAL;
	flags = READ_ONCE(sqe->len);
	if (flags & ~IORING_POLL_ADD_MULTI)
		return -EINVAL;
	if ((flags & IORING_POLL_ADD_MULTI) && (req->flags & REQ_F_CQE_SKIP))
		return -EINVAL;

	poll->events = io_poll_parse_events(sqe, flags);
	return 0;
}

int io_poll_add(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_poll *poll = io_kiocb_to_cmd(req, struct io_poll);
	struct io_poll_table ipt;
	int ret;

	ipt.pt._qproc = io_poll_queue_proc;

	/*
	 * If sqpoll or single issuer, there is no contention for ->uring_lock
	 * and we'll end up holding it in tw handlers anyway.
	 */
	if (req->ctx->flags & (IORING_SETUP_SQPOLL|IORING_SETUP_SINGLE_ISSUER))
		req->flags |= REQ_F_HASH_LOCKED;

	ret = __io_arm_poll_handler(req, poll, &ipt, poll->events, issue_flags);
	if (ret > 0) {
		io_req_set_res(req, ipt.result_mask, 0);
		return IOU_OK;
	}
	return ret ?: IOU_ISSUE_SKIP_COMPLETE;
}

int io_poll_remove(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_poll_update *poll_update = io_kiocb_to_cmd(req, struct io_poll_update);
	struct io_cancel_data cd = { .data = poll_update->old_user_data, };
	struct io_ring_ctx *ctx = req->ctx;
	struct io_hash_bucket *bucket;
	struct io_kiocb *preq;
	int ret2, ret = 0;
	bool locked = true;

	io_ring_submit_lock(ctx, issue_flags);
	preq = io_poll_find(ctx, true, &cd, &ctx->cancel_table, &bucket);
	ret2 = io_poll_disarm(preq);
	if (bucket)
		spin_unlock(&bucket->lock);
	if (!ret2)
		goto found;
	if (ret2 != -ENOENT) {
		ret = ret2;
		goto out;
	}

	preq = io_poll_find(ctx, true, &cd, &ctx->cancel_table_locked, &bucket);
	ret2 = io_poll_disarm(preq);
	if (bucket)
		spin_unlock(&bucket->lock);
	if (ret2) {
		ret = ret2;
		goto out;
	}

found:
	if (WARN_ON_ONCE(preq->opcode != IORING_OP_POLL_ADD)) {
		ret = -EFAULT;
		goto out;
	}

	if (poll_update->update_events || poll_update->update_user_data) {
		/* only mask one event flags, keep behavior flags */
		if (poll_update->update_events) {
			struct io_poll *poll = io_kiocb_to_cmd(preq, struct io_poll);

			poll->events &= ~0xffff;
			poll->events |= poll_update->events & 0xffff;
			poll->events |= IO_POLL_UNMASK;
		}
		if (poll_update->update_user_data)
			preq->cqe.user_data = poll_update->new_user_data;

		ret2 = io_poll_add(preq, issue_flags & ~IO_URING_F_UNLOCKED);
		/* successfully updated, don't complete poll request */
		if (!ret2 || ret2 == -EIOCBQUEUED)
			goto out;
	}

	req_set_fail(preq);
	io_req_set_res(preq, -ECANCELED, 0);
	io_req_task_complete(preq, &locked);
out:
	io_ring_submit_unlock(ctx, issue_flags);
	if (ret < 0) {
		req_set_fail(req);
		return ret;
	}
	/* complete update request, we're done with it */
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

void io_apoll_cache_free(struct io_cache_entry *entry)
{
	kfree(container_of(entry, struct async_poll, cache));
}
