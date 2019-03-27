/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/ioccom.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/conf.h>
#include <sys/jail.h>
#include <sys/sx.h>

#include <security/mac/mac_framework.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/inode.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

static d_open_t ffs_susp_open;
static d_write_t ffs_susp_rdwr;
static d_ioctl_t ffs_susp_ioctl;

static struct cdevsw ffs_susp_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	ffs_susp_open,
	.d_read =	ffs_susp_rdwr,
	.d_write =	ffs_susp_rdwr,
	.d_ioctl =	ffs_susp_ioctl,
	.d_name =	"ffs_susp",
};

static struct cdev *ffs_susp_dev;
static struct sx ffs_susp_lock;

static int
ffs_susp_suspended(struct mount *mp)
{
	struct ufsmount *ump;

	sx_assert(&ffs_susp_lock, SA_LOCKED);

	ump = VFSTOUFS(mp);
	if ((ump->um_flags & UM_WRITESUSPENDED) != 0)
		return (1);
	return (0);
}

static int
ffs_susp_open(struct cdev *dev __unused, int flags __unused,
    int fmt __unused, struct thread *td __unused)
{

	return (0);
}

static int
ffs_susp_rdwr(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error, i;
	struct vnode *devvp;
	struct mount *mp;
	struct ufsmount *ump;
	struct buf *bp;
	void *base;
	size_t len;
	ssize_t cnt;
	struct fs *fs;

	sx_slock(&ffs_susp_lock);

	error = devfs_get_cdevpriv((void **)&mp);
	if (error != 0) {
		sx_sunlock(&ffs_susp_lock);
		return (ENXIO);
	}

	ump = VFSTOUFS(mp);
	devvp = ump->um_devvp;
	fs = ump->um_fs;

	if (ffs_susp_suspended(mp) == 0) {
		sx_sunlock(&ffs_susp_lock);
		return (ENXIO);
	}

	KASSERT(uio->uio_rw == UIO_READ || uio->uio_rw == UIO_WRITE,
	    ("neither UIO_READ or UIO_WRITE"));
	KASSERT(uio->uio_segflg == UIO_USERSPACE,
	    ("uio->uio_segflg != UIO_USERSPACE"));

	cnt = uio->uio_resid;

	for (i = 0; i < uio->uio_iovcnt; i++) {
		while (uio->uio_iov[i].iov_len) {
			base = uio->uio_iov[i].iov_base;
			len = uio->uio_iov[i].iov_len;
			if (len > fs->fs_bsize)
				len = fs->fs_bsize;
			if (fragoff(fs, uio->uio_offset) != 0 ||
			    fragoff(fs, len) != 0) {
				error = EINVAL;
				goto out;
			}
			error = bread(devvp, btodb(uio->uio_offset), len,
			    NOCRED, &bp);
			if (error != 0)
				goto out;
			if (uio->uio_rw == UIO_WRITE) {
				error = copyin(base, bp->b_data, len);
				if (error != 0) {
					bp->b_flags |= B_INVAL | B_NOCACHE;
					brelse(bp);
					goto out;
				}
				error = bwrite(bp);
				if (error != 0)
					goto out;
			} else {
				error = copyout(bp->b_data, base, len);
				brelse(bp);
				if (error != 0)
					goto out;
			}
			uio->uio_iov[i].iov_base =
			    (char *)uio->uio_iov[i].iov_base + len;
			uio->uio_iov[i].iov_len -= len;
			uio->uio_resid -= len;
			uio->uio_offset += len;
		}
	}

out:
	sx_sunlock(&ffs_susp_lock);

	if (uio->uio_resid < cnt)
		return (0);

	return (error);
}

