/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

/*
 * device.c
 *
 * This is the userland interface to the DLM.
 *
 * The locking is done via a misc char device (find the
 * registered minor number in /proc/misc).
 *
 * User code should not use this interface directly but
 * call the library routines in libdlm.a instead.
 *
 */

#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/signal.h>
#include <linux/spinlock.h>
#include <linux/idr.h>

#include <linux/dlm.h>
#include <linux/dlm_device.h>

#include "lvb_table.h"

static struct file_operations _dlm_fops;
static const char *name_prefix="dlm";
static struct list_head user_ls_list;
static struct semaphore user_ls_lock;

/* Lock infos are stored in here indexed by lock ID */
static DEFINE_IDR(lockinfo_idr);
static rwlock_t lockinfo_lock;

/* Flags in li_flags */
#define LI_FLAG_COMPLETE   1
#define LI_FLAG_FIRSTLOCK  2
#define LI_FLAG_PERSISTENT 3

/* flags in ls_flags*/
#define LS_FLAG_DELETED   1
#define LS_FLAG_AUTOFREE  2


#define LOCKINFO_MAGIC 0x53595324

struct lock_info {
	uint32_t li_magic;
	uint8_t li_cmd;
	int8_t	li_grmode;
	int8_t  li_rqmode;
	struct dlm_lksb li_lksb;
	wait_queue_head_t li_waitq;
	unsigned long li_flags;
	void __user *li_castparam;
	void __user *li_castaddr;
	void __user *li_bastparam;
	void __user *li_bastaddr;
	void __user *li_pend_bastparam;
	void __user *li_pend_bastaddr;
	struct list_head li_ownerqueue;
	struct file_info *li_file;
	struct dlm_lksb __user *li_user_lksb;
	struct semaphore li_firstlock;
};

/* A queued AST no less */
struct ast_info {
	struct dlm_lock_result result;
	struct list_head list;
	uint32_t lvb_updated;
	uint32_t progress;      /* How much has been read */
};

/* One of these per userland lockspace */
struct user_ls {
	void    *ls_lockspace;
	atomic_t ls_refcnt;
	long     ls_flags;

	/* Passed into misc_register() */
	struct miscdevice ls_miscinfo;
	struct list_head  ls_list;
};

/* misc_device info for the control device */
static struct miscdevice ctl_device;

/*
 * Stuff we hang off the file struct.
 * The first two are to cope with unlocking all the
 * locks help by a process when it dies.
 */
struct file_info {
	struct list_head    fi_li_list;  /* List of active lock_infos */
	spinlock_t          fi_li_lock;
	struct list_head    fi_ast_list; /* Queue of ASTs to be delivered */
	spinlock_t          fi_ast_lock;
	wait_queue_head_t   fi_wait;
	struct user_ls     *fi_ls;
	atomic_t            fi_refcnt;   /* Number of users */
	unsigned long       fi_flags;    /* Bit 1 means the device is open */
};


/* get and put ops for file_info.
   Actually I don't really like "get" and "put", but everyone
   else seems to use them and I can't think of anything
   nicer at the moment */
static void get_file_info(struct file_info *f)
{
	atomic_inc(&f->fi_refcnt);
}

static void put_file_info(struct file_info *f)
{
	if (atomic_dec_and_test(&f->fi_refcnt))
		kfree(f);
}

static void release_lockinfo(struct lock_info *li)
{
	put_file_info(li->li_file);

	write_lock(&lockinfo_lock);
	idr_remove(&lockinfo_idr, li->li_lksb.sb_lkid);
	write_unlock(&lockinfo_lock);

	if (li->li_lksb.sb_lvbptr)
		kfree(li->li_lksb.sb_lvbptr);
	kfree(li);

	module_put(THIS_MODULE);
}

static struct lock_info *get_lockinfo(uint32_t lockid)
{
	struct lock_info *li;

	read_lock(&lockinfo_lock);
	li = idr_find(&lockinfo_idr, lockid);
	read_unlock(&lockinfo_lock);

	return li;
}

