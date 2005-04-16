/*
 * linux/fs/lockd/svclock.c
 *
 * Handling of server-side locks, mostly of the blocked variety.
 * This is the ugliest part of lockd because we tread on very thin ice.
 * GRANT and CANCEL calls may get stuck, meet in mid-flight, etc.
 * IMNSHO introducing the grant callback into the NLM protocol was one
 * of the worst ideas Sun ever had. Except maybe for the idea of doing
 * NFS file locking at all.
 *
 * I'm trying hard to avoid race conditions by protecting most accesses
 * to a file's list of blocked locks through a semaphore. The global
 * list of blocked locks is not protected in this fashion however.
 * Therefore, some functions (such as the RPC callback for the async grant
 * call) move blocked locks towards the head of the list *while some other
 * process might be traversing it*. This should not be a problem in
 * practice, because this will only cause functions traversing the list
 * to visit some blocks twice.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/lockd/nlm.h>
#include <linux/lockd/lockd.h>

#define NLMDBG_FACILITY		NLMDBG_SVCLOCK

#ifdef CONFIG_LOCKD_V4
#define nlm_deadlock	nlm4_deadlock
#else
#define nlm_deadlock	nlm_lck_denied
#endif

static void	nlmsvc_insert_block(struct nlm_block *block, unsigned long);
static int	nlmsvc_remove_block(struct nlm_block *block);
static void	nlmsvc_grant_callback(struct rpc_task *task);

/*
 * The list of blocked locks to retry
 */
static struct nlm_block *	nlm_blocked;

/*
 * Insert a blocked lock into the global list
 */
static void
nlmsvc_insert_block(struct nlm_block *block, unsigned long when)
{
	struct nlm_block **bp, *b;

	dprintk("lockd: nlmsvc_insert_block(%p, %ld)\n", block, when);
	if (block->b_queued)
		nlmsvc_remove_block(block);
	bp = &nlm_blocked;
	if (when != NLM_NEVER) {
		if ((when += jiffies) == NLM_NEVER)
			when ++;
		while ((b = *bp) && time_before_eq(b->b_when,when) && b->b_when != NLM_NEVER)
			bp = &b->b_next;
	} else
		while ((b = *bp) != 0)
			bp = &b->b_next;

	block->b_queued = 1;
	block->b_when = when;
	block->b_next = b;
	*bp = block;
}

/*
 * Remove a block from the global list
 */
static int
nlmsvc_remove_block(struct nlm_block *block)
{
	struct nlm_block **bp, *b;

	if (!block->b_queued)
		return 1;
	for (bp = &nlm_blocked; (b = *bp) != 0; bp = &b->b_next) {
		if (b == block) {
			*bp = block->b_next;
			block->b_queued = 0;
			return 1;
		}
	}

	return 0;
}

/*
 * Find a block for a given lock and optionally remove it from
 * the list.
 */
static struct nlm_block *
nlmsvc_lookup_block(struct nlm_file *file, struct nlm_lock *lock, int remove)
{
	struct nlm_block	**head, *block;
	struct file_lock	*fl;

	dprintk("lockd: nlmsvc_lookup_block f=%p pd=%d %Ld-%Ld ty=%d\n",
				file, lock->fl.fl_pid,
				(long long)lock->fl.fl_start,
				(long long)lock->fl.fl_end, lock->fl.fl_type);
	for (head = &nlm_blocked; (block = *head) != 0; head = &block->b_next) {
		fl = &block->b_call.a_args.lock.fl;
		dprintk("lockd: check f=%p pd=%d %Ld-%Ld ty=%d cookie=%s\n",
				block->b_file, fl->fl_pid,
				(long long)fl->fl_start,
				(long long)fl->fl_end, fl->fl_type,
				nlmdbg_cookie2a(&block->b_call.a_args.cookie));
		if (block->b_file == file && nlm_compare_locks(fl, &lock->fl)) {
			if (remove) {
				*head = block->b_next;
				block->b_queued = 0;
			}
			return block;
		}
	}

	return NULL;
}

