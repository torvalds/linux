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
struct nlm_wait *nlmclnt_prepare_block(struct nlm_host *host, struct file_lock *fl)
{
	struct nlm_wait *block;

	block = kmalloc(sizeof(*block), GFP_KERNEL);
	if (block != NULL) {
		block->b_host = host;
		block->b_lock = fl;
		init_waitqueue_head(&block->b_wait);
		block->b_status = NLM_LCK_BLOCKED;
		list_add(&block->b_list, &nlm_blocked);
	}
	return block;
}

void nlmclnt_finish_block(struct nlm_wait *block)
{
	if (block == NULL)
		return;
	list_del(&block->b_list);
	kfree(block);
}

/*
 * Block on a lock
 */
int nlmclnt_block(struct nlm_wait *block, struct nlm_rqst *req, long timeout)
{
	long ret;

	/* A borken server might ask us to block even if we didn't
	 * request it. Just say no!
	 */
	if (block == NULL)
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
	if (ret < 0)
		return -ERESTARTSYS;
	req->a_res.status = block->b_status;
	return 0;
}

/*
 * The server lockd has called us back to tell us the lock was granted
 */
__be32 nlmclnt_grant(const struct sockaddr_in *addr, const struct nlm_lock *lock)
{
	const struct file_lock *fl = &lock->fl;
	const struct nfs_fh *fh = &lock->fh;
	struct nlm_wait	*block;
	__be32 res = nlm_lck_denied;

	/*
	 * Look up blocked request based on arguments. 
	 * Warning: must not use cookie to match it!
	 */
	list_for_each_entry(block, &nlm_blocked, b_list) {
		struct file_lock *fl_blocked = block->b_lock;

		if (fl_blocked->fl_start != fl->fl_start)
			continue;
		if (fl_blocked->fl_end != fl->fl_end)
			continue;
		/*
		 * Careful! The NLM server will return the 32-bit "pid" that
		 * we put on the wire: in this case the lockowner "pid".
		 */
		if (fl_blocked->fl_u.nfs_fl.owner->pid != lock->svid)
			continue;
		if (!nlm_cmp_addr(&block->b_host->h_addr, addr))
			continue;
		if (nfs_compare_fh(NFS_FH(fl_blocked->fl_file->f_path.dentry->d_inode) ,fh) != 0)
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
 * Reclaim all locks on server host. We do this by spawning a separate
 * reclaimer thread.
 */
void
nlmclnt_recovery(struct nlm_host *host)
{
	if (!host->h_reclaiming++) {
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
	struct file_lock *fl, *next;
	u32 nsmstate;

	daemonize("%s-reclaim", host->h_name);
	allow_signal(SIGKILL);

	down_write(&host->h_rwsem);

	/* This one ensures that our parent doesn't terminate while the
	 * reclaim is in progress */
	lock_kernel();
	lockd_up(0); /* note: this cannot fail as lockd is already running */

	dprintk("lockd: reclaiming locks for host %s", host->h_name);

restart:
	nsmstate = host->h_nsmstate;

	/* Force a portmap getport - the peer's lockd will
	 * most likely end up on a different port.
	 */
	host->h_nextrebind = jiffies;
	nlm_rebind_host(host);

	/* First, reclaim all locks that have been granted. */
	list_splice_init(&host->h_granted, &host->h_reclaim);
	list_for_each_entry_safe(fl, next, &host->h_reclaim, fl_u.nfs_fl.list) {
		list_del_init(&fl->fl_u.nfs_fl.list);

		/* Why are we leaking memory here? --okir */
		if (signalled())
			continue;
		if (nlmclnt_reclaim(host, fl) != 0)
			continue;
		list_add_tail(&fl->fl_u.nfs_fl.list, &host->h_granted);
		if (host->h_nsmstate != nsmstate) {
			/* Argh! The server rebooted again! */
			goto restart;
		}
	}

	host->h_reclaiming = 0;
	up_write(&host->h_rwsem);
	dprintk("NLM: done reclaiming locks for host %s", host->h_name);

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