static int add_lockinfo(struct lock_info *li)
{
	int n;
	int r;
	int ret = -EINVAL;

	write_lock(&lockinfo_lock);

	if (idr_find(&lockinfo_idr, li->li_lksb.sb_lkid))
		goto out_up;

	ret = -ENOMEM;
	r = idr_pre_get(&lockinfo_idr, GFP_KERNEL);
	if (!r)
		goto out_up;

	r = idr_get_new_above(&lockinfo_idr, li, li->li_lksb.sb_lkid, &n);
	if (r)
		goto out_up;

	if (n != li->li_lksb.sb_lkid) {
		idr_remove(&lockinfo_idr, n);
		goto out_up;
	}

	ret = 0;

 out_up:
	write_unlock(&lockinfo_lock);

	return ret;
}


static struct user_ls *__find_lockspace(int minor)
{
	struct user_ls *lsinfo;

	list_for_each_entry(lsinfo, &user_ls_list, ls_list) {
		if (lsinfo->ls_miscinfo.minor == minor)
			return lsinfo;
	}
	return NULL;
}

/* Find a lockspace struct given the device minor number */
static struct user_ls *find_lockspace(int minor)
{
	struct user_ls *lsinfo;

	down(&user_ls_lock);
	lsinfo = __find_lockspace(minor);
	up(&user_ls_lock);

	return lsinfo;
}

static void add_lockspace_to_list(struct user_ls *lsinfo)
{
	down(&user_ls_lock);
	list_add(&lsinfo->ls_list, &user_ls_list);
	up(&user_ls_lock);
}

/* Register a lockspace with the DLM and create a misc
   device for userland to access it */
static int register_lockspace(char *name, struct user_ls **ls, int flags)
{
	struct user_ls *newls;
	int status;
	int namelen;

	namelen = strlen(name)+strlen(name_prefix)+2;

	newls = kmalloc(sizeof(struct user_ls), GFP_KERNEL);
	if (!newls)
		return -ENOMEM;
	memset(newls, 0, sizeof(struct user_ls));

	newls->ls_miscinfo.name = kmalloc(namelen, GFP_KERNEL);
	if (!newls->ls_miscinfo.name) {
		kfree(newls);
		return -ENOMEM;
	}

	status = dlm_new_lockspace(name, strlen(name), &newls->ls_lockspace, 0,
				   DLM_USER_LVB_LEN);
	if (status != 0) {
		kfree(newls->ls_miscinfo.name);
		kfree(newls);
		return status;
	}

	snprintf((char*)newls->ls_miscinfo.name, namelen, "%s_%s",
		 name_prefix, name);

	newls->ls_miscinfo.fops = &_dlm_fops;
	newls->ls_miscinfo.minor = MISC_DYNAMIC_MINOR;

	status = misc_register(&newls->ls_miscinfo);
	if (status) {
		printk(KERN_ERR "dlm: misc register failed for %s\n", name);
		dlm_release_lockspace(newls->ls_lockspace, 0);
		kfree(newls->ls_miscinfo.name);
		kfree(newls);
		return status;
	}

	if (flags & DLM_USER_LSFLG_AUTOFREE)
		set_bit(LS_FLAG_AUTOFREE, &newls->ls_flags);

	add_lockspace_to_list(newls);
	*ls = newls;
	return 0;
}

/* Called with the user_ls_lock semaphore held */
static int unregister_lockspace(struct user_ls *lsinfo, int force)
{
	int status;

	status = dlm_release_lockspace(lsinfo->ls_lockspace, force);
	if (status)
		return status;

	status = misc_deregister(&lsinfo->ls_miscinfo);
	if (status)
		return status;

	list_del(&lsinfo->ls_list);
	set_bit(LS_FLAG_DELETED, &lsinfo->ls_flags);
	lsinfo->ls_lockspace = NULL;
	if (atomic_read(&lsinfo->ls_refcnt) == 0) {
		kfree(lsinfo->ls_miscinfo.name);
		kfree(lsinfo);
	}

	return 0;
}