static inline int nlm_cookie_match(struct nlm_cookie *a, struct nlm_cookie *b)
{
	if(a->len != b->len)
		return 0;
	if(memcmp(a->data,b->data,a->len))
		return 0;
	return 1;
}

/*
 * Find a block with a given NLM cookie.
 */
static inline struct nlm_block *
nlmsvc_find_block(struct nlm_cookie *cookie,  struct sockaddr_in *sin)
{
	struct nlm_block *block;

	for (block = nlm_blocked; block; block = block->b_next) {
		dprintk("cookie: head of blocked queue %p, block %p\n", 
			nlm_blocked, block);
		if (nlm_cookie_match(&block->b_call.a_args.cookie,cookie)
				&& nlm_cmp_addr(sin, &block->b_host->h_addr))
			break;
	}

	return block;
}

/*
 * Create a block and initialize it.
 *
 * Note: we explicitly set the cookie of the grant reply to that of
 * the blocked lock request. The spec explicitly mentions that the client
 * should _not_ rely on the callback containing the same cookie as the
 * request, but (as I found out later) that's because some implementations
 * do just this. Never mind the standards comittees, they support our
 * logging industries.
 */
static inline struct nlm_block *
nlmsvc_create_block(struct svc_rqst *rqstp, struct nlm_file *file,
				struct nlm_lock *lock, struct nlm_cookie *cookie)
{
	struct nlm_block	*block;
	struct nlm_host		*host;
	struct nlm_rqst		*call;

	/* Create host handle for callback */
	host = nlmclnt_lookup_host(&rqstp->rq_addr,
				rqstp->rq_prot, rqstp->rq_vers);
	if (host == NULL)
		return NULL;

	/* Allocate memory for block, and initialize arguments */
	if (!(block = (struct nlm_block *) kmalloc(sizeof(*block), GFP_KERNEL)))
		goto failed;
	memset(block, 0, sizeof(*block));
	locks_init_lock(&block->b_call.a_args.lock.fl);
	locks_init_lock(&block->b_call.a_res.lock.fl);

	if (!nlmclnt_setgrantargs(&block->b_call, lock))
		goto failed_free;

	/* Set notifier function for VFS, and init args */
	block->b_call.a_args.lock.fl.fl_lmops = &nlmsvc_lock_operations;
	block->b_call.a_args.cookie = *cookie;	/* see above */

	dprintk("lockd: created block %p...\n", block);

	/* Create and initialize the block */
	block->b_daemon = rqstp->rq_server;
	block->b_host   = host;
	block->b_file   = file;

	/* Add to file's list of blocks */
	block->b_fnext  = file->f_blocks;
	file->f_blocks  = block;

	/* Set up RPC arguments for callback */
	call = &block->b_call;
	call->a_host    = host;
	call->a_flags   = RPC_TASK_ASYNC;

	return block;

failed_free:
	kfree(block);
failed:
	nlm_release_host(host);
	return NULL;
}

/*
 * Delete a block. If the lock was cancelled or the grant callback
 * failed, unlock is set to 1.
 * It is the caller's responsibility to check whether the file
 * can be closed hereafter.
 */
