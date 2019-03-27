/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007-2009 Google Inc. and Amit Singh
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Google Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright (C) 2005 Csaba Henk.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/capsicum.h>
#include <sys/conf.h>
#include <sys/filedesc.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/fcntl.h>

#include "fuse.h"
#include "fuse_param.h"
#include "fuse_node.h"
#include "fuse_ipc.h"
#include "fuse_internal.h"

#include <sys/priv.h>
#include <security/mac/mac_framework.h>

#define FUSE_DEBUG_MODULE VFSOPS
#include "fuse_debug.h"

/* This will do for privilege types for now */
#ifndef PRIV_VFS_FUSE_ALLOWOTHER
#define PRIV_VFS_FUSE_ALLOWOTHER PRIV_VFS_MOUNT_NONUSER
#endif
#ifndef PRIV_VFS_FUSE_MOUNT_NONUSER
#define PRIV_VFS_FUSE_MOUNT_NONUSER PRIV_VFS_MOUNT_NONUSER
#endif
#ifndef PRIV_VFS_FUSE_SYNC_UNMOUNT
#define PRIV_VFS_FUSE_SYNC_UNMOUNT PRIV_VFS_MOUNT_NONUSER
#endif

static vfs_mount_t fuse_vfsop_mount;
static vfs_unmount_t fuse_vfsop_unmount;
static vfs_root_t fuse_vfsop_root;
static vfs_statfs_t fuse_vfsop_statfs;

struct vfsops fuse_vfsops = {
	.vfs_mount = fuse_vfsop_mount,
	.vfs_unmount = fuse_vfsop_unmount,
	.vfs_root = fuse_vfsop_root,
	.vfs_statfs = fuse_vfsop_statfs,
};

SYSCTL_INT(_vfs_fusefs, OID_AUTO, init_backgrounded, CTLFLAG_RD,
    SYSCTL_NULL_INT_PTR, 1, "indicate async handshake");
static int fuse_enforce_dev_perms = 0;

SYSCTL_INT(_vfs_fusefs, OID_AUTO, enforce_dev_perms, CTLFLAG_RW,
    &fuse_enforce_dev_perms, 0,
    "enforce fuse device permissions for secondary mounts");
static unsigned sync_unmount = 1;

SYSCTL_UINT(_vfs_fusefs, OID_AUTO, sync_unmount, CTLFLAG_RW,
    &sync_unmount, 0, "specify when to use synchronous unmount");

MALLOC_DEFINE(M_FUSEVFS, "fuse_filesystem", "buffer for fuse vfs layer");

static int
fuse_getdevice(const char *fspec, struct thread *td, struct cdev **fdevp)
{
	struct nameidata nd, *ndp = &nd;
	struct vnode *devvp;
	struct cdev *fdev;
	int err;

	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible disk device.
	 */

	NDINIT(ndp, LOOKUP, FOLLOW, UIO_SYSSPACE, fspec, td);
	if ((err = namei(ndp)) != 0)
		return err;
	NDFREE(ndp, NDF_ONLY_PNBUF);
	devvp = ndp->ni_vp;

	if (devvp->v_type != VCHR) {
		vrele(devvp);
		return ENXIO;
	}
	fdev = devvp->v_rdev;
	dev_ref(fdev);

	if (fuse_enforce_dev_perms) {
		/*
	         * Check if mounter can open the fuse device.
	         *
	         * This has significance only if we are doing a secondary mount
	         * which doesn't involve actually opening fuse devices, but we
	         * still want to enforce the permissions of the device (in
	         * order to keep control over the circle of fuse users).
	         *
	         * (In case of primary mounts, we are either the superuser so
	         * we can do anything anyway, or we can mount only if the
	         * device is already opened by us, ie. we are permitted to open
	         * the device.)
	         */
#if 0
#ifdef MAC
		err = mac_check_vnode_open(td->td_ucred, devvp, VREAD | VWRITE);
		if (!err)
#endif
#endif /* 0 */
			err = VOP_ACCESS(devvp, VREAD | VWRITE, td->td_ucred, td);
		if (err) {
			vrele(devvp);
			dev_rel(fdev);
			return err;
		}
	}
	/*
	 * according to coda code, no extra lock is needed --
	 * although in sys/vnode.h this field is marked "v"
	 */
	vrele(devvp);

	if (!fdev->si_devsw ||
	    strcmp("fuse", fdev->si_devsw->d_name)) {
		dev_rel(fdev);
		return ENXIO;
	}
	*fdevp = fdev;

	return 0;
}

