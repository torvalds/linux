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
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

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
		spin_lock(&op->lock);
		set_op_state_purged(op);
		spin_unlock(&op->lock);
		wake_up_interruptible(&op->waitq);
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
	/* flags to modify behavior */
	sigset_t orig_sigset;
	int ret = 0;

	/* irqflags and wait_entry are only used IF the client-core aborts */
	unsigned long irqflags;

	DECLARE_WAITQUEUE(wait_entry, current);

	op->upcall.tgid = current->tgid;
	op->upcall.pid = current->pid;

retry_servicing:
	op->downcall.status = 0;
	gossip_debug(GOSSIP_WAIT_DEBUG,
		     "orangefs: service_operation: %s %p\n",
		     op_name,
		     op);
	gossip_debug(GOSSIP_WAIT_DEBUG,
		     "orangefs: operation posted by process: %s, pid: %i\n",
		     current->comm,
		     current->pid);

	/* mask out signals if this operation is not to be interrupted */
	if (!(flags & ORANGEFS_OP_INTERRUPTIBLE))
		block_signals(&orig_sigset);

	if (!(flags & ORANGEFS_OP_NO_SEMAPHORE)) {
		ret = mutex_lock_interruptible(&request_mutex);
		/*
		 * check to see if we were interrupted while waiting for
		 * semaphore
		 */
		if (ret < 0) {
			if (!(flags & ORANGEFS_OP_INTERRUPTIBLE))
				set_signals(&orig_sigset);
			op->downcall.status = ret;
			gossip_debug(GOSSIP_WAIT_DEBUG,
				     "orangefs: service_operation interrupted.\n");
			return ret;
		}
	}

	gossip_debug(GOSSIP_WAIT_DEBUG,
		     "%s:About to call is_daemon_in_service().\n",
		     __func__);

	if (is_daemon_in_service() < 0) {
		/*
		 * By incrementing the per-operation attempt counter, we
		 * directly go into the timeout logic while waiting for
		 * the matching downcall to be read
		 */
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "%s:client core is NOT in service(%d).\n",
			     __func__,
			     is_daemon_in_service());
		op->attempts++;
	}

	/* queue up the operation */
	if (flags & ORANGEFS_OP_PRIORITY) {
		add_priority_op_to_request_list(op);
	} else {
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "%s:About to call add_op_to_request_list().\n",
			     __func__);
		add_op_to_request_list(op);
	}

	if (!(flags & ORANGEFS_OP_NO_SEMAPHORE))
		mutex_unlock(&request_mutex);

	/*
	 * If we are asked to service an asynchronous operation from
	 * VFS perspective, we are done.
	 */
	if (flags & ORANGEFS_OP_ASYNC)
		return 0;

	if (flags & ORANGEFS_OP_CANCELLATION) {
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "%s:"
			     "About to call wait_for_cancellation_downcall.\n",
			     __func__);
		ret = wait_for_cancellation_downcall(op);
	} else {
		ret = wait_for_matching_downcall(op);
	}

	if (ret < 0) {
		/* failed to get matching downcall */
		if (ret == -ETIMEDOUT) {
			gossip_err("orangefs: %s -- wait timed out; aborting attempt.\n",
				   op_name);
		}
		op->downcall.status = ret;
	} else {
		/* got matching downcall; make sure status is in errno format */
		op->downcall.status =
		    orangefs_normalize_to_errno(op->downcall.status);
		ret = op->downcall.status;
	}

	if (!(flags & ORANGEFS_OP_INTERRUPTIBLE))
		set_signals(&orig_sigset);

	BUG_ON(ret != op->downcall.status);
	/* retry if operation has not been serviced and if requested */
	if (!op_state_serviced(op) && op->downcall.status == -EAGAIN) {
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "orangefs: tag %llu (%s)"
			     " -- operation to be retried (%d attempt)\n",
			     llu(op->tag),
			     op_name,
			     op->attempts + 1);

		if (!op->uses_shared_memory)
			/*
			 * this operation doesn't use the shared memory
			 * system
			 */
			goto retry_servicing;

		/* op uses shared memory */
		if (get_bufmap_init() == 0) {
			/*
			 * This operation uses the shared memory system AND
			 * the system is not yet ready. This situation occurs
			 * when the client-core is restarted AND there were
			 * operations waiting to be processed or were already
			 * in process.
			 */
			gossip_debug(GOSSIP_WAIT_DEBUG,
				     "uses_shared_memory is true.\n");
			gossip_debug(GOSSIP_WAIT_DEBUG,
				     "Client core in-service status(%d).\n",
				     is_daemon_in_service());
			gossip_debug(GOSSIP_WAIT_DEBUG, "bufmap_init:%d.\n",
				     get_bufmap_init());
			gossip_debug(GOSSIP_WAIT_DEBUG,
				     "operation's status is 0x%0x.\n",
				     op->op_state);

			/*
			 * let process sleep for a few seconds so shared
			 * memory system can be initialized.
			 */
			spin_lock_irqsave(&op->lock, irqflags);
			add_wait_queue(&orangefs_bufmap_init_waitq, &wait_entry);
			spin_unlock_irqrestore(&op->lock, irqflags);

			set_current_state(TASK_INTERRUPTIBLE);

			/*
			 * Wait for orangefs_bufmap_initialize() to wake me up
			 * within the allotted time.
			 */
			ret = schedule_timeout(MSECS_TO_JIFFIES
				(1000 * ORANGEFS_BUFMAP_WAIT_TIMEOUT_SECS));

			gossip_debug(GOSSIP_WAIT_DEBUG,
				     "Value returned from schedule_timeout:"
				     "%d.\n",
				     ret);
			gossip_debug(GOSSIP_WAIT_DEBUG,
				     "Is shared memory available? (%d).\n",
				     get_bufmap_init());

			spin_lock_irqsave(&op->lock, irqflags);
			remove_wait_queue(&orangefs_bufmap_init_waitq,
					  &wait_entry);
			spin_unlock_irqrestore(&op->lock, irqflags);

			if (get_bufmap_init() == 0) {
				gossip_err("%s:The shared memory system has not started in %d seconds after the client core restarted.  Aborting user's request(%s).\n",
					   __func__,
					   ORANGEFS_BUFMAP_WAIT_TIMEOUT_SECS,
					   get_opname_string(op));
				return -EIO;
			}

			/*
			 * Return to the calling function and re-populate a
			 * shared memory buffer.
			 */
			return -EAGAIN;
		}
	}

	gossip_debug(GOSSIP_WAIT_DEBUG,
		     "orangefs: service_operation %s returning: %d for %p.\n",
		     op_name,
		     ret,
		     op);
	return ret;
}