static void
nlmsvc_delete_block(struct nlm_block *block, int unlock)
{
	struct file_lock	*fl = &block->b_call.a_args.lock.fl;
	struct nlm_file		*file = block->b_file;
	struct nlm_block	**bp;

	dprintk("lockd: deleting block %p...\n", block);

	/* Remove block from list */
	nlmsvc_remove_block(block);
	if (fl->fl_next)
		posix_unblock_lock(file->f_file, fl);
	if (unlock) {
		fl->fl_type = F_UNLCK;
		posix_lock_file(file->f_file, fl);
		block->b_granted = 0;
	}

	/* If the block is in the middle of a GRANT callback,
	 * don't kill it yet. */
	if (block->b_incall) {
		nlmsvc_insert_block(block, NLM_NEVER);
		block->b_done = 1;
		return;
	}

	/* Remove block from file's list of blocks */
	for (bp = &file->f_blocks; *bp; bp = &(*bp)->b_fnext) {
		if (*bp == block) {
			*bp = block->b_fnext;
			break;
		}
	}

	if (block->b_host)
		nlm_release_host(block->b_host);
	nlmclnt_freegrantargs(&block->b_call);
	kfree(block);
}

/*
 * Loop over all blocks and perform the action specified.
 * (NLM_ACT_CHECK handled by nlmsvc_inspect_file).
 */
int
nlmsvc_traverse_blocks(struct nlm_host *host, struct nlm_file *file, int action)
{
	struct nlm_block	*block, *next;

	down(&file->f_sema);
	for (block = file->f_blocks; block; block = next) {
		next = block->b_fnext;
		if (action == NLM_ACT_MARK)
			block->b_host->h_inuse = 1;
		else if (action == NLM_ACT_UNLOCK) {
			if (host == NULL || host == block->b_host)
				nlmsvc_delete_block(block, 1);
		}
	}
	up(&file->f_sema);
	return 0;
}

/*
 * Attempt to establish a lock, and if it can't be granted, block it
 * if required.
 */
u32
nlmsvc_lock(struct svc_rqst *rqstp, struct nlm_file *file,
			struct nlm_lock *lock, int wait, struct nlm_cookie *cookie)
{
	struct file_lock	*conflock;
	struct nlm_block	*block;
	int			error;

	dprintk("lockd: nlmsvc_lock(%s/%ld, ty=%d, pi=%d, %Ld-%Ld, bl=%d)\n",
				file->f_file->f_dentry->d_inode->i_sb->s_id,
				file->f_file->f_dentry->d_inode->i_ino,
				lock->fl.fl_type, lock->fl.fl_pid,
				(long long)lock->fl.fl_start,
				(long long)lock->fl.fl_end,
				wait);


	/* Get existing block (in case client is busy-waiting) */
	block = nlmsvc_lookup_block(file, lock, 0);

	lock->fl.fl_flags |= FL_LOCKD;

again:
	/* Lock file against concurrent access */
	down(&file->f_sema);

	if (!(conflock = posix_test_lock(file->f_file, &lock->fl))) {
		error = posix_lock_file(file->f_file, &lock->fl);

		if (block)
			nlmsvc_delete_block(block, 0);
		up(&file->f_sema);

		dprintk("lockd: posix_lock_file returned %d\n", -error);
		switch(-error) {
		case 0:
			return nlm_granted;
		case EDEADLK:
			return nlm_deadlock;
		case EAGAIN:
			return nlm_lck_denied;
		default:			/* includes ENOLCK */
			return nlm_lck_denied_nolocks;
		}
	}

	if (!wait) {
		up(&file->f_sema);
		return nlm_lck_denied;
	}

	if (posix_locks_deadlock(&lock->fl, conflock)) {
		up(&file->f_sema);
		return nlm_deadlock;
	}

	/* If we don't have a block, create and initialize it. Then
	 * retry because we may have slept in kmalloc. */
	/* We have to release f_sema as nlmsvc_create_block may try to
	 * to claim it while doing host garbage collection */
	if (block == NULL) {
		up(&file->f_sema);
		dprintk("lockd: blocking on this lock (allocating).\n");
		if (!(block = nlmsvc_create_block(rqstp, file, lock, cookie)))
			return nlm_lck_denied_nolocks;
		goto again;
	}

	/* Append to list of blocked */
	nlmsvc_insert_block(block, NLM_NEVER);

