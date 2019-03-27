/*-
 *  modified for Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)ufs_lookup.c	8.6 (Berkeley) 4/1/94
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/sysctl.h>

#include <ufs/ufs/dir.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_dir.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/fs.h>

#ifdef INVARIANTS
static int dirchk = 1;
#else
static int dirchk = 0;
#endif

static SYSCTL_NODE(_vfs, OID_AUTO, e2fs, CTLFLAG_RD, 0, "EXT2FS filesystem");
SYSCTL_INT(_vfs_e2fs, OID_AUTO, dircheck, CTLFLAG_RW, &dirchk, 0, "");

/*
   DIRBLKSIZE in ffs is DEV_BSIZE (in most cases 512)
   while it is the native blocksize in ext2fs - thus, a #define
   is no longer appropriate
*/
#undef  DIRBLKSIZ

static u_char ext2_ft_to_dt[] = {
	DT_UNKNOWN,		/* EXT2_FT_UNKNOWN */
	DT_REG,			/* EXT2_FT_REG_FILE */
	DT_DIR,			/* EXT2_FT_DIR */
	DT_CHR,			/* EXT2_FT_CHRDEV */
	DT_BLK,			/* EXT2_FT_BLKDEV */
	DT_FIFO,		/* EXT2_FT_FIFO */
	DT_SOCK,		/* EXT2_FT_SOCK */
	DT_LNK,			/* EXT2_FT_SYMLINK */
};
#define	FTTODT(ft) \
    ((ft) < nitems(ext2_ft_to_dt) ? ext2_ft_to_dt[(ft)] : DT_UNKNOWN)

static u_char dt_to_ext2_ft[] = {
	EXT2_FT_UNKNOWN,	/* DT_UNKNOWN */
	EXT2_FT_FIFO,		/* DT_FIFO */
	EXT2_FT_CHRDEV,		/* DT_CHR */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_DIR,		/* DT_DIR */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_BLKDEV,		/* DT_BLK */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_REG_FILE,	/* DT_REG */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_SYMLINK,	/* DT_LNK */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_SOCK,		/* DT_SOCK */
	EXT2_FT_UNKNOWN,	/* unused */
	EXT2_FT_UNKNOWN,	/* DT_WHT */
};
#define	DTTOFT(dt) \
    ((dt) < nitems(dt_to_ext2_ft) ? dt_to_ext2_ft[(dt)] : EXT2_FT_UNKNOWN)

static int	ext2_dirbadentry(struct vnode *dp, struct ext2fs_direct_2 *de,
		    int entryoffsetinblock);
static int	ext2_is_dot_entry(struct componentname *cnp);
static int	ext2_lookup_ino(struct vnode *vdp, struct vnode **vpp,
		    struct componentname *cnp, ino_t *dd_ino);

static int
ext2_is_dot_entry(struct componentname *cnp)
{
	if (cnp->cn_namelen <= 2 && cnp->cn_nameptr[0] == '.' &&
	    (cnp->cn_nameptr[1] == '.' || cnp->cn_nameptr[1] == '\0'))
		return (1);
	return (0);
}

/*
 * Vnode op for reading directories.
 */
