/*
 * (C) 2001 Clemson University and The University of Chicago
 * (C) 2011 Omnibond Systems
 *
 * Changes by Acxiom Corporation to implement generic service_operation()
 * function, Copyright Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

/*
 *  In-kernel waitqueue operations.
 */

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"

static int wait_for_matching_downcall(struct orangefs_kernel_op_s *, long, bool);
static void orangefs_clean_up_interrupted_operation(struct orangefs_kernel_op_s *);

/*
 * What we do in this function is to walk the list of operations that are
 * present in the request queue and mark them as purged.
 * NOTE: This is called from the device close after client-core has
 * guaranteed that no new operations could appear on the list since the
 * client-core is anyway going to exit.
 */
void purge_waiting_ops(void)
{
	struct orangefs_kernel_op_s *op;

	spin_lock(&orangefs_request_list_lock);
	list_for_each_entry(op, &orangefs_request_list, list) {
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "pvfs2-client-core: purging op tag %llu %s\n",
			     llu(op->tag),
			     get_opname_string(op));
		set_op_state_purged(op);
	}
	spin_unlock(&orangefs_request_list_lock);
}

/*
 * submits a ORANGEFS operation and waits for it to complete
 *
 * Note op->downcall.status will contain the status of the operation (in
 * errno format), whether provided by pvfs2-client or a result of failure to
 * service the operation.  If the caller wishes to distinguish, then
 * op->state can be checked to see if it was serviced or not.
 *
 * Returns contents of op->downcall.status for convenience
 */
int service_operation(struct orangefs_kernel_op_s *op,
		      const char *op_name,
		      int flags)
{
	long timeout = MAX_SCHEDULE_TIMEOUT;
	/* flags to modify behavior */
	int ret = 0;

	DEFINE_WAIT(wait_entry);

	op->upcall.tgid = current->tgid;
	op->upcall.pid = current->pid;

retry_servicing:
	op->downcall.status = 0;
	gossip_debug(GOSSIP_WAIT_DEBUG,
		     "%s: %s op:%p: process:%s: pid:%d:\n",
		     __func__,
		     op_name,
		     op,
		     current->comm,
		     current->pid);

	if (!(flags & ORANGEFS_OP_NO_SEMAPHORE)) {
		if (flags & ORANGEFS_OP_INTERRUPTIBLE)
			ret = mutex_lock_interruptible(&request_mutex);
		else
			ret = mutex_lock_killable(&request_mutex);
		/*
		 * check to see if we were interrupted while waiting for
		 * semaphore
		 */
		if (ret < 0) {
			op->downcall.status = ret;
			gossip_debug(GOSSIP_WAIT_DEBUG,
				     "orangefs: service_operation interrupted.\n");
			return ret;
		}
	}

	/* queue up the operation */
	spin_lock(&orangefs_request_list_lock);
	spin_lock(&op->lock);
	set_op_state_waiting(op);
	if (flags & ORANGEFS_OP_PRIORITY)
		list_add(&op->list, &orangefs_request_list);
	else
		list_add_tail(&op->list, &orangefs_request_list);
	spin_unlock(&op->lock);
	wake_up_interruptible(&orangefs_request_list_waitq);
	if (!__is_daemon_in_service()) {
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "%s:client core is NOT in service.\n",
			     __func__);
		timeout = op_timeout_secs * HZ;
	}
	spin_unlock(&orangefs_request_list_lock);

	if (!(flags & ORANGEFS_OP_NO_SEMAPHORE))
		mutex_unlock(&request_mutex);

	/*
	 * If we are asked to service an asynchronous operation from
	 * VFS perspective, we are done.
	 */
	if (flags & ORANGEFS_OP_ASYNC)
		return 0;

	ret = wait_for_matching_downcall(op, timeout,
					 flags & ORANGEFS_OP_INTERRUPTIBLE);

	gossip_debug(GOSSIP_WAIT_DEBUG,
		     "%s: wait_for_matching_downcall returned %d for %p\n",
		     __func__,
		     ret,
		     op);

	if (!ret) {
		spin_unlock(&op->lock);
		/* got matching downcall; make sure status is in errno format */
		op->downcall.status =
		    orangefs_normalize_to_errno(op->downcall.status);
		ret = op->downcall.status;
		goto out;
	}

	/* failed to get matching downcall */
	if (ret == -ETIMEDOUT) {
		gossip_err("orangefs: %s -- wait timed out; aborting attempt.\n",
			   op_name);
	}
	orangefs_clean_up_interrupted_operation(op);
	op->downcall.status = ret;
	/* retry if operation has not been serviced and if requested */
	if (ret == -EAGAIN) {
		op->attempts++;
		timeout = op_timeout_secs * HZ;
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "orangefs: tag %llu (%s)"
			     " -- operation to be retried (%d attempt)\n",
			     llu(op->tag),
			     op_name,
			     op->attempts);

		if (!op->uses_shared_memory)
			/*
			 * this operation doesn't use the shared memory
			 * system
			 */
			goto retry_servicing;
	}

