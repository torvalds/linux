/*-
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
 */

#include "am.h"

extern qelem mfhead;
qelem mfhead = { &mfhead, &mfhead };

int mntfs_allocated;

#ifdef notdef
/*
 * This is the default attributes field which
 * is copied into every new node to be created.
 * The individual filesystem fs_init() routines
 * patch the copy to represent the particular
 * details for the relevant filesystem type
 */
static struct fattr gen_fattr = {
	NFDIR,				/* type */
	NFSMODE_DIR | 0555,		/* mode */
	2,				/* nlink */
	0,				/* uid */
	0,				/* gid */
	512,				/* size */
	4096,				/* blocksize */
	0,				/* rdev */
	1,				/* blocks */
	0,				/* fsid */
	0,				/* fileid */
	{ 0, 0 },			/* atime */
	{ 0, 0 },			/* mtime */
	{ 0, 0 },			/* ctime */
};
#endif /* notdef */

mntfs *
dup_mntfs(mntfs *mf)
{
	if (mf->mf_refc == 0) {
		if (mf->mf_cid)
			untimeout(mf->mf_cid);
		mf->mf_cid = 0;
#ifdef notdef
		mf->mf_error = -1;
		mf->mf_flags &= ~MFF_ERROR;
#endif
	}
	mf->mf_refc++;
	return mf;
}

static void
init_mntfs(mntfs *mf, am_ops *ops, am_opts *mo, char *mp, char *info,
    char *auto_opts, char *mopts, char *remopts)
{
	mf->mf_ops = ops;
	mf->mf_fo = mo;
	mf->mf_mount = strdup(mp);
	mf->mf_info = strdup(info);
	mf->mf_auto = strdup(auto_opts);
	mf->mf_mopts = strdup(mopts);
	mf->mf_remopts = strdup(remopts);
	mf->mf_refc = 1;
	mf->mf_flags = 0;
	mf->mf_error = -1;
	mf->mf_cid = 0;
	mf->mf_private = 0;
	mf->mf_prfree = 0;
#ifdef notdef
	mf->mf_attr.status = NFS_OK;
	mf->mf_fattr = gen_fattr;
	mf->mf_fattr.fsid = 42;
	mf->mf_fattr.fileid = 0;
	mf->mf_fattr.atime.seconds = clocktime();
	mf->mf_fattr.atime.useconds = 0;
	mf->mf_fattr.mtime = mf->mf_fattr.ctime = mf->mf_fattr.atime;
#endif

	if (ops->ffserver)
		mf->mf_server = (*ops->ffserver)(mf);
	else
		mf->mf_server = 0;
}

static mntfs *
alloc_mntfs(am_ops *ops, am_opts *mo, char *mp, char *info,
    char *auto_opts, char *mopts, char *remopts)
{
	mntfs *mf = ALLOC(mntfs);
	init_mntfs(mf, ops, mo, mp, info, auto_opts, mopts, remopts);
	ins_que(&mf->mf_q, &mfhead);
	mntfs_allocated++;

	return mf;
}

mntfs *
find_mntfs(am_ops *ops, am_opts *mo, char *mp, char *info,
    char *auto_opts, char *mopts, char *remopts)
{
	mntfs *mf;

#ifdef DEBUG
	dlog("Locating mntfs reference to %s", mp);
#endif /* DEBUG */
	ITER(mf, mntfs, &mfhead) {
		if (STREQ(mf->mf_mount, mp)) {
			/*
			 * Handle cases where error ops are involved
			 */
			if (ops == &efs_ops) {
				/*
				 * If the existing ops are not efs_ops
				 * then continue...
				 */
				if (mf->mf_ops != &efs_ops)
					continue;
			} else /* ops != &efs_ops */ {
				/*
				 * If the existing ops are efs_ops
				 * then continue...
				 */
				if (mf->mf_ops == &efs_ops)
					continue;
			}

			if ((mf->mf_flags & MFF_RESTART) && amd_state == Run) {
				/*
				 * Restart a previously mounted filesystem.
				 */
				mntfs *mf2 = alloc_mntfs(&ifs_ops, mo, mp, info, auto_opts, mopts, remopts);
#ifdef DEBUG
				dlog("Restarting filesystem %s", mf->mf_mount);
#endif /* DEBUG */
				/*
				 * Remember who we are restarting
				 */
				mf2->mf_private = dup_mntfs(mf);
				mf2->mf_prfree = free_mntfs;
				return mf2;
			}
			mf->mf_fo = mo;
			if (!(mf->mf_flags & (MFF_MOUNTED|MFF_MOUNTING|MFF_UNMOUNTING))) {
				fserver *fs;
				mf->mf_flags &= ~MFF_ERROR;
				mf->mf_error = -1;
				mf->mf_auto = strealloc(mf->mf_auto, auto_opts);
				mf->mf_mopts = strealloc(mf->mf_mopts, mopts);
				mf->mf_remopts = strealloc(mf->mf_remopts, remopts);
				mf->mf_info = strealloc(mf->mf_info, info);
				if (mf->mf_private && mf->mf_prfree) {
					(*mf->mf_prfree)(mf->mf_private);
					mf->mf_private = 0;
				}
				fs = ops->ffserver ? (*ops->ffserver)(mf) : (fserver *) 0;
				if (mf->mf_server)
					free_srvr(mf->mf_server);
				mf->mf_server = fs;
			}
			return dup_mntfs(mf);
		}
	}

	return alloc_mntfs(ops, mo, mp, info, auto_opts, mopts, remopts);
}