int
ext2_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct buf *bp;
	struct inode *ip;
	struct ext2fs_direct_2 *dp, *edp;
	u_long *cookies;
	struct dirent dstdp;
	off_t offset, startoffset;
	size_t readcnt, skipcnt;
	ssize_t startresid;
	u_int ncookies;
	int DIRBLKSIZ = VTOI(ap->a_vp)->i_e2fs->e2fs_bsize;
	int error;

	if (uio->uio_offset < 0)
		return (EINVAL);
	ip = VTOI(vp);
	if (ap->a_ncookies != NULL) {
		if (uio->uio_resid < 0)
			ncookies = 0;
		else
			ncookies = uio->uio_resid;
		if (uio->uio_offset >= ip->i_size)
			ncookies = 0;
		else if (ip->i_size - uio->uio_offset < ncookies)
			ncookies = ip->i_size - uio->uio_offset;
		ncookies = ncookies / (offsetof(struct ext2fs_direct_2,
		    e2d_namlen) + 4) + 1;
		cookies = malloc(ncookies * sizeof(*cookies), M_TEMP, M_WAITOK);
		*ap->a_ncookies = ncookies;
		*ap->a_cookies = cookies;
	} else {
		ncookies = 0;
		cookies = NULL;
	}
	offset = startoffset = uio->uio_offset;
	startresid = uio->uio_resid;
	error = 0;
	while (error == 0 && uio->uio_resid > 0 &&
	    uio->uio_offset < ip->i_size) {
		error = ext2_blkatoff(vp, uio->uio_offset, NULL, &bp);
		if (error)
			break;
		if (bp->b_offset + bp->b_bcount > ip->i_size)
			readcnt = ip->i_size - bp->b_offset;
		else
			readcnt = bp->b_bcount;
		skipcnt = (size_t)(uio->uio_offset - bp->b_offset) &
		    ~(size_t)(DIRBLKSIZ - 1);
		offset = bp->b_offset + skipcnt;
		dp = (struct ext2fs_direct_2 *)&bp->b_data[skipcnt];
		edp = (struct ext2fs_direct_2 *)&bp->b_data[readcnt];
		while (error == 0 && uio->uio_resid > 0 && dp < edp) {
			if (dp->e2d_reclen <= offsetof(struct ext2fs_direct_2,
			    e2d_namlen) || (caddr_t)dp + dp->e2d_reclen >
			    (caddr_t)edp) {
				error = EIO;
				break;
			}
			/*-
			 * "New" ext2fs directory entries differ in 3 ways
			 * from ufs on-disk ones:
			 * - the name is not necessarily NUL-terminated.
			 * - the file type field always exists and always
			 *   follows the name length field.
			 * - the file type is encoded in a different way.
			 *
			 * "Old" ext2fs directory entries need no special
			 * conversions, since they are binary compatible
			 * with "new" entries having a file type of 0 (i.e.,
			 * EXT2_FT_UNKNOWN).  Splitting the old name length
			 * field didn't make a mess like it did in ufs,
			 * because ext2fs uses a machine-independent disk
			 * layout.
			 */
			dstdp.d_namlen = dp->e2d_namlen;
			dstdp.d_type = FTTODT(dp->e2d_type);
			if (offsetof(struct ext2fs_direct_2, e2d_namlen) +
			    dstdp.d_namlen > dp->e2d_reclen) {
				error = EIO;
				break;
			}
			if (offset < startoffset || dp->e2d_ino == 0)
				goto nextentry;
			dstdp.d_fileno = dp->e2d_ino;
			dstdp.d_reclen = GENERIC_DIRSIZ(&dstdp);
			bcopy(dp->e2d_name, dstdp.d_name, dstdp.d_namlen);
			/* NOTE: d_off is the offset of the *next* entry. */
			dstdp.d_off = offset + dp->e2d_reclen;
			dirent_terminate(&dstdp);
			if (dstdp.d_reclen > uio->uio_resid) {
				if (uio->uio_resid == startresid)
					error = EINVAL;
				else
					error = EJUSTRETURN;
				break;
			}
			/* Advance dp. */
			error = uiomove((caddr_t)&dstdp, dstdp.d_reclen, uio);
			if (error)
				break;
			if (cookies != NULL) {
				KASSERT(ncookies > 0,
				    ("ext2_readdir: cookies buffer too small"));
				*cookies = offset + dp->e2d_reclen;
				cookies++;
				ncookies--;
			}
nextentry:
			offset += dp->e2d_reclen;
			dp = (struct ext2fs_direct_2 *)((caddr_t)dp +
			    dp->e2d_reclen);
		}
		bqrelse(bp);
		uio->uio_offset = offset;
	}
	/* We need to correct uio_offset. */
	uio->uio_offset = offset;
	if (error == EJUSTRETURN)
		error = 0;
	if (ap->a_ncookies != NULL) {
		if (error == 0) {
			ap->a_ncookies -= ncookies;
		} else {
			free(*ap->a_cookies, M_TEMP);
			*ap->a_ncookies = 0;
			*ap->a_cookies = NULL;
		}
	}
	if (error == 0 && ap->a_eofflag)
		*ap->a_eofflag = ip->i_size <= uio->uio_offset;
	return (error);
}

/*
 * Convert a component of a pathname into a pointer to a locked inode.
 * This is a very central and rather complicated routine.
 * If the file system is not maintained in a strict tree hierarchy,
 * this can result in a deadlock situation (see comments in code below).
 *
 * The cnp->cn_nameiop argument is LOOKUP, CREATE, RENAME, or DELETE depending
 * on whether the name is to be looked up, created, renamed, or deleted.
 * When CREATE, RENAME, or DELETE is specified, information usable in
 * creating, renaming, or deleting a directory entry may be calculated.
 * If flag has LOCKPARENT or'ed into it and the target of the pathname
 * exists, lookup returns both the target and its parent directory locked.
 * When creating or renaming and LOCKPARENT is specified, the target may
 * not be ".".  When deleting and LOCKPARENT is specified, the target may
 * be "."., but the caller must check to ensure it does an vrele and vput
 * instead of two vputs.
 *
 * Overall outline of ext2_lookup:
 *
 *	search for name in directory, to found or notfound
 * notfound:
 *	if creating, return locked directory, leaving info on available slots
 *	else return error
 * found:
 *	if at end of path and deleting, return information to allow delete
 *	if at end of path and rewriting (RENAME and LOCKPARENT), lock target
 *	  inode and return info to allow rewrite
 *	if not at end, add name to cache; if at end and neither creating
 *	  nor deleting, add name to cache
 */
int
ext2_lookup(struct vop_cachedlookup_args *ap)
{

	return (ext2_lookup_ino(ap->a_dvp, ap->a_vpp, ap->a_cnp, NULL));
}

