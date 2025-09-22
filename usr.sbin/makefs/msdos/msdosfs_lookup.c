/*	$OpenBSD: msdosfs_lookup.c,v 1.4 2021/10/06 00:40:41 deraadt Exp $	*/
/*	$NetBSD: msdosfs_lookup.c,v 1.35 2016/01/30 09:59:27 mlelstv Exp $	*/

/*-
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
/*
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


#include "ffs/buf.h"

#include <msdosfs/bpb.h>
#include "msdos/direntry.h"
#include "msdos/denode.h"
#include "msdos/msdosfsmount.h"
#include "msdos/fat.h"



/*
 * dep  - directory entry to copy into the directory
 * ddep - directory to add to
 * depp - return the address of the denode for the created directory entry
 *	  if depp != 0
 * cnp  - componentname needed for Win95 long filenames
 */
int
createde(struct denode *dep, struct denode *ddep, struct denode **depp, struct componentname *cnp)
{
	int error, rberror;
	u_long dirclust, clusoffset;
	u_long fndoffset, havecnt = 0, wcnt = 1, i;
	struct direntry *ndep;
	struct msdosfsmount *pmp = ddep->de_pmp;
	struct mkfsbuf *bp;
	daddr_t bn;
	int blsize;
#define async 0

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
		u_long needlen = ddep->de_fndoffset + sizeof(struct direntry)
		    - ddep->de_FileSize;
		dirclust = de_clcount(pmp, needlen);
		if ((error = extendfile(ddep, dirclust, 0, 0, DE_CLEAR)) != 0) {
			(void)detrunc(ddep, ddep->de_FileSize, 0);
			goto err_norollback;
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
		goto err_norollback;
	clusoffset = ddep->de_fndoffset;
	if (dirclust != MSDOSFSROOT)
		clusoffset &= pmp->pm_crbomask;
	if ((error = bread(pmp->pm_devvp, de_bn2kb(pmp, bn), blsize,
	    B_MODIFY, &bp)) != 0) {
		goto err_norollback;
	}
	ndep = bptoep(pmp, bp, clusoffset);

	DE_EXTERNALIZE(ndep, dep);

	/*
	 * Now write the Win95 long name
	 */
	if (ddep->de_fndcnt > 0) {
		u_int8_t chksum = winChksum(ndep->deName);
		u_char *un = cnp->cn_nameptr;
		int unlen = cnp->cn_namelen;
		u_long xhavecnt;

		fndoffset = ddep->de_fndoffset;
		xhavecnt = ddep->de_fndcnt + 1;

		for(; wcnt < xhavecnt; wcnt++) {
			if ((fndoffset & pmp->pm_crbomask) == 0) {
				/* we should never get here if ddep is root
				 * directory */

				if (async)
					(void) bdwrite(bp);
				else if ((error = bwrite(bp)) != 0)
					goto rollback;

				fndoffset -= sizeof(struct direntry);
				error = pcbmap(ddep,
					       de_cluster(pmp, fndoffset),
					       &bn, 0, &blsize);
				if (error)
					goto rollback;

				error = bread(pmp->pm_devvp, de_bn2kb(pmp, bn),
				    blsize, B_MODIFY, &bp);
				if (error) {
					goto rollback;
				}
				ndep = bptoep(pmp, bp,
						fndoffset & pmp->pm_crbomask);
			} else {
				ndep--;
				fndoffset -= sizeof(struct direntry);
			}
			if (!unix2winfn(un, unlen, (struct winentry *)ndep,
					wcnt, chksum))
				break;
		}
	}

	if (async)
		bdwrite(bp);
	else if ((error = bwrite(bp)) != 0)
		goto rollback;

	/*
	 * If they want us to return with the denode gotten.
	 */
	if (depp) {
		u_long diroffset = clusoffset;

		if (dep->de_Attributes & ATTR_DIRECTORY) {
			dirclust = dep->de_StartCluster;
			if (FAT32(pmp) && dirclust == pmp->pm_rootdirblk)
				dirclust = MSDOSFSROOT;
			if (dirclust == MSDOSFSROOT)
				diroffset = MSDOSFSROOT_OFS;
			else
				diroffset = 0;
		}
		error = deget(pmp, dirclust, diroffset, depp);
		return error;
	}

	return 0;

    rollback:
	/*
	 * Mark all slots modified so far as deleted. Note that we
	 * can't just call removede(), since directory is not in
	 * consistent state.
	 */
	fndoffset = ddep->de_fndoffset;
	rberror = pcbmap(ddep, de_cluster(pmp, fndoffset),
	       &bn, NULL, &blsize);
	if (rberror)
		goto err_norollback;
	if ((rberror = bread(pmp->pm_devvp, de_bn2kb(pmp, bn), blsize,
	    B_MODIFY, &bp)) != 0) {
		goto err_norollback;
	}
	ndep = bptoep(pmp, bp, clusoffset);

	havecnt = ddep->de_fndcnt + 1;
	for(i = wcnt; i <= havecnt; i++) {
		/* mark entry as deleted */
		ndep->deName[0] = SLOT_DELETED;

		if ((fndoffset & pmp->pm_crbomask) == 0) {
			/* we should never get here if ddep is root
			 * directory */

			if (async)
				bdwrite(bp);
			else if ((rberror = bwrite(bp)) != 0)
				goto err_norollback;

			fndoffset -= sizeof(struct direntry);
			rberror = pcbmap(ddep,
				       de_cluster(pmp, fndoffset),
				       &bn, 0, &blsize);
			if (rberror)
				goto err_norollback;

			rberror = bread(pmp->pm_devvp, de_bn2kb(pmp, bn),
			    blsize, B_MODIFY, &bp);
			if (rberror) {
				goto err_norollback;
			}
			ndep = bptoep(pmp, bp, fndoffset);
		} else {
			ndep--;
			fndoffset -= sizeof(struct direntry);
		}
	}

	/* ignore any further error */
	if (async)
		(void) bdwrite(bp);
	else
		(void) bwrite(bp);

    err_norollback:
	return error;
}

/*
 * Read in the disk block containing the directory entry (dirclu, dirofs)
 * and return the address of the buf header, and the address of the
 * directory entry within the block.
 */
int
readep(struct msdosfsmount *pmp, u_long dirclust, u_long diroffset, struct mkfsbuf **bpp, struct direntry **epp)
{
	int error;
	daddr_t bn;
	int blsize;

	blsize = pmp->pm_bpcluster;
	if (dirclust == MSDOSFSROOT
	    && de_blk(pmp, diroffset + blsize) > pmp->pm_rootdirsize)
		blsize = de_bn2off(pmp, pmp->pm_rootdirsize) & pmp->pm_crbomask;
	bn = detobn(pmp, dirclust, diroffset);
	if ((error = bread(pmp->pm_devvp, de_bn2kb(pmp, bn), blsize,
	    0, bpp)) != 0) {
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
readde(struct denode *dep, struct mkfsbuf **bpp, struct direntry **epp)
{
	return (readep(dep->de_pmp, dep->de_dirclust, dep->de_diroffset,
			bpp, epp));
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
	struct mkfsbuf *bp;
	int error;

	for (gen = 1;; gen++) {
		/*
		 * Generate DOS name with generation number
		 */
		if (!unix2dosfn(cnp->cn_nameptr, cp, cnp->cn_namelen, gen))
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
			error = bread(pmp->pm_devvp, de_bn2kb(pmp, bn), blsize,
			    0, &bp);
			if (error) {
				return error;
			}
			for (dentp = (struct direntry *)bp->b_data;
			     (char *)dentp < (char *)bp->b_data + blsize;
			     dentp++) {
				if (dentp->deName[0] == SLOT_EMPTY) {
					/*
					 * Last used entry and not found
					 */
					brelse(bp, 0);
					return 0;
				}
				/*
				 * Ignore volume labels and Win95 entries
				 */
				if (dentp->deAttributes & ATTR_VOLUME)
					continue;
				if (!memcmp(dentp->deName, cp, 11)) {
					error = EEXIST;
					break;
				}
			}
			brelse(bp, 0);
		}
	}
}