mntfs *
new_mntfs()
{
	return alloc_mntfs(&efs_ops, (am_opts *) 0, "//nil//", ".", "", "", "");
}

static void
uninit_mntfs(mntfs *mf, int rmd)
{
	free(mf->mf_auto);
	free(mf->mf_mopts);
	free(mf->mf_remopts);
	free(mf->mf_info);
	if (mf->mf_private && mf->mf_prfree)
		(*mf->mf_prfree)(mf->mf_private);
	/*
	 * Clean up any directories that were made
	 */
	if (rmd && (mf->mf_flags & MFF_MKMNT))
		rmdirs(mf->mf_mount);
	free(mf->mf_mount);

	/*
	 * Clean up the file server
	 */
	if (mf->mf_server)
		free_srvr(mf->mf_server);

	/*
	 * Don't do a callback on this mount
	 */
	if (mf->mf_cid) {
		untimeout(mf->mf_cid);
		mf->mf_cid = 0;
	}
}

static void
discard_mntfs(void *arg)
{
	mntfs *mf = arg;

	rem_que(&mf->mf_q);
	/*
	 * Free memory
	 */
	uninit_mntfs(mf, TRUE);
	free(mf);

	--mntfs_allocated;
}

void
flush_mntfs(void)
{
	mntfs *mf;

	mf = FIRST(mntfs, &mfhead);
	while (mf != HEAD(mntfs, &mfhead)) {
		mntfs *mf2 = mf;
		mf = NEXT(mntfs, mf);
		if (mf2->mf_refc == 0 && mf2->mf_cid)
			discard_mntfs(mf2);
	}
}

void
free_mntfs(void *arg)
{
	mntfs *mf = arg;

	if (--mf->mf_refc == 0) {
		if (mf->mf_flags & MFF_MOUNTED) {
			int quoted;
			mf->mf_flags &= ~MFF_MOUNTED;

			/*
			 * Record for posterity
			 */
			quoted = strchr(mf->mf_info, ' ') != 0;	/* cheap */
			plog(XLOG_INFO, "%s%s%s %sed fstype %s from %s",
				quoted ? "\"" : "",
				mf->mf_info,
				quoted ? "\"" : "",
				mf->mf_error ? "discard" : "unmount",
				mf->mf_ops->fs_type, mf->mf_mount);
		}

		if (mf->mf_ops->fs_flags & FS_DISCARD) {
#ifdef DEBUG
			dlog("Immediately discarding mntfs for %s", mf->mf_mount);
#endif /* DEBUG */
			discard_mntfs(mf);
		} else {
#ifdef DEBUG
			if (mf->mf_flags & MFF_RESTART) {
				dlog("Discarding remount hook for %s", mf->mf_mount);
			} else {
				dlog("Discarding last mntfs reference to %s fstype %s",
					mf->mf_mount, mf->mf_ops->fs_type);
			}
			if (mf->mf_flags & (MFF_MOUNTED|MFF_MOUNTING|MFF_UNMOUNTING))
				dlog("mntfs reference for %s still active", mf->mf_mount);
#endif /* DEBUG */
			mf->mf_cid = timeout(ALLOWED_MOUNT_TIME,
			    discard_mntfs, mf);
		}
	}
}

mntfs *
realloc_mntfs(mntfs *mf, am_ops *ops, am_opts *mo, char *mp,
    char *info, char *auto_opts, char *mopts, char *remopts)
{
	mntfs *mf2;

	if (mf->mf_refc == 1 && mf->mf_ops == &ifs_ops && STREQ(mf->mf_mount, mp)) {
		/*
		 * If we are inheriting then just return
		 * the same node...
		 */
		return mf;
	}

	/*
	 * Re-use the existing mntfs if it is mounted.
	 * This traps a race in nfsx.
	 */
	if (mf->mf_ops != &efs_ops &&
			(mf->mf_flags & MFF_MOUNTED) &&
			!FSRV_ISDOWN(mf->mf_server)) {
		mf->mf_fo = mo;
		return mf;
	}

	mf2 = find_mntfs(ops, mo, mp, info, auto_opts, mopts, remopts);
	free_mntfs(mf);
	return mf2;
}