static int
ext2_lookup_ino(struct vnode *vdp, struct vnode **vpp, struct componentname *cnp,
    ino_t *dd_ino)
{
	struct inode *dp;		/* inode for directory being searched */
	struct buf *bp;			/* a buffer of directory entries */
	struct ext2fs_direct_2 *ep;	/* the current directory entry */
	int entryoffsetinblock;		/* offset of ep in bp's buffer */
	struct ext2fs_searchslot ss;
	doff_t i_diroff;		/* cached i_diroff value */
	doff_t i_offset;		/* cached i_offset value */
	int numdirpasses;		/* strategy for directory search */
	doff_t endsearch;		/* offset to end directory search */
	doff_t prevoff;			/* prev entry dp->i_offset */
	struct vnode *pdp;		/* saved dp during symlink work */
	struct vnode *tdp;		/* returned by VFS_VGET */
	doff_t enduseful;		/* pointer past last used dir slot */
	u_long bmask;			/* block offset mask */
	int error;
	struct ucred *cred = cnp->cn_cred;
	int flags = cnp->cn_flags;
	int nameiop = cnp->cn_nameiop;
	ino_t ino, ino1;
	int ltype;
	int entry_found = 0;

	int DIRBLKSIZ = VTOI(vdp)->i_e2fs->e2fs_bsize;

	if (vpp != NULL)
		*vpp = NULL;

	dp = VTOI(vdp);
	bmask = VFSTOEXT2(vdp->v_mount)->um_mountp->mnt_stat.f_iosize - 1;
restart:
	bp = NULL;
	ss.slotoffset = -1;

	/*
	 * We now have a segment name to search for, and a directory to search.
	 *
	 * Suppress search for slots unless creating
	 * file and at end of pathname, in which case
	 * we watch for a place to put the new file in
	 * case it doesn't already exist.
	 */
	i_diroff = dp->i_diroff;
	ss.slotstatus = FOUND;
	ss.slotfreespace = ss.slotsize = ss.slotneeded = 0;
	if ((nameiop == CREATE || nameiop == RENAME) &&
	    (flags & ISLASTCN)) {
		ss.slotstatus = NONE;
		ss.slotneeded = EXT2_DIR_REC_LEN(cnp->cn_namelen);
		/*
		 * was ss.slotneeded = (sizeof(struct direct) - MAXNAMLEN +
		 * cnp->cn_namelen + 3) &~ 3;
		 */
	}
	/*
	 * Try to lookup dir entry using htree directory index.
	 *
	 * If we got an error or we want to find '.' or '..' entry,
	 * we will fall back to linear search.
	 */
	if (!ext2_is_dot_entry(cnp) && ext2_htree_has_idx(dp)) {
		numdirpasses = 1;
		entryoffsetinblock = 0;
		switch (ext2_htree_lookup(dp, cnp->cn_nameptr, cnp->cn_namelen,
		    &bp, &entryoffsetinblock, &i_offset, &prevoff,
		    &enduseful, &ss)) {
		case 0:
			ep = (struct ext2fs_direct_2 *)((char *)bp->b_data +
			    (i_offset & bmask));
			goto foundentry;
		case ENOENT:
			i_offset = roundup2(dp->i_size, DIRBLKSIZ);
			goto notfound;
		default:
			/*
			 * Something failed; just fallback to do a linear
			 * search.
			 */
			break;
		}
	}

	/*
	 * If there is cached information on a previous search of
	 * this directory, pick up where we last left off.
	 * We cache only lookups as these are the most common
	 * and have the greatest payoff. Caching CREATE has little
	 * benefit as it usually must search the entire directory
	 * to determine that the entry does not exist. Caching the
	 * location of the last DELETE or RENAME has not reduced
	 * profiling time and hence has been removed in the interest
	 * of simplicity.
	 */
	if (nameiop != LOOKUP || i_diroff == 0 ||
	    i_diroff > dp->i_size) {
		entryoffsetinblock = 0;
		i_offset = 0;
		numdirpasses = 1;
	} else {
		i_offset = i_diroff;
		if ((entryoffsetinblock = i_offset & bmask) &&
		    (error = ext2_blkatoff(vdp, (off_t)i_offset, NULL,
		    &bp)))
			return (error);
		numdirpasses = 2;
		nchstats.ncs_2passes++;
	}
	prevoff = i_offset;
	endsearch = roundup2(dp->i_size, DIRBLKSIZ);
	enduseful = 0;

searchloop:
	while (i_offset < endsearch) {
		/*
		 * If necessary, get the next directory block.
		 */
		if (bp != NULL)
			brelse(bp);
		error = ext2_blkatoff(vdp, (off_t)i_offset, NULL, &bp);
		if (error != 0)
			return (error);

		entryoffsetinblock = 0;
		if (ss.slotstatus == NONE) {
			ss.slotoffset = -1;
			ss.slotfreespace = 0;
		}

		error = ext2_search_dirblock(dp, bp->b_data, &entry_found,
		    cnp->cn_nameptr, cnp->cn_namelen,
		    &entryoffsetinblock, &i_offset, &prevoff,
		    &enduseful, &ss);
		if (error != 0) {
			brelse(bp);
			return (error);
		}
		if (entry_found) {
			ep = (struct ext2fs_direct_2 *)((char *)bp->b_data +
			    (entryoffsetinblock & bmask));
foundentry:
			ino = ep->e2d_ino;
			goto found;
		}
	}
notfound:
	/*
	 * If we started in the middle of the directory and failed
	 * to find our target, we must check the beginning as well.
	 */
	if (numdirpasses == 2) {
		numdirpasses--;
		i_offset = 0;
		endsearch = i_diroff;
		goto searchloop;
	}
	if (bp != NULL)
		brelse(bp);
	/*
	 * If creating, and at end of pathname and current
	 * directory has not been removed, then can consider
	 * allowing file to be created.
	 */
	if ((nameiop == CREATE || nameiop == RENAME) &&
	    (flags & ISLASTCN) && dp->i_nlink != 0) {
		/*
		 * Access for write is interpreted as allowing
		 * creation of files in the directory.
		 */
		if ((error = VOP_ACCESS(vdp, VWRITE, cred, cnp->cn_thread)) != 0)
			return (error);
		/*
		 * Return an indication of where the new directory
		 * entry should be put.  If we didn't find a slot,
		 * then set dp->i_count to 0 indicating
		 * that the new slot belongs at the end of the
		 * directory. If we found a slot, then the new entry
		 * can be put in the range from dp->i_offset to
		 * dp->i_offset + dp->i_count.
		 */
		if (ss.slotstatus == NONE) {
			dp->i_offset = roundup2(dp->i_size, DIRBLKSIZ);
			dp->i_count = 0;
			enduseful = dp->i_offset;
		} else {
			dp->i_offset = ss.slotoffset;
			dp->i_count = ss.slotsize;
			if (enduseful < ss.slotoffset + ss.slotsize)
				enduseful = ss.slotoffset + ss.slotsize;
		}
		dp->i_endoff = roundup2(enduseful, DIRBLKSIZ);
		/*
		 * We return with the directory locked, so that
		 * the parameters we set up above will still be
		 * valid if we actually decide to do a direnter().
		 * We return ni_vp == NULL to indicate that the entry
		 * does not currently exist; we leave a pointer to
		 * the (locked) directory inode in ndp->ni_dvp.
		 * The pathname buffer is saved so that the name
		 * can be obtained later.
		 *
		 * NB - if the directory is unlocked, then this
		 * information cannot be used.
		 */
		cnp->cn_flags |= SAVENAME;
		return (EJUSTRETURN);
	}
	/*
	 * Insert name into cache (as non-existent) if appropriate.
	 */
	if ((cnp->cn_flags & MAKEENTRY) != 0)
		cache_enter(vdp, NULL, cnp);
	return (ENOENT);

found:
	if (dd_ino != NULL)
		*dd_ino = ino;
	if (numdirpasses == 2)
		nchstats.ncs_pass2++;
	/*
	 * Check that directory length properly reflects presence
	 * of this entry.
	 */
	if (entryoffsetinblock + EXT2_DIR_REC_LEN(ep->e2d_namlen)
	    > dp->i_size) {
		ext2_dirbad(dp, i_offset, "i_size too small");
		dp->i_size = entryoffsetinblock + EXT2_DIR_REC_LEN(ep->e2d_namlen);
		dp->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	brelse(bp);

	/*
	 * Found component in pathname.
	 * If the final component of path name, save information
	 * in the cache as to where the entry was found.
	 */
	if ((flags & ISLASTCN) && nameiop == LOOKUP)
		dp->i_diroff = rounddown2(i_offset, DIRBLKSIZ);
	/*
	 * If deleting, and at end of pathname, return
	 * parameters which can be used to remove file.
	 */
	if (nameiop == DELETE && (flags & ISLASTCN)) {
		if (flags & LOCKPARENT)
			ASSERT_VOP_ELOCKED(vdp, __FUNCTION__);
		/*
		 * Write access to directory required to delete files.
		 */
		if ((error = VOP_ACCESS(vdp, VWRITE, cred, cnp->cn_thread)) != 0)
			return (error);
		/*
		 * Return pointer to current entry in dp->i_offset,
		 * and distance past previous entry (if there
		 * is a previous entry in this block) in dp->i_count.
		 * Save directory inode pointer in ndp->ni_dvp for dirremove().
		 *
		 * Technically we shouldn't be setting these in the
		 * WANTPARENT case (first lookup in rename()), but any
		 * lookups that will result in directory changes will
		 * overwrite these.
		 */
		dp->i_offset = i_offset;
		if ((dp->i_offset & (DIRBLKSIZ - 1)) == 0)
			dp->i_count = 0;
		else
			dp->i_count = dp->i_offset - prevoff;
		if (dd_ino != NULL)
			return (0);
		if (dp->i_number == ino) {
			VREF(vdp);
			*vpp = vdp;
			return (0);
		}
		if ((error = VFS_VGET(vdp->v_mount, ino, LK_EXCLUSIVE,
		    &tdp)) != 0)
			return (error);
		/*
		 * If directory is "sticky", then user must own
		 * the directory, or the file in it, else she
		 * may not delete it (unless she's root). This
		 * implements append-only directories.
		 */
		if ((dp->i_mode & ISVTX) &&
		    cred->cr_uid != 0 &&
		    cred->cr_uid != dp->i_uid &&
		    VTOI(tdp)->i_uid != cred->cr_uid) {
			vput(tdp);
			return (EPERM);
		}
		*vpp = tdp;
		return (0);
	}

	/*
	 * If rewriting (RENAME), return the inode and the
	 * information required to rewrite the present directory
	 * Must get inode of directory entry to verify it's a
	 * regular file, or empty directory.
	 */
	if (nameiop == RENAME && (flags & ISLASTCN)) {
		if ((error = VOP_ACCESS(vdp, VWRITE, cred, cnp->cn_thread)) != 0)
			return (error);
		/*
		 * Careful about locking second inode.
		 * This can only occur if the target is ".".
		 */
		dp->i_offset = i_offset;
		if (dp->i_number == ino)
			return (EISDIR);
		if (dd_ino != NULL)
			return (0);
		if ((error = VFS_VGET(vdp->v_mount, ino, LK_EXCLUSIVE,
		    &tdp)) != 0)
			return (error);
		*vpp = tdp;
		cnp->cn_flags |= SAVENAME;
		return (0);
	}
	if (dd_ino != NULL)
		return (0);

	/*
	 * Step through the translation in the name.  We do not `vput' the
	 * directory because we may need it again if a symbolic link
	 * is relative to the current directory.  Instead we save it
	 * unlocked as "pdp".  We must get the target inode before unlocking
	 * the directory to insure that the inode will not be removed
	 * before we get it.  We prevent deadlock by always fetching
	 * inodes from the root, moving down the directory tree. Thus
	 * when following backward pointers ".." we must unlock the
	 * parent directory before getting the requested directory.
	 * There is a potential race condition here if both the current
	 * and parent directories are removed before the VFS_VGET for the
	 * inode associated with ".." returns.  We hope that this occurs
	 * infrequently since we cannot avoid this race condition without
	 * implementing a sophisticated deadlock detection algorithm.
	 * Note also that this simple deadlock detection scheme will not
	 * work if the file system has any hard links other than ".."
	 * that point backwards in the directory structure.
	 */
	pdp = vdp;
	if (flags & ISDOTDOT) {
		error = vn_vget_ino(pdp, ino, cnp->cn_lkflags, &tdp);
		if (pdp->v_iflag & VI_DOOMED) {
			if (error == 0)
				vput(tdp);
			error = ENOENT;
		}
		if (error)
			return (error);
		/*
		 * Recheck that ".." entry in the vdp directory points
		 * to the inode we looked up before vdp lock was
		 * dropped.
		 */
		error = ext2_lookup_ino(pdp, NULL, cnp, &ino1);
		if (error) {
			vput(tdp);
			return (error);
		}
		if (ino1 != ino) {
			vput(tdp);
			goto restart;
		}
		*vpp = tdp;
	} else if (dp->i_number == ino) {
		VREF(vdp);	/* we want ourself, ie "." */
		/*
		 * When we lookup "." we still can be asked to lock it
		 * differently.
		 */
		ltype = cnp->cn_lkflags & LK_TYPE_MASK;
		if (ltype != VOP_ISLOCKED(vdp)) {
			if (ltype == LK_EXCLUSIVE)
				vn_lock(vdp, LK_UPGRADE | LK_RETRY);
			else	/* if (ltype == LK_SHARED) */
				vn_lock(vdp, LK_DOWNGRADE | LK_RETRY);
		}
		*vpp = vdp;
	} else {
		if ((error = VFS_VGET(vdp->v_mount, ino, cnp->cn_lkflags,
		    &tdp)) != 0)
			return (error);
		*vpp = tdp;
	}

	/*
	 * Insert name into cache if appropriate.
	 */
	if (cnp->cn_flags & MAKEENTRY)
		cache_enter(vdp, *vpp, cnp);
	return (0);
}

int
ext2_search_dirblock(struct inode *ip, void *data, int *foundp,
    const char *name, int namelen, int *entryoffsetinblockp,
    doff_t *offp, doff_t *prevoffp, doff_t *endusefulp,
    struct ext2fs_searchslot *ssp)
{
	struct vnode *vdp;
	struct ext2fs_direct_2 *ep, *top;
	uint32_t bsize = ip->i_e2fs->e2fs_bsize;
	int offset = *entryoffsetinblockp;
	int namlen;

	vdp = ITOV(ip);

	ep = (struct ext2fs_direct_2 *)((char *)data + offset);
	top = (struct ext2fs_direct_2 *)((char *)data + bsize);
	while (ep < top) {
		/*
		 * Full validation checks are slow, so we only check
		 * enough to insure forward progress through the
		 * directory. Complete checks can be run by setting
		 * "vfs.e2fs.dirchk" to be true.
		 */
		if (ep->e2d_reclen == 0 ||
		    (dirchk && ext2_dirbadentry(vdp, ep, offset))) {
			int i;

			ext2_dirbad(ip, *offp, "mangled entry");
			i = bsize - (offset & (bsize - 1));
			*offp += i;
			offset += i;
			continue;
		}

		/*
		 * If an appropriate sized slot has not yet been found,
		 * check to see if one is available. Also accumulate space
		 * in the current block so that we can determine if
		 * compaction is viable.
		 */
		if (ssp->slotstatus != FOUND) {
			int size = ep->e2d_reclen;

			if (ep->e2d_ino != 0)
				size -= EXT2_DIR_REC_LEN(ep->e2d_namlen);
			else if (ext2_is_dirent_tail(ip, ep))
				size -= sizeof(struct ext2fs_direct_tail);
			if (size > 0) {
				if (size >= ssp->slotneeded) {
					ssp->slotstatus = FOUND;
					ssp->slotoffset = *offp;
					ssp->slotsize = ep->e2d_reclen;
				} else if (ssp->slotstatus == NONE) {
					ssp->slotfreespace += size;
					if (ssp->slotoffset == -1)
						ssp->slotoffset = *offp;
					if (ssp->slotfreespace >= ssp->slotneeded) {
						ssp->slotstatus = COMPACT;
						ssp->slotsize = *offp +
						    ep->e2d_reclen -
						    ssp->slotoffset;
					}
				}
			}
		}
		/*
		 * Check for a name match.
		 */
		if (ep->e2d_ino) {
			namlen = ep->e2d_namlen;
			if (namlen == namelen &&
			    !bcmp(name, ep->e2d_name, (unsigned)namlen)) {
				/*
				 * Save directory entry's inode number and
				 * reclen in ndp->ni_ufs area, and release
				 * directory buffer.
				 */
				*foundp = 1;
				return (0);
			}
		}
		*prevoffp = *offp;
		*offp += ep->e2d_reclen;
		offset += ep->e2d_reclen;
		*entryoffsetinblockp = offset;
		if (ep->e2d_ino)
			*endusefulp = *offp;
		/*
		 * Get pointer to the next entry.
		 */
		ep = (struct ext2fs_direct_2 *)((char *)data + offset);
	}

	return (0);
}

void
ext2_dirbad(struct inode *ip, doff_t offset, char *how)
{
	struct mount *mp;

	mp = ITOV(ip)->v_mount;
	if ((mp->mnt_flag & MNT_RDONLY) == 0)
		panic("ext2_dirbad: %s: bad dir ino %ju at offset %ld: %s\n",
		    mp->mnt_stat.f_mntonname, (uintmax_t)ip->i_number,
		    (long)offset, how);
	else
		(void)printf("%s: bad dir ino %ju at offset %ld: %s\n",
		    mp->mnt_stat.f_mntonname, (uintmax_t)ip->i_number,
		    (long)offset, how);

}

/*
 * Do consistency checking on a directory entry:
 *	record length must be multiple of 4
 *	entry must fit in rest of its DIRBLKSIZ block
 *	record must be large enough to contain entry
 *	name is not longer than MAXNAMLEN
 *	name must be as long as advertised, and null terminated
 */
/*
 *	changed so that it confirms to ext2_check_dir_entry
 */
static int
ext2_dirbadentry(struct vnode *dp, struct ext2fs_direct_2 *de,
    int entryoffsetinblock)
{
	int DIRBLKSIZ = VTOI(dp)->i_e2fs->e2fs_bsize;

	char *error_msg = NULL;

	if (de->e2d_reclen < EXT2_DIR_REC_LEN(1))
		error_msg = "rec_len is smaller than minimal";
	else if (de->e2d_reclen % 4 != 0)
		error_msg = "rec_len % 4 != 0";
	else if (de->e2d_reclen < EXT2_DIR_REC_LEN(de->e2d_namlen))
		error_msg = "reclen is too small for name_len";
	else if (entryoffsetinblock + de->e2d_reclen > DIRBLKSIZ)
		error_msg = "directory entry across blocks";
	/* else LATER
	     if (de->inode > dir->i_sb->u.ext2_sb.s_es->s_inodes_count)
		error_msg = "inode out of bounds";
	*/

	if (error_msg != NULL) {
		printf("bad directory entry: %s\n", error_msg);
		printf("offset=%d, inode=%lu, rec_len=%u, name_len=%u\n",
			entryoffsetinblock, (unsigned long)de->e2d_ino,
			de->e2d_reclen, de->e2d_namlen);
	}
	return error_msg == NULL ? 0 : 1;
}

/*
 * Insert an entry into the fresh directory block.
 * Initialize entry tail if the metadata_csum feature is turned on.
 */
static int
ext2_add_first_entry(struct vnode *dvp, struct ext2fs_direct_2 *entry,
    struct componentname *cnp)
{
	struct inode *dp;
	struct iovec aiov;
	struct uio auio;
	char* buf = NULL;
	int dirblksize, error;

	dp = VTOI(dvp);
	dirblksize = dp->i_e2fs->e2fs_bsize;

	if (dp->i_offset & (dirblksize - 1))
		panic("ext2_add_first_entry: bad directory offset");

	if (EXT2_HAS_RO_COMPAT_FEATURE(dp->i_e2fs,
	    EXT2F_ROCOMPAT_METADATA_CKSUM)) {
		entry->e2d_reclen = dirblksize - sizeof(struct ext2fs_direct_tail);
		buf = malloc(dirblksize, M_TEMP, M_WAITOK);
		if (!buf) {
			error = ENOMEM;
			goto out;
		}
		memcpy(buf, entry, EXT2_DIR_REC_LEN(entry->e2d_namlen));
		ext2_init_dirent_tail(EXT2_DIRENT_TAIL(buf, dirblksize));
		ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)buf);

		auio.uio_offset = dp->i_offset;
		auio.uio_resid = dirblksize;
		aiov.iov_len = auio.uio_resid;
		aiov.iov_base = (caddr_t)buf;
	} else {
		entry->e2d_reclen = dirblksize;
		auio.uio_offset = dp->i_offset;
		auio.uio_resid = EXT2_DIR_REC_LEN(entry->e2d_namlen);
		aiov.iov_len = auio.uio_resid;
		aiov.iov_base = (caddr_t)entry;
	}

	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = (struct thread *)0;
	error = VOP_WRITE(dvp, &auio, IO_SYNC, cnp->cn_cred);
	if (error)
		goto out;

	dp->i_size = roundup2(dp->i_size, dirblksize);
	dp->i_flag |= IN_CHANGE;

