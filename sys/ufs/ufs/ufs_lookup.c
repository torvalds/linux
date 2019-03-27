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
 *	@(#)ufs_lookup.c	8.15 (Berkeley) 6/16/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ufs.h"
#include "opt_quota.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/namei.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/dir.h>
#ifdef UFS_DIRHASH
#include <ufs/ufs/dirhash.h>
#endif
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

#ifdef DIAGNOSTIC
static int	dirchk = 1;
#else
static int	dirchk = 0;
#endif

SYSCTL_INT(_debug, OID_AUTO, dircheck, CTLFLAG_RW, &dirchk, 0, "");

/* true if old FS format...*/
#define OFSFMT(vp)	((vp)->v_mount->mnt_maxsymlinklen <= 0)

static int
ufs_delete_denied(struct vnode *vdp, struct vnode *tdp, struct ucred *cred,
    struct thread *td)
{
	int error;

#ifdef UFS_ACL
	/*
	 * NFSv4 Minor Version 1, draft-ietf-nfsv4-minorversion1-03.txt
	 *
	 * 3.16.2.1. ACE4_DELETE vs. ACE4_DELETE_CHILD
	 */

	/*
	 * XXX: Is this check required?
	 */
	error = VOP_ACCESS(vdp, VEXEC, cred, td);
	if (error)
		return (error);

	error = VOP_ACCESSX(tdp, VDELETE, cred, td);
	if (error == 0)
		return (0);

	error = VOP_ACCESSX(vdp, VDELETE_CHILD, cred, td);
	if (error == 0)
		return (0);

	error = VOP_ACCESSX(vdp, VEXPLICIT_DENY | VDELETE_CHILD, cred, td);
	if (error)
		return (error);

#endif /* !UFS_ACL */

	/*
	 * Standard Unix access control - delete access requires VWRITE.
	 */
	error = VOP_ACCESS(vdp, VWRITE, cred, td);
	if (error)
		return (error);

	/*
	 * If directory is "sticky", then user must own
	 * the directory, or the file in it, else she
	 * may not delete it (unless she's root). This
	 * implements append-only directories.
	 */
	if ((VTOI(vdp)->i_mode & ISVTX) &&
	    VOP_ACCESS(vdp, VADMIN, cred, td) &&
	    VOP_ACCESS(tdp, VADMIN, cred, td))
		return (EPERM);

	return (0);
}

/*
 * Convert a component of a pathname into a pointer to a locked inode.
 * This is a very central and rather complicated routine.
 * If the filesystem is not maintained in a strict tree hierarchy,
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
 * This routine is actually used as VOP_CACHEDLOOKUP method, and the
 * filesystem employs the generic vfs_cache_lookup() as VOP_LOOKUP
 * method.
 *
 * vfs_cache_lookup() performs the following for us:
 *	check that it is a directory
 *	check accessibility of directory
 *	check for modification attempts on read-only mounts
 *	if name found in cache
 *	    if at end of path and deleting or creating
 *		drop it
 *	     else
 *		return name.
 *	return VOP_CACHEDLOOKUP()
 *
 * Overall outline of ufs_lookup:
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
ufs_lookup(ap)
	struct vop_cachedlookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{

	return (ufs_lookup_ino(ap->a_dvp, ap->a_vpp, ap->a_cnp, NULL));
}

int
ufs_lookup_ino(struct vnode *vdp, struct vnode **vpp, struct componentname *cnp,
    ino_t *dd_ino)
{
	struct inode *dp;		/* inode for directory being searched */
	struct buf *bp;			/* a buffer of directory entries */
	struct direct *ep;		/* the current directory entry */
	int entryoffsetinblock;		/* offset of ep in bp's buffer */
	enum {NONE, COMPACT, FOUND} slotstatus;
	doff_t slotoffset;		/* offset of area with free space */
	doff_t i_diroff;		/* cached i_diroff value. */
	doff_t i_offset;		/* cached i_offset value. */
	int slotsize;			/* size of area at slotoffset */
	int slotfreespace;		/* amount of space free in slot */
	int slotneeded;			/* size of the entry we're seeking */
	int numdirpasses;		/* strategy for directory search */
	doff_t endsearch;		/* offset to end directory search */
	doff_t prevoff;			/* prev entry dp->i_offset */
	struct vnode *pdp;		/* saved dp during symlink work */
	struct vnode *tdp;		/* returned by VFS_VGET */
	doff_t enduseful;		/* pointer past last used dir slot */
	u_long bmask;			/* block offset mask */
	int namlen, error;
	struct ucred *cred = cnp->cn_cred;
	int flags = cnp->cn_flags;
	int nameiop = cnp->cn_nameiop;
	ino_t ino, ino1;
	int ltype;

	if (vpp != NULL)
		*vpp = NULL;

	dp = VTOI(vdp);
	if (dp->i_effnlink == 0)
		return (ENOENT);

	/*
	 * Create a vm object if vmiodirenable is enabled.
	 * Alternatively we could call vnode_create_vobject
	 * in VFS_VGET but we could end up creating objects
	 * that are never used.
	 */
	vnode_create_vobject(vdp, DIP(dp, i_size), cnp->cn_thread);

	bmask = VFSTOUFS(vdp->v_mount)->um_mountp->mnt_stat.f_iosize - 1;

#ifdef DEBUG_VFS_LOCKS
	/*
	 * Assert that the directory vnode is locked, and locked
	 * exclusively for the last component lookup for modifying
	 * operations.
	 *
	 * The directory-modifying operations need to save
	 * intermediate state in the inode between namei() call and
	 * actual directory manipulations.  See fields in the struct
	 * inode marked as 'used during directory lookup'.  We must
	 * ensure that upgrade in namei() does not happen, since
	 * upgrade might need to unlock vdp.  If quotas are enabled,
	 * getinoquota() also requires exclusive lock to modify inode.
	 */
	ASSERT_VOP_LOCKED(vdp, "ufs_lookup1");
	if ((nameiop == CREATE || nameiop == DELETE || nameiop == RENAME) &&
	    (flags & (LOCKPARENT | ISLASTCN)) == (LOCKPARENT | ISLASTCN))
		ASSERT_VOP_ELOCKED(vdp, "ufs_lookup2");
#endif

restart:
	bp = NULL;
	slotoffset = -1;

	/*
	 * We now have a segment name to search for, and a directory to search.
	 *
	 * Suppress search for slots unless creating
	 * file and at end of pathname, in which case
	 * we watch for a place to put the new file in
	 * case it doesn't already exist.
	 */
	ino = 0;
	i_diroff = dp->i_diroff;
	slotstatus = FOUND;
	slotfreespace = slotsize = slotneeded = 0;
	if ((nameiop == CREATE || nameiop == RENAME) &&
	    (flags & ISLASTCN)) {
		slotstatus = NONE;
		slotneeded = DIRECTSIZ(cnp->cn_namelen);
	}

#ifdef UFS_DIRHASH
	/*
	 * Use dirhash for fast operations on large directories. The logic
	 * to determine whether to hash the directory is contained within
	 * ufsdirhash_build(); a zero return means that it decided to hash
	 * this directory and it successfully built up the hash table.
	 */
	if (ufsdirhash_build(dp) == 0) {
		/* Look for a free slot if needed. */
		enduseful = dp->i_size;
		if (slotstatus != FOUND) {
			slotoffset = ufsdirhash_findfree(dp, slotneeded,
			    &slotsize);
			if (slotoffset >= 0) {
				slotstatus = COMPACT;
				enduseful = ufsdirhash_enduseful(dp);
				if (enduseful < 0)
					enduseful = dp->i_size;
			}
		}
		/* Look up the component. */
		numdirpasses = 1;
		entryoffsetinblock = 0; /* silence compiler warning */
		switch (ufsdirhash_lookup(dp, cnp->cn_nameptr, cnp->cn_namelen,
		    &i_offset, &bp, nameiop == DELETE ? &prevoff : NULL)) {
		case 0:
			ep = (struct direct *)((char *)bp->b_data +
			    (i_offset & bmask));
			goto foundentry;
		case ENOENT:
			i_offset = roundup2(dp->i_size, DIRBLKSIZ);
			goto notfound;
		default:
			/* Something failed; just do a linear search. */
			break;
		}
	}
#endif /* UFS_DIRHASH */
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
	if (nameiop != LOOKUP || i_diroff == 0 || i_diroff >= dp->i_size) {
		entryoffsetinblock = 0;
		i_offset = 0;
		numdirpasses = 1;
	} else {
		i_offset = i_diroff;
		if ((entryoffsetinblock = i_offset & bmask) &&
		    (error = UFS_BLKATOFF(vdp, (off_t)i_offset, NULL, &bp)))
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
		if ((i_offset & bmask) == 0) {
			if (bp != NULL)
				brelse(bp);
			error =
			    UFS_BLKATOFF(vdp, (off_t)i_offset, NULL, &bp);
			if (error)
				return (error);
			entryoffsetinblock = 0;
		}
		/*
		 * If still looking for a slot, and at a DIRBLKSIZE
		 * boundary, have to start looking for free space again.
		 */
		if (slotstatus == NONE &&
		    (entryoffsetinblock & (DIRBLKSIZ - 1)) == 0) {
			slotoffset = -1;
			slotfreespace = 0;
		}
		/*
		 * Get pointer to next entry.
		 * Full validation checks are slow, so we only check
		 * enough to insure forward progress through the
		 * directory. Complete checks can be run by patching
		 * "dirchk" to be true.
		 */
		ep = (struct direct *)((char *)bp->b_data + entryoffsetinblock);
		if (ep->d_reclen == 0 || ep->d_reclen >
		    DIRBLKSIZ - (entryoffsetinblock & (DIRBLKSIZ - 1)) ||
		    (dirchk && ufs_dirbadentry(vdp, ep, entryoffsetinblock))) {
			int i;

			ufs_dirbad(dp, i_offset, "mangled entry");
			i = DIRBLKSIZ - (entryoffsetinblock & (DIRBLKSIZ - 1));
			i_offset += i;
			entryoffsetinblock += i;
			continue;
		}

		/*
		 * If an appropriate sized slot has not yet been found,
		 * check to see if one is available. Also accumulate space
		 * in the current block so that we can determine if
		 * compaction is viable.
		 */
		if (slotstatus != FOUND) {
			int size = ep->d_reclen;

			if (ep->d_ino != 0)
				size -= DIRSIZ(OFSFMT(vdp), ep);
			if (size > 0) {
				if (size >= slotneeded) {
					slotstatus = FOUND;
					slotoffset = i_offset;
					slotsize = ep->d_reclen;
				} else if (slotstatus == NONE) {
					slotfreespace += size;
					if (slotoffset == -1)
						slotoffset = i_offset;
					if (slotfreespace >= slotneeded) {
						slotstatus = COMPACT;
						slotsize = i_offset +
						      ep->d_reclen - slotoffset;
					}
				}
			}
		}

		/*
		 * Check for a name match.
		 */
		if (ep->d_ino) {
#			if (BYTE_ORDER == LITTLE_ENDIAN)
				if (OFSFMT(vdp))
					namlen = ep->d_type;
				else
					namlen = ep->d_namlen;
#			else
				namlen = ep->d_namlen;
#			endif
			if (namlen == cnp->cn_namelen &&
				(cnp->cn_nameptr[0] == ep->d_name[0]) &&
			    !bcmp(cnp->cn_nameptr, ep->d_name,
				(unsigned)namlen)) {
#ifdef UFS_DIRHASH
foundentry:
#endif
				/*
				 * Save directory entry's inode number and
				 * reclen in ndp->ni_ufs area, and release
				 * directory buffer.
				 */
				if (vdp->v_mount->mnt_maxsymlinklen > 0 &&
				    ep->d_type == DT_WHT) {
					slotstatus = FOUND;
					slotoffset = i_offset;
					slotsize = ep->d_reclen;
					enduseful = dp->i_size;
					cnp->cn_flags |= ISWHITEOUT;
					numdirpasses--;
					goto notfound;
				}
				ino = ep->d_ino;
				goto found;
			}
		}
		prevoff = i_offset;
		i_offset += ep->d_reclen;
		entryoffsetinblock += ep->d_reclen;
		if (ep->d_ino)
			enduseful = i_offset;
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
	if ((nameiop == CREATE || nameiop == RENAME ||
	     (nameiop == DELETE &&
	      (cnp->cn_flags & DOWHITEOUT) &&
	      (cnp->cn_flags & ISWHITEOUT))) &&
	    (flags & ISLASTCN) && dp->i_effnlink != 0) {
		/*
		 * Access for write is interpreted as allowing
		 * creation of files in the directory.
		 *
		 * XXX: Fix the comment above.
		 */
		if (flags & WILLBEDIR)
			error = VOP_ACCESSX(vdp, VWRITE | VAPPEND, cred, cnp->cn_thread);
		else
			error = VOP_ACCESS(vdp, VWRITE, cred, cnp->cn_thread);
		if (error)
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
		if (slotstatus == NONE) {
			dp->i_offset = roundup2(dp->i_size, DIRBLKSIZ);
			dp->i_count = 0;
			enduseful = dp->i_offset;
		} else if (nameiop == DELETE) {
			dp->i_offset = slotoffset;
			if ((dp->i_offset & (DIRBLKSIZ - 1)) == 0)
				dp->i_count = 0;
			else
				dp->i_count = dp->i_offset - prevoff;
		} else {
			dp->i_offset = slotoffset;
			dp->i_count = slotsize;
			if (enduseful < slotoffset + slotsize)
				enduseful = slotoffset + slotsize;
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
	if (i_offset + DIRSIZ(OFSFMT(vdp), ep) > dp->i_size) {
		ufs_dirbad(dp, i_offset, "i_size too small");
		dp->i_size = i_offset + DIRSIZ(OFSFMT(vdp), ep);
		DIP_SET(dp, i_size, dp->i_size);
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
		if ((error = VFS_VGET(vdp->v_mount, ino,
		    LK_EXCLUSIVE, &tdp)) != 0)
			return (error);
		error = ufs_delete_denied(vdp, tdp, cred, cnp->cn_thread);
		if (error) {
			vput(tdp);
			return (error);
		}
		if (dp->i_number == ino) {
			VREF(vdp);
			*vpp = vdp;
			vput(tdp);
			return (0);
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
		if (flags & WILLBEDIR)
			error = VOP_ACCESSX(vdp, VWRITE | VAPPEND, cred, cnp->cn_thread);
		else
			error = VOP_ACCESS(vdp, VWRITE, cred, cnp->cn_thread);
		if (error)
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
		if ((error = VFS_VGET(vdp->v_mount, ino,
		    LK_EXCLUSIVE, &tdp)) != 0)
			return (error);

		error = ufs_delete_denied(vdp, tdp, cred, cnp->cn_thread);
		if (error) {
			vput(tdp);
			return (error);
		}

#ifdef SunOS_doesnt_do_that
		/*
		 * The only purpose of this check is to return the correct
		 * error.  Assume that we want to rename directory "a"
		 * to a file "b", and that we have no ACL_WRITE_DATA on
		 * a containing directory, but we _do_ have ACL_APPEND_DATA. 
		 * In that case, the VOP_ACCESS check above will return 0,
		 * and the operation will fail with ENOTDIR instead
		 * of EACCESS.
		 */
		if (tdp->v_type == VDIR)
			error = VOP_ACCESSX(vdp, VWRITE | VAPPEND, cred, cnp->cn_thread);
		else
			error = VOP_ACCESS(vdp, VWRITE, cred, cnp->cn_thread);
		if (error) {
			vput(tdp);
			return (error);
		}
#endif

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
	 * work if the filesystem has any hard links other than ".."
	 * that point backwards in the directory structure.
	 */
	pdp = vdp;
	if (flags & ISDOTDOT) {
		error = vn_vget_ino(pdp, ino, cnp->cn_lkflags, &tdp);
		if (error)
			return (error);

		/*
		 * Recheck that ".." entry in the vdp directory points
		 * to the inode we looked up before vdp lock was
		 * dropped.
		 */
		error = ufs_lookup_ino(pdp, NULL, cnp, &ino1);
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
			else /* if (ltype == LK_SHARED) */
				vn_lock(vdp, LK_DOWNGRADE | LK_RETRY);
			/*
			 * Relock for the "." case may left us with
			 * reclaimed vnode.
			 */
			if (vdp->v_iflag & VI_DOOMED) {
				vrele(vdp);
				return (ENOENT);
			}
		}
		*vpp = vdp;
	} else {
		error = VFS_VGET(pdp->v_mount, ino, cnp->cn_lkflags, &tdp);
		if (error)
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

void
ufs_dirbad(ip, offset, how)
	struct inode *ip;
	doff_t offset;
	char *how;
{
	struct mount *mp;

	mp = ITOV(ip)->v_mount;
	if ((mp->mnt_flag & MNT_RDONLY) == 0)
		panic("ufs_dirbad: %s: bad dir ino %ju at offset %ld: %s",
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
 *	name is not longer than UFS_MAXNAMLEN
 *	name must be as long as advertised, and null terminated
 */
int
ufs_dirbadentry(dp, ep, entryoffsetinblock)
	struct vnode *dp;
	struct direct *ep;
	int entryoffsetinblock;
{
	int i, namlen;

#	if (BYTE_ORDER == LITTLE_ENDIAN)
		if (OFSFMT(dp))
			namlen = ep->d_type;
		else
			namlen = ep->d_namlen;
#	else
		namlen = ep->d_namlen;
#	endif
	if ((ep->d_reclen & 0x3) != 0 ||
	    ep->d_reclen > DIRBLKSIZ - (entryoffsetinblock & (DIRBLKSIZ - 1)) ||
	    ep->d_reclen < DIRSIZ(OFSFMT(dp), ep) || namlen > UFS_MAXNAMLEN) {
		/*return (1); */
		printf("First bad\n");
		goto bad;
	}
	if (ep->d_ino == 0)
		return (0);
	for (i = 0; i < namlen; i++)
		if (ep->d_name[i] == '\0') {
			/*return (1); */
			printf("Second bad\n");
			goto bad;
		}
	if (ep->d_name[i])
		goto bad;
	return (0);
bad:
	return (1);
}

/*
 * Construct a new directory entry after a call to namei, using the
 * parameters that it left in the componentname argument cnp. The
 * argument ip is the inode to which the new directory entry will refer.
 */
void
ufs_makedirentry(ip, cnp, newdirp)
	struct inode *ip;
	struct componentname *cnp;
	struct direct *newdirp;
{

#ifdef INVARIANTS
	if ((cnp->cn_flags & SAVENAME) == 0)
		panic("ufs_makedirentry: missing name");
#endif
	newdirp->d_ino = ip->i_number;
	newdirp->d_namlen = cnp->cn_namelen;
	bcopy(cnp->cn_nameptr, newdirp->d_name, (unsigned)cnp->cn_namelen + 1);
	if (ITOV(ip)->v_mount->mnt_maxsymlinklen > 0)
		newdirp->d_type = IFTODT(ip->i_mode);
	else {
		newdirp->d_type = 0;
#		if (BYTE_ORDER == LITTLE_ENDIAN)
			{ u_char tmp = newdirp->d_namlen;
			newdirp->d_namlen = newdirp->d_type;
			newdirp->d_type = tmp; }
#		endif
	}
}

/*
 * Write a directory entry after a call to namei, using the parameters
 * that it left in nameidata. The argument dirp is the new directory
 * entry contents. Dvp is a pointer to the directory to be written,
 * which was left locked by namei. Remaining parameters (dp->i_offset, 
 * dp->i_count) indicate how the space for the new entry is to be obtained.
 * Non-null bp indicates that a directory is being created (for the
 * soft dependency code).
 */
int
ufs_direnter(dvp, tvp, dirp, cnp, newdirbp, isrename)
	struct vnode *dvp;
	struct vnode *tvp;
	struct direct *dirp;
	struct componentname *cnp;
	struct buf *newdirbp;
	int isrename;
{
	struct ucred *cr;
	struct thread *td;
	int newentrysize;
	struct inode *dp;
	struct buf *bp;
	u_int dsize;
	struct direct *ep, *nep;
	u_int64_t old_isize;
	int error, ret, blkoff, loc, spacefree, flags, namlen;
	char *dirbuf;

	td = curthread;	/* XXX */
	cr = td->td_ucred;

	dp = VTOI(dvp);
	newentrysize = DIRSIZ(OFSFMT(dvp), dirp);

	if (dp->i_count == 0) {
		/*
		 * If dp->i_count is 0, then namei could find no
		 * space in the directory. Here, dp->i_offset will
		 * be on a directory block boundary and we will write the
		 * new entry into a fresh block.
		 */
		if (dp->i_offset & (DIRBLKSIZ - 1))
			panic("ufs_direnter: newblk");
		flags = BA_CLRBUF;
		if (!DOINGSOFTDEP(dvp) && !DOINGASYNC(dvp))
			flags |= IO_SYNC;
#ifdef QUOTA
		if ((error = getinoquota(dp)) != 0) {
			if (DOINGSOFTDEP(dvp) && newdirbp != NULL)
				bdwrite(newdirbp);
			return (error);
		}
#endif
		old_isize = dp->i_size;
		vnode_pager_setsize(dvp, (u_long)dp->i_offset + DIRBLKSIZ);
		if ((error = UFS_BALLOC(dvp, (off_t)dp->i_offset, DIRBLKSIZ,
		    cr, flags, &bp)) != 0) {
			if (DOINGSOFTDEP(dvp) && newdirbp != NULL)
				bdwrite(newdirbp);
			vnode_pager_setsize(dvp, (u_long)old_isize);
			return (error);
		}
		dp->i_size = dp->i_offset + DIRBLKSIZ;
		DIP_SET(dp, i_size, dp->i_size);
		dp->i_endoff = dp->i_size;
		dp->i_flag |= IN_CHANGE | IN_UPDATE;
		dirp->d_reclen = DIRBLKSIZ;
		blkoff = dp->i_offset &
		    (VFSTOUFS(dvp->v_mount)->um_mountp->mnt_stat.f_iosize - 1);
		bcopy((caddr_t)dirp, (caddr_t)bp->b_data + blkoff,newentrysize);
#ifdef UFS_DIRHASH
		if (dp->i_dirhash != NULL) {
			ufsdirhash_newblk(dp, dp->i_offset);
			ufsdirhash_add(dp, dirp, dp->i_offset);
			ufsdirhash_checkblock(dp, (char *)bp->b_data + blkoff,
			    dp->i_offset);
		}
#endif
		if (DOINGSOFTDEP(dvp)) {
			/*
			 * Ensure that the entire newly allocated block is a
			 * valid directory so that future growth within the
			 * block does not have to ensure that the block is
			 * written before the inode.
			 */
			blkoff += DIRBLKSIZ;
			while (blkoff < bp->b_bcount) {
				((struct direct *)
				   (bp->b_data + blkoff))->d_reclen = DIRBLKSIZ;
				blkoff += DIRBLKSIZ;
			}
			if (softdep_setup_directory_add(bp, dp, dp->i_offset,
			    dirp->d_ino, newdirbp, 1))
				dp->i_flag |= IN_NEEDSYNC;
			if (newdirbp)
				bdwrite(newdirbp);
			bdwrite(bp);
			if ((dp->i_flag & IN_NEEDSYNC) == 0)
				return (UFS_UPDATE(dvp, 0));
			/*
			 * We have just allocated a directory block in an
			 * indirect block.  We must prevent holes in the
			 * directory created if directory entries are
			 * written out of order.  To accomplish this we
			 * fsync when we extend a directory into indirects.
			 * During rename it's not safe to drop the tvp lock
			 * so sync must be delayed until it is.
			 *
			 * This synchronous step could be removed if fsck and
			 * the kernel were taught to fill in sparse
			 * directories rather than panic.
			 */
			if (isrename)
				return (0);
			if (tvp != NULL)
				VOP_UNLOCK(tvp, 0);
			(void) VOP_FSYNC(dvp, MNT_WAIT, td);
			if (tvp != NULL)
				vn_lock(tvp, LK_EXCLUSIVE | LK_RETRY);
			return (error);
		}
		if (DOINGASYNC(dvp)) {
			bdwrite(bp);
			return (UFS_UPDATE(dvp, 0));
		}
		error = bwrite(bp);
		ret = UFS_UPDATE(dvp, 1);
		if (error == 0)
			return (ret);
		return (error);
	}

	/*
	 * If dp->i_count is non-zero, then namei found space for the new
	 * entry in the range dp->i_offset to dp->i_offset + dp->i_count
	 * in the directory. To use this space, we may have to compact
	 * the entries located there, by copying them together towards the
	 * beginning of the block, leaving the free space in one usable
	 * chunk at the end.
	 */

	/*
	 * Increase size of directory if entry eats into new space.
	 * This should never push the size past a new multiple of
	 * DIRBLKSIZE.
	 *
	 * N.B. - THIS IS AN ARTIFACT OF 4.2 AND SHOULD NEVER HAPPEN.
	 */
	if (dp->i_offset + dp->i_count > dp->i_size) {
		dp->i_size = dp->i_offset + dp->i_count;
		DIP_SET(dp, i_size, dp->i_size);
	}
	/*
	 * Get the block containing the space for the new directory entry.
	 */
	error = UFS_BLKATOFF(dvp, (off_t)dp->i_offset, &dirbuf, &bp);
	if (error) {
		if (DOINGSOFTDEP(dvp) && newdirbp != NULL)
			bdwrite(newdirbp);
		return (error);
	}
	/*
	 * Find space for the new entry. In the simple case, the entry at
	 * offset base will have the space. If it does not, then namei
	 * arranged that compacting the region dp->i_offset to
	 * dp->i_offset + dp->i_count would yield the space.
	 */
	ep = (struct direct *)dirbuf;
	dsize = ep->d_ino ? DIRSIZ(OFSFMT(dvp), ep) : 0;
	spacefree = ep->d_reclen - dsize;
	for (loc = ep->d_reclen; loc < dp->i_count; ) {
		nep = (struct direct *)(dirbuf + loc);

		/* Trim the existing slot (NB: dsize may be zero). */
		ep->d_reclen = dsize;
		ep = (struct direct *)((char *)ep + dsize);

		/* Read nep->d_reclen now as the bcopy() may clobber it. */
		loc += nep->d_reclen;
		if (nep->d_ino == 0) {
			/*
			 * A mid-block unused entry. Such entries are
			 * never created by the kernel, but fsck_ffs
			 * can create them (and it doesn't fix them).
			 *
			 * Add up the free space, and initialise the
			 * relocated entry since we don't bcopy it.
			 */
			spacefree += nep->d_reclen;
			ep->d_ino = 0;
			dsize = 0;
			continue;
		}
		dsize = DIRSIZ(OFSFMT(dvp), nep);
		spacefree += nep->d_reclen - dsize;
#ifdef UFS_DIRHASH
		if (dp->i_dirhash != NULL)
			ufsdirhash_move(dp, nep,
			    dp->i_offset + ((char *)nep - dirbuf),
			    dp->i_offset + ((char *)ep - dirbuf));
#endif
		if (DOINGSOFTDEP(dvp))
			softdep_change_directoryentry_offset(bp, dp, dirbuf,
			    (caddr_t)nep, (caddr_t)ep, dsize); 
		else
			bcopy((caddr_t)nep, (caddr_t)ep, dsize);
	}
	/*
	 * Here, `ep' points to a directory entry containing `dsize' in-use
	 * bytes followed by `spacefree' unused bytes. If ep->d_ino == 0,
	 * then the entry is completely unused (dsize == 0). The value
	 * of ep->d_reclen is always indeterminate.
	 *
	 * Update the pointer fields in the previous entry (if any),
	 * copy in the new entry, and write out the block.
	 */
#	if (BYTE_ORDER == LITTLE_ENDIAN)
		if (OFSFMT(dvp))
			namlen = ep->d_type;
		else
			namlen = ep->d_namlen;
#	else
		namlen = ep->d_namlen;
#	endif
	if (ep->d_ino == 0 ||
	    (ep->d_ino == UFS_WINO && namlen == dirp->d_namlen &&
	     bcmp(ep->d_name, dirp->d_name, dirp->d_namlen) == 0)) {
		if (spacefree + dsize < newentrysize)
			panic("ufs_direnter: compact1");
		dirp->d_reclen = spacefree + dsize;
	} else {
		if (spacefree < newentrysize)
			panic("ufs_direnter: compact2");
		dirp->d_reclen = spacefree;
		ep->d_reclen = dsize;
		ep = (struct direct *)((char *)ep + dsize);
	}
#ifdef UFS_DIRHASH
	if (dp->i_dirhash != NULL && (ep->d_ino == 0 ||
	    dirp->d_reclen == spacefree))
		ufsdirhash_add(dp, dirp, dp->i_offset + ((char *)ep - dirbuf));
#endif
	bcopy((caddr_t)dirp, (caddr_t)ep, (u_int)newentrysize);
#ifdef UFS_DIRHASH
	if (dp->i_dirhash != NULL)
		ufsdirhash_checkblock(dp, dirbuf -
		    (dp->i_offset & (DIRBLKSIZ - 1)),
		    rounddown2(dp->i_offset, DIRBLKSIZ));
#endif

	if (DOINGSOFTDEP(dvp)) {
		(void) softdep_setup_directory_add(bp, dp,
		    dp->i_offset + (caddr_t)ep - dirbuf,
		    dirp->d_ino, newdirbp, 0);
		if (newdirbp != NULL)
			bdwrite(newdirbp);
		bdwrite(bp);
	} else {
		if (DOINGASYNC(dvp)) {
			bdwrite(bp);
			error = 0;
		} else {
			error = bwrite(bp);
		}
	}
	dp->i_flag |= IN_CHANGE | IN_UPDATE;
	/*
	 * If all went well, and the directory can be shortened, proceed
	 * with the truncation. Note that we have to unlock the inode for
	 * the entry that we just entered, as the truncation may need to
	 * lock other inodes which can lead to deadlock if we also hold a
	 * lock on the newly entered node.
	 */
	if (isrename == 0 && error == 0 &&
	    dp->i_endoff && dp->i_endoff < dp->i_size) {
		if (tvp != NULL)
			VOP_UNLOCK(tvp, 0);
		error = UFS_TRUNCATE(dvp, (off_t)dp->i_endoff,
		    IO_NORMAL | (DOINGASYNC(dvp) ? 0 : IO_SYNC), cr);
		if (error != 0)
			vn_printf(dvp,
			    "ufs_direnter: failed to truncate, error %d\n",
			    error);
#ifdef UFS_DIRHASH
		if (error == 0 && dp->i_dirhash != NULL)
			ufsdirhash_dirtrunc(dp, dp->i_endoff);
#endif
		error = 0;
		if (tvp != NULL)
			vn_lock(tvp, LK_EXCLUSIVE | LK_RETRY);
	}
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
ufs_dirremove(dvp, ip, flags, isrmdir)
	struct vnode *dvp;
	struct inode *ip;
	int flags;
	int isrmdir;
{
	struct inode *dp;
	struct direct *ep, *rep;
	struct buf *bp;
	int error;

	dp = VTOI(dvp);

	/*
	 * Adjust the link count early so softdep can block if necessary.
	 */
	if (ip) {
		ip->i_effnlink--;
		if (DOINGSOFTDEP(dvp)) {
			softdep_setup_unlink(dp, ip);
		} else {
			ip->i_nlink--;
			DIP_SET(ip, i_nlink, ip->i_nlink);
			ip->i_flag |= IN_CHANGE;
		}
	}
	if (flags & DOWHITEOUT) {
		/*
		 * Whiteout entry: set d_ino to UFS_WINO.
		 */
		if ((error =
		    UFS_BLKATOFF(dvp, (off_t)dp->i_offset, (char **)&ep, &bp)) != 0)
			return (error);
		ep->d_ino = UFS_WINO;
		ep->d_type = DT_WHT;
		goto out;
	}

	if ((error = UFS_BLKATOFF(dvp,
	    (off_t)(dp->i_offset - dp->i_count), (char **)&ep, &bp)) != 0)
		return (error);

	/* Set 'rep' to the entry being removed. */
	if (dp->i_count == 0)
		rep = ep;
	else
		rep = (struct direct *)((char *)ep + ep->d_reclen);
#ifdef UFS_DIRHASH
	/*
	 * Remove the dirhash entry. This is complicated by the fact
	 * that `ep' is the previous entry when dp->i_count != 0.
	 */
	if (dp->i_dirhash != NULL)
		ufsdirhash_remove(dp, rep, dp->i_offset);
#endif
	if (ip && rep->d_ino != ip->i_number)
		panic("ufs_dirremove: ip %ju does not match dirent ino %ju\n",
		    (uintmax_t)ip->i_number, (uintmax_t)rep->d_ino);
	if (dp->i_count == 0) {
		/*
		 * First entry in block: set d_ino to zero.
		 */
		ep->d_ino = 0;
	} else {
		/*
		 * Collapse new free space into previous entry.
		 */
		ep->d_reclen += rep->d_reclen;
	}
#ifdef UFS_DIRHASH
	if (dp->i_dirhash != NULL)
		ufsdirhash_checkblock(dp, (char *)ep -
		    ((dp->i_offset - dp->i_count) & (DIRBLKSIZ - 1)),
		    rounddown2(dp->i_offset, DIRBLKSIZ));
#endif
out:
	error = 0;
	if (DOINGSOFTDEP(dvp)) {
		if (ip)
			softdep_setup_remove(bp, dp, ip, isrmdir);
		if (softdep_slowdown(dvp))
			error = bwrite(bp);
		else
			bdwrite(bp);
	} else {
		if (flags & DOWHITEOUT)
			error = bwrite(bp);
		else if (DOINGASYNC(dvp))
			bdwrite(bp);
		else
			error = bwrite(bp);
	}
	dp->i_flag |= IN_CHANGE | IN_UPDATE;
	/*
	 * If the last named reference to a snapshot goes away,
	 * drop its snapshot reference so that it will be reclaimed
	 * when last open reference goes away.
	 */
	if (ip != NULL && (ip->i_flags & SF_SNAPSHOT) != 0 &&
	    ip->i_effnlink == 0)
		UFS_SNAPGONE(ip);
	return (error);
}

/*
 * Rewrite an existing directory entry to point at the inode
 * supplied.  The parameters describing the directory entry are
 * set up by a call to namei.
 */
int
ufs_dirrewrite(dp, oip, newinum, newtype, isrmdir)
	struct inode *dp, *oip;
	ino_t newinum;
	int newtype;
	int isrmdir;
{
	struct buf *bp;
	struct direct *ep;
	struct vnode *vdp = ITOV(dp);
	int error;

	/*
	 * Drop the link before we lock the buf so softdep can block if
	 * necessary.
	 */
	oip->i_effnlink--;
	if (DOINGSOFTDEP(vdp)) {
		softdep_setup_unlink(dp, oip);
	} else {
		oip->i_nlink--;
		DIP_SET(oip, i_nlink, oip->i_nlink);
		oip->i_flag |= IN_CHANGE;
	}

	error = UFS_BLKATOFF(vdp, (off_t)dp->i_offset, (char **)&ep, &bp);
	if (error)
		return (error);
	if (ep->d_namlen == 2 && ep->d_name[1] == '.' && ep->d_name[0] == '.' &&
	    ep->d_ino != oip->i_number) {
		brelse(bp);
		return (EIDRM);
	}
	ep->d_ino = newinum;
	if (!OFSFMT(vdp))
		ep->d_type = newtype;
	if (DOINGSOFTDEP(vdp)) {
		softdep_setup_directory_change(bp, dp, oip, newinum, isrmdir);
		bdwrite(bp);
	} else {
		if (DOINGASYNC(vdp)) {
			bdwrite(bp);
			error = 0;
		} else {
			error = bwrite(bp);
		}
	}
	dp->i_flag |= IN_CHANGE | IN_UPDATE;
	/*
	 * If the last named reference to a snapshot goes away,
	 * drop its snapshot reference so that it will be reclaimed
	 * when last open reference goes away.
	 */
	if ((oip->i_flags & SF_SNAPSHOT) != 0 && oip->i_effnlink == 0)
		UFS_SNAPGONE(oip);
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
ufs_dirempty(ip, parentino, cred)
	struct inode *ip;
	ino_t parentino;
	struct ucred *cred;
{
	doff_t off;
	struct dirtemplate dbuf;
	struct direct *dp = (struct direct *)&dbuf;
	int error, namlen;
	ssize_t count;
#define	MINDIRSIZ (sizeof (struct dirtemplate) / 2)

	for (off = 0; off < ip->i_size; off += dp->d_reclen) {
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
		if (dp->d_reclen == 0)
			return (0);
		/* skip empty entries */
		if (dp->d_ino == 0 || dp->d_ino == UFS_WINO)
			continue;
		/* accept only "." and ".." */
#		if (BYTE_ORDER == LITTLE_ENDIAN)
			if (OFSFMT(ITOV(ip)))
				namlen = dp->d_type;
			else
				namlen = dp->d_namlen;
#		else
			namlen = dp->d_namlen;
#		endif
		if (namlen > 2)
			return (0);
		if (dp->d_name[0] != '.')
			return (0);
		/*
		 * At this point namlen must be 1 or 2.
		 * 1 implies ".", 2 implies ".." if second
		 * char is also "."
		 */
		if (namlen == 1 && dp->d_ino == ip->i_number)
			continue;
		if (dp->d_name[1] == '.' && dp->d_ino == parentino)
			continue;
		return (0);
	}
	return (1);
}

static int
ufs_dir_dd_ino(struct vnode *vp, struct ucred *cred, ino_t *dd_ino,
    struct vnode **dd_vp)
{
	struct dirtemplate dirbuf;
	struct vnode *ddvp;
	int error, namlen;

	ASSERT_VOP_LOCKED(vp, "ufs_dir_dd_ino");
	if (vp->v_type != VDIR)
		return (ENOTDIR);
	/*
	 * First check to see if we have it in the name cache.
	 */
	if ((ddvp = vn_dir_dd_ino(vp)) != NULL) {
		KASSERT(ddvp->v_mount == vp->v_mount,
		    ("ufs_dir_dd_ino: Unexpected mount point crossing"));
		*dd_ino = VTOI(ddvp)->i_number;
		*dd_vp = ddvp;
		return (0);
	}
	/*
	 * Have to read the directory.
	 */
	error = vn_rdwr(UIO_READ, vp, (caddr_t)&dirbuf,
	    sizeof (struct dirtemplate), (off_t)0, UIO_SYSSPACE,
	    IO_NODELOCKED | IO_NOMACCHECK, cred, NOCRED, NULL, NULL);
	if (error != 0)
		return (error);
#if (BYTE_ORDER == LITTLE_ENDIAN)
	if (OFSFMT(vp))
		namlen = dirbuf.dotdot_type;
	else
		namlen = dirbuf.dotdot_namlen;
#else
	namlen = dirbuf.dotdot_namlen;
#endif
	if (namlen != 2 || dirbuf.dotdot_name[0] != '.' ||
	    dirbuf.dotdot_name[1] != '.')
		return (ENOTDIR);
	*dd_ino = dirbuf.dotdot_ino;
	*dd_vp = NULL;
	return (0);
}

/*
 * Check if source directory is in the path of the target directory.
 */
int
ufs_checkpath(ino_t source_ino, ino_t parent_ino, struct inode *target, struct ucred *cred, ino_t *wait_ino)
{
	struct mount *mp;
	struct vnode *tvp, *vp, *vp1;
	int error;
	ino_t dd_ino;

	vp = tvp = ITOV(target);
	mp = vp->v_mount;
	*wait_ino = 0;
	if (target->i_number == source_ino)
		return (EEXIST);
	if (target->i_number == parent_ino)
		return (0);
	if (target->i_number == UFS_ROOTINO)
		return (0);
	for (;;) {
		error = ufs_dir_dd_ino(vp, cred, &dd_ino, &vp1);
		if (error != 0)
			break;
		if (dd_ino == source_ino) {
			error = EINVAL;
			break;
		}
		if (dd_ino == UFS_ROOTINO)
			break;
		if (dd_ino == parent_ino)
			break;
		if (vp1 == NULL) {
			error = VFS_VGET(mp, dd_ino, LK_SHARED | LK_NOWAIT,
			    &vp1);
			if (error != 0) {
				*wait_ino = dd_ino;
				break;
			}
		}
		KASSERT(dd_ino == VTOI(vp1)->i_number,
		    ("directory %ju reparented\n",
		    (uintmax_t)VTOI(vp1)->i_number));
		if (vp != tvp)
			vput(vp);
		vp = vp1;
	}

	if (error == ENOTDIR)
		panic("checkpath: .. not a directory\n");
	if (vp1 != NULL)
		vput(vp1);
	if (vp != tvp)
		vput(vp);
	return (error);
}