void orangefs_clean_up_interrupted_operation(struct orangefs_kernel_op_s *op)
{
	/*
	 * handle interrupted cases depending on what state we were in when
	 * the interruption is detected.  there is a coarse grained lock
	 * across the operation.
	 *
	 * NOTE: be sure not to reverse lock ordering by locking an op lock
	 * while holding the request_list lock.  Here, we first lock the op
	 * and then lock the appropriate list.
	 */
	if (!op) {
		gossip_debug(GOSSIP_WAIT_DEBUG,
			    "%s: op is null, ignoring\n",
			     __func__);
		return;
	}

	/*
	 * one more sanity check, make sure it's in one of the possible states
	 * or don't try to cancel it
	 */
	if (!(op_state_waiting(op) ||
	      op_state_in_progress(op) ||
	      op_state_serviced(op) ||
	      op_state_purged(op))) {
		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "%s: op %p not in a valid state (%0x), "
			     "ignoring\n",
			     __func__,
			     op,
			     op->op_state);
		return;
	}

	spin_lock(&op->lock);

	if (op_state_waiting(op)) {
		/*
		 * upcall hasn't been read; remove op from upcall request
		 * list.
		 */
		spin_unlock(&op->lock);
		remove_op_from_request_list(op);
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
 */
int wait_for_matching_downcall(struct orangefs_kernel_op_s *op)
{
	int ret = -EINVAL;
	DECLARE_WAITQUEUE(wait_entry, current);

	spin_lock(&op->lock);
	add_wait_queue(&op->waitq, &wait_entry);
	spin_unlock(&op->lock);

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock(&op->lock);
		if (op_state_serviced(op)) {
			spin_unlock(&op->lock);
			ret = 0;
			break;
		}
		spin_unlock(&op->lock);

		if (!signal_pending(current)) {
			/*
			 * if this was our first attempt and client-core
			 * has not purged our operation, we are happy to
			 * simply wait
			 */
			spin_lock(&op->lock);
			if (op->attempts == 0 && !op_state_purged(op)) {
				spin_unlock(&op->lock);
				schedule();
			} else {
				spin_unlock(&op->lock);
				/*
				 * subsequent attempts, we retry exactly once
				 * with timeouts
				 */
				if (!schedule_timeout(MSECS_TO_JIFFIES
				      (1000 * op_timeout_secs))) {
					gossip_debug(GOSSIP_WAIT_DEBUG,
						     "*** %s:"
						     " operation timed out (tag"
						     " %llu, %p, att %d)\n",
						     __func__,
						     llu(op->tag),
						     op,
						     op->attempts);
					ret = -ETIMEDOUT;
					orangefs_clean_up_interrupted_operation
					    (op);
					break;
				}
			}
			spin_lock(&op->lock);
			op->attempts++;
			/*
			 * if the operation was purged in the meantime, it
			 * is better to requeue it afresh but ensure that
			 * we have not been purged repeatedly. This could
			 * happen if client-core crashes when an op
			 * is being serviced, so we requeue the op, client
			 * core crashes again so we requeue the op, client
			 * core starts, and so on...
			 */
			if (op_state_purged(op)) {
				ret = (op->attempts < ORANGEFS_PURGE_RETRY_COUNT) ?
					 -EAGAIN :
					 -EIO;
				spin_unlock(&op->lock);
				gossip_debug(GOSSIP_WAIT_DEBUG,
					     "*** %s:"
					     " operation purged (tag "
					     "%llu, %p, att %d)\n",
					     __func__,
					     llu(op->tag),
					     op,
					     op->attempts);
				orangefs_clean_up_interrupted_operation(op);
				break;
			}
			spin_unlock(&op->lock);
			continue;
		}

		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "*** %s:"
			     " operation interrupted by a signal (tag "
			     "%llu, op %p)\n",
			     __func__,
			     llu(op->tag),
			     op);
		orangefs_clean_up_interrupted_operation(op);
		ret = -EINTR;
		break;
	}

	set_current_state(TASK_RUNNING);

	spin_lock(&op->lock);
	remove_wait_queue(&op->waitq, &wait_entry);
	spin_unlock(&op->lock);

	return ret;
}