out:
	free(buf, M_TEMP);
	return (error);

}

/*
 * Write a directory entry after a call to namei, using the parameters
 * that it left in nameidata.  The argument ip is the inode which the new
 * directory entry will refer to.  Dvp is a pointer to the directory to
 * be written, which was left locked by namei. Remaining parameters
 * (dp->i_offset, dp->i_count) indicate how the space for the new
 * entry is to be obtained.
 */
int
ext2_direnter(struct inode *ip, struct vnode *dvp, struct componentname *cnp)
{
	struct inode *dp;
	struct ext2fs_direct_2 newdir;
	int DIRBLKSIZ = ip->i_e2fs->e2fs_bsize;
	int error;


#ifdef INVARIANTS
	if ((cnp->cn_flags & SAVENAME) == 0)
		panic("ext2_direnter: missing name");
#endif
	dp = VTOI(dvp);
	newdir.e2d_ino = ip->i_number;
	newdir.e2d_namlen = cnp->cn_namelen;
	if (EXT2_HAS_INCOMPAT_FEATURE(ip->i_e2fs,
	    EXT2F_INCOMPAT_FTYPE))
		newdir.e2d_type = DTTOFT(IFTODT(ip->i_mode));
	else
		newdir.e2d_type = EXT2_FT_UNKNOWN;
	bcopy(cnp->cn_nameptr, newdir.e2d_name, (unsigned)cnp->cn_namelen + 1);

	if (ext2_htree_has_idx(dp)) {
		error = ext2_htree_add_entry(dvp, &newdir, cnp);
		if (error) {
			dp->i_flag &= ~IN_E3INDEX;
			dp->i_flag |= IN_CHANGE | IN_UPDATE;
		}
		return (error);
	}

	if (EXT2_HAS_COMPAT_FEATURE(ip->i_e2fs, EXT2F_COMPAT_DIRHASHINDEX) &&
	    !ext2_htree_has_idx(dp)) {
		if ((dp->i_size / DIRBLKSIZ) == 1 &&
		    dp->i_offset == DIRBLKSIZ) {
			/*
			 * Making indexed directory when one block is not
			 * enough to save all entries.
			 */
			return ext2_htree_create_index(dvp, cnp, &newdir);
		}
	}

	/*
	 * If dp->i_count is 0, then namei could find no
	 * space in the directory. Here, dp->i_offset will
	 * be on a directory block boundary and we will write the
	 * new entry into a fresh block.
	 */
	if (dp->i_count == 0)
		return ext2_add_first_entry(dvp, &newdir, cnp);

	error = ext2_add_entry(dvp, &newdir);
	if (!error && dp->i_endoff && dp->i_endoff < dp->i_size)
		error = ext2_truncate(dvp, (off_t)dp->i_endoff, IO_SYNC,
		    cnp->cn_cred, cnp->cn_thread);
	return (error);
}

