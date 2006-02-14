/*
 * linux/fs/lockd/clntlock.c
 *
 * Lock handling for the client side NLM implementation
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/nfs_fs.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/lockd/lockd.h>
#include <linux/smp_lock.h>

#define NLMDBG_FACILITY		NLMDBG_CLIENT

/*
 * Local function prototypes
 */
static int			reclaimer(void *ptr);

/*
 * The following functions handle blocking and granting from the
 * client perspective.
 */

/*
 * This is the representation of a blocked client lock.
 */
struct nlm_wait {
	struct list_head	b_list;		/* linked list */
	wait_queue_head_t	b_wait;		/* where to wait on */
	struct nlm_host *	b_host;
	struct file_lock *	b_lock;		/* local file lock */
	unsigned short		b_reclaim;	/* got to reclaim lock */
	u32			b_status;	/* grant callback status */
};

static LIST_HEAD(nlm_blocked);

/*
 * Queue up a lock for blocking so that the GRANTED request can see it
 */
int nlmclnt_prepare_block(struct nlm_rqst *req, struct nlm_host *host, struct file_lock *fl)
{
	struct nlm_wait *block;

	BUG_ON(req->a_block != NULL);
	block = kmalloc(sizeof(*block), GFP_KERNEL);
	if (block == NULL)
		return -ENOMEM;
	block->b_host = host;
	block->b_lock = fl;
	init_waitqueue_head(&block->b_wait);
	block->b_status = NLM_LCK_BLOCKED;

	list_add(&block->b_list, &nlm_blocked);
	req->a_block = block;

	return 0;
}

void nlmclnt_finish_block(struct nlm_rqst *req)
{
	struct nlm_wait *block = req->a_block;

	if (block == NULL)
		return;
	req->a_block = NULL;
	list_del(&block->b_list);
	kfree(block);
}

/*
 * Block on a lock
 */
long nlmclnt_block(struct nlm_rqst *req, long timeout)
{
	struct nlm_wait	*block = req->a_block;
	long ret;

	/* A borken server might ask us to block even if we didn't
	 * request it. Just say no!
	 */
	if (!req->a_args.block)
		return -EAGAIN;

	/* Go to sleep waiting for GRANT callback. Some servers seem
	 * to lose callbacks, however, so we're going to poll from
	 * time to time just to make sure.
	 *
	 * For now, the retry frequency is pretty high; normally 
	 * a 1 minute timeout would do. See the comment before
	 * nlmclnt_lock for an explanation.
	 */
	ret = wait_event_interruptible_timeout(block->b_wait,
			block->b_status != NLM_LCK_BLOCKED,
			timeout);

	if (block->b_status != NLM_LCK_BLOCKED) {
		req->a_res.status = block->b_status;
		block->b_status = NLM_LCK_BLOCKED;
	}

	return ret;
}

/*
 * The server lockd has called us back to tell us the lock was granted
 */
u32 nlmclnt_grant(const struct sockaddr_in *addr, const struct nlm_lock *lock)
{
	const struct file_lock *fl = &lock->fl;
	const struct nfs_fh *fh = &lock->fh;
	struct nlm_wait	*block;
	u32 res = nlm_lck_denied;

	/*
	 * Look up blocked request based on arguments. 
	 * Warning: must not use cookie to match it!
	 */
	list_for_each_entry(block, &nlm_blocked, b_list) {
		struct file_lock *fl_blocked = block->b_lock;

		if (!nlm_compare_locks(fl_blocked, fl))
			continue;
		if (!nlm_cmp_addr(&block->b_host->h_addr, addr))
			continue;
		if (nfs_compare_fh(NFS_FH(fl_blocked->fl_file->f_dentry->d_inode) ,fh) != 0)
			continue;
		/* Alright, we found a lock. Set the return status
		 * and wake up the caller
		 */
		block->b_status = NLM_LCK_GRANTED;
		wake_up(&block->b_wait);
		res = nlm_granted;
	}
	return res;
}