/* Add it to userland's AST queue */
static void add_to_astqueue(struct lock_info *li, void *astaddr, void *astparam,
			    int lvb_updated)
{
	struct ast_info *ast = kmalloc(sizeof(struct ast_info), GFP_KERNEL);
	if (!ast)
		return;

	memset(ast, 0, sizeof(*ast));
	ast->result.user_astparam = astparam;
	ast->result.user_astaddr  = astaddr;
	ast->result.user_lksb     = li->li_user_lksb;
	memcpy(&ast->result.lksb, &li->li_lksb, sizeof(struct dlm_lksb));
	ast->lvb_updated = lvb_updated;

	spin_lock(&li->li_file->fi_ast_lock);
	list_add_tail(&ast->list, &li->li_file->fi_ast_list);
	spin_unlock(&li->li_file->fi_ast_lock);
	wake_up_interruptible(&li->li_file->fi_wait);
}

static void bast_routine(void *param, int mode)
{
	struct lock_info *li = param;

	if (li && li->li_bastaddr)
		add_to_astqueue(li, li->li_bastaddr, li->li_bastparam, 0);
}

/*
 * This is the kernel's AST routine.
 * All lock, unlock & query operations complete here.
 * The only syncronous ops are those done during device close.
 */
static void ast_routine(void *param)
{
	struct lock_info *li = param;

	/* Param may be NULL if a persistent lock is unlocked by someone else */
	if (!li)
		return;

	/* If this is a succesful conversion then activate the blocking ast
	 * args from the conversion request */
	if (!test_bit(LI_FLAG_FIRSTLOCK, &li->li_flags) &&
	    li->li_lksb.sb_status == 0) {

		li->li_bastparam = li->li_pend_bastparam;
		li->li_bastaddr = li->li_pend_bastaddr;
		li->li_pend_bastaddr = NULL;
	}

	/* If it's an async request then post data to the user's AST queue. */
	if (li->li_castaddr) {
		int lvb_updated = 0;

		/* See if the lvb has been updated */
		if (dlm_lvb_operations[li->li_grmode+1][li->li_rqmode+1] == 1)
			lvb_updated = 1;

		if (li->li_lksb.sb_status == 0)
			li->li_grmode = li->li_rqmode;

		/* Only queue AST if the device is still open */
		if (test_bit(1, &li->li_file->fi_flags))
			add_to_astqueue(li, li->li_castaddr, li->li_castparam,
					lvb_updated);

		/* If it's a new lock operation that failed, then
		 * remove it from the owner queue and free the
		 * lock_info.
		 */
		if (test_and_clear_bit(LI_FLAG_FIRSTLOCK, &li->li_flags) &&
		    li->li_lksb.sb_status != 0) {

			/* Wait till dlm_lock() has finished */
			down(&li->li_firstlock);
			up(&li->li_firstlock);

			spin_lock(&li->li_file->fi_li_lock);
			list_del(&li->li_ownerqueue);
			spin_unlock(&li->li_file->fi_li_lock);
			release_lockinfo(li);
			return;
		}
		/* Free unlocks & queries */
		if (li->li_lksb.sb_status == -DLM_EUNLOCK ||
		    li->li_cmd == DLM_USER_QUERY) {
			release_lockinfo(li);
		}
	} else {
		/* Synchronous request, just wake up the caller */
		set_bit(LI_FLAG_COMPLETE, &li->li_flags);
		wake_up_interruptible(&li->li_waitq);
	}
}

/*
 * Wait for the lock op to complete and return the status.
 */
static int wait_for_ast(struct lock_info *li)
{
	/* Wait for the AST routine to complete */
	set_task_state(current, TASK_INTERRUPTIBLE);
	while (!test_bit(LI_FLAG_COMPLETE, &li->li_flags))
		schedule();

	set_task_state(current, TASK_RUNNING);

	return li->li_lksb.sb_status;
}


/* Open on control device */
static int dlm_ctl_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

/* Close on control device */
static int dlm_ctl_close(struct inode *inode, struct file *file)
{
	return 0;
}