/*
 * Insert an entry into the directory block.
 * Compact the contents.
 */
int
ext2_add_entry(struct vnode *dvp, struct ext2fs_direct_2 *entry)
{
	struct ext2fs_direct_2 *ep, *nep;
	struct inode *dp;
	struct buf *bp;
	u_int dsize;
	int error, loc, newentrysize, spacefree;
	char *dirbuf;

	dp = VTOI(dvp);

	/*
	 * If dp->i_count is non-zero, then namei found space
	 * for the new entry in the range dp->i_offset to
	 * dp->i_offset + dp->i_count in the directory.
	 * To use this space, we may have to compact the entries located
	 * there, by copying them together towards the beginning of the
	 * block, leaving the free space in one usable chunk at the end.
	 */

	/*
	 * Increase size of directory if entry eats into new space.
	 * This should never push the size past a new multiple of
	 * DIRBLKSIZE.
	 *
	 * N.B. - THIS IS AN ARTIFACT OF 4.2 AND SHOULD NEVER HAPPEN.
	 */
	if (dp->i_offset + dp->i_count > dp->i_size)
		dp->i_size = dp->i_offset + dp->i_count;
	/*
	 * Get the block containing the space for the new directory entry.
	 */
	if ((error = ext2_blkatoff(dvp, (off_t)dp->i_offset, &dirbuf,
	    &bp)) != 0)
		return (error);
	/*
	 * Find space for the new entry. In the simple case, the entry at
	 * offset base will have the space. If it does not, then namei
	 * arranged that compacting the region dp->i_offset to
	 * dp->i_offset + dp->i_count would yield the
	 * space.
	 */
	newentrysize = EXT2_DIR_REC_LEN(entry->e2d_namlen);
	ep = (struct ext2fs_direct_2 *)dirbuf;
	dsize = EXT2_DIR_REC_LEN(ep->e2d_namlen);
	spacefree = ep->e2d_reclen - dsize;
	for (loc = ep->e2d_reclen; loc < dp->i_count; ) {
		nep = (struct ext2fs_direct_2 *)(dirbuf + loc);
		if (ep->e2d_ino) {
			/* trim the existing slot */
			ep->e2d_reclen = dsize;
			ep = (struct ext2fs_direct_2 *)((char *)ep + dsize);
		} else {
			/* overwrite; nothing there; header is ours */
			spacefree += dsize;
		}
		dsize = EXT2_DIR_REC_LEN(nep->e2d_namlen);
		spacefree += nep->e2d_reclen - dsize;
		loc += nep->e2d_reclen;
		bcopy((caddr_t)nep, (caddr_t)ep, dsize);
	}
	/*
	 * Update the pointer fields in the previous entry (if any),
	 * copy in the new entry, and write out the block.
	 */
	if (ep->e2d_ino == 0) {
		if (spacefree + dsize < newentrysize)
			panic("ext2_direnter: compact1");
		entry->e2d_reclen = spacefree + dsize;
	} else {
		if (spacefree < newentrysize)
			panic("ext2_direnter: compact2");
		entry->e2d_reclen = spacefree;
		ep->e2d_reclen = dsize;
		ep = (struct ext2fs_direct_2 *)((char *)ep + dsize);
	}
	bcopy((caddr_t)entry, (caddr_t)ep, (u_int)newentrysize);
	ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)bp->b_data);
	if (DOINGASYNC(dvp)) {
		bdwrite(bp);
		error = 0;
	} else {
		error = bwrite(bp);
	}
	dp->i_flag |= IN_CHANGE | IN_UPDATE;
	return (error);
}

