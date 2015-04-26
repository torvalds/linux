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

struct ecryptfs_open_req {
	struct file **lower_file;
	struct path path;
	struct completion done;
	struct list_head kthread_ctl_list;
};

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
 * the lower file with RW permissions.
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
			list_del(&req->kthread_ctl_list);
			*req->lower_file = dentry_open(&req->path,
				(O_RDWR | O_LARGEFILE), current_cred());
			complete(&req->done);
		}
		mutex_unlock(&ecryptfs_kthread_ctl.mux);
	}
out:
	return 0;
}

int __init ecryptfs_init_kthread(void)
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
	struct ecryptfs_open_req *req, *tmp;

	mutex_lock(&ecryptfs_kthread_ctl.mux);
	ecryptfs_kthread_ctl.flags |= ECRYPTFS_KTHREAD_ZOMBIE;
	list_for_each_entry_safe(req, tmp, &ecryptfs_kthread_ctl.req_list,
				 kthread_ctl_list) {
		list_del(&req->kthread_ctl_list);
		*req->lower_file = ERR_PTR(-EIO);
		complete(&req->done);
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
	struct ecryptfs_open_req req;
	int flags = O_LARGEFILE;
	int rc = 0;

	init_completion(&req.done);
	req.lower_file = lower_file;
	req.path.dentry = lower_dentry;
	req.path.mnt = lower_mnt;

	/* Corresponding dput() and mntput() are done when the
	 * lower file is fput() when all eCryptfs files for the inode are
	 * released. */
	flags |= IS_RDONLY(d_inode(lower_dentry)) ? O_RDONLY : O_RDWR;
	(*lower_file) = dentry_open(&req.path, flags, cred);
	if (!IS_ERR(*lower_file))
		goto out;
	if ((flags & O_ACCMODE) == O_RDONLY) {
		rc = PTR_ERR((*lower_file));
		goto out;
	}
	mutex_lock(&ecryptfs_kthread_ctl.mux);
	if (ecryptfs_kthread_ctl.flags & ECRYPTFS_KTHREAD_ZOMBIE) {
		rc = -EIO;
		mutex_unlock(&ecryptfs_kthread_ctl.mux);
		printk(KERN_ERR "%s: We are in the middle of shutting down; "
		       "aborting privileged request to open lower file\n",
			__func__);
		goto out;
	}
	list_add_tail(&req.kthread_ctl_list, &ecryptfs_kthread_ctl.req_list);
	mutex_unlock(&ecryptfs_kthread_ctl.mux);
	wake_up(&ecryptfs_kthread_ctl.wait);
	wait_for_completion(&req.done);
	if (IS_ERR(*lower_file))
		rc = PTR_ERR(*lower_file);
out:
	return rc;
}