	if (list_empty(&block->b_call.a_args.lock.fl.fl_block)) {
		/* Now add block to block list of the conflicting lock
		   if we haven't done so. */
		dprintk("lockd: blocking on this lock.\n");
		posix_block_lock(conflock, &block->b_call.a_args.lock.fl);
	}

	up(&file->f_sema);
	return nlm_lck_blocked;
}

/*
 * Test for presence of a conflicting lock.
 */
u32
nlmsvc_testlock(struct nlm_file *file, struct nlm_lock *lock,
				       struct nlm_lock *conflock)
{
	struct file_lock	*fl;

	dprintk("lockd: nlmsvc_testlock(%s/%ld, ty=%d, %Ld-%Ld)\n",
				file->f_file->f_dentry->d_inode->i_sb->s_id,
				file->f_file->f_dentry->d_inode->i_ino,
				lock->fl.fl_type,
				(long long)lock->fl.fl_start,
				(long long)lock->fl.fl_end);

	if ((fl = posix_test_lock(file->f_file, &lock->fl)) != NULL) {
		dprintk("lockd: conflicting lock(ty=%d, %Ld-%Ld)\n",
				fl->fl_type, (long long)fl->fl_start,
				(long long)fl->fl_end);
		conflock->caller = "somehost";	/* FIXME */
		conflock->oh.len = 0;		/* don't return OH info */
		conflock->fl = *fl;
		return nlm_lck_denied;
	}

	return nlm_granted;
}

/*
 * Remove a lock.
 * This implies a CANCEL call: We send a GRANT_MSG, the client replies
 * with a GRANT_RES call which gets lost, and calls UNLOCK immediately
 * afterwards. In this case the block will still be there, and hence
 * must be removed.
 */
u32
nlmsvc_unlock(struct nlm_file *file, struct nlm_lock *lock)
{
	int	error;

	dprintk("lockd: nlmsvc_unlock(%s/%ld, pi=%d, %Ld-%Ld)\n",
				file->f_file->f_dentry->d_inode->i_sb->s_id,
				file->f_file->f_dentry->d_inode->i_ino,
				lock->fl.fl_pid,
				(long long)lock->fl.fl_start,
				(long long)lock->fl.fl_end);

	/* First, cancel any lock that might be there */
	nlmsvc_cancel_blocked(file, lock);

	lock->fl.fl_type = F_UNLCK;
	error = posix_lock_file(file->f_file, &lock->fl);

	return (error < 0)? nlm_lck_denied_nolocks : nlm_granted;
}

/*
 * Cancel a previously blocked request.
 *
 * A cancel request always overrides any grant that may currently
 * be in progress.
 * The calling procedure must check whether the file can be closed.
 */
u32
nlmsvc_cancel_blocked(struct nlm_file *file, struct nlm_lock *lock)
{
	struct nlm_block	*block;

	dprintk("lockd: nlmsvc_cancel(%s/%ld, pi=%d, %Ld-%Ld)\n",
				file->f_file->f_dentry->d_inode->i_sb->s_id,
				file->f_file->f_dentry->d_inode->i_ino,
				lock->fl.fl_pid,
				(long long)lock->fl.fl_start,
				(long long)lock->fl.fl_end);

	down(&file->f_sema);
	if ((block = nlmsvc_lookup_block(file, lock, 1)) != NULL)
		nlmsvc_delete_block(block, 1);
	up(&file->f_sema);
	return nlm_granted;
}

/*
 * Unblock a blocked lock request. This is a callback invoked from the
 * VFS layer when a lock on which we blocked is removed.
 *
 * This function doesn't grant the blocked lock instantly, but rather moves
 * the block to the head of nlm_blocked where it can be picked up by lockd.
 */
