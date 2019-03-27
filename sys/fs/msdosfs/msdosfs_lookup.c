/* $FreeBSD$ */
/*	$NetBSD: msdosfs_lookup.c,v 1.37 1997/11/17 15:36:54 ws Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1994, 1995, 1997 Wolfgang Solfrank.
 * Copyright (C) 1994, 1995, 1997 TooLs GmbH.
 * All rights reserved.
 * Original code by Paul Popelka (paulp@uts.amdahl.com) (see below).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Written by Paul Popelka (paulp@uts.amdahl.com)
 *
 * You can do anything you want with this software, just don't say you wrote
 * it, and don't remove this notice.
 *
 * This software is provided "as is".
 *
 * The author supplies this software to be publicly redistributed on the
 * understanding that the author is not responsible for the correct
 * functioning of this software in any circumstances and is not liable for
 * any damages caused by this software.
 *
 * October 1992
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>

#include <fs/msdosfs/bpb.h>
#include <fs/msdosfs/direntry.h>
#include <fs/msdosfs/denode.h>
#include <fs/msdosfs/fat.h>
#include <fs/msdosfs/msdosfsmount.h>

static int msdosfs_lookup_(struct vnode *vdp, struct vnode **vpp,
    struct componentname *cnp, uint64_t *inum);

int
msdosfs_lookup(struct vop_cachedlookup_args *ap)
{

	return (msdosfs_lookup_(ap->a_dvp, ap->a_vpp, ap->a_cnp, NULL));
}

struct deget_dotdot {
	u_long cluster;
	int blkoff;
};

static int
msdosfs_deget_dotdot(struct mount *mp, void *arg, int lkflags,
    struct vnode **rvp)
{
	struct deget_dotdot *dd_arg;
	struct denode *rdp;
	struct msdosfsmount *pmp;
	int error;

	pmp = VFSTOMSDOSFS(mp);
	dd_arg = arg;
	error = deget(pmp, dd_arg->cluster, dd_arg->blkoff,  &rdp);
	if (error == 0)
		*rvp = DETOV(rdp);
	return (error);
}

/*
 * When we search a directory the blocks containing directory entries are
 * read and examined.  The directory entries contain information that would
 * normally be in the inode of a unix filesystem.  This means that some of
 * a directory's contents may also be in memory resident denodes (sort of
 * an inode).  This can cause problems if we are searching while some other
 * process is modifying a directory.  To prevent one process from accessing
 * incompletely modified directory information we depend upon being the
 * sole owner of a directory block.  bread/brelse provide this service.
 * This being the case, when a process modifies a directory it must first
 * acquire the disk block that contains the directory entry to be modified.
 * Then update the disk block and the denode, and then write the disk block
 * out to disk.  This way disk blocks containing directory entries and in
 * memory denode's will be in synch.
 */
static int
msdosfs_lookup_(struct vnode *vdp, struct vnode **vpp,
    struct componentname *cnp, uint64_t *dd_inum)
{
	struct mbnambuf nb;
	daddr_t bn;
	int error;
	int slotcount;
	int slotoffset = 0;
	int frcn;
	u_long cluster;
	int blkoff;
	int diroff;
	int blsize;
	int isadir;		/* ~0 if found direntry is a directory	 */
	u_long scn;		/* starting cluster number		 */
	struct vnode *pdp;
	struct denode *dp;
	struct denode *tdp;
	struct msdosfsmount *pmp;
	struct buf *bp = NULL;
	struct direntry *dep = NULL;
	struct deget_dotdot dd_arg;
	u_char dosfilename[12];
	int flags = cnp->cn_flags;
	int nameiop = cnp->cn_nameiop;
	int unlen;
	uint64_t inode1;

	int wincnt = 1;
	int chksum = -1, chksum_ok;
	int olddos = 1;

#ifdef MSDOSFS_DEBUG
	printf("msdosfs_lookup(): looking for %s\n", cnp->cn_nameptr);
#endif
	dp = VTODE(vdp);
	pmp = dp->de_pmp;
#ifdef MSDOSFS_DEBUG
	printf("msdosfs_lookup(): vdp %p, dp %p, Attr %02x\n",
	    vdp, dp, dp->de_Attributes);
#endif