/*
 * Remove a directory entry after a call to namei, using
 * the parameters which it left in nameidata. The entry
 * dp->i_offset contains the offset into the directory of the
 * entry to be eliminated.  The dp->i_count field contains the
 * size of the previous record in the directory.  If this
 * is 0, the first entry is being deleted, so we need only
 * zero the inode number to mark the entry as free.  If the
 * entry is not the first in the directory, we must reclaim
 * the space of the now empty record by adding the record size
 * to the size of the previous entry.
 */
int
ext2_dirremove(struct vnode *dvp, struct componentname *cnp)
{
	struct inode *dp;
	struct ext2fs_direct_2 *ep, *rep;
	struct buf *bp;
	int error;

	dp = VTOI(dvp);
	if (dp->i_count == 0) {
		/*
		 * First entry in block: set d_ino to zero.
		 */
		if ((error =
		    ext2_blkatoff(dvp, (off_t)dp->i_offset, (char **)&ep,
		    &bp)) != 0)
			return (error);
		ep->e2d_ino = 0;
		ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)bp->b_data);
		error = bwrite(bp);
		dp->i_flag |= IN_CHANGE | IN_UPDATE;
		return (error);
	}
	/*
	 * Collapse new free space into previous entry.
	 */
	if ((error = ext2_blkatoff(dvp, (off_t)(dp->i_offset - dp->i_count),
	    (char **)&ep, &bp)) != 0)
		return (error);

	/* Set 'rep' to the entry being removed. */
	if (dp->i_count == 0)
		rep = ep;
	else
		rep = (struct ext2fs_direct_2 *)((char *)ep + ep->e2d_reclen);
	ep->e2d_reclen += rep->e2d_reclen;
	ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)bp->b_data);
	if (DOINGASYNC(dvp) && dp->i_count != 0)
		bdwrite(bp);
	else
		error = bwrite(bp);
	dp->i_flag |= IN_CHANGE | IN_UPDATE;
	return (error);
}