/* Open on lockspace device */
static int dlm_open(struct inode *inode, struct file *file)
{
	struct file_info *f;
	struct user_ls *lsinfo;

	lsinfo = find_lockspace(iminor(inode));
	if (!lsinfo)
		return -ENOENT;

	f = kmalloc(sizeof(struct file_info), GFP_KERNEL);
	if (!f)
		return -ENOMEM;

	atomic_inc(&lsinfo->ls_refcnt);
	INIT_LIST_HEAD(&f->fi_li_list);
	INIT_LIST_HEAD(&f->fi_ast_list);
	spin_lock_init(&f->fi_li_lock);
	spin_lock_init(&f->fi_ast_lock);
	init_waitqueue_head(&f->fi_wait);
	f->fi_ls = lsinfo;
	f->fi_flags = 0;
	get_file_info(f);
	set_bit(1, &f->fi_flags);

	file->private_data = f;

	return 0;
}

/* Check the user's version matches ours */
static int check_version(struct dlm_write_request *req)
{
	if (req->version[0] != DLM_DEVICE_VERSION_MAJOR ||
	    (req->version[0] == DLM_DEVICE_VERSION_MAJOR &&
	     req->version[1] > DLM_DEVICE_VERSION_MINOR)) {

		printk(KERN_DEBUG "dlm: process %s (%d) version mismatch "
		       "user (%d.%d.%d) kernel (%d.%d.%d)\n",
		       current->comm,
		       current->pid,
		       req->version[0],
		       req->version[1],
		       req->version[2],
		       DLM_DEVICE_VERSION_MAJOR,
		       DLM_DEVICE_VERSION_MINOR,
		       DLM_DEVICE_VERSION_PATCH);
		return -EINVAL;
	}
	return 0;
}

/* Close on lockspace device */
static int dlm_close(struct inode *inode, struct file *file)
{
	struct file_info *f = file->private_data;
	struct lock_info li;
	struct lock_info *old_li, *safe;
	sigset_t tmpsig;
	sigset_t allsigs;
	struct user_ls *lsinfo;
	DECLARE_WAITQUEUE(wq, current);

	lsinfo = find_lockspace(iminor(inode));
	if (!lsinfo)
		return -ENOENT;

	/* Mark this closed so that ASTs will not be delivered any more */
	clear_bit(1, &f->fi_flags);

	/* Block signals while we are doing this */
	sigfillset(&allsigs);
	sigprocmask(SIG_BLOCK, &allsigs, &tmpsig);

	/* We use our own lock_info struct here, so that any
	 * outstanding "real" ASTs will be delivered with the
	 * corresponding "real" params, thus freeing the lock_info
	 * that belongs the lock. This catches the corner case where
	 * a lock is BUSY when we try to unlock it here
	 */
	memset(&li, 0, sizeof(li));
	clear_bit(LI_FLAG_COMPLETE, &li.li_flags);
	init_waitqueue_head(&li.li_waitq);
	add_wait_queue(&li.li_waitq, &wq);

	/*
	 * Free any outstanding locks, they are on the
	 * list in LIFO order so there should be no problems
	 * about unlocking parents before children.
	 */
	list_for_each_entry_safe(old_li, safe, &f->fi_li_list, li_ownerqueue) {
		int status;
		int flags = 0;

		/* Don't unlock persistent locks, just mark them orphaned */
		if (test_bit(LI_FLAG_PERSISTENT, &old_li->li_flags)) {
			list_del(&old_li->li_ownerqueue);

			/* Update master copy */
			/* TODO: Check locking core updates the local and
			   remote ORPHAN flags */
			li.li_lksb.sb_lkid = old_li->li_lksb.sb_lkid;
			status = dlm_lock(f->fi_ls->ls_lockspace,
					  old_li->li_grmode, &li.li_lksb,
					  DLM_LKF_CONVERT|DLM_LKF_ORPHAN,
					  NULL, 0, 0, ast_routine, NULL,
					  NULL, NULL);
			if (status != 0)
				printk("dlm: Error orphaning lock %x: %d\n",
				       old_li->li_lksb.sb_lkid, status);

			/* But tidy our references in it */
			release_lockinfo(old_li);
			continue;
		}

		clear_bit(LI_FLAG_COMPLETE, &li.li_flags);

		flags = DLM_LKF_FORCEUNLOCK;
		if (old_li->li_grmode >= DLM_LOCK_PW)
			flags |= DLM_LKF_IVVALBLK;

		status = dlm_unlock(f->fi_ls->ls_lockspace,
				    old_li->li_lksb.sb_lkid, flags,
				    &li.li_lksb, &li);

		/* Must wait for it to complete as the next lock could be its
		 * parent */
		if (status == 0)
			wait_for_ast(&li);

		/* Unlock suceeded, free the lock_info struct. */
		if (status == 0)
			release_lockinfo(old_li);
	}

	remove_wait_queue(&li.li_waitq, &wq);

	/*
	 * If this is the last reference to the lockspace
	 * then free the struct. If it's an AUTOFREE lockspace
	 * then free the whole thing.
	 */
	down(&user_ls_lock);
	if (atomic_dec_and_test(&lsinfo->ls_refcnt)) {

		if (lsinfo->ls_lockspace) {
			if (test_bit(LS_FLAG_AUTOFREE, &lsinfo->ls_flags)) {
				unregister_lockspace(lsinfo, 1);
			}
		} else {
			kfree(lsinfo->ls_miscinfo.name);
			kfree(lsinfo);
		}
	}
	up(&user_ls_lock);
	put_file_info(f);

	/* Restore signals */
	sigprocmask(SIG_SETMASK, &tmpsig, NULL);
	recalc_sigpending();

	return 0;
}

