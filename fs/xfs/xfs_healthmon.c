// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2024-2026 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trace.h"
#include "xfs_ag.h"
#include "xfs_btree.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_quota_defs.h"
#include "xfs_rtgroup.h"
#include "xfs_healthmon.h"

#include <linux/anon_inodes.h>
#include <linux/eventpoll.h>
#include <linux/poll.h>

/*
 * Live Health Monitoring
 * ======================
 *
 * Autonomous self-healing of XFS filesystems requires a means for the kernel
 * to send filesystem health events to a monitoring daemon in userspace.  To
 * accomplish this, we establish a thread_with_file kthread object to handle
 * translating internal events about filesystem health into a format that can
 * be parsed easily by userspace.  When those internal events occur, the core
 * filesystem code calls this health monitor to convey the events to userspace.
 * Userspace reads events from the file descriptor returned by the ioctl.
 *
 * The healthmon abstraction has a weak reference to the host filesystem mount
 * so that the queueing and processing of the events do not pin the mount and
 * cannot slow down the main filesystem.  The healthmon object can exist past
 * the end of the filesystem mount.
 */

/* sign of a detached health monitor */
#define DETACHED_MOUNT_COOKIE		((uintptr_t)0)

/* spinlock for atomically updating xfs_mount <-> xfs_healthmon pointers */
static DEFINE_SPINLOCK(xfs_healthmon_lock);

/* Grab a reference to the healthmon object for a given mount, if any. */
static struct xfs_healthmon *
xfs_healthmon_get(
	struct xfs_mount		*mp)
{
	struct xfs_healthmon		*hm;

	rcu_read_lock();
	hm = mp->m_healthmon;
	if (hm && !refcount_inc_not_zero(&hm->ref))
		hm = NULL;
	rcu_read_unlock();

	return hm;
}

/*
 * Release the reference to a healthmon object.  If there are no more holders,
 * free the health monitor after an RCU grace period to eliminate possibility
 * of races with xfs_healthmon_get.
 */
static void
xfs_healthmon_put(
	struct xfs_healthmon		*hm)
{
	if (refcount_dec_and_test(&hm->ref))
		kfree_rcu_mightsleep(hm);
}

/* Attach a health monitor to an xfs_mount.  Only one allowed at a time. */
STATIC int
xfs_healthmon_attach(
	struct xfs_mount	*mp,
	struct xfs_healthmon	*hm)
{
	spin_lock(&xfs_healthmon_lock);
	if (mp->m_healthmon != NULL) {
		spin_unlock(&xfs_healthmon_lock);
		return -EEXIST;
	}

	refcount_inc(&hm->ref);
	mp->m_healthmon = hm;
	hm->mount_cookie = (uintptr_t)mp->m_super;
	spin_unlock(&xfs_healthmon_lock);

	return 0;
}

/* Detach a xfs mount from a specific healthmon instance. */
STATIC void
xfs_healthmon_detach(
	struct xfs_healthmon	*hm)
{
	spin_lock(&xfs_healthmon_lock);
	if (hm->mount_cookie == DETACHED_MOUNT_COOKIE) {
		spin_unlock(&xfs_healthmon_lock);
		return;
	}

	XFS_M((struct super_block *)hm->mount_cookie)->m_healthmon = NULL;
	hm->mount_cookie = DETACHED_MOUNT_COOKIE;
	spin_unlock(&xfs_healthmon_lock);

	xfs_healthmon_put(hm);
}

/* Detach the xfs mount from this healthmon instance. */
void
xfs_healthmon_unmount(
	struct xfs_mount		*mp)
{
	struct xfs_healthmon		*hm = xfs_healthmon_get(mp);

	if (!hm)
		return;

	xfs_healthmon_detach(hm);
	xfs_healthmon_put(hm);
}

STATIC ssize_t
xfs_healthmon_read_iter(
	struct kiocb		*iocb,
	struct iov_iter		*to)
{
	return -EIO;
}

/* Free the health monitoring information. */
STATIC int
xfs_healthmon_release(
	struct inode		*inode,
	struct file		*file)
{
	struct xfs_healthmon	*hm = file->private_data;

	/*
	 * We might be closing the healthmon file before the filesystem
	 * unmounts, because userspace processes can terminate at any time and
	 * for any reason.  Null out xfs_mount::m_healthmon so that another
	 * process can create another health monitor file.
	 */
	xfs_healthmon_detach(hm);

	xfs_healthmon_put(hm);
	return 0;
}

/* Validate ioctl parameters. */
static inline bool
xfs_healthmon_validate(
	const struct xfs_health_monitor	*hmo)
{
	if (hmo->flags)
		return false;
	if (hmo->format)
		return false;
	if (memchr_inv(&hmo->pad, 0, sizeof(hmo->pad)))
		return false;
	return true;
}

/* Emit some data about the health monitoring fd. */
static void
xfs_healthmon_show_fdinfo(
	struct seq_file		*m,
	struct file		*file)
{
	struct xfs_healthmon	*hm = file->private_data;

	seq_printf(m, "state:\t%s\ndev:\t%d:%d\n",
			hm->mount_cookie == DETACHED_MOUNT_COOKIE ?
				"dead" : "alive",
			MAJOR(hm->dev), MINOR(hm->dev));
}

static const struct file_operations xfs_healthmon_fops = {
	.owner		= THIS_MODULE,
	.show_fdinfo	= xfs_healthmon_show_fdinfo,
	.read_iter	= xfs_healthmon_read_iter,
	.release	= xfs_healthmon_release,
};

/*
 * Create a health monitoring file.  Returns an index to the fd table or a
 * negative errno.
 */
long
xfs_ioc_health_monitor(
	struct file			*file,
	struct xfs_health_monitor __user *arg)
{
	struct xfs_health_monitor	hmo;
	struct xfs_healthmon		*hm;
	struct xfs_inode		*ip = XFS_I(file_inode(file));
	struct xfs_mount		*mp = ip->i_mount;
	int				ret;

	/*
	 * The only intended user of the health monitoring system should be the
	 * xfs_healer daemon running on behalf of the whole filesystem in the
	 * initial user namespace.  IOWs, we don't allow unprivileged userspace
	 * (they can use fsnotify) nor do we allow containers.
	 */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (ip->i_ino != mp->m_sb.sb_rootino)
		return -EPERM;
	if (current_user_ns() != &init_user_ns)
		return -EPERM;

	if (copy_from_user(&hmo, arg, sizeof(hmo)))
		return -EFAULT;

	if (!xfs_healthmon_validate(&hmo))
		return -EINVAL;

	hm = kzalloc(sizeof(*hm), GFP_KERNEL);
	if (!hm)
		return -ENOMEM;
	hm->dev = mp->m_super->s_dev;
	refcount_set(&hm->ref, 1);

	/*
	 * Try to attach this health monitor to the xfs_mount.  The monitor is
	 * considered live and will receive events if this succeeds.
	 */
	ret = xfs_healthmon_attach(mp, hm);
	if (ret)
		goto out_hm;

	/*
	 * Create the anonymous file and install a fd for it.  If it succeeds,
	 * the file owns hm and can go away at any time, so we must not access
	 * it again.  This must go last because we can't undo a fd table
	 * installation.
	 */
	ret = anon_inode_getfd("xfs_healthmon", &xfs_healthmon_fops, hm,
			O_CLOEXEC | O_RDONLY);
	if (ret < 0)
		goto out_mp;

	return ret;

out_mp:
	xfs_healthmon_detach(hm);
out_hm:
	ASSERT(refcount_read(&hm->ref) == 1);
	xfs_healthmon_put(hm);
	return ret;
}
