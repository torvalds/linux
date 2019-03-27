/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp).
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
 *	from: @(#)ufs_lookup.c	7.33 (Berkeley) 5/19/91
 *	@(#)cd9660_lookup.c	8.2 (Berkeley) 1/23/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/mount.h>

#include <fs/cd9660/iso.h>
#include <fs/cd9660/cd9660_node.h>
#include <fs/cd9660/iso_rrip.h>

struct cd9660_ino_alloc_arg {
	cd_ino_t ino;
	cd_ino_t i_ino;
	struct iso_directory_record *ep;
};

static int
cd9660_ino_alloc(struct mount *mp, void *arg, int lkflags,
    struct vnode **vpp)
{
	struct cd9660_ino_alloc_arg *dd_arg;

	dd_arg = arg;
	return (cd9660_vget_internal(mp, dd_arg->i_ino, lkflags, vpp,
	    dd_arg->i_ino != dd_arg->ino, dd_arg->ep));
}

/*
 * Convert a component of a pathname into a pointer to a locked inode.
 * This is a very central and rather complicated routine.
 * If the filesystem is not maintained in a strict tree hierarchy,
 * this can result in a deadlock situation (see comments in code below).
 *
 * The flag argument is LOOKUP, CREATE, RENAME, or DELETE depending on
 * whether the name is to be looked up, created, renamed, or deleted.
 * When CREATE, RENAME, or DELETE is specified, information usable in
 * creating, renaming, or deleting a directory entry may be calculated.
 * If flag has LOCKPARENT or'ed into it and the target of the pathname
 * exists, lookup returns both the target and its parent directory locked.
 * When creating or renaming and LOCKPARENT is specified, the target may
 * not be ".".  When deleting and LOCKPARENT is specified, the target may
 * be "."., but the caller must check to ensure it does an vrele and iput
 * instead of two iputs.
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
 *
 * NOTE: (LOOKUP | LOCKPARENT) currently returns the parent inode unlocked.
 */
int
cd9660_lookup(ap)
	struct vop_cachedlookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct vnode *vdp;		/* vnode for directory being searched */
	struct iso_node *dp;		/* inode for directory being searched */
	struct iso_mnt *imp;		/* filesystem that directory is in */
	struct buf *bp;			/* a buffer of directory entries */
	struct iso_directory_record *ep;/* the current directory entry */
	struct iso_directory_record *ep2;/* copy of current directory entry */
	int entryoffsetinblock;		/* offset of ep in bp's buffer */
	int saveoffset = 0;		/* offset of last directory entry in dir */
	doff_t i_diroff;		/* cached i_diroff value. */
	doff_t i_offset;		/* cached i_offset value. */
	int numdirpasses;		/* strategy for directory search */
	doff_t endsearch;		/* offset to end directory search */
	struct vnode *pdp;		/* saved dp during symlink work */
	struct vnode *tdp;		/* returned by cd9660_vget_internal */
	struct cd9660_ino_alloc_arg dd_arg;
	u_long bmask;			/* block offset mask */
	int error;
	cd_ino_t ino, i_ino;
	int ltype, reclen;
	u_short namelen;
	int isoflags;
	char altname[NAME_MAX];
	int res;
	int assoc, len;
	char *name;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	int flags = cnp->cn_flags;
	int nameiop = cnp->cn_nameiop;

	ep2 = ep = NULL;
	bp = NULL;
	*vpp = NULL;
	vdp = ap->a_dvp;
	dp = VTOI(vdp);
	imp = dp->i_mnt;

	/*
	 * We now have a segment name to search for, and a directory to search.
	 */
	ino = reclen = 0;
	i_diroff = dp->i_diroff;
	len = cnp->cn_namelen;
	name = cnp->cn_nameptr;

	/*
	 * A leading `=' means, we are looking for an associated file
	 */
	if ((assoc = (imp->iso_ftype != ISO_FTYPE_RRIP && *name == ASSOCCHAR)))
	{
		len--;
		name++;
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
	bmask = imp->im_bmask;
	if (nameiop != LOOKUP || i_diroff == 0 || i_diroff > dp->i_size) {
		entryoffsetinblock = 0;
		i_offset = 0;
		numdirpasses = 1;
	} else {
		i_offset = i_diroff;
		if ((entryoffsetinblock = i_offset & bmask) &&
		    (error = cd9660_blkatoff(vdp, (off_t)i_offset, NULL, &bp)))
				return (error);
		numdirpasses = 2;
		nchstats.ncs_2passes++;
	}
	endsearch = dp->i_size;

searchloop:
	while (i_offset < endsearch) {
		/*
		 * If offset is on a block boundary,
		 * read the next directory block.
		 * Release previous if it exists.
		 */
		if ((i_offset & bmask) == 0) {
			if (bp != NULL)
				brelse(bp);
			if ((error =
			    cd9660_blkatoff(vdp, (off_t)i_offset, NULL, &bp)) != 0)
				return (error);
			entryoffsetinblock = 0;
		}
		/*
		 * Get pointer to next entry.
		 */
		ep = (struct iso_directory_record *)
			((char *)bp->b_data + entryoffsetinblock);

		reclen = isonum_711(ep->length);
		if (reclen == 0) {
			/* skip to next block, if any */
			i_offset =
			    (i_offset & ~bmask) + imp->logical_block_size;
			continue;
		}

		if (reclen < ISO_DIRECTORY_RECORD_SIZE)
			/* illegal entry, stop */
			break;

		if (entryoffsetinblock + reclen > imp->logical_block_size)
			/* entries are not allowed to cross boundaries */
			break;

		namelen = isonum_711(ep->name_len);
		isoflags = isonum_711(imp->iso_ftype == ISO_FTYPE_HIGH_SIERRA?
				      &ep->date[6]: ep->flags);

		if (reclen < ISO_DIRECTORY_RECORD_SIZE + namelen)
			/* illegal entry, stop */
			break;

		/*
		 * Check for a name match.
		 */
		switch (imp->iso_ftype) {
		default:
			if (!(isoflags & 4) == !assoc) {
				if ((len == 1
				     && *name == '.')
				    || (flags & ISDOTDOT)) {
					if (namelen == 1
					    && ep->name[0] == ((flags & ISDOTDOT) ? 1 : 0)) {
						/*
						 * Save directory entry's inode number and
						 * release directory buffer.
						 */
						i_ino = isodirino(ep, imp);
						goto found;
					}
					if (namelen != 1
					    || ep->name[0] != 0)
						goto notfound;
				} else if (!(res = isofncmp(name, len,
							    ep->name, namelen,
							    imp->joliet_level,
							    imp->im_flags,
							    imp->im_d2l,
							    imp->im_l2d))) {
					if (isoflags & 2)
						ino = isodirino(ep, imp);
					else
						ino = dbtob(bp->b_blkno)
							+ entryoffsetinblock;
					saveoffset = i_offset;
				} else if (ino)
					goto foundino;
#ifdef	NOSORTBUG	/* On some CDs directory entries are not sorted correctly */
				else if (res < 0)
					goto notfound;
				else if (res > 0 && numdirpasses == 2)
					numdirpasses++;
#endif
			}
			break;
		case ISO_FTYPE_RRIP:
			if (isonum_711(ep->flags)&2)
				ino = isodirino(ep, imp);
			else
				ino = dbtob(bp->b_blkno) + entryoffsetinblock;
			i_ino = ino;
			cd9660_rrip_getname(ep, altname, &namelen, &i_ino, imp);
			if (namelen == cnp->cn_namelen
			    && !bcmp(name,altname,namelen))
				goto found;
			ino = 0;
			break;
		}
		i_offset += reclen;
		entryoffsetinblock += reclen;
	}
	if (ino) {
foundino:
		i_ino = ino;
		if (saveoffset != i_offset) {
			if (lblkno(imp, i_offset) !=
			    lblkno(imp, saveoffset)) {
				if (bp != NULL)
					brelse(bp);
				if ((error = cd9660_blkatoff(vdp,
				    (off_t)saveoffset, NULL, &bp)) != 0)
					return (error);
			}
			entryoffsetinblock = saveoffset & bmask;
			ep = (struct iso_directory_record *)
				((char *)bp->b_data + entryoffsetinblock);
			reclen = isonum_711(ep->length);
			i_offset = saveoffset;
		}
		goto found;
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
	 * Insert name into cache (as non-existent) if appropriate.
	 */
	if (cnp->cn_flags & MAKEENTRY)
		cache_enter(vdp, *vpp, cnp);
	if (nameiop == CREATE || nameiop == RENAME)
		return (EROFS);
	return (ENOENT);

found:
	if (numdirpasses == 2)
		nchstats.ncs_pass2++;

	/*
	 * Found component in pathname.
	 * If the final component of path name, save information
	 * in the cache as to where the entry was found.
	 */
	if ((flags & ISLASTCN) && nameiop == LOOKUP)
		dp->i_diroff = i_offset;

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
	 * and parent directories are removed before the `vget' for the
	 * inode associated with ".." returns.  We hope that this occurs
	 * infrequently since we cannot avoid this race condition without
	 * implementing a sophisticated deadlock detection algorithm.
	 * Note also that this simple deadlock detection scheme will not
	 * work if the filesystem has any hard links other than ".."
	 * that point backwards in the directory structure.
	 */
	pdp = vdp;

	/*
	 * Make a copy of the directory entry for non "." lookups so
	 * we can drop the buffer before calling vget() to avoid a
	 * lock order reversal between the vnode lock and the buffer
	 * lock.
	 */
	if (dp->i_number != i_ino) {
		ep2 = malloc(reclen, M_TEMP, M_WAITOK);
		memcpy(ep2, ep, reclen);
		ep = ep2;
	}
	brelse(bp);

	/*
	 * If ino is different from i_ino,
	 * it's a relocated directory.
	 */
	if (flags & ISDOTDOT) {
		dd_arg.ino = ino;
		dd_arg.i_ino = i_ino;
		dd_arg.ep = ep;
		error = vn_vget_ino_gen(pdp, cd9660_ino_alloc, &dd_arg,
		    cnp->cn_lkflags, &tdp);
		free(ep2, M_TEMP);
		if (error != 0)
			return (error);
		*vpp = tdp;
	} else if (dp->i_number == i_ino) {
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
		}
		*vpp = vdp;
	} else {
		error = cd9660_vget_internal(vdp->v_mount, i_ino,
					     cnp->cn_lkflags, &tdp,
					     i_ino != ino, ep);
		free(ep2, M_TEMP);
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

/*
 * Return buffer with the contents of block "offset" from the beginning of
 * directory "ip".  If "res" is non-zero, fill it in with a pointer to the
 * remaining space in the directory.
 */
int
cd9660_blkatoff(vp, offset, res, bpp)
	struct vnode *vp;
	off_t offset;
	char **res;
	struct buf **bpp;
{
	struct iso_node *ip;
	struct iso_mnt *imp;
	struct buf *bp;
	daddr_t lbn;
	int bsize, bshift, error;

	ip = VTOI(vp);
	imp = ip->i_mnt;
	lbn = lblkno(imp, offset);
	bsize = blksize(imp, ip, lbn);
	bshift = imp->im_bshift;

	if ((error = bread(vp, lbn, bsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		*bpp = NULL;
		return (error);
	}

	/*
	 * We must BMAP the buffer because the directory code may use b_blkno
	 * to calculate the inode for certain types of directory entries.
	 * We could get away with not doing it before we VMIO-backed the
	 * directories because the buffers would get freed atomically with
	 * the invalidation of their data.  But with VMIO-backed buffers
	 * the buffers may be freed and then later reconstituted - and the
	 * reconstituted buffer will have no knowledge of b_blkno.
	 */
	if (bp->b_blkno == bp->b_lblkno) {
	        bp->b_blkno = (ip->iso_start + bp->b_lblkno) << (bshift - DEV_BSHIFT);
        }

	if (res)
		*res = (char *)bp->b_data + blkoff(imp, offset);
	*bpp = bp;
	return (0);
}