static int do_user_create_lockspace(struct file_info *fi, uint8_t cmd,
				    struct dlm_lspace_params *kparams)
{
	int status;
	struct user_ls *lsinfo;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	status = register_lockspace(kparams->name, &lsinfo, kparams->flags);

	/* If it succeeded then return the minor number */
	if (status == 0)
		status = lsinfo->ls_miscinfo.minor;

	return status;
}

static int do_user_remove_lockspace(struct file_info *fi, uint8_t cmd,
				    struct dlm_lspace_params *kparams)
{
	int status;
	int force = 1;
	struct user_ls *lsinfo;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	down(&user_ls_lock);
	lsinfo = __find_lockspace(kparams->minor);
	if (!lsinfo) {
		up(&user_ls_lock);
		return -EINVAL;
	}

	if (kparams->flags & DLM_USER_LSFLG_FORCEFREE)
		force = 2;

	status = unregister_lockspace(lsinfo, force);
	up(&user_ls_lock);

	return status;
}

/* Read call, might block if no ASTs are waiting.
 * It will only ever return one message at a time, regardless
 * of how many are pending.
 */
static ssize_t dlm_read(struct file *file, char __user *buffer, size_t count,
			loff_t *ppos)
{
	struct file_info *fi = file->private_data;
	struct ast_info *ast;
	int data_size;
	int offset;
	DECLARE_WAITQUEUE(wait, current);

	if (count < sizeof(struct dlm_lock_result))
		return -EINVAL;

	spin_lock(&fi->fi_ast_lock);
	if (list_empty(&fi->fi_ast_list)) {

		/* No waiting ASTs.
		 * Return EOF if the lockspace been deleted.
		 */
		if (test_bit(LS_FLAG_DELETED, &fi->fi_ls->ls_flags))
			return 0;

		if (file->f_flags & O_NONBLOCK) {
			spin_unlock(&fi->fi_ast_lock);
			return -EAGAIN;
		}

		add_wait_queue(&fi->fi_wait, &wait);

	repeat:
		set_current_state(TASK_INTERRUPTIBLE);
		if (list_empty(&fi->fi_ast_list) &&
		    !signal_pending(current)) {

			spin_unlock(&fi->fi_ast_lock);
			schedule();
			spin_lock(&fi->fi_ast_lock);
			goto repeat;
		}

		current->state = TASK_RUNNING;
		remove_wait_queue(&fi->fi_wait, &wait);

		if (signal_pending(current)) {
			spin_unlock(&fi->fi_ast_lock);
			return -ERESTARTSYS;
		}
	}

	ast = list_entry(fi->fi_ast_list.next, struct ast_info, list);
	list_del(&ast->list);
	spin_unlock(&fi->fi_ast_lock);

	/* Work out the size of the returned data */
	data_size = sizeof(struct dlm_lock_result);
	if (ast->lvb_updated && ast->result.lksb.sb_lvbptr)
		data_size += DLM_USER_LVB_LEN;

	offset = sizeof(struct dlm_lock_result);

	/* Room for the extended data ? */
	if (count >= data_size) {

		if (ast->lvb_updated && ast->result.lksb.sb_lvbptr) {
			if (copy_to_user(buffer+offset,
					 ast->result.lksb.sb_lvbptr,
					 DLM_USER_LVB_LEN))
				return -EFAULT;
			ast->result.lvb_offset = offset;
			offset += DLM_USER_LVB_LEN;
		}
	}

	ast->result.length = data_size;
	/* Copy the header now it has all the offsets in it */
	if (copy_to_user(buffer, &ast->result, sizeof(struct dlm_lock_result)))
		offset = -EFAULT;

	/* If we only returned a header and there's more to come then put it
	   back on the list */
	if (count < data_size) {
		spin_lock(&fi->fi_ast_lock);
		list_add(&ast->list, &fi->fi_ast_list);
		spin_unlock(&fi->fi_ast_lock);
	} else
		kfree(ast);
	return offset;
}

