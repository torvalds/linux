/*	$OpenBSD: pass1.c,v 1.19 2024/05/09 08:35:40 florian Exp $	*/
/*	$NetBSD: pass1.c,v 1.9 2000/01/31 11:40:12 bouyer Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#include <sys/param.h>	/* setbit btodb isset */
#include <sys/time.h>
#include <ufs/ext2fs/ext2fs_dinode.h>
#include <ufs/ext2fs/ext2fs_dir.h>
#include <ufs/ext2fs/ext2fs.h>

#include <ufs/ufs/dinode.h> /* for IFMT & friends */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fsck.h"
#include "extern.h"
#include "fsutil.h"

static daddr32_t badblk;
static daddr32_t dupblk;
static void checkinode(ino_t, struct inodesc *);

void
pass1(void)
{
	ino_t inumber;
	int c, i;
	daddr32_t dbase;
	struct inodesc idesc;

	/*
	 * Set file system reserved blocks in used block map.
	 */
	for (c = 0; c < sblock.e2fs_ncg; c++) {
		dbase = c * sblock.e2fs.e2fs_bpg +
		    sblock.e2fs.e2fs_first_dblock;
		/* Mark the blocks used for the inode table */
		if (letoh32(sblock.e2fs_gd[c].ext2bgd_i_tables) >= dbase) {
			for (i = 0; i < sblock.e2fs_itpg; i++)
				setbmap(
				    letoh32(sblock.e2fs_gd[c].ext2bgd_i_tables)
				    + i);
		}
		/* Mark the blocks used for the block bitmap */
		if (letoh32(sblock.e2fs_gd[c].ext2bgd_b_bitmap) >= dbase)
			setbmap(letoh32(sblock.e2fs_gd[c].ext2bgd_b_bitmap));
		/* Mark the blocks used for the inode bitmap */
		if (letoh32(sblock.e2fs_gd[c].ext2bgd_i_bitmap) >= dbase)
			setbmap(letoh32(sblock.e2fs_gd[c].ext2bgd_i_bitmap));

		if (sblock.e2fs.e2fs_rev == E2FS_REV0 ||
		    (sblock.e2fs.e2fs_features_rocompat &
			EXT2F_ROCOMPAT_SPARSE_SUPER) == 0 ||
		    cg_has_sb(c)) {
			/* Mark copuy of SB and descriptors */
			setbmap(dbase);
			for (i = 1; i <= sblock.e2fs_ngdb; i++)
				setbmap(dbase+i);
		}


		if (c == 0) {
			for(i = 0; i < dbase; i++)
				setbmap(i);
		}
	}

	/*
	 * Find all allocated blocks.
	 */
	memset(&idesc, 0, sizeof(struct inodesc));
	idesc.id_type = ADDR;
	idesc.id_func = pass1check;
	inumber = 1;
	n_files = n_blks = 0;
	resetinodebuf();
	for (c = 0; c < sblock.e2fs_ncg; c++) {
		for (i = 0;
			i < sblock.e2fs.e2fs_ipg && inumber <= sblock.e2fs.e2fs_icount;
			i++, inumber++) {
			if (inumber < EXT2_ROOTINO) /* XXX */
				continue;
			checkinode(inumber, &idesc);
		}
	}
	freeinodebuf();
}