/*
 * similar to wait_for_matching_downcall(), but used in the special case
 * of I/O cancellations.
 *
 * Note we need a special wait function because if this is called we already
 *      know that a signal is pending in current and need to service the
 *      cancellation upcall anyway.  the only way to exit this is to either
 *      timeout or have the cancellation be serviced properly.
 */
int wait_for_cancellation_downcall(struct orangefs_kernel_op_s *op)
{
	int ret = -EINVAL;
	DECLARE_WAITQUEUE(wait_entry, current);

	spin_lock(&op->lock);
	add_wait_queue(&op->waitq, &wait_entry);
	spin_unlock(&op->lock);

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock(&op->lock);
		if (op_state_serviced(op)) {
			gossip_debug(GOSSIP_WAIT_DEBUG,
				     "%s:op-state is SERVICED.\n",
				     __func__);
			spin_unlock(&op->lock);
			ret = 0;
			break;
		}
		spin_unlock(&op->lock);

		if (signal_pending(current)) {
			gossip_debug(GOSSIP_WAIT_DEBUG,
				     "%s:operation interrupted by a signal (tag"
				     " %llu, op %p)\n",
				     __func__,
				     llu(op->tag),
				     op);
			orangefs_clean_up_interrupted_operation(op);
			ret = -EINTR;
			break;
		}

		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "%s:About to call schedule_timeout.\n",
			     __func__);
		ret =
		    schedule_timeout(MSECS_TO_JIFFIES(1000 * op_timeout_secs));

		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "%s:Value returned from schedule_timeout(%d).\n",
			     __func__,
			     ret);
		if (!ret) {
			gossip_debug(GOSSIP_WAIT_DEBUG,
				     "%s:*** operation timed out: %p\n",
				     __func__,
				     op);
			orangefs_clean_up_interrupted_operation(op);
			ret = -ETIMEDOUT;
			break;
		}

		gossip_debug(GOSSIP_WAIT_DEBUG,
			     "%s:Breaking out of loop, regardless of value returned by schedule_timeout.\n",
			     __func__);
		ret = -ETIMEDOUT;
		break;
	}

	set_current_state(TASK_RUNNING);

	spin_lock(&op->lock);
	remove_wait_queue(&op->waitq, &wait_entry);
	spin_unlock(&op->lock);

	gossip_debug(GOSSIP_WAIT_DEBUG,
		     "%s:returning ret(%d)\n",
		     __func__,
		     ret);

	return ret;
}
