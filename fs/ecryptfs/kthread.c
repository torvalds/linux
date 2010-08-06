/**
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 2008 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mahalcro@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/mount.h>
#include "ecryptfs_kernel.h"

struct kmem_cache *ecryptfs_open_req_cache;

static struct ecryptfs_kthread_ctl {
#define ECRYPTFS_KTHREAD_ZOMBIE 0x00000001
	u32 flags;
	struct mutex mux;
	struct list_head req_list;
	wait_queue_head_t wait;
} ecryptfs_kthread_ctl;

static struct task_struct *ecryptfs_kthread;

/**
 * ecryptfs_threadfn
 * @ignored: ignored
 *
 * The eCryptfs kernel thread that has the responsibility of getting
 * the lower persistent file with RW permissions.
 *
 * Returns zero on success; non-zero otherwise
 */
static int ecryptfs_threadfn(void *ignored)
{
	set_freezable();
	while (1)  {
		struct ecryptfs_open_req *req;

		wait_event_freezable(
			ecryptfs_kthread_ctl.wait,
			(!list_empty(&ecryptfs_kthread_ctl.req_list)
			 || kthread_should_stop()));
		mutex_lock(&ecryptfs_kthread_ctl.mux);
		if (ecryptfs_kthread_ctl.flags & ECRYPTFS_KTHREAD_ZOMBIE) {
			mutex_unlock(&ecryptfs_kthread_ctl.mux);
			goto out;
		}
		while (!list_empty(&ecryptfs_kthread_ctl.req_list)) {
			req = list_first_entry(&ecryptfs_kthread_ctl.req_list,
					       struct ecryptfs_open_req,
					       kthread_ctl_list);
			mutex_lock(&req->mux);
			list_del(&req->kthread_ctl_list);
			if (!(req->flags & ECRYPTFS_REQ_ZOMBIE)) {
				dget(req->lower_dentry);
				mntget(req->lower_mnt);
				(*req->lower_file) = dentry_open(
					req->lower_dentry, req->lower_mnt,
					(O_RDWR | O_LARGEFILE), current_cred());
				req->flags |= ECRYPTFS_REQ_PROCESSED;
			}
			wake_up(&req->wait);
			mutex_unlock(&req->mux);
		}
		mutex_unlock(&ecryptfs_kthread_ctl.mux);
	}
out:
	return 0;
}

int ecryptfs_init_kthread(void)
{
	int rc = 0;

	mutex_init(&ecryptfs_kthread_ctl.mux);
	init_waitqueue_head(&ecryptfs_kthread_ctl.wait);
	INIT_LIST_HEAD(&ecryptfs_kthread_ctl.req_list);
	ecryptfs_kthread = kthread_run(&ecryptfs_threadfn, NULL,
				       "ecryptfs-kthread");
	if (IS_ERR(ecryptfs_kthread)) {
		rc = PTR_ERR(ecryptfs_kthread);
		printk(KERN_ERR "%s: Failed to create kernel thread; rc = [%d]"
		       "\n", __func__, rc);
	}
	return rc;
}

void ecryptfs_destroy_kthread(void)
{
	struct ecryptfs_open_req *req;

	mutex_lock(&ecryptfs_kthread_ctl.mux);
	ecryptfs_kthread_ctl.flags |= ECRYPTFS_KTHREAD_ZOMBIE;
	list_for_each_entry(req, &ecryptfs_kthread_ctl.req_list,
			    kthread_ctl_list) {
		mutex_lock(&req->mux);
		req->flags |= ECRYPTFS_REQ_ZOMBIE;
		wake_up(&req->wait);
		mutex_unlock(&req->mux);
	}
	mutex_unlock(&ecryptfs_kthread_ctl.mux);
	kthread_stop(ecryptfs_kthread);
	wake_up(&ecryptfs_kthread_ctl.wait);
}

/**
 * ecryptfs_privileged_open
 * @lower_file: Result of dentry_open by root on lower dentry
 * @lower_dentry: Lower dentry for file to open
 * @lower_mnt: Lower vfsmount for file to open
 *
 * This function gets a r/w file opened againt the lower dentry.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_privileged_open(struct file **lower_file,
			     struct dentry *lower_dentry,
			     struct vfsmount *lower_mnt,
			     const struct cred *cred)
{
	struct ecryptfs_open_req *req;
	int flags = O_LARGEFILE;
	int rc = 0;

	/* Corresponding dput() and mntput() are done when the
	 * persistent file is fput() when the eCryptfs inode is
	 * destroyed. */
	dget(lower_dentry);
	mntget(lower_mnt);
	flags |= IS_RDONLY(lower_dentry->d_inode) ? O_RDONLY : O_RDWR;
	(*lower_file) = dentry_open(lower_dentry, lower_mnt, flags, cred);
	if (!IS_ERR(*lower_file))
		goto out;
	if (flags & O_RDONLY) {
		rc = PTR_ERR((*lower_file));
		goto out;
	}
	req = kmem_cache_alloc(ecryptfs_open_req_cache, GFP_KERNEL);
	if (!req) {
		rc = -ENOMEM;
		goto out;
	}
	mutex_init(&req->mux);
	req->lower_file = lower_file;
	req->lower_dentry = lower_dentry;
	req->lower_mnt = lower_mnt;
	init_waitqueue_head(&req->wait);
	req->flags = 0;
	mutex_lock(&ecryptfs_kthread_ctl.mux);
	if (ecryptfs_kthread_ctl.flags & ECRYPTFS_KTHREAD_ZOMBIE) {
		rc = -EIO;
		mutex_unlock(&ecryptfs_kthread_ctl.mux);
		printk(KERN_ERR "%s: We are in the middle of shutting down; "
		       "aborting privileged request to open lower file\n",
			__func__);
		goto out_free;
	}
	list_add_tail(&req->kthread_ctl_list, &ecryptfs_kthread_ctl.req_list);
	mutex_unlock(&ecryptfs_kthread_ctl.mux);
	wake_up(&ecryptfs_kthread_ctl.wait);
	wait_event(req->wait, (req->flags != 0));
	mutex_lock(&req->mux);
	BUG_ON(req->flags == 0);
	if (req->flags & ECRYPTFS_REQ_DROPPED
	    || req->flags & ECRYPTFS_REQ_ZOMBIE) {
		rc = -EIO;
		printk(KERN_WARNING "%s: Privileged open request dropped\n",
		       __func__);
		goto out_unlock;
	}
	if (IS_ERR(*req->lower_file))
		rc = PTR_ERR(*req->lower_file);
out_unlock:
	mutex_unlock(&req->mux);
out_free:
	kmem_cache_free(ecryptfs_open_req_cache, req);
out:
	return rc;
}