static void
checkinode(ino_t inumber, struct inodesc *idesc)
{
	struct ext2fs_dinode *dp;
	struct zlncnt *zlnp;
	int ndb, j;
	mode_t mode;

	dp = getnextinode(inumber);
	if (inumber < EXT2_FIRSTINO && inumber != EXT2_ROOTINO)
		return;

	mode = letoh16(dp->e2di_mode) & IFMT;
	if (mode == 0 || (dp->e2di_dtime != 0 && dp->e2di_nlink == 0)) {
		if (mode == 0 && (
			memcmp(dp->e2di_blocks, zino.e2di_blocks,
			(NDADDR + NIADDR) * sizeof(u_int32_t)) ||
		    dp->e2di_mode || inosize(dp))) {
			pfatal("PARTIALLY ALLOCATED INODE I=%llu",
			    (unsigned long long)inumber);
			if (reply("CLEAR") == 1) {
				dp = ginode(inumber);
				clearinode(dp);
				inodirty();
			}
		}
#ifdef notyet /* it seems that dtime == 0 is valid for a unallocated inode */
		if (dp->e2di_dtime == 0) {
			pwarn("DELETED INODE I=%llu HAS A NULL DTIME",
			    (unsigned long long)inumber);
			if (preen) {
				printf(" (CORRECTED)\n");
			}
			if (preen || reply("CORRECT")) {
				time_t t;
				time(&t);
				dp->e2di_dtime = htole32(t);
				dp = ginode(inumber);
				inodirty();
			}
		}
#endif
		statemap[inumber] = USTATE;
		return;
	}
	lastino = inumber;
	if (dp->e2di_dtime != 0) {
		time_t t = letoh32(dp->e2di_dtime);
		char *p = ctime(&t);
		if (p)
			pwarn("INODE I=%llu HAS DTIME=%12.12s %4.4s",
			    (unsigned long long)inumber, &p[4], &p[20]);
		else
			pwarn("INODE I=%llu HAS DTIME=%lld",
			    (unsigned long long)inumber, t);
		if (preen) {
			printf(" (CORRECTED)\n");
		}
		if (preen || reply("CORRECT")) {
			dp = ginode(inumber);
			dp->e2di_dtime = 0;
			inodirty();
		}
	}
	if (inosize(dp) + sblock.e2fs_bsize - 1 < inosize(dp)) {
		if (debug)
			printf("bad size %llu:", (unsigned long long)inosize(dp));
		goto unknown;
	}
	if (!preen && mode == IFMT && reply("HOLD BAD BLOCK") == 1) {
		dp = ginode(inumber);
		dp->e2di_mode = htole16(IFREG|0600);
		inossize(dp, sblock.e2fs_bsize);
		inodirty();
	}
	ndb = howmany(inosize(dp), sblock.e2fs_bsize);
	if (ndb < 0) {
		if (debug)
			printf("bad size %llu ndb %d:",
			    (unsigned long long)inosize(dp), ndb);
		goto unknown;
	}
	if (mode == IFBLK || mode == IFCHR)
		ndb++;
	if (mode == IFLNK) {
		/*
		 * Fake ndb value so direct/indirect block checks below
		 * will detect any garbage after symlink string.
		 */
		if (inosize(dp) < EXT2_MAXSYMLINKLEN ||
		    (EXT2_MAXSYMLINKLEN == 0 && htole32(dp->e2di_nblock) == 0)) {
			ndb = howmany(inosize(dp), sizeof(u_int32_t));
			if (ndb > NDADDR) {
				j = ndb - NDADDR;
				for (ndb = 1; j > 1; j--)
					ndb *= NINDIR(&sblock);
				ndb += NDADDR;
			}
		}
	}
	/* Linux puts things in blocks for FIFO, so skip this check */
	if (mode != IFIFO) {
		for (j = ndb; j < NDADDR; j++)
			if (dp->e2di_blocks[j] != 0) {
				if (debug)
					printf("bad direct addr: %d\n",
					    letoh32(dp->e2di_blocks[j]));
				goto unknown;
			}
		for (j = 0, ndb -= NDADDR; ndb > 0; j++)
			ndb /= NINDIR(&sblock);
		for (; j < NIADDR; j++) {
			if (dp->e2di_blocks[j+NDADDR] != 0) {
				if (debug)
					printf("bad indirect addr: %d\n",
					    letoh32(dp->e2di_blocks[j+NDADDR]));
				goto unknown;
			}
		}
	}
	if (ftypeok(dp) == 0)
		goto unknown;
	n_files++;
	lncntp[inumber] = letoh16(dp->e2di_nlink);
	if (dp->e2di_nlink == 0) {
		zlnp = malloc(sizeof *zlnp);
		if (zlnp == NULL) {
			pfatal("LINK COUNT TABLE OVERFLOW");
			if (reply("CONTINUE") == 0)
				errexit("%s\n", "");
		} else {
			zlnp->zlncnt = inumber;
			zlnp->next = zlnhead;
			zlnhead = zlnp;
		}
	}
	if (mode == IFDIR) {
		if (inosize(dp) == 0)
			statemap[inumber] = DCLEAR;
		else
			statemap[inumber] = DSTATE;
		cacheino(dp, inumber);
	} else {
		statemap[inumber] = FSTATE;
	}
	typemap[inumber] = E2IFTODT(mode);
	badblk = dupblk = 0;
	idesc->id_number = inumber;
	(void)ckinode(dp, idesc);
	idesc->id_entryno *= btodb(sblock.e2fs_bsize);
	if (letoh32(dp->e2di_nblock) != idesc->id_entryno) {
		pwarn("INCORRECT BLOCK COUNT I=%llu (%d should be %d)",
		    (unsigned long long)inumber,
		    letoh32(dp->e2di_nblock), idesc->id_entryno);
		if (preen)
			printf(" (CORRECTED)\n");
		else if (reply("CORRECT") == 0)
			return;
		dp = ginode(inumber);
		dp->e2di_nblock = htole32(idesc->id_entryno);
		inodirty();
	}
	return;
unknown:
	pfatal("UNKNOWN FILE TYPE I=%llu", (unsigned long long)inumber);
	statemap[inumber] = FCLEAR;
	if (reply("CLEAR") == 1) {
		statemap[inumber] = USTATE;
		dp = ginode(inumber);
		clearinode(dp);
		inodirty();
	}
}