static unsigned int dlm_poll(struct file *file, poll_table *wait)
{
	struct file_info *fi = file->private_data;

	poll_wait(file, &fi->fi_wait, wait);

	spin_lock(&fi->fi_ast_lock);
	if (!list_empty(&fi->fi_ast_list)) {
		spin_unlock(&fi->fi_ast_lock);
		return POLLIN | POLLRDNORM;
	}

	spin_unlock(&fi->fi_ast_lock);
	return 0;
}

static struct lock_info *allocate_lockinfo(struct file_info *fi, uint8_t cmd,
					   struct dlm_lock_params *kparams)
{
	struct lock_info *li;

	if (!try_module_get(THIS_MODULE))
		return NULL;

	li = kmalloc(sizeof(struct lock_info), GFP_KERNEL);
	if (li) {
		li->li_magic     = LOCKINFO_MAGIC;
		li->li_file      = fi;
		li->li_cmd       = cmd;
		li->li_flags     = 0;
		li->li_grmode    = -1;
		li->li_rqmode    = -1;
		li->li_pend_bastparam = NULL;
		li->li_pend_bastaddr  = NULL;
		li->li_castaddr   = NULL;
		li->li_castparam  = NULL;
		li->li_lksb.sb_lvbptr = NULL;
		li->li_bastaddr  = kparams->bastaddr;
		li->li_bastparam = kparams->bastparam;

		get_file_info(fi);
	}
	return li;
}

static int do_user_lock(struct file_info *fi, uint8_t cmd,
			struct dlm_lock_params *kparams)
{
	struct lock_info *li;
	int status;

	/*
	 * Validate things that we need to have correct.
	 */
	if (!kparams->castaddr)
		return -EINVAL;

	if (!kparams->lksb)
		return -EINVAL;

	/* Persistent child locks are not available yet */
	if ((kparams->flags & DLM_LKF_PERSISTENT) && kparams->parent)
		return -EINVAL;

        /* For conversions, there should already be a lockinfo struct,
	   unless we are adopting an orphaned persistent lock */
	if (kparams->flags & DLM_LKF_CONVERT) {

		li = get_lockinfo(kparams->lkid);

		/* If this is a persistent lock we will have to create a
		   lockinfo again */
		if (!li && DLM_LKF_PERSISTENT) {
			li = allocate_lockinfo(fi, cmd, kparams);

			li->li_lksb.sb_lkid = kparams->lkid;
			li->li_castaddr  = kparams->castaddr;
			li->li_castparam = kparams->castparam;

			/* OK, this isn;t exactly a FIRSTLOCK but it is the
			   first time we've used this lockinfo, and if things
			   fail we want rid of it */
			init_MUTEX_LOCKED(&li->li_firstlock);
			set_bit(LI_FLAG_FIRSTLOCK, &li->li_flags);
			add_lockinfo(li);

			/* TODO: do a query to get the current state ?? */
		}
		if (!li)
			return -EINVAL;

		if (li->li_magic != LOCKINFO_MAGIC)
			return -EINVAL;

		/* For conversions don't overwrite the current blocking AST
		   info so that:
		   a) if a blocking AST fires before the conversion is queued
		      it runs the current handler
		   b) if the conversion is cancelled, the original blocking AST
		      declaration is active
		   The pend_ info is made active when the conversion
		   completes.
		*/
		li->li_pend_bastaddr  = kparams->bastaddr;
		li->li_pend_bastparam = kparams->bastparam;
	} else {
		li = allocate_lockinfo(fi, cmd, kparams);
		if (!li)
			return -ENOMEM;

		/* semaphore to allow us to complete our work before
  		   the AST routine runs. In fact we only need (and use) this
		   when the initial lock fails */
		init_MUTEX_LOCKED(&li->li_firstlock);
		set_bit(LI_FLAG_FIRSTLOCK, &li->li_flags);
	}