/*
 * Rewrite an existing directory entry to point at the inode
 * supplied.  The parameters describing the directory entry are
 * set up by a call to namei.
 */
int
ext2_dirrewrite(struct inode *dp, struct inode *ip, struct componentname *cnp)
{
	struct buf *bp;
	struct ext2fs_direct_2 *ep;
	struct vnode *vdp = ITOV(dp);
	int error;

	if ((error = ext2_blkatoff(vdp, (off_t)dp->i_offset, (char **)&ep,
	    &bp)) != 0)
		return (error);
	ep->e2d_ino = ip->i_number;
	if (EXT2_HAS_INCOMPAT_FEATURE(ip->i_e2fs,
	    EXT2F_INCOMPAT_FTYPE))
		ep->e2d_type = DTTOFT(IFTODT(ip->i_mode));
	else
		ep->e2d_type = EXT2_FT_UNKNOWN;
	ext2_dirent_csum_set(dp, (struct ext2fs_direct_2 *)bp->b_data);
	error = bwrite(bp);
	dp->i_flag |= IN_CHANGE | IN_UPDATE;
	return (error);
}

/*
 * Check if a directory is empty or not.
 * Inode supplied must be locked.
 *
 * Using a struct dirtemplate here is not precisely
 * what we want, but better than using a struct direct.
 *
 * NB: does not handle corrupted directories.
 */
