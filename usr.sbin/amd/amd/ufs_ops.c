/*	$OpenBSD: ufs_ops.c,v 1.10 2014/10/26 03:28:41 guenther Exp $	*/

/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)ufs_ops.c	8.1 (Berkeley) 6/6/93
 */

#include "am.h"

#ifdef HAS_UFS

#include <sys/stat.h>

/*
 * UN*X file system
 */

/*
 * UFS needs local filesystem and device.
 */
static char *
ufs_match(am_opts *fo)
{
	if (!fo->opt_dev) {
		plog(XLOG_USER, "ufs: no device specified");
		return 0;
	}

#ifdef DEBUG
	dlog("UFS: mounting device \"%s\" on \"%s\"",
		fo->opt_dev, fo->opt_fs);
#endif /* DEBUG */

	/*
	 * Determine magic cookie to put in mtab
	 */
	return strdup(fo->opt_dev);
}

static int
mount_ufs(char *dir, char *fs_name, char *opts)
{
	struct ufs_args ufs_args;
	struct mntent mnt;
	int flags;

	/*
	 * Figure out the name of the file system type.
	 */
	const char *type = MOUNT_FFS;

	bzero(&ufs_args, sizeof(ufs_args));	/* Paranoid */

	/*
	 * Fill in the mount structure
	 */
	mnt.mnt_dir = dir;
	mnt.mnt_fsname = fs_name;
	mnt.mnt_type = "ffs";
	mnt.mnt_opts = opts;
	mnt.mnt_freq = 1;
	mnt.mnt_passno = 2;

	flags = compute_mount_flags(&mnt);

	ufs_args.fspec = fs_name;

	/*
	 * Call generic mount routine
	 */
	return mount_fs(&mnt, flags, (caddr_t) &ufs_args, 0, type);
}

static int
ufs_fmount(mntfs *mf)
{
	int error;

	error = mount_ufs(mf->mf_mount, mf->mf_info, mf->mf_mopts);
	if (error) {
		errno = error;
		plog(XLOG_ERROR, "mount_ufs: %m");
		return error;
	}

	return 0;
}

static int
ufs_fumount(mntfs *mf)
{
	return umount_fs(mf->mf_mount);
}

/*
 * Ops structure
 */
am_ops ufs_ops = {
	"ufs",
	ufs_match,
	0, /* ufs_init */
	auto_fmount,
	ufs_fmount,
	auto_fumount,
	ufs_fumount,
	efs_lookuppn,
	efs_readdir,
	0, /* ufs_readlink */
	0, /* ufs_mounted */
	0, /* ufs_umounted */
	find_afs_srvr,
#ifdef FLUSH_KERNEL_NAME_CACHE
	FS_MKMNT|FS_NOTIMEOUT|FS_UBACKGROUND|FS_AMQINFO
#else /* FLUSH_KERNEL_NAME_CACHE */
	FS_MKMNT|FS_NOTIMEOUT|FS_UBACKGROUND|FS_AMQINFO
#endif /* FLUSH_KERNEL_NAME_CACHE */
};

#endif /* HAS_UFS */