	li->li_user_lksb = kparams->lksb;
	li->li_castaddr  = kparams->castaddr;
	li->li_castparam = kparams->castparam;
	li->li_lksb.sb_lkid = kparams->lkid;
	li->li_rqmode    = kparams->mode;
	if (kparams->flags & DLM_LKF_PERSISTENT)
		set_bit(LI_FLAG_PERSISTENT, &li->li_flags);

	/* Copy in the value block */
	if (kparams->flags & DLM_LKF_VALBLK) {
		if (!li->li_lksb.sb_lvbptr) {
			li->li_lksb.sb_lvbptr = kmalloc(DLM_USER_LVB_LEN,
							GFP_KERNEL);
			if (!li->li_lksb.sb_lvbptr) {
				status = -ENOMEM;
				goto out_err;
			}
		}

		memcpy(li->li_lksb.sb_lvbptr, kparams->lvb, DLM_USER_LVB_LEN);
	}

	/* Lock it ... */
	status = dlm_lock(fi->fi_ls->ls_lockspace,
			  kparams->mode, &li->li_lksb,
			  kparams->flags,
			  kparams->name, kparams->namelen,
			  kparams->parent,
			  ast_routine,
			  li,
			  (li->li_pend_bastaddr || li->li_bastaddr) ?
			   bast_routine : NULL,
			  kparams->range.ra_end ? &kparams->range : NULL);
	if (status)
		goto out_err;

	/* If it succeeded (this far) with a new lock then keep track of
	   it on the file's lockinfo list */
	if (!status && test_bit(LI_FLAG_FIRSTLOCK, &li->li_flags)) {

		spin_lock(&fi->fi_li_lock);
		list_add(&li->li_ownerqueue, &fi->fi_li_list);
		spin_unlock(&fi->fi_li_lock);
		if (add_lockinfo(li))
			printk(KERN_WARNING "Add lockinfo failed\n");

		up(&li->li_firstlock);
	}

	/* Return the lockid as the user needs it /now/ */
	return li->li_lksb.sb_lkid;

 out_err:
	if (test_bit(LI_FLAG_FIRSTLOCK, &li->li_flags))
		release_lockinfo(li);
	return status;

}

static int do_user_unlock(struct file_info *fi, uint8_t cmd,
			  struct dlm_lock_params *kparams)
{
	struct lock_info *li;
	int status;
	int convert_cancel = 0;

	li = get_lockinfo(kparams->lkid);
	if (!li) {
		li = allocate_lockinfo(fi, cmd, kparams);
		spin_lock(&fi->fi_li_lock);
		list_add(&li->li_ownerqueue, &fi->fi_li_list);
		spin_unlock(&fi->fi_li_lock);
	}
 	if (!li)
		return -ENOMEM;

	if (li->li_magic != LOCKINFO_MAGIC)
		return -EINVAL;

	li->li_user_lksb = kparams->lksb;
	li->li_castparam = kparams->castparam;
	li->li_cmd       = cmd;

	/* Cancelling a conversion doesn't remove the lock...*/
	if (kparams->flags & DLM_LKF_CANCEL && li->li_grmode != -1)
		convert_cancel = 1;