int
pass1check(struct inodesc *idesc)
{
	int res = KEEPON;
	int anyout, nfrags;
	daddr32_t blkno = idesc->id_blkno;
	struct dups *dlp;
	struct dups *new;

	if ((anyout = chkrange(blkno, idesc->id_numfrags)) != 0) {
		blkerror(idesc->id_number, "BAD", blkno);
		if (badblk++ >= MAXBAD) {
			pwarn("EXCESSIVE BAD BLKS I=%llu",
			    (unsigned long long)idesc->id_number);
			if (preen)
				printf(" (SKIPPING)\n");
			else if (reply("CONTINUE") == 0)
				errexit("%s\n", "");
			return (STOP);
		}
	}
	for (nfrags = idesc->id_numfrags; nfrags > 0; blkno++, nfrags--) {
		if (anyout && chkrange(blkno, 1)) {
			res = SKIP;
		} else if (!testbmap(blkno)) {
			n_blks++;
			setbmap(blkno);
		} else {
			blkerror(idesc->id_number, "DUP", blkno);
			if (dupblk++ >= MAXDUP) {
				pwarn("EXCESSIVE DUP BLKS I=%lluu",
				    (unsigned long long)idesc->id_number);
				if (preen)
					printf(" (SKIPPING)\n");
				else if (reply("CONTINUE") == 0)
					errexit("%s\n", "");
				return (STOP);
			}
			new = malloc(sizeof(struct dups));
			if (new == NULL) {
				pfatal("DUP TABLE OVERFLOW.");
				if (reply("CONTINUE") == 0)
					errexit("%s\n", "");
				return (STOP);
			}
			new->dup = blkno;
			if (muldup == 0) {
				duplist = muldup = new;
				new->next = 0;
			} else {
				new->next = muldup->next;
				muldup->next = new;
			}
			for (dlp = duplist; dlp != muldup; dlp = dlp->next)
				if (dlp->dup == blkno)
					break;
			if (dlp == muldup && dlp->dup != blkno)
				muldup = new;
		}
		/*
		 * count the number of blocks found in id_entryno
		 */
		idesc->id_entryno++;
	}
	return (res);
}