 restart:
	if (vpp != NULL)
		*vpp = NULL;
	/*
	 * If they are going after the . or .. entry in the root directory,
	 * they won't find it.  DOS filesystems don't have them in the root
	 * directory.  So, we fake it. deget() is in on this scam too.
	 */
	if ((vdp->v_vflag & VV_ROOT) && cnp->cn_nameptr[0] == '.' &&
	    (cnp->cn_namelen == 1 ||
		(cnp->cn_namelen == 2 && cnp->cn_nameptr[1] == '.'))) {
		isadir = ATTR_DIRECTORY;
		scn = MSDOSFSROOT;
#ifdef MSDOSFS_DEBUG
		printf("msdosfs_lookup(): looking for . or .. in root directory\n");
#endif
		cluster = MSDOSFSROOT;
		blkoff = MSDOSFSROOT_OFS;
		goto foundroot;
	}

	switch (unix2dosfn((const u_char *)cnp->cn_nameptr, dosfilename,
	    cnp->cn_namelen, 0, pmp)) {
	case 0:
		return (EINVAL);
	case 1:
		break;
	case 2:
		wincnt = winSlotCnt((const u_char *)cnp->cn_nameptr,
		    cnp->cn_namelen, pmp) + 1;
		break;
	case 3:
		olddos = 0;
		wincnt = winSlotCnt((const u_char *)cnp->cn_nameptr,
		    cnp->cn_namelen, pmp) + 1;
		break;
	}
	if (pmp->pm_flags & MSDOSFSMNT_SHORTNAME) {
		wincnt = 1;
		olddos = 1;
	}
	unlen = winLenFixup(cnp->cn_nameptr, cnp->cn_namelen);