static void
nlmsvc_notify_blocked(struct file_lock *fl)
{
	struct nlm_block	**bp, *block;

	dprintk("lockd: VFS unblock notification for block %p\n", fl);
	for (bp = &nlm_blocked; (block = *bp) != 0; bp = &block->b_next) {
		if (nlm_compare_locks(&block->b_call.a_args.lock.fl, fl)) {
			nlmsvc_insert_block(block, 0);
			svc_wake_up(block->b_daemon);
			return;
		}
	}

	printk(KERN_WARNING "lockd: notification for unknown block!\n");
}

static int nlmsvc_same_owner(struct file_lock *fl1, struct file_lock *fl2)
{
	return fl1->fl_owner == fl2->fl_owner && fl1->fl_pid == fl2->fl_pid;
}

struct lock_manager_operations nlmsvc_lock_operations = {
	.fl_compare_owner = nlmsvc_same_owner,
	.fl_notify = nlmsvc_notify_blocked,
};

/*
 * Try to claim a lock that was previously blocked.
 *
 * Note that we use both the RPC_GRANTED_MSG call _and_ an async
 * RPC thread when notifying the client. This seems like overkill...
 * Here's why:
 *  -	we don't want to use a synchronous RPC thread, otherwise
 *	we might find ourselves hanging on a dead portmapper.
 *  -	Some lockd implementations (e.g. HP) don't react to
 *	RPC_GRANTED calls; they seem to insist on RPC_GRANTED_MSG calls.
 */
static void
nlmsvc_grant_blocked(struct nlm_block *block)
{
	struct nlm_file		*file = block->b_file;
	struct nlm_lock		*lock = &block->b_call.a_args.lock;
	struct file_lock	*conflock;
	int			error;

	dprintk("lockd: grant blocked lock %p\n", block);

	/* First thing is lock the file */
	down(&file->f_sema);

	/* Unlink block request from list */
	nlmsvc_remove_block(block);

	/* If b_granted is true this means we've been here before.
	 * Just retry the grant callback, possibly refreshing the RPC
	 * binding */
	if (block->b_granted) {
		nlm_rebind_host(block->b_host);
		goto callback;
	}

	/* Try the lock operation again */
	if ((conflock = posix_test_lock(file->f_file, &lock->fl)) != NULL) {
		/* Bummer, we blocked again */
		dprintk("lockd: lock still blocked\n");
		nlmsvc_insert_block(block, NLM_NEVER);
		posix_block_lock(conflock, &lock->fl);
		up(&file->f_sema);
		return;
	}

	/* Alright, no conflicting lock. Now lock it for real. If the
	 * following yields an error, this is most probably due to low
	 * memory. Retry the lock in a few seconds.
	 */
	if ((error = posix_lock_file(file->f_file, &lock->fl)) < 0) {
		printk(KERN_WARNING "lockd: unexpected error %d in %s!\n",
				-error, __FUNCTION__);
		nlmsvc_insert_block(block, 10 * HZ);
		up(&file->f_sema);
		return;
	}

callback:
	/* Lock was granted by VFS. */
	dprintk("lockd: GRANTing blocked lock.\n");
	block->b_granted = 1;
	block->b_incall  = 1;

	/* Schedule next grant callback in 30 seconds */
	nlmsvc_insert_block(block, 30 * HZ);

	/* Call the client */
	nlm_get_host(block->b_call.a_host);
	if (nlmsvc_async_call(&block->b_call, NLMPROC_GRANTED_MSG,
						nlmsvc_grant_callback) < 0)
		nlm_release_host(block->b_call.a_host);
	up(&file->f_sema);
}

/*
 * This is the callback from the RPC layer when the NLM_GRANTED_MSG
 * RPC call has succeeded or timed out.
 * Like all RPC callbacks, it is invoked by the rpciod process, so it
 * better not sleep. Therefore, we put the blocked lock on the nlm_blocked
 * chain once more in order to have it removed by lockd itself (which can
 * then sleep on the file semaphore without disrupting e.g. the nfs client).
 */
