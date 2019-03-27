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

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
/* $NetBSD: msdosfs_vfsops.c,v 1.10 2016/01/30 09:59:27 mlelstv Exp $ */
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <ffs/buf.h>

#include <fs/msdosfs/bpb.h>
#include <fs/msdosfs/bootsect.h>
#include <fs/msdosfs/direntry.h>
#include <fs/msdosfs/denode.h>
#include <fs/msdosfs/msdosfsmount.h>
#include <fs/msdosfs/fat.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>

#include "makefs.h"
#include "msdos.h"
#include "mkfs_msdos.h"

#ifdef MSDOSFS_DEBUG
#define DPRINTF(a) printf a
#else
#define DPRINTF(a)
#endif

struct msdosfsmount *
msdosfs_mount(struct vnode *devvp, int flags)
{
	struct msdosfsmount *pmp = NULL;
	struct buf *bp;
	union bootsector *bsp;
	struct byte_bpb33 *b33;
	struct byte_bpb50 *b50;
	struct byte_bpb710 *b710;
	uint8_t SecPerClust;
	int	ronly = 0, error, tmp;
	int	bsize;
	struct msdos_options *m = devvp->fs->fs_specific;
	uint64_t psize = m->create_size;
	unsigned secsize = 512;

	DPRINTF(("%s(bread 0)\n", __func__));
	if ((error = bread(devvp, 0, secsize, 0, &bp)) != 0)
		goto error_exit;

	bsp = (union bootsector *)bp->b_data;
	b33 = (struct byte_bpb33 *)bsp->bs33.bsBPB;
	b50 = (struct byte_bpb50 *)bsp->bs50.bsBPB;
	b710 = (struct byte_bpb710 *)bsp->bs710.bsBPB;

	if (!(flags & MSDOSFSMNT_GEMDOSFS)) {
		if (bsp->bs50.bsBootSectSig0 != BOOTSIG0
		    || bsp->bs50.bsBootSectSig1 != BOOTSIG1) {
			DPRINTF(("bootsig0 %d bootsig1 %d\n",
			    bsp->bs50.bsBootSectSig0,
			    bsp->bs50.bsBootSectSig1));
			error = EINVAL;
			goto error_exit;
		}
		bsize = 0;
	} else
		bsize = 512;

	pmp = ecalloc(1, sizeof *pmp);
	/*
	 * Compute several useful quantities from the bpb in the
	 * bootsector.  Copy in the dos 5 variant of the bpb then fix up
	 * the fields that are different between dos 5 and dos 3.3.
	 */
	SecPerClust = b50->bpbSecPerClust;
	pmp->pm_BytesPerSec = getushort(b50->bpbBytesPerSec);
	pmp->pm_ResSectors = getushort(b50->bpbResSectors);
	pmp->pm_FATs = b50->bpbFATs;
	pmp->pm_RootDirEnts = getushort(b50->bpbRootDirEnts);
	pmp->pm_Sectors = getushort(b50->bpbSectors);
	pmp->pm_FATsecs = getushort(b50->bpbFATsecs);
	pmp->pm_SecPerTrack = getushort(b50->bpbSecPerTrack);
	pmp->pm_Heads = getushort(b50->bpbHeads);
	pmp->pm_Media = b50->bpbMedia;

	DPRINTF(("%s(BytesPerSec=%u, ResSectors=%u, FATs=%d, RootDirEnts=%u, "
	    "Sectors=%u, FATsecs=%lu, SecPerTrack=%u, Heads=%u, Media=%u)\n",
	    __func__, pmp->pm_BytesPerSec, pmp->pm_ResSectors, pmp->pm_FATs,
	    pmp->pm_RootDirEnts, pmp->pm_Sectors, pmp->pm_FATsecs,
	    pmp->pm_SecPerTrack, pmp->pm_Heads, pmp->pm_Media));
	if (!(flags & MSDOSFSMNT_GEMDOSFS)) {
		/* XXX - We should probably check more values here */
		if (!pmp->pm_BytesPerSec || !SecPerClust
			|| pmp->pm_SecPerTrack > 63) {
			DPRINTF(("bytespersec %d secperclust %d "
			    "secpertrack %d\n",
			    pmp->pm_BytesPerSec, SecPerClust,
			    pmp->pm_SecPerTrack));
			error = EINVAL;
			goto error_exit;
		}
	}

	pmp->pm_flags = flags & MSDOSFSMNT_MNTOPT;
	if (pmp->pm_flags & MSDOSFSMNT_GEMDOSFS)
		pmp->pm_flags |= MSDOSFSMNT_NOWIN95;
	if (pmp->pm_flags & MSDOSFSMNT_NOWIN95)
		pmp->pm_flags |= MSDOSFSMNT_SHORTNAME;

	if (pmp->pm_Sectors == 0) {
		pmp->pm_HiddenSects = getulong(b50->bpbHiddenSecs);
		pmp->pm_HugeSectors = getulong(b50->bpbHugeSectors);
	} else {
		pmp->pm_HiddenSects = getushort(b33->bpbHiddenSecs);
		pmp->pm_HugeSectors = pmp->pm_Sectors;
	}

	if (pmp->pm_RootDirEnts == 0) {
		unsigned short vers = getushort(b710->bpbFSVers);
		/*
		 * Some say that bsBootSectSig[23] must be zero, but
		 * Windows does not require this and some digital cameras
		 * do not set these to zero.  Therefore, do not insist.
		 */
		if (pmp->pm_Sectors || pmp->pm_FATsecs || vers) {
			DPRINTF(("sectors %d fatsecs %lu vers %d\n",
			    pmp->pm_Sectors, pmp->pm_FATsecs, vers));
			error = EINVAL;
			goto error_exit;
		}
		pmp->pm_fatmask = FAT32_MASK;
		pmp->pm_fatmult = 4;
		pmp->pm_fatdiv = 1;
		pmp->pm_FATsecs = getulong(b710->bpbBigFATsecs);

		/* mirrorring is enabled if the FATMIRROR bit is not set */
		if ((getushort(b710->bpbExtFlags) & FATMIRROR) == 0)
			pmp->pm_flags |= MSDOSFS_FATMIRROR;
		else
			pmp->pm_curfat = getushort(b710->bpbExtFlags) & FATNUM;
	} else
		pmp->pm_flags |= MSDOSFS_FATMIRROR;

	if (flags & MSDOSFSMNT_GEMDOSFS) {
		if (FAT32(pmp)) {
			DPRINTF(("FAT32 for GEMDOS\n"));
			/*
			 * GEMDOS doesn't know FAT32.
			 */
			error = EINVAL;
			goto error_exit;
		}

		/*
		 * Check a few values (could do some more):
		 * - logical sector size: power of 2, >= block size
		 * - sectors per cluster: power of 2, >= 1
		 * - number of sectors:   >= 1, <= size of partition
		 */
		if ( (SecPerClust == 0)
		  || (SecPerClust & (SecPerClust - 1))
		  || (pmp->pm_BytesPerSec < bsize)
		  || (pmp->pm_BytesPerSec & (pmp->pm_BytesPerSec - 1))
		  || (pmp->pm_HugeSectors == 0)
		  || (pmp->pm_HugeSectors * (pmp->pm_BytesPerSec / bsize)
		      > psize)) {
			DPRINTF(("consistency checks for GEMDOS\n"));
			error = EINVAL;
			goto error_exit;
		}
		/*
		 * XXX - Many parts of the msdosfs driver seem to assume that
		 * the number of bytes per logical sector (BytesPerSec) will
		 * always be the same as the number of bytes per disk block
		 * Let's pretend it is.
		 */
		tmp = pmp->pm_BytesPerSec / bsize;
		pmp->pm_BytesPerSec  = bsize;
		pmp->pm_HugeSectors *= tmp;
		pmp->pm_HiddenSects *= tmp;
		pmp->pm_ResSectors  *= tmp;
		pmp->pm_Sectors     *= tmp;
		pmp->pm_FATsecs     *= tmp;
		SecPerClust         *= tmp;
	}

	/* Check that fs has nonzero FAT size */
	if (pmp->pm_FATsecs == 0) {
		DPRINTF(("FATsecs is 0\n"));
		error = EINVAL;
		goto error_exit;
	}

	pmp->pm_fatblk = pmp->pm_ResSectors;
	if (FAT32(pmp)) {
		pmp->pm_rootdirblk = getulong(b710->bpbRootClust);
		pmp->pm_firstcluster = pmp->pm_fatblk
			+ (pmp->pm_FATs * pmp->pm_FATsecs);
		pmp->pm_fsinfo = getushort(b710->bpbFSInfo);
	} else {
		pmp->pm_rootdirblk = pmp->pm_fatblk +
			(pmp->pm_FATs * pmp->pm_FATsecs);
		pmp->pm_rootdirsize = (pmp->pm_RootDirEnts * sizeof(struct direntry)
				       + pmp->pm_BytesPerSec - 1)
			/ pmp->pm_BytesPerSec;/* in sectors */
		pmp->pm_firstcluster = pmp->pm_rootdirblk + pmp->pm_rootdirsize;
	}

	pmp->pm_nmbrofclusters = (pmp->pm_HugeSectors - pmp->pm_firstcluster) /
	    SecPerClust;
	pmp->pm_maxcluster = pmp->pm_nmbrofclusters + 1;
	pmp->pm_fatsize = pmp->pm_FATsecs * pmp->pm_BytesPerSec;

	if (flags & MSDOSFSMNT_GEMDOSFS) {
		if (pmp->pm_nmbrofclusters <= (0xff0 - 2)) {
			pmp->pm_fatmask = FAT12_MASK;
			pmp->pm_fatmult = 3;
			pmp->pm_fatdiv = 2;
		} else {
			pmp->pm_fatmask = FAT16_MASK;
			pmp->pm_fatmult = 2;
			pmp->pm_fatdiv = 1;
		}
	} else if (pmp->pm_fatmask == 0) {
		if (pmp->pm_maxcluster
		    <= ((CLUST_RSRVD - CLUST_FIRST) & FAT12_MASK)) {
			/*
			 * This will usually be a floppy disk. This size makes
			 * sure that one FAT entry will not be split across
			 * multiple blocks.
			 */
			pmp->pm_fatmask = FAT12_MASK;
			pmp->pm_fatmult = 3;
			pmp->pm_fatdiv = 2;
		} else {
			pmp->pm_fatmask = FAT16_MASK;
			pmp->pm_fatmult = 2;
			pmp->pm_fatdiv = 1;
		}
	}
	if (FAT12(pmp))
		pmp->pm_fatblocksize = 3 * pmp->pm_BytesPerSec;
	else
		pmp->pm_fatblocksize = MAXBSIZE;

	pmp->pm_fatblocksec = pmp->pm_fatblocksize / pmp->pm_BytesPerSec;
	pmp->pm_bnshift = ffs(pmp->pm_BytesPerSec) - 1;

	/*
	 * Compute mask and shift value for isolating cluster relative byte
	 * offsets and cluster numbers from a file offset.
	 */
	pmp->pm_bpcluster = SecPerClust * pmp->pm_BytesPerSec;
	pmp->pm_crbomask = pmp->pm_bpcluster - 1;
	pmp->pm_cnshift = ffs(pmp->pm_bpcluster) - 1;

	DPRINTF(("%s(fatmask=%lu, fatmult=%u, fatdiv=%u, fatblocksize=%lu, "
	    "fatblocksec=%lu, bnshift=%lu, pbcluster=%lu, crbomask=%lu, "
	    "cnshift=%lu)\n",
	    __func__, pmp->pm_fatmask, pmp->pm_fatmult, pmp->pm_fatdiv,
	    pmp->pm_fatblocksize, pmp->pm_fatblocksec, pmp->pm_bnshift,
	    pmp->pm_bpcluster, pmp->pm_crbomask, pmp->pm_cnshift));
	/*
	 * Check for valid cluster size
	 * must be a power of 2
	 */
	if (pmp->pm_bpcluster ^ (1 << pmp->pm_cnshift)) {
		DPRINTF(("bpcluster %lu cnshift %lu\n",
		    pmp->pm_bpcluster, pmp->pm_cnshift));
		error = EINVAL;
		goto error_exit;
	}

	/*
	 * Release the bootsector buffer.
	 */
	brelse(bp);
	bp = NULL;

	/*
	 * Check FSInfo.
	 */
	if (pmp->pm_fsinfo) {
		struct fsinfo *fp;

		/*
		 * XXX	If the fsinfo block is stored on media with
		 *	2KB or larger sectors, is the fsinfo structure
		 *	padded at the end or in the middle?
		 */
		DPRINTF(("%s(bread %lu)\n", __func__,
		    (unsigned long)de_bn2kb(pmp, pmp->pm_fsinfo)));
		if ((error = bread(devvp, de_bn2kb(pmp, pmp->pm_fsinfo),
		    pmp->pm_BytesPerSec, 0, &bp)) != 0)
			goto error_exit;
		fp = (struct fsinfo *)bp->b_data;
		if (!memcmp(fp->fsisig1, "RRaA", 4)
		    && !memcmp(fp->fsisig2, "rrAa", 4)
		    && !memcmp(fp->fsisig3, "\0\0\125\252", 4)
		    && !memcmp(fp->fsisig4, "\0\0\125\252", 4))
			pmp->pm_nxtfree = getulong(fp->fsinxtfree);
		else
			pmp->pm_fsinfo = 0;
		brelse(bp);
		bp = NULL;
	}

	/*
	 * Check and validate (or perhaps invalidate?) the fsinfo structure?
	 * XXX
	 */
	if (pmp->pm_fsinfo) {
		if ((pmp->pm_nxtfree == 0xffffffffUL) ||
		    (pmp->pm_nxtfree > pmp->pm_maxcluster))
			pmp->pm_fsinfo = 0;
	}

	/*
	 * Allocate memory for the bitmap of allocated clusters, and then
	 * fill it in.
	 */
	pmp->pm_inusemap = ecalloc(sizeof(*pmp->pm_inusemap),
	    ((pmp->pm_maxcluster + N_INUSEBITS) / N_INUSEBITS));
	/*
	 * fillinusemap() needs pm_devvp.
	 */
	pmp->pm_dev = 0;
	pmp->pm_devvp = devvp;

	/*
	 * Have the inuse map filled in.
	 */
	if ((error = fillinusemap(pmp)) != 0) {
		DPRINTF(("fillinusemap %d\n", error));
		goto error_exit;
	}

	/*
	 * Finish up.
	 */
	if (ronly)
		pmp->pm_flags |= MSDOSFSMNT_RONLY;
	else
		pmp->pm_fmod = 1;

	/*
	 * If we ever do quotas for DOS filesystems this would be a place
	 * to fill in the info in the msdosfsmount structure. You dolt,
	 * quotas on dos filesystems make no sense because files have no
	 * owners on dos filesystems. of course there is some empty space
	 * in the directory entry where we could put uid's and gid's.
	 */

	return pmp;

error_exit:
	if (bp)
		brelse(bp, BC_AGE);
	if (pmp) {
		if (pmp->pm_inusemap)
			free(pmp->pm_inusemap);
		free(pmp);
	}
	errno = error;
	return NULL;
}

int
msdosfs_root(struct msdosfsmount *pmp, struct vnode *vp) {
	struct denode *ndep;
	int error;

	*vp = *pmp->pm_devvp;
	if ((error = deget(pmp, MSDOSFSROOT, MSDOSFSROOT_OFS, &ndep)) != 0) {
		errno = error;
		return -1;
	}
	vp->v_data = ndep;
	return 0;
}