/*
 * The following procedures deal with the recovery of locks after a
 * server crash.
 */

/*
 * Mark the locks for reclaiming.
 * FIXME: In 2.5 we don't want to iterate through any global file_lock_list.
 *        Maintain NLM lock reclaiming lists in the nlm_host instead.
 */
static
void nlmclnt_mark_reclaim(struct nlm_host *host)
{
	struct file_lock *fl;
	struct inode *inode;
	struct list_head *tmp;

	list_for_each(tmp, &file_lock_list) {
		fl = list_entry(tmp, struct file_lock, fl_link);

		inode = fl->fl_file->f_dentry->d_inode;
		if (inode->i_sb->s_magic != NFS_SUPER_MAGIC)
			continue;
		if (fl->fl_u.nfs_fl.owner == NULL)
			continue;
		if (fl->fl_u.nfs_fl.owner->host != host)
			continue;
		if (!(fl->fl_u.nfs_fl.flags & NFS_LCK_GRANTED))
			continue;
		fl->fl_u.nfs_fl.flags |= NFS_LCK_RECLAIM;
	}
}

/*
 * Someone has sent us an SM_NOTIFY. Ensure we bind to the new port number,
 * that we mark locks for reclaiming, and that we bump the pseudo NSM state.
 */
static inline
void nlmclnt_prepare_reclaim(struct nlm_host *host, u32 newstate)
{
	host->h_monitored = 0;
	host->h_nsmstate = newstate;
	host->h_state++;
	host->h_nextrebind = 0;
	nlm_rebind_host(host);
	nlmclnt_mark_reclaim(host);
	dprintk("NLM: reclaiming locks for host %s", host->h_name);
}

/*
 * Reclaim all locks on server host. We do this by spawning a separate
 * reclaimer thread.
 */
void
nlmclnt_recovery(struct nlm_host *host, u32 newstate)
{
	if (host->h_reclaiming++) {
		if (host->h_nsmstate == newstate)
			return;
		nlmclnt_prepare_reclaim(host, newstate);
	} else {
		nlmclnt_prepare_reclaim(host, newstate);
		nlm_get_host(host);
		__module_get(THIS_MODULE);
		if (kernel_thread(reclaimer, host, CLONE_KERNEL) < 0)
			module_put(THIS_MODULE);
	}
}

static int
reclaimer(void *ptr)
{
	struct nlm_host	  *host = (struct nlm_host *) ptr;
	struct nlm_wait	  *block;
	struct list_head *tmp;
	struct file_lock *fl;
	struct inode *inode;

	daemonize("%s-reclaim", host->h_name);
	allow_signal(SIGKILL);

	/* This one ensures that our parent doesn't terminate while the
	 * reclaim is in progress */
	lock_kernel();
	lockd_up();

	/* First, reclaim all locks that have been marked. */
restart:
	list_for_each(tmp, &file_lock_list) {
		fl = list_entry(tmp, struct file_lock, fl_link);

		inode = fl->fl_file->f_dentry->d_inode;
		if (inode->i_sb->s_magic != NFS_SUPER_MAGIC)
			continue;
		if (fl->fl_u.nfs_fl.owner == NULL)
			continue;
		if (fl->fl_u.nfs_fl.owner->host != host)
			continue;
		if (!(fl->fl_u.nfs_fl.flags & NFS_LCK_RECLAIM))
			continue;

		fl->fl_u.nfs_fl.flags &= ~NFS_LCK_RECLAIM;
		nlmclnt_reclaim(host, fl);
		if (signalled())
			break;
		goto restart;
	}

	host->h_reclaiming = 0;

	/* Now, wake up all processes that sleep on a blocked lock */
	list_for_each_entry(block, &nlm_blocked, b_list) {
		if (block->b_host == host) {
			block->b_status = NLM_LCK_DENIED_GRACE_PERIOD;
			wake_up(&block->b_wait);
		}
	}

	/* Release host handle after use */
	nlm_release_host(host);
	lockd_down();
	unlock_kernel();
	module_put_and_exit(0);
}