#define FUSE_FLAGOPT(fnam, fval) do {				\
	vfs_flagopt(opts, #fnam, &mntopts, fval);		\
	vfs_flagopt(opts, "__" #fnam, &__mntopts, fval);	\
} while (0)

static int
fuse_vfsop_mount(struct mount *mp)
{
	int err;

	uint64_t mntopts, __mntopts;
	uint32_t max_read;
	int daemon_timeout;
	int fd;

	size_t len;

	struct cdev *fdev;
	struct fuse_data *data;
	struct thread *td;
	struct file *fp, *fptmp;
	char *fspec, *subtype;
	struct vfsoptlist *opts;

	subtype = NULL;
	max_read = ~0;
	err = 0;
	mntopts = 0;
	__mntopts = 0;
	td = curthread;

	fuse_trace_printf_vfsop();

	if (mp->mnt_flag & MNT_UPDATE)
		return EOPNOTSUPP;

	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_SYNCHRONOUS;
	mp->mnt_data = NULL;
	MNT_IUNLOCK(mp);
	/* Get the new options passed to mount */
	opts = mp->mnt_optnew;

	if (!opts)
		return EINVAL;

	/* `fspath' contains the mount point (eg. /mnt/fuse/sshfs); REQUIRED */
	if (!vfs_getopts(opts, "fspath", &err))
		return err;

	/* `from' contains the device name (eg. /dev/fuse0); REQUIRED */
	fspec = vfs_getopts(opts, "from", &err);
	if (!fspec)
		return err;

	/* `fd' contains the filedescriptor for this session; REQUIRED */
	if (vfs_scanopt(opts, "fd", "%d", &fd) != 1)
		return EINVAL;

	err = fuse_getdevice(fspec, td, &fdev);
	if (err != 0)
		return err;

	/*
	 * With the help of underscored options the mount program
	 * can inform us from the flags it sets by default
	 */
	FUSE_FLAGOPT(allow_other, FSESS_DAEMON_CAN_SPY);
	FUSE_FLAGOPT(push_symlinks_in, FSESS_PUSH_SYMLINKS_IN);
	FUSE_FLAGOPT(default_permissions, FSESS_DEFAULT_PERMISSIONS);
	FUSE_FLAGOPT(no_attrcache, FSESS_NO_ATTRCACHE);
	FUSE_FLAGOPT(no_readahed, FSESS_NO_READAHEAD);
	FUSE_FLAGOPT(no_datacache, FSESS_NO_DATACACHE);
	FUSE_FLAGOPT(no_namecache, FSESS_NO_NAMECACHE);
	FUSE_FLAGOPT(no_mmap, FSESS_NO_MMAP);
	FUSE_FLAGOPT(brokenio, FSESS_BROKENIO);

	(void)vfs_scanopt(opts, "max_read=", "%u", &max_read);
	if (vfs_scanopt(opts, "timeout=", "%u", &daemon_timeout) == 1) {
		if (daemon_timeout < FUSE_MIN_DAEMON_TIMEOUT)
			daemon_timeout = FUSE_MIN_DAEMON_TIMEOUT;
		else if (daemon_timeout > FUSE_MAX_DAEMON_TIMEOUT)
			daemon_timeout = FUSE_MAX_DAEMON_TIMEOUT;
	} else {
		daemon_timeout = FUSE_DEFAULT_DAEMON_TIMEOUT;
	}
	subtype = vfs_getopts(opts, "subtype=", &err);

	FS_DEBUG2G("mntopts 0x%jx\n", (uintmax_t)mntopts);

	err = fget(td, fd, &cap_read_rights, &fp);
	if (err != 0) {
		FS_DEBUG("invalid or not opened device: data=%p\n", data);
		goto out;
	}
	fptmp = td->td_fpop;
	td->td_fpop = fp;
	err = devfs_get_cdevpriv((void **)&data);
	td->td_fpop = fptmp;
	fdrop(fp, td);
	FUSE_LOCK();
	if (err != 0 || data == NULL || data->mp != NULL) {
		FS_DEBUG("invalid or not opened device: data=%p data.mp=%p\n",
		    data, data != NULL ? data->mp : NULL);
		err = ENXIO;
		FUSE_UNLOCK();
		goto out;
	}
	if (fdata_get_dead(data)) {
		FS_DEBUG("device is dead during mount: data=%p\n", data);
		err = ENOTCONN;
		FUSE_UNLOCK();
		goto out;
	}
	/* Sanity + permission checks */
	if (!data->daemoncred)
		panic("fuse daemon found, but identity unknown");
	if (mntopts & FSESS_DAEMON_CAN_SPY)
		err = priv_check(td, PRIV_VFS_FUSE_ALLOWOTHER);
	if (err == 0 && td->td_ucred->cr_uid != data->daemoncred->cr_uid)
		/* are we allowed to do the first mount? */
		err = priv_check(td, PRIV_VFS_FUSE_MOUNT_NONUSER);
	if (err) {
		FUSE_UNLOCK();
		goto out;
	}
	data->ref++;
	data->mp = mp;
	data->dataflags |= mntopts;
	data->max_read = max_read;
	data->daemon_timeout = daemon_timeout;
	FUSE_UNLOCK();

	vfs_getnewfsid(mp);
	MNT_ILOCK(mp);
	mp->mnt_data = data;
	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_USES_BCACHE;
	MNT_IUNLOCK(mp);
	/* We need this here as this slot is used by getnewvnode() */
	mp->mnt_stat.f_iosize = maxbcachebuf;
	if (subtype) {
		strlcat(mp->mnt_stat.f_fstypename, ".", MFSNAMELEN);
		strlcat(mp->mnt_stat.f_fstypename, subtype, MFSNAMELEN);
	}
	copystr(fspec, mp->mnt_stat.f_mntfromname, MNAMELEN - 1, &len);
	bzero(mp->mnt_stat.f_mntfromname + len, MNAMELEN - len);
	FS_DEBUG2G("mp %p: %s\n", mp, mp->mnt_stat.f_mntfromname);

	/* Now handshaking with daemon */
	fuse_internal_send_init(data, td);

out:
	if (err) {
		FUSE_LOCK();
		if (data->mp == mp) {
			/*
			 * Destroy device only if we acquired reference to
			 * it
			 */
			FS_DEBUG("mount failed, destroy device: data=%p mp=%p"
			      " err=%d\n",
			    data, mp, err);
			data->mp = NULL;
			fdata_trydestroy(data);
		}
		FUSE_UNLOCK();
		dev_rel(fdev);
	}
	return err;
}

static int
fuse_vfsop_unmount(struct mount *mp, int mntflags)
{
	int err = 0;
	int flags = 0;

	struct cdev *fdev;
	struct fuse_data *data;
	struct fuse_dispatcher fdi;
	struct thread *td = curthread;

	fuse_trace_printf_vfsop();

	if (mntflags & MNT_FORCE) {
		flags |= FORCECLOSE;
	}
	data = fuse_get_mpdata(mp);
	if (!data) {
		panic("no private data for mount point?");
	}
	/* There is 1 extra root vnode reference (mp->mnt_data). */
	FUSE_LOCK();
	if (data->vroot != NULL) {
		struct vnode *vroot = data->vroot;

		data->vroot = NULL;
		FUSE_UNLOCK();
		vrele(vroot);
	} else
		FUSE_UNLOCK();
	err = vflush(mp, 0, flags, td);
	if (err) {
		debug_printf("vflush failed");
		return err;
	}
	if (fdata_get_dead(data)) {
		goto alreadydead;
	}
	fdisp_init(&fdi, 0);
	fdisp_make(&fdi, FUSE_DESTROY, mp, 0, td, NULL);

	err = fdisp_wait_answ(&fdi);
	fdisp_destroy(&fdi);

	fdata_set_dead(data);

alreadydead:
	FUSE_LOCK();
	data->mp = NULL;
	fdev = data->fdev;
	fdata_trydestroy(data);
	FUSE_UNLOCK();

	MNT_ILOCK(mp);
	mp->mnt_data = NULL;
	mp->mnt_flag &= ~MNT_LOCAL;
	MNT_IUNLOCK(mp);

	dev_rel(fdev);

	return 0;
}

static int
fuse_vfsop_root(struct mount *mp, int lkflags, struct vnode **vpp)
{
	struct fuse_data *data = fuse_get_mpdata(mp);
	int err = 0;

	if (data->vroot != NULL) {
		err = vget(data->vroot, lkflags, curthread);
		if (err == 0)
			*vpp = data->vroot;
	} else {
		err = fuse_vnode_get(mp, NULL, FUSE_ROOT_ID, NULL, vpp, NULL,
		    VDIR);
		if (err == 0) {
			FUSE_LOCK();
			MPASS(data->vroot == NULL || data->vroot == *vpp);
			if (data->vroot == NULL) {
				FS_DEBUG("new root vnode\n");
				data->vroot = *vpp;
				FUSE_UNLOCK();
				vref(*vpp);
			} else if (data->vroot != *vpp) {
				FS_DEBUG("root vnode race\n");
				FUSE_UNLOCK();
				VOP_UNLOCK(*vpp, 0);
				vrele(*vpp);
				vrecycle(*vpp);
				*vpp = data->vroot;
			} else
				FUSE_UNLOCK();
		}
	}
	return err;
}

static int
fuse_vfsop_statfs(struct mount *mp, struct statfs *sbp)
{
	struct fuse_dispatcher fdi;
	int err = 0;

	struct fuse_statfs_out *fsfo;
	struct fuse_data *data;

	FS_DEBUG2G("mp %p: %s\n", mp, mp->mnt_stat.f_mntfromname);
	data = fuse_get_mpdata(mp);

	if (!(data->dataflags & FSESS_INITED))
		goto fake;

	fdisp_init(&fdi, 0);
	fdisp_make(&fdi, FUSE_STATFS, mp, FUSE_ROOT_ID, NULL, NULL);
	err = fdisp_wait_answ(&fdi);
	if (err) {
		fdisp_destroy(&fdi);
		if (err == ENOTCONN) {
			/*
	                 * We want to seem a legitimate fs even if the daemon
	                 * is stiff dead... (so that, eg., we can still do path
	                 * based unmounting after the daemon dies).
	                 */
			goto fake;
		}
		return err;
	}
	fsfo = fdi.answ;

	sbp->f_blocks = fsfo->st.blocks;
	sbp->f_bfree = fsfo->st.bfree;
	sbp->f_bavail = fsfo->st.bavail;
	sbp->f_files = fsfo->st.files;
	sbp->f_ffree = fsfo->st.ffree;	/* cast from uint64_t to int64_t */
	sbp->f_namemax = fsfo->st.namelen;
	sbp->f_bsize = fsfo->st.frsize;	/* cast from uint32_t to uint64_t */

	FS_DEBUG("fuse_statfs_out -- blocks: %llu, bfree: %llu, bavail: %llu, "
	      "fil	es: %llu, ffree: %llu, bsize: %i, namelen: %i\n",
	      (unsigned long long)fsfo->st.blocks, 
	      (unsigned long long)fsfo->st.bfree,
	      (unsigned long long)fsfo->st.bavail, 
	      (unsigned long long)fsfo->st.files,
	      (unsigned long long)fsfo->st.ffree, fsfo->st.bsize, 
	      fsfo->st.namelen);

	fdisp_destroy(&fdi);
	return 0;

fake:
	sbp->f_blocks = 0;
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 0;
	sbp->f_ffree = 0;
	sbp->f_namemax = 0;
	sbp->f_bsize = FUSE_DEFAULT_BLOCKSIZE;

	return 0;
}