static void
nlmsvc_grant_callback(struct rpc_task *task)
{
	struct nlm_rqst		*call = (struct nlm_rqst *) task->tk_calldata;
	struct nlm_block	*block;
	unsigned long		timeout;
	struct sockaddr_in	*peer_addr = RPC_PEERADDR(task->tk_client);

	dprintk("lockd: GRANT_MSG RPC callback\n");
	dprintk("callback: looking for cookie %s, host (%u.%u.%u.%u)\n",
		nlmdbg_cookie2a(&call->a_args.cookie),
		NIPQUAD(peer_addr->sin_addr.s_addr));
	if (!(block = nlmsvc_find_block(&call->a_args.cookie, peer_addr))) {
		dprintk("lockd: no block for cookie %s, host (%u.%u.%u.%u)\n",
			nlmdbg_cookie2a(&call->a_args.cookie),
			NIPQUAD(peer_addr->sin_addr.s_addr));
		return;
	}

	/* Technically, we should down the file semaphore here. Since we
	 * move the block towards the head of the queue only, no harm
	 * can be done, though. */
	if (task->tk_status < 0) {
		/* RPC error: Re-insert for retransmission */
		timeout = 10 * HZ;
	} else if (block->b_done) {
		/* Block already removed, kill it for real */
		timeout = 0;
	} else {
		/* Call was successful, now wait for client callback */
		timeout = 60 * HZ;
	}
	nlmsvc_insert_block(block, timeout);
	svc_wake_up(block->b_daemon);
	block->b_incall = 0;

	nlm_release_host(call->a_host);
}

/*
 * We received a GRANT_RES callback. Try to find the corresponding
 * block.
 */
void
nlmsvc_grant_reply(struct svc_rqst *rqstp, struct nlm_cookie *cookie, u32 status)
{
	struct nlm_block	*block;
	struct nlm_file		*file;

	dprintk("grant_reply: looking for cookie %x, host (%08x), s=%d \n", 
		*(unsigned int *)(cookie->data), 
		ntohl(rqstp->rq_addr.sin_addr.s_addr), status);
	if (!(block = nlmsvc_find_block(cookie, &rqstp->rq_addr)))
		return;
	file = block->b_file;

	file->f_count++;
	down(&file->f_sema);
	if ((block = nlmsvc_find_block(cookie,&rqstp->rq_addr)) != NULL) {
		if (status == NLM_LCK_DENIED_GRACE_PERIOD) {
			/* Try again in a couple of seconds */
			nlmsvc_insert_block(block, 10 * HZ);
			block = NULL;
		} else {
			/* Lock is now held by client, or has been rejected.
			 * In both cases, the block should be removed. */
			up(&file->f_sema);
			if (status == NLM_LCK_GRANTED)
				nlmsvc_delete_block(block, 0);
			else
				nlmsvc_delete_block(block, 1);
		}
	}
	if (!block)
		up(&file->f_sema);
	nlm_release_file(file);
}

/*
 * Retry all blocked locks that have been notified. This is where lockd
 * picks up locks that can be granted, or grant notifications that must
 * be retransmitted.
 */
unsigned long
nlmsvc_retry_blocked(void)
{
	struct nlm_block	*block;

	dprintk("nlmsvc_retry_blocked(%p, when=%ld)\n",
			nlm_blocked,
			nlm_blocked? nlm_blocked->b_when : 0);
	while ((block = nlm_blocked) != 0) {
		if (block->b_when == NLM_NEVER)
			break;
	        if (time_after(block->b_when,jiffies))
			break;
		dprintk("nlmsvc_retry_blocked(%p, when=%ld, done=%d)\n",
			block, block->b_when, block->b_done);
		if (block->b_done)
			nlmsvc_delete_block(block, 0);
		else
			nlmsvc_grant_blocked(block);
	}

	if ((block = nlm_blocked) && block->b_when != NLM_NEVER)
		return (block->b_when - jiffies);

	return MAX_SCHEDULE_TIMEOUT;
}