out:
	gossip_debug(GOSSIP_WAIT_DEBUG,
		     "orangefs: service_operation %s returning: %d for %p.\n",
		     op_name,
		     ret,
		     op);
	return ret;
}

bool orangefs_cancel_op_in_progress(struct orangefs_kernel_op_s *op)
{
	u64 tag = op->tag;
	if (!op_state_in_progress(op))
		return false;

	op->slot_to_free = op->upcall.req.io.buf_index;
	memset(&op->upcall, 0, sizeof(op->upcall));
	memset(&op->downcall, 0, sizeof(op->downcall));
	op->upcall.type = ORANGEFS_VFS_OP_CANCEL;
	op->upcall.req.cancel.op_tag = tag;
	op->downcall.type = ORANGEFS_VFS_OP_INVALID;
	op->downcall.status = -1;
	orangefs_new_tag(op);

	spin_lock(&orangefs_request_list_lock);
	/* orangefs_request_list_lock is enough of a barrier here */
	if (!__is_daemon_in_service()) {
		spin_unlock(&orangefs_request_list_lock);
		return false;
	}
	spin_lock(&op->lock);
	set_op_state_waiting(op);
	list_add(&op->list, &orangefs_request_list);
	spin_unlock(&op->lock);
	spin_unlock(&orangefs_request_list_lock);

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "Attempting ORANGEFS operation cancellation of tag %llu\n",
		     llu(tag));
	return true;
}

static void orangefs_clean_up_interrupted_operation(struct orangefs_kernel_op_s *op)
{
	/*
	 * handle interrupted cases depending on what state we were in when
	 * the interruption is detected.  there is a coarse grained lock
	 * across the operation.
	 *
	 * Called with op->lock held.
	 */
	op->op_state |= OP_VFS_STATE_GIVEN_UP;

	if (op_state_waiting(op)) {
		/*
		 * upcall hasn't been read; remove op from upcall request
		 * list.
		 */
		spin_unlock(&op->lock);
		spin_lock(&orangefs_request_list_lock);
		list_del(&op->list);
		spin_unlock(&orangefs_request_list_lock);
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "Interrupted: Removed op %p from request_list\n",
			     op);
	} else if (op_state_in_progress(op)) {
		/* op must be removed from the in progress htable */
		spin_unlock(&op->lock);
		spin_lock(&htable_ops_in_progress_lock);
		list_del(&op->list);
		spin_unlock(&htable_ops_in_progress_lock);
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "Interrupted: Removed op %p"
			     " from htable_ops_in_progress\n",
			     op);
	} else if (!op_state_serviced(op)) {
		spin_unlock(&op->lock);
		gossip_err("interrupted operation is in a weird state 0x%x\n",
			   op->op_state);
	} else {
		/*
		 * It is not intended for execution to flow here,
		 * but having this unlock here makes sparse happy.
		 */
		gossip_err("%s: can't get here.\n", __func__);
		spin_unlock(&op->lock);
	}
	reinit_completion(&op->waitq);
}

/*
 * sleeps on waitqueue waiting for matching downcall.
 * if client-core finishes servicing, then we are good to go.
 * else if client-core exits, we get woken up here, and retry with a timeout
 *
 * Post when this call returns to the caller, the specified op will no
 * longer be on any list or htable.
 *
 * Returns 0 on success and -errno on failure
 * Errors are:
 * EAGAIN in case we want the caller to requeue and try again..
 * EINTR/EIO/ETIMEDOUT indicating we are done trying to service this
 * operation since client-core seems to be exiting too often
 * or if we were interrupted.
 *
 * Returns with op->lock taken.
 */
static int wait_for_matching_downcall(struct orangefs_kernel_op_s *op,
				      long timeout,
				      bool interruptible)
{
	long n;

	if (interruptible)
		n = wait_for_completion_interruptible_timeout(&op->waitq, timeout);
	else
		n = wait_for_completion_killable_timeout(&op->waitq, timeout);

	spin_lock(&op->lock);

	if (op_state_serviced(op))
		return 0;

	if (unlikely(n < 0)) {
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "*** %s:"
			     " operation interrupted by a signal (tag "
			     "%llu, op %p)\n",
			     __func__,
			     llu(op->tag),
			     op);
		return -EINTR;
	}
	if (op_state_purged(op)) {
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "*** %s:"
			     " operation purged (tag "
			     "%llu, %p, att %d)\n",
			     __func__,
			     llu(op->tag),
			     op,
			     op->attempts);
		return (op->attempts < ORANGEFS_PURGE_RETRY_COUNT) ?
			 -EAGAIN :
			 -EIO;
	}
	/* must have timed out, then... */
	gossip_debug(GOSSIP_WAIT_DEBUG,
		     "*** %s:"
		     " operation timed out (tag"
		     " %llu, %p, att %d)\n",
		     __func__,
		     llu(op->tag),
		     op,
		     op->attempts);
	return -ETIMEDOUT;
}