static int
ffs_susp_suspend(struct mount *mp)
{
	struct ufsmount *ump;
	int error;

	sx_assert(&ffs_susp_lock, SA_XLOCKED);

	if (!ffs_own_mount(mp))
		return (EINVAL);
	if (ffs_susp_suspended(mp))
		return (EBUSY);

	ump = VFSTOUFS(mp);

	/*
	 * Make sure the calling thread is permitted to access the mounted
	 * device.  The permissions can change after we unlock the vnode;
	 * it's harmless.
	 */
	vn_lock(ump->um_devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_ACCESS(ump->um_devvp, VREAD | VWRITE,
	    curthread->td_ucred, curthread);
	VOP_UNLOCK(ump->um_devvp, 0);
	if (error != 0)
		return (error);
#ifdef MAC
	if (mac_mount_check_stat(curthread->td_ucred, mp) != 0)
		return (EPERM);
#endif

	if ((error = vfs_write_suspend(mp, VS_SKIP_UNMOUNT)) != 0)
		return (error);

	UFS_LOCK(ump);
	ump->um_flags |= UM_WRITESUSPENDED;
	UFS_UNLOCK(ump);

	return (0);
}

static void
ffs_susp_unsuspend(struct mount *mp)
{
	struct ufsmount *ump;

	sx_assert(&ffs_susp_lock, SA_XLOCKED);

	/*
	 * XXX: The status is kept per-process; the vfs_write_resume() routine
	 * 	asserts that the resuming thread is the same one that called
	 * 	vfs_write_suspend().  The cdevpriv data, however, is attached
	 * 	to the file descriptor, e.g. is inherited during fork.  Thus,
	 * 	it's possible that the resuming process will be different from
	 * 	the one that started the suspension.
	 *
	 * 	Work around by fooling the check in vfs_write_resume().
	 */
	mp->mnt_susp_owner = curthread;

	vfs_write_resume(mp, 0);
	ump = VFSTOUFS(mp);
	UFS_LOCK(ump);
	ump->um_flags &= ~UM_WRITESUSPENDED;
	UFS_UNLOCK(ump);
	vfs_unbusy(mp);
}

static void
ffs_susp_dtor(void *data)
{
	struct fs *fs;
	struct ufsmount *ump;
	struct mount *mp;
	int error;

	sx_xlock(&ffs_susp_lock);

	mp = (struct mount *)data;
	ump = VFSTOUFS(mp);
	fs = ump->um_fs;

	if (ffs_susp_suspended(mp) == 0) {
		sx_xunlock(&ffs_susp_lock);
		return;
	}

	KASSERT((mp->mnt_kern_flag & MNTK_SUSPEND) != 0,
	    ("MNTK_SUSPEND not set"));

	error = ffs_reload(mp, curthread, FFSR_FORCE | FFSR_UNSUSPEND);
	if (error != 0)
		panic("failed to unsuspend writes on %s", fs->fs_fsmnt);

	ffs_susp_unsuspend(mp);
	sx_xunlock(&ffs_susp_lock);
}

static int
ffs_susp_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flags,
    struct thread *td)
{
	struct mount *mp;
	fsid_t *fsidp;
	int error;

	/*
	 * No suspend inside the jail.  Allowing it would require making
	 * sure that e.g. the devfs ruleset for that jail permits access
	 * to the devvp.
	 */
	if (jailed(td->td_ucred))
		return (EPERM);

	sx_xlock(&ffs_susp_lock);

	switch (cmd) {
	case UFSSUSPEND:
		fsidp = (fsid_t *)addr;
		mp = vfs_getvfs(fsidp);
		if (mp == NULL) {
			error = ENOENT;
			break;
		}
		error = vfs_busy(mp, 0);
		vfs_rel(mp);
		if (error != 0)
			break;
		error = ffs_susp_suspend(mp);
		if (error != 0) {
			vfs_unbusy(mp);
			break;
		}
		error = devfs_set_cdevpriv(mp, ffs_susp_dtor);
		if (error != 0)
			ffs_susp_unsuspend(mp);
		break;
	case UFSRESUME:
		error = devfs_get_cdevpriv((void **)&mp);
		if (error != 0)
			break;
		/*
		 * This calls ffs_susp_dtor, which in turn unsuspends the fs.
		 * The dtor expects to be called without lock held, because
		 * sometimes it's called from here, and sometimes due to the
		 * file being closed or process exiting.
		 */
		sx_xunlock(&ffs_susp_lock);
		devfs_clear_cdevpriv();
		return (0);
	default:
		error = ENXIO;
		break;
	}

	sx_xunlock(&ffs_susp_lock);

	return (error);
}

void
ffs_susp_initialize(void)
{

	sx_init(&ffs_susp_lock, "ffs_susp");
	ffs_susp_dev = make_dev(&ffs_susp_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
	    "ufssuspend");
}

void
ffs_susp_uninitialize(void)
{

	destroy_dev(ffs_susp_dev);
	sx_destroy(&ffs_susp_lock);
}