	/*
	 * Suppress search for slots unless creating
	 * file and at end of pathname, in which case
	 * we watch for a place to put the new file in
	 * case it doesn't already exist.
	 */
	slotcount = wincnt;
	if ((nameiop == CREATE || nameiop == RENAME) &&
	    (flags & ISLASTCN))
		slotcount = 0;

#ifdef MSDOSFS_DEBUG
	printf("msdosfs_lookup(): dos version of filename %s, length %ld\n",
	    dosfilename, cnp->cn_namelen);
#endif
	/*
	 * Search the directory pointed at by vdp for the name pointed at
	 * by cnp->cn_nameptr.
	 */
	tdp = NULL;
	mbnambuf_init(&nb);
	/*
	 * The outer loop ranges over the clusters that make up the
	 * directory.  Note that the root directory is different from all
	 * other directories.  It has a fixed number of blocks that are not
	 * part of the pool of allocatable clusters.  So, we treat it a
	 * little differently. The root directory starts at "cluster" 0.
	 */
	diroff = 0;
	for (frcn = 0;; frcn++) {
		error = pcbmap(dp, frcn, &bn, &cluster, &blsize);
		if (error) {
			if (error == E2BIG)
				break;
			return (error);
		}
		error = bread(pmp->pm_devvp, bn, blsize, NOCRED, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		for (blkoff = 0; blkoff < blsize;
		     blkoff += sizeof(struct direntry),
		     diroff += sizeof(struct direntry)) {
			dep = (struct direntry *)(bp->b_data + blkoff);
			/*
			 * If the slot is empty and we are still looking
			 * for an empty then remember this one.  If the
			 * slot is not empty then check to see if it
			 * matches what we are looking for.  If the slot
			 * has never been filled with anything, then the
			 * remainder of the directory has never been used,
			 * so there is no point in searching it.
			 */
			if (dep->deName[0] == SLOT_EMPTY ||
			    dep->deName[0] == SLOT_DELETED) {
				/*
				 * Drop memory of previous long matches
				 */
				chksum = -1;
				mbnambuf_init(&nb);

				if (slotcount < wincnt) {
					slotcount++;
					slotoffset = diroff;
				}
				if (dep->deName[0] == SLOT_EMPTY) {
					brelse(bp);
					goto notfound;
				}
			} else {
				/*
				 * If there wasn't enough space for our winentries,
				 * forget about the empty space
				 */
				if (slotcount < wincnt)
					slotcount = 0;

				/*
				 * Check for Win95 long filename entry
				 */
				if (dep->deAttributes == ATTR_WIN95) {
					if (pmp->pm_flags & MSDOSFSMNT_SHORTNAME)
						continue;

					chksum = win2unixfn(&nb,
					    (struct winentry *)dep, chksum,
					    pmp);
					continue;
				}

				chksum = winChkName(&nb,
				    (const u_char *)cnp->cn_nameptr, unlen,
				    chksum, pmp);
				if (chksum == -2) {
					chksum = -1;
					continue;
				}

				/*
				 * Ignore volume labels (anywhere, not just
				 * the root directory).
				 */
				if (dep->deAttributes & ATTR_VOLUME) {
					chksum = -1;
					continue;
				}

				/*
				 * Check for a checksum or name match
				 */
				chksum_ok = (chksum == winChksum(dep->deName));
				if (!chksum_ok
				    && (!olddos || bcmp(dosfilename, dep->deName, 11))) {
					chksum = -1;
					continue;
				}
#ifdef MSDOSFS_DEBUG
				printf("msdosfs_lookup(): match blkoff %d, diroff %d\n",
				    blkoff, diroff);
#endif
				/*
				 * Remember where this directory
				 * entry came from for whoever did
				 * this lookup.
				 */
				dp->de_fndoffset = diroff;
				if (chksum_ok && nameiop == RENAME) {
					/*
					 * Target had correct long name
					 * directory entries, reuse them
					 * as needed.
					 */
					dp->de_fndcnt = wincnt - 1;
				} else {
					/*
					 * Long name directory entries
					 * not present or corrupt, can only
					 * reuse dos directory entry.
					 */
					dp->de_fndcnt = 0;
				}

				goto found;
			}
		}	/* for (blkoff = 0; .... */
		/*
		 * Release the buffer holding the directory cluster just
		 * searched.
		 */
		brelse(bp);
	}	/* for (frcn = 0; ; frcn++) */

notfound:
	/*
	 * We hold no disk buffers at this point.
	 */

	/*
	 * Fixup the slot description to point to the place where
	 * we might put the new DOS direntry (putting the Win95
	 * long name entries before that)
	 */
	if (!slotcount) {
		slotcount = 1;
		slotoffset = diroff;
	}
	if (wincnt > slotcount)
		slotoffset += sizeof(struct direntry) * (wincnt - slotcount);

	/*
	 * If we get here we didn't find the entry we were looking for. But
	 * that's ok if we are creating or renaming and are at the end of
	 * the pathname and the directory hasn't been removed.
	 */
#ifdef MSDOSFS_DEBUG
	printf("msdosfs_lookup(): op %d, refcnt %ld\n",
	    nameiop, dp->de_refcnt);
	printf("               slotcount %d, slotoffset %d\n",
	       slotcount, slotoffset);
#endif
	if ((nameiop == CREATE || nameiop == RENAME) &&
	    (flags & ISLASTCN) && dp->de_refcnt != 0) {
		/*
		 * Access for write is interpreted as allowing
		 * creation of files in the directory.
		 */
		error = VOP_ACCESS(vdp, VWRITE, cnp->cn_cred, cnp->cn_thread);
		if (error)
			return (error);
		/*
		 * Return an indication of where the new directory
		 * entry should be put.
		 */
		dp->de_fndoffset = slotoffset;
		dp->de_fndcnt = wincnt - 1;

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
#if 0
	/*
	 * Insert name into cache (as non-existent) if appropriate.
	 *
	 * XXX Negative caching is broken for msdosfs because the name
	 * cache doesn't understand peculiarities such as case insensitivity
	 * and 8.3 filenames.  Hence, it may not invalidate all negative
	 * entries if a file with this name is later created.
	 */
	if ((cnp->cn_flags & MAKEENTRY) != 0)
		cache_enter(vdp, *vpp, cnp);
#endif
	return (ENOENT);

found:
	/*
	 * NOTE:  We still have the buffer with matched directory entry at
	 * this point.
	 */
	isadir = dep->deAttributes & ATTR_DIRECTORY;
	scn = getushort(dep->deStartCluster);
	if (FAT32(pmp)) {
		scn |= getushort(dep->deHighClust) << 16;
		if (scn == pmp->pm_rootdirblk) {
			/*
			 * There should actually be 0 here.
			 * Just ignore the error.
			 */
			scn = MSDOSFSROOT;
		}
	}

	if (isadir) {
		cluster = scn;
		if (cluster == MSDOSFSROOT)
			blkoff = MSDOSFSROOT_OFS;
		else
			blkoff = 0;
	} else if (cluster == MSDOSFSROOT)
		blkoff = diroff;

	/*
	 * Now release buf to allow deget to read the entry again.
	 * Reserving it here and giving it to deget could result
	 * in a deadlock.
	 */
	brelse(bp);
	bp = NULL;

foundroot:
	/*
	 * If we entered at foundroot, then we are looking for the . or ..
	 * entry of the filesystems root directory.  isadir and scn were
	 * setup before jumping here.  And, bp is already null.
	 */
	if (FAT32(pmp) && scn == MSDOSFSROOT)
		scn = pmp->pm_rootdirblk;

	if (dd_inum != NULL) {
		*dd_inum = (uint64_t)pmp->pm_bpcluster * scn + blkoff;
		return (0);
	}

	/*
	 * If deleting, and at end of pathname, return
	 * parameters which can be used to remove file.
	 */
	if (nameiop == DELETE && (flags & ISLASTCN)) {
		/*
		 * Don't allow deleting the root.
		 */
		if (blkoff == MSDOSFSROOT_OFS)
			return (EBUSY);

		/*
		 * Write access to directory required to delete files.
		 */
		error = VOP_ACCESS(vdp, VWRITE, cnp->cn_cred, cnp->cn_thread);
		if (error)
			return (error);

		/*
		 * Return pointer to current entry in dp->i_offset.
		 * Save directory inode pointer in ndp->ni_dvp for dirremove().
		 */
		if (dp->de_StartCluster == scn && isadir) {	/* "." */
			VREF(vdp);
			*vpp = vdp;
			return (0);
		}
		error = deget(pmp, cluster, blkoff, &tdp);
		if (error)
			return (error);
		*vpp = DETOV(tdp);
		return (0);
	}

	/*
	 * If rewriting (RENAME), return the inode and the
	 * information required to rewrite the present directory
	 * Must get inode of directory entry to verify it's a
	 * regular file, or empty directory.
	 */
	if (nameiop == RENAME && (flags & ISLASTCN)) {
		if (blkoff == MSDOSFSROOT_OFS)
			return (EBUSY);

		error = VOP_ACCESS(vdp, VWRITE, cnp->cn_cred, cnp->cn_thread);
		if (error)
			return (error);

		/*
		 * Careful about locking second inode.
		 * This can only occur if the target is ".".
		 */
		if (dp->de_StartCluster == scn && isadir)
			return (EISDIR);

		if ((error = deget(pmp, cluster, blkoff, &tdp)) != 0)
			return (error);
		*vpp = DETOV(tdp);
		cnp->cn_flags |= SAVENAME;
		return (0);
	}

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
	 */
	pdp = vdp;
	if (flags & ISDOTDOT) {
		dd_arg.cluster = cluster;
		dd_arg.blkoff = blkoff;
		error = vn_vget_ino_gen(vdp, msdosfs_deget_dotdot,
		    &dd_arg, cnp->cn_lkflags, vpp);
		if (error != 0) {
			*vpp = NULL;
			return (error);
		}
		/*
		 * Recheck that ".." still points to the inode we
		 * looked up before pdp lock was dropped.
		 */
		error = msdosfs_lookup_(pdp, NULL, cnp, &inode1);
		if (error) {
			vput(*vpp);
			*vpp = NULL;
			return (error);
		}
		if (VTODE(*vpp)->de_inode != inode1) {
			vput(*vpp);
			goto restart;
		}
	} else if (dp->de_StartCluster == scn && isadir) {
		VREF(vdp);	/* we want ourself, ie "." */
		*vpp = vdp;
	} else {
		if ((error = deget(pmp, cluster, blkoff, &tdp)) != 0)
			return (error);
		*vpp = DETOV(tdp);
	}

	/*
	 * Insert name into cache if appropriate.
	 */
	if (cnp->cn_flags & MAKEENTRY)
		cache_enter(vdp, *vpp, cnp);
	return (0);
}

/*
 * dep  - directory entry to copy into the directory
 * ddep - directory to add to
 * depp - return the address of the denode for the created directory entry
 *	  if depp != 0
 * cnp  - componentname needed for Win95 long filenames
 */
int
createde(struct denode *dep, struct denode *ddep, struct denode **depp,
    struct componentname *cnp)
{
	int error;
	u_long dirclust, diroffset;
	struct direntry *ndep;
	struct msdosfsmount *pmp = ddep->de_pmp;
	struct buf *bp;
	daddr_t bn;
	int blsize;

#ifdef MSDOSFS_DEBUG
	printf("createde(dep %p, ddep %p, depp %p, cnp %p)\n",
	    dep, ddep, depp, cnp);
#endif

	/*
	 * If no space left in the directory then allocate another cluster
	 * and chain it onto the end of the file.  There is one exception
	 * to this.  That is, if the root directory has no more space it
	 * can NOT be expanded.  extendfile() checks for and fails attempts
	 * to extend the root directory.  We just return an error in that
	 * case.
	 */
	if (ddep->de_fndoffset >= ddep->de_FileSize) {
		diroffset = ddep->de_fndoffset + sizeof(struct direntry)
		    - ddep->de_FileSize;
		dirclust = de_clcount(pmp, diroffset);
		error = extendfile(ddep, dirclust, 0, 0, DE_CLEAR);
		if (error) {
			(void)detrunc(ddep, ddep->de_FileSize, 0, NOCRED);
			return error;
		}

		/*
		 * Update the size of the directory
		 */
		ddep->de_FileSize += de_cn2off(pmp, dirclust);
	}

	/*
	 * We just read in the cluster with space.  Copy the new directory
	 * entry in.  Then write it to disk. NOTE:  DOS directories
	 * do not get smaller as clusters are emptied.
	 */
	error = pcbmap(ddep, de_cluster(pmp, ddep->de_fndoffset),
		       &bn, &dirclust, &blsize);
	if (error)
		return error;
	diroffset = ddep->de_fndoffset;
	if (dirclust != MSDOSFSROOT)
		diroffset &= pmp->pm_crbomask;
	if ((error = bread(pmp->pm_devvp, bn, blsize, NOCRED, &bp)) != 0) {
		brelse(bp);
		return error;
	}
	ndep = bptoep(pmp, bp, ddep->de_fndoffset);

	DE_EXTERNALIZE(ndep, dep);

	/*
	 * Now write the Win95 long name
	 */
	if (ddep->de_fndcnt > 0) {
		uint8_t chksum = winChksum(ndep->deName);
		const u_char *un = (const u_char *)cnp->cn_nameptr;
		int unlen = cnp->cn_namelen;
		int cnt = 1;

		while (--ddep->de_fndcnt >= 0) {
			if (!(ddep->de_fndoffset & pmp->pm_crbomask)) {
				if (DOINGASYNC(DETOV(ddep)))
					bdwrite(bp);
				else if ((error = bwrite(bp)) != 0)
					return error;

				ddep->de_fndoffset -= sizeof(struct direntry);
				error = pcbmap(ddep,
					       de_cluster(pmp,
							  ddep->de_fndoffset),
					       &bn, 0, &blsize);
				if (error)
					return error;

				error = bread(pmp->pm_devvp, bn, blsize,
					      NOCRED, &bp);
				if (error) {
					brelse(bp);
					return error;
				}
				ndep = bptoep(pmp, bp, ddep->de_fndoffset);
			} else {
				ndep--;
				ddep->de_fndoffset -= sizeof(struct direntry);
			}
			if (!unix2winfn(un, unlen, (struct winentry *)ndep,
					cnt++, chksum, pmp))
				break;
		}
	}

	if (DOINGASYNC(DETOV(ddep)))
		bdwrite(bp);
	else if ((error = bwrite(bp)) != 0)
		return error;

	/*
	 * If they want us to return with the denode gotten.
	 */
	if (depp) {
		if (dep->de_Attributes & ATTR_DIRECTORY) {
			dirclust = dep->de_StartCluster;
			if (FAT32(pmp) && dirclust == pmp->pm_rootdirblk)
				dirclust = MSDOSFSROOT;
			if (dirclust == MSDOSFSROOT)
				diroffset = MSDOSFSROOT_OFS;
			else
				diroffset = 0;
		}
		return deget(pmp, dirclust, diroffset, depp);
	}

	return 0;
}

/*
 * Be sure a directory is empty except for "." and "..". Return 1 if empty,
 * return 0 if not empty or error.
 */
int
dosdirempty(struct denode *dep)
{
	int blsize;
	int error;
	u_long cn;
	daddr_t bn;
	struct buf *bp;
	struct msdosfsmount *pmp = dep->de_pmp;
	struct direntry *dentp;

	/*
	 * Since the filesize field in directory entries for a directory is
	 * zero, we just have to feel our way through the directory until
	 * we hit end of file.
	 */
	for (cn = 0;; cn++) {
		if ((error = pcbmap(dep, cn, &bn, 0, &blsize)) != 0) {
			if (error == E2BIG)
				return (1);	/* it's empty */
			return (0);
		}
		error = bread(pmp->pm_devvp, bn, blsize, NOCRED, &bp);
		if (error) {
			brelse(bp);
			return (0);
		}
		for (dentp = (struct direntry *)bp->b_data;
		     (char *)dentp < bp->b_data + blsize;
		     dentp++) {
			if (dentp->deName[0] != SLOT_DELETED &&
			    (dentp->deAttributes & ATTR_VOLUME) == 0) {
				/*
				 * In dos directories an entry whose name
				 * starts with SLOT_EMPTY (0) starts the
				 * beginning of the unused part of the
				 * directory, so we can just return that it
				 * is empty.
				 */
				if (dentp->deName[0] == SLOT_EMPTY) {
					brelse(bp);
					return (1);
				}
				/*
				 * Any names other than "." and ".." in a
				 * directory mean it is not empty.
				 */
				if (bcmp(dentp->deName, ".          ", 11) &&
				    bcmp(dentp->deName, "..         ", 11)) {
					brelse(bp);
#ifdef MSDOSFS_DEBUG
					printf("dosdirempty(): entry found %02x, %02x\n",
					    dentp->deName[0], dentp->deName[1]);
#endif
					return (0);	/* not empty */
				}
			}
		}
		brelse(bp);
	}
	/* NOTREACHED */
}

/*
 * Check to see if the directory described by target is in some
 * subdirectory of source.  This prevents something like the following from
 * succeeding and leaving a bunch or files and directories orphaned. mv
 * /a/b/c /a/b/c/d/e/f Where c and f are directories.
 *
 * source - the inode for /a/b/c
 * target - the inode for /a/b/c/d/e/f
 *
 * Returns 0 if target is NOT a subdirectory of source.
 * Otherwise returns a non-zero error number.
 * The target inode is always unlocked on return.
 */
int
doscheckpath(struct denode *source, struct denode *target)
{
	daddr_t scn;
	struct msdosfsmount *pmp;
	struct direntry *ep;
	struct denode *dep;
	struct buf *bp = NULL;
	int error = 0;

	dep = target;
	if ((target->de_Attributes & ATTR_DIRECTORY) == 0 ||
	    (source->de_Attributes & ATTR_DIRECTORY) == 0) {
		error = ENOTDIR;
		goto out;
	}
	if (dep->de_StartCluster == source->de_StartCluster) {
		error = EEXIST;
		goto out;
	}
	if (dep->de_StartCluster == MSDOSFSROOT)
		goto out;
	pmp = dep->de_pmp;
#ifdef	DIAGNOSTIC
	if (pmp != source->de_pmp)
		panic("doscheckpath: source and target on different filesystems");
#endif
	if (FAT32(pmp) && dep->de_StartCluster == pmp->pm_rootdirblk)
		goto out;

	for (;;) {
		if ((dep->de_Attributes & ATTR_DIRECTORY) == 0) {
			error = ENOTDIR;
			break;
		}
		scn = dep->de_StartCluster;
		error = bread(pmp->pm_devvp, cntobn(pmp, scn),
			      pmp->pm_bpcluster, NOCRED, &bp);
		if (error)
			break;

		ep = (struct direntry *) bp->b_data + 1;
		if ((ep->deAttributes & ATTR_DIRECTORY) == 0 ||
		    bcmp(ep->deName, "..         ", 11) != 0) {
			error = ENOTDIR;
			break;
		}
		scn = getushort(ep->deStartCluster);
		if (FAT32(pmp))
			scn |= getushort(ep->deHighClust) << 16;

		if (scn == source->de_StartCluster) {
			error = EINVAL;
			break;
		}
		if (scn == MSDOSFSROOT)
			break;
		if (FAT32(pmp) && scn == pmp->pm_rootdirblk) {
			/*
			 * scn should be 0 in this case,
			 * but we silently ignore the error.
			 */
			break;
		}

		vput(DETOV(dep));
		brelse(bp);
		bp = NULL;
		/* NOTE: deget() clears dep on error */
		if ((error = deget(pmp, scn, 0, &dep)) != 0)
			break;
	}
out:;
	if (bp)
		brelse(bp);
#ifdef MSDOSFS_DEBUG
	if (error == ENOTDIR)
		printf("doscheckpath(): .. not a directory?\n");
#endif
	if (dep != NULL)
		vput(DETOV(dep));
	return (error);
}

/*
 * Read in the disk block containing the directory entry (dirclu, dirofs)
 * and return the address of the buf header, and the address of the
 * directory entry within the block.
 */
int
readep(struct msdosfsmount *pmp, u_long dirclust, u_long diroffset,
    struct buf **bpp, struct direntry **epp)
{
	int error;
	daddr_t bn;
	int blsize;

	blsize = pmp->pm_bpcluster;
	if (dirclust == MSDOSFSROOT
	    && de_blk(pmp, diroffset + blsize) > pmp->pm_rootdirsize)
		blsize = de_bn2off(pmp, pmp->pm_rootdirsize) & pmp->pm_crbomask;
	bn = detobn(pmp, dirclust, diroffset);
	if ((error = bread(pmp->pm_devvp, bn, blsize, NOCRED, bpp)) != 0) {
		brelse(*bpp);
		*bpp = NULL;
		return (error);
	}
	if (epp)
		*epp = bptoep(pmp, *bpp, diroffset);
	return (0);
}

/*
 * Read in the disk block containing the directory entry dep came from and
 * return the address of the buf header, and the address of the directory
 * entry within the block.
 */
int
readde(struct denode *dep, struct buf **bpp, struct direntry **epp)
{

	return (readep(dep->de_pmp, dep->de_dirclust, dep->de_diroffset,
	    bpp, epp));
}

/*
 * Remove a directory entry. At this point the file represented by the
 * directory entry to be removed is still full length until no one has it
 * open.  When the file no longer being used msdosfs_inactive() is called
 * and will truncate the file to 0 length.  When the vnode containing the
 * denode is needed for some other purpose by VFS it will call
 * msdosfs_reclaim() which will remove the denode from the denode cache.
 *
 * pdep	directory where the entry is removed
 * dep	file to be removed
 */
int
removede(struct denode *pdep, struct denode *dep)
{
	int error;
	struct direntry *ep;
	struct buf *bp;
	daddr_t bn;
	int blsize;
	struct msdosfsmount *pmp = pdep->de_pmp;
	u_long offset = pdep->de_fndoffset;

#ifdef MSDOSFS_DEBUG
	printf("removede(): filename %s, dep %p, offset %08lx\n",
	    dep->de_Name, dep, offset);
#endif

	dep->de_refcnt--;
	offset += sizeof(struct direntry);
	do {
		offset -= sizeof(struct direntry);
		error = pcbmap(pdep, de_cluster(pmp, offset), &bn, 0, &blsize);
		if (error)
			return error;
		error = bread(pmp->pm_devvp, bn, blsize, NOCRED, &bp);
		if (error) {
			brelse(bp);
			return error;
		}
		ep = bptoep(pmp, bp, offset);
		/*
		 * Check whether, if we came here the second time, i.e.
		 * when underflowing into the previous block, the last
		 * entry in this block is a longfilename entry, too.
		 */
		if (ep->deAttributes != ATTR_WIN95
		    && offset != pdep->de_fndoffset) {
			brelse(bp);
			break;
		}
		offset += sizeof(struct direntry);
		while (1) {
			/*
			 * We are a bit aggressive here in that we delete any Win95
			 * entries preceding this entry, not just the ones we "own".
			 * Since these presumably aren't valid anyway,
			 * there should be no harm.
			 */
			offset -= sizeof(struct direntry);
			ep--->deName[0] = SLOT_DELETED;
			if ((pmp->pm_flags & MSDOSFSMNT_NOWIN95)
			    || !(offset & pmp->pm_crbomask)
			    || ep->deAttributes != ATTR_WIN95)
				break;
		}
		if (DOINGASYNC(DETOV(pdep)))
			bdwrite(bp);
		else if ((error = bwrite(bp)) != 0)
			return error;
	} while (!(pmp->pm_flags & MSDOSFSMNT_NOWIN95)
	    && !(offset & pmp->pm_crbomask)
	    && offset);
	return 0;
}

/*
 * Create a unique DOS name in dvp
 */
int
uniqdosname(struct denode *dep, struct componentname *cnp, u_char *cp)
{
	struct msdosfsmount *pmp = dep->de_pmp;
	struct direntry *dentp;
	int gen;
	int blsize;
	u_long cn;
	daddr_t bn;
	struct buf *bp;
	int error;

	if (pmp->pm_flags & MSDOSFSMNT_SHORTNAME)
		return (unix2dosfn((const u_char *)cnp->cn_nameptr, cp,
		    cnp->cn_namelen, 0, pmp) ? 0 : EINVAL);

	for (gen = 1;; gen++) {
		/*
		 * Generate DOS name with generation number
		 */
		if (!unix2dosfn((const u_char *)cnp->cn_nameptr, cp,
		    cnp->cn_namelen, gen, pmp))
			return gen == 1 ? EINVAL : EEXIST;

		/*
		 * Now look for a dir entry with this exact name
		 */
		for (cn = error = 0; !error; cn++) {
			if ((error = pcbmap(dep, cn, &bn, 0, &blsize)) != 0) {
				if (error == E2BIG)	/* EOF reached and not found */
					return 0;
				return error;
			}
			error = bread(pmp->pm_devvp, bn, blsize, NOCRED, &bp);
			if (error) {
				brelse(bp);
				return error;
			}
			for (dentp = (struct direntry *)bp->b_data;
			     (char *)dentp < bp->b_data + blsize;
			     dentp++) {
				if (dentp->deName[0] == SLOT_EMPTY) {
					/*
					 * Last used entry and not found
					 */
					brelse(bp);
					return 0;
				}
				/*
				 * Ignore volume labels and Win95 entries
				 */
				if (dentp->deAttributes & ATTR_VOLUME)
					continue;
				if (!bcmp(dentp->deName, cp, 11)) {
					error = EEXIST;
					break;
				}
			}
			brelse(bp);
		}
	}
}
