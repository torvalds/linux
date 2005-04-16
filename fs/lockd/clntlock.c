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
	struct nlm_wait *	b_next;		/* linked list */
	wait_queue_head_t	b_wait;		/* where to wait on */
	struct nlm_host *	b_host;
	struct file_lock *	b_lock;		/* local file lock */
	unsigned short		b_reclaim;	/* got to reclaim lock */
	u32			b_status;	/* grant callback status */
};

static struct nlm_wait *	nlm_blocked;

/*
 * Block on a lock
 */
int
nlmclnt_block(struct nlm_host *host, struct file_lock *fl, u32 *statp)
{
	struct nlm_wait	block, **head;
	int		err;
	u32		pstate;

	block.b_host   = host;
	block.b_lock   = fl;
	init_waitqueue_head(&block.b_wait);
	block.b_status = NLM_LCK_BLOCKED;
	block.b_next   = nlm_blocked;
	nlm_blocked    = &block;

	/* Remember pseudo nsm state */
	pstate = host->h_state;

	/* Go to sleep waiting for GRANT callback. Some servers seem
	 * to lose callbacks, however, so we're going to poll from
	 * time to time just to make sure.
	 *
	 * For now, the retry frequency is pretty high; normally 
	 * a 1 minute timeout would do. See the comment before
	 * nlmclnt_lock for an explanation.
	 */
	sleep_on_timeout(&block.b_wait, 30*HZ);

	for (head = &nlm_blocked; *head; head = &(*head)->b_next) {
		if (*head == &block) {
			*head = block.b_next;
			break;
		}
	}

	if (!signalled()) {
		*statp = block.b_status;
		return 0;
	}

	/* Okay, we were interrupted. Cancel the pending request
	 * unless the server has rebooted.
	 */
	if (pstate == host->h_state && (err = nlmclnt_cancel(host, fl)) < 0)
		printk(KERN_NOTICE
			"lockd: CANCEL call failed (errno %d)\n", -err);

	return -ERESTARTSYS;
}

/*
 * The server lockd has called us back to tell us the lock was granted
 */
u32
nlmclnt_grant(struct nlm_lock *lock)
{
	struct nlm_wait	*block;

	/*
	 * Look up blocked request based on arguments. 
	 * Warning: must not use cookie to match it!
	 */
	for (block = nlm_blocked; block; block = block->b_next) {
		if (nlm_compare_locks(block->b_lock, &lock->fl))
			break;
	}

	/* Ooops, no blocked request found. */
	if (block == NULL)
		return nlm_lck_denied;

	/* Alright, we found the lock. Set the return status and
	 * wake up the caller.
	 */
	block->b_status = NLM_LCK_GRANTED;
	wake_up(&block->b_wait);

	return nlm_granted;
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
	for (block = nlm_blocked; block; block = block->b_next) {
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
