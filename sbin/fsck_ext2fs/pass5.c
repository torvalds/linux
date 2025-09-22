/*	$OpenBSD: pass5.c,v 1.18 2015/01/16 06:39:57 deraadt Exp $	*/
/*	$NetBSD: pass5.c,v 1.7 2000/01/28 16:01:46 bouyer Exp $ */

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

#include <sys/param.h>	/* setbit isset */
#include <sys/time.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ext2fs/ext2fs_dinode.h>
#include <ufs/ext2fs/ext2fs.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "fsutil.h"
#include "fsck.h"
#include "extern.h"


void print_bmap(u_char *,u_int32_t);

void
pass5(void)
{
	int c;
	struct m_ext2fs *fs = &sblock;
	daddr32_t dbase, dmax;
	daddr32_t d;
	long i, j;
	struct inodesc idesc[3];
	struct bufarea *ino_bitmap = NULL, *blk_bitmap = NULL;
	char *ibmap, *bbmap;
	u_int32_t cs_ndir, cs_nbfree, cs_nifree;
	char msg[255];

	cs_ndir = 0;
	cs_nbfree = 0;
	cs_nifree = 0;

	ibmap = malloc(fs->e2fs_bsize);
	bbmap = malloc(fs->e2fs_bsize);
	if (ibmap == NULL || bbmap == NULL) {
		errexit("out of memory\n");
	}

	for (c = 0; c < fs->e2fs_ncg; c++) {
		u_int32_t nbfree = 0;
		u_int32_t nifree = 0;
		u_int32_t ndirs = 0;

		nbfree = 0;
		nifree = fs->e2fs.e2fs_ipg;
		ndirs = 0;

		if (blk_bitmap == NULL) {
			blk_bitmap = getdatablk(letoh32(fs->e2fs_gd[c].ext2bgd_b_bitmap),
				fs->e2fs_bsize);
		} else {
			getblk(blk_bitmap, letoh32(fs->e2fs_gd[c].ext2bgd_b_bitmap),
				fs->e2fs_bsize);
		}
		if (ino_bitmap == NULL) {
			ino_bitmap = getdatablk(letoh32(fs->e2fs_gd[c].ext2bgd_i_bitmap),
				fs->e2fs_bsize);
		} else {
			getblk(ino_bitmap, letoh32(fs->e2fs_gd[c].ext2bgd_i_bitmap),
				fs->e2fs_bsize);
		}
		memset(bbmap, 0, fs->e2fs_bsize);
		memset(ibmap, 0, fs->e2fs_bsize);
		memset(&idesc[0], 0, sizeof idesc);
		for (i = 0; i < 3; i++) {
			idesc[i].id_type = ADDR;
		}

		j = fs->e2fs.e2fs_ipg * c + 1;

		for (i = 0; i < fs->e2fs.e2fs_ipg; j++, i++) {
			if ((j < EXT2_FIRSTINO) && (j != EXT2_ROOTINO)) {
				setbit(ibmap, i);
				nifree--;
				continue;
			}
			if (j > fs->e2fs.e2fs_icount) {
				setbit(ibmap, i);
				continue;
			}
			switch (statemap[j]) {

			case USTATE:
				break;

			case DSTATE:
			case DCLEAR:
			case DFOUND:
				ndirs++;
				/* fall through */

			case FSTATE:
			case FCLEAR:
				nifree--;
				setbit(ibmap, i);
				break;

			default:
				errexit("BAD STATE %d FOR INODE I=%llu\n",
				    statemap[j], (unsigned long long)j);
			}
		}

		/* fill in unused par of the inode map */
		for (i = fs->e2fs.e2fs_ipg / NBBY; i < fs->e2fs_bsize; i++)
			ibmap[i] = 0xff;

		dbase = c * sblock.e2fs.e2fs_bpg +
		    sblock.e2fs.e2fs_first_dblock;
		dmax = (c+1) * sblock.e2fs.e2fs_bpg +
		    sblock.e2fs.e2fs_first_dblock;

		for (i = 0, d = dbase;
		     d < dmax;
		     d ++, i ++) {
			if (testbmap(d) || d >= sblock.e2fs.e2fs_bcount) {
				setbit(bbmap, i);
				continue;
			} else {
				nbfree++;
			}

		}
		cs_nbfree += nbfree;
		cs_nifree += nifree;
		cs_ndir += ndirs;

		if (debug && (letoh16(fs->e2fs_gd[c].ext2bgd_nbfree) != nbfree ||
		    letoh16(fs->e2fs_gd[c].ext2bgd_nifree) != nifree ||
		    letoh16(fs->e2fs_gd[c].ext2bgd_ndirs) != ndirs)) {
			printf("summary info for cg %d is %d, %d, %d,"
					"should be %d, %d, %d\n", c,
					letoh16(fs->e2fs_gd[c].ext2bgd_nbfree),
					letoh16(fs->e2fs_gd[c].ext2bgd_nifree),
					letoh16(fs->e2fs_gd[c].ext2bgd_ndirs),
					nbfree,
					nifree,
					ndirs);
		}
		(void)snprintf(msg, sizeof(msg),
		    "SUMMARY INFORMATIONS WRONG FOR CG #%d", c);
		if ((letoh16(fs->e2fs_gd[c].ext2bgd_nbfree) != nbfree ||
			letoh16(fs->e2fs_gd[c].ext2bgd_nifree) != nifree ||
			letoh16(fs->e2fs_gd[c].ext2bgd_ndirs) != ndirs) &&
			dofix(&idesc[0], msg)) {
			fs->e2fs_gd[c].ext2bgd_nbfree = htole16(nbfree);
			fs->e2fs_gd[c].ext2bgd_nifree = htole16(nifree);
			fs->e2fs_gd[c].ext2bgd_ndirs = htole16(ndirs);
			sbdirty();
		}

		if (debug && memcmp(blk_bitmap->b_un.b_buf, bbmap, fs->e2fs_bsize)) {
			printf("blk_bitmap:\n");
			print_bmap(blk_bitmap->b_un.b_buf, fs->e2fs_bsize);
			printf("bbmap:\n");
			print_bmap(bbmap, fs->e2fs_bsize);
		}

		(void)snprintf(msg, sizeof(msg),
		    "BLK(S) MISSING IN BIT MAPS #%d", c);
		if (memcmp(blk_bitmap->b_un.b_buf, bbmap, fs->e2fs_bsize) &&
			dofix(&idesc[1], msg)) {
			memcpy(blk_bitmap->b_un.b_buf, bbmap, fs->e2fs_bsize);
			dirty(blk_bitmap);
		}
		if (debug && memcmp(ino_bitmap->b_un.b_buf, ibmap, fs->e2fs_bsize)) {
			printf("ino_bitmap:\n");
			print_bmap(ino_bitmap->b_un.b_buf, fs->e2fs_bsize);
			printf("ibmap:\n");
			print_bmap(ibmap, fs->e2fs_bsize);
		}
		(void)snprintf(msg, sizeof(msg),
		    "INODE(S) MISSING IN BIT MAPS #%d", c);
		if (memcmp(ino_bitmap->b_un.b_buf, ibmap, fs->e2fs_bsize) &&
			dofix(&idesc[1], msg)) {
			memcpy(ino_bitmap->b_un.b_buf, ibmap, fs->e2fs_bsize);
			dirty(ino_bitmap);
		}

	}
	if (debug && (fs->e2fs.e2fs_fbcount != cs_nbfree ||
		fs->e2fs.e2fs_ficount != cs_nifree)) {
		printf("summary info bad in superblock: %d, %d should be %d, %d\n",
		fs->e2fs.e2fs_fbcount, fs->e2fs.e2fs_ficount,
		cs_nbfree, cs_nifree);
	}
	if ((fs->e2fs.e2fs_fbcount != cs_nbfree ||
		fs->e2fs.e2fs_ficount != cs_nifree)
	    && dofix(&idesc[0], "SUPERBLK SUMMARY INFORMATION BAD")) {
		fs->e2fs.e2fs_fbcount = cs_nbfree;
		fs->e2fs.e2fs_ficount = cs_nifree;
		sbdirty();
	}
	free(ibmap);
	free(bbmap);
}

void
print_bmap(u_char *map, u_int32_t size)
{
	int i, j;

	i = 0;
	while (i < size) {
		printf("%u: ",i);
		for (j = 0; j < 16; j++, i++)
			printf("%2x ", (u_int)map[i] & 0xff);
		printf("\n");
	}
}