int
ext2_dirempty(struct inode *ip, ino_t parentino, struct ucred *cred)
{
	off_t off;
	struct dirtemplate dbuf;
	struct ext2fs_direct_2 *dp = (struct ext2fs_direct_2 *)&dbuf;
	int error, namlen;
	ssize_t count;
#define	MINDIRSIZ (sizeof(struct dirtemplate) / 2)

	for (off = 0; off < ip->i_size; off += dp->e2d_reclen) {
		error = vn_rdwr(UIO_READ, ITOV(ip), (caddr_t)dp, MINDIRSIZ,
		    off, UIO_SYSSPACE, IO_NODELOCKED | IO_NOMACCHECK, cred,
		    NOCRED, &count, (struct thread *)0);
		/*
		 * Since we read MINDIRSIZ, residual must
		 * be 0 unless we're at end of file.
		 */
		if (error || count != 0)
			return (0);
		/* avoid infinite loops */
		if (dp->e2d_reclen == 0)
			return (0);
		/* skip empty entries */
		if (dp->e2d_ino == 0)
			continue;
		/* accept only "." and ".." */
		namlen = dp->e2d_namlen;
		if (namlen > 2)
			return (0);
		if (dp->e2d_name[0] != '.')
			return (0);
		/*
		 * At this point namlen must be 1 or 2.
		 * 1 implies ".", 2 implies ".." if second
		 * char is also "."
		 */
		if (namlen == 1)
			continue;
		if (dp->e2d_name[1] == '.' && dp->e2d_ino == parentino)
			continue;
		return (0);
	}
	return (1);
}

/*
 * Check if source directory is in the path of the target directory.
 * Target is supplied locked, source is unlocked.
 * The target is always vput before returning.
 */
int
ext2_checkpath(struct inode *source, struct inode *target, struct ucred *cred)
{
	struct vnode *vp;
	int error, namlen;
	struct dirtemplate dirbuf;

	vp = ITOV(target);
	if (target->i_number == source->i_number) {
		error = EEXIST;
		goto out;
	}
	if (target->i_number == EXT2_ROOTINO) {
		error = 0;
		goto out;
	}

	for (;;) {
		if (vp->v_type != VDIR) {
			error = ENOTDIR;
			break;
		}
		error = vn_rdwr(UIO_READ, vp, (caddr_t)&dirbuf,
		    sizeof(struct dirtemplate), (off_t)0, UIO_SYSSPACE,
		    IO_NODELOCKED | IO_NOMACCHECK, cred, NOCRED, NULL,
		    NULL);
		if (error != 0)
			break;
		namlen = dirbuf.dotdot_type;	/* like ufs little-endian */
		if (namlen != 2 ||
		    dirbuf.dotdot_name[0] != '.' ||
		    dirbuf.dotdot_name[1] != '.') {
			error = ENOTDIR;
			break;
		}
		if (dirbuf.dotdot_ino == source->i_number) {
			error = EINVAL;
			break;
		}
		if (dirbuf.dotdot_ino == EXT2_ROOTINO)
			break;
		vput(vp);
		if ((error = VFS_VGET(vp->v_mount, dirbuf.dotdot_ino,
		    LK_EXCLUSIVE, &vp)) != 0) {
			vp = NULL;
			break;
		}
	}

out:
	if (error == ENOTDIR)
		printf("checkpath: .. not a directory\n");
	if (vp != NULL)
		vput(vp);
	return (error);
}