	/* dlm_unlock() passes a 0 for castaddr which means don't overwrite
	   the existing li_castaddr as that's the completion routine for
	   unlocks. dlm_unlock_wait() specifies a new AST routine to be
	   executed when the unlock completes. */
	if (kparams->castaddr)
		li->li_castaddr = kparams->castaddr;

	/* Use existing lksb & astparams */
	status = dlm_unlock(fi->fi_ls->ls_lockspace,
			     kparams->lkid,
			     kparams->flags, &li->li_lksb, li);

	if (!status && !convert_cancel) {
		spin_lock(&fi->fi_li_lock);
		list_del(&li->li_ownerqueue);
		spin_unlock(&fi->fi_li_lock);
	}

	return status;
}

/* Write call, submit a locking request */
static ssize_t dlm_write(struct file *file, const char __user *buffer,
			 size_t count, loff_t *ppos)
{
	struct file_info *fi = file->private_data;
	struct dlm_write_request *kparams;
	sigset_t tmpsig;
	sigset_t allsigs;
	int status;

	/* -1 because lock name is optional */
	if (count < sizeof(struct dlm_write_request)-1)
		return -EINVAL;

	/* Has the lockspace been deleted */
	if (fi && test_bit(LS_FLAG_DELETED, &fi->fi_ls->ls_flags))
		return -ENOENT;

	kparams = kmalloc(count, GFP_KERNEL);
	if (!kparams)
		return -ENOMEM;

	status = -EFAULT;
	/* Get the command info */
	if (copy_from_user(kparams, buffer, count))
		goto out_free;

	status = -EBADE;
	if (check_version(kparams))
		goto out_free;

	/* Block signals while we are doing this */
	sigfillset(&allsigs);
	sigprocmask(SIG_BLOCK, &allsigs, &tmpsig);

	status = -EINVAL;
	switch (kparams->cmd)
	{
	case DLM_USER_LOCK:
		if (!fi) goto out_sig;
		status = do_user_lock(fi, kparams->cmd, &kparams->i.lock);
		break;

	case DLM_USER_UNLOCK:
		if (!fi) goto out_sig;
		status = do_user_unlock(fi, kparams->cmd, &kparams->i.lock);
		break;

	case DLM_USER_CREATE_LOCKSPACE:
		if (fi) goto out_sig;
		status = do_user_create_lockspace(fi, kparams->cmd,
						  &kparams->i.lspace);
		break;

	case DLM_USER_REMOVE_LOCKSPACE:
		if (fi) goto out_sig;
		status = do_user_remove_lockspace(fi, kparams->cmd,
						  &kparams->i.lspace);
		break;
	default:
		printk("Unknown command passed to DLM device : %d\n",
			kparams->cmd);
		break;
	}

 out_sig:
	/* Restore signals */
	sigprocmask(SIG_SETMASK, &tmpsig, NULL);
	recalc_sigpending();

 out_free:
	kfree(kparams);
	if (status == 0)
		return count;
	else
		return status;
}

static struct file_operations _dlm_fops = {
      .open    = dlm_open,
      .release = dlm_close,
      .read    = dlm_read,
      .write   = dlm_write,
      .poll    = dlm_poll,
      .owner   = THIS_MODULE,
};

static struct file_operations _dlm_ctl_fops = {
      .open    = dlm_ctl_open,
      .release = dlm_ctl_close,
      .write   = dlm_write,
      .owner   = THIS_MODULE,
};

/*
 * Create control device
 */
static int __init dlm_device_init(void)
{
	int r;

	INIT_LIST_HEAD(&user_ls_list);
	init_MUTEX(&user_ls_lock);
	rwlock_init(&lockinfo_lock);

	ctl_device.name = "dlm-control";
	ctl_device.fops = &_dlm_ctl_fops;
	ctl_device.minor = MISC_DYNAMIC_MINOR;

	r = misc_register(&ctl_device);
	if (r) {
		printk(KERN_ERR "dlm: misc_register failed for control dev\n");
		return r;
	}

	return 0;
}

static void __exit dlm_device_exit(void)
{
	misc_deregister(&ctl_device);
}

MODULE_DESCRIPTION("Distributed Lock Manager device interface");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

module_init(dlm_device_init);
module_exit(dlm_device_exit);
