/*	$OpenBSD: pass5.c,v 1.52 2024/09/15 07:14:58 jsg Exp $	*/
/*	$NetBSD: pass5.c,v 1.16 1996/09/27 22:45:18 christos Exp $	*/

/*
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

#include <sys/param.h>	/* MAXBSIZE roundup setbit */
#include <sys/time.h>
#include <sys/lock.h>
#include <sys/ucred.h>
#include <ufs/ffs/fs.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ffs/ffs_extern.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "fsutil.h"
#include "fsck.h"
#include "extern.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

static u_int info_cg;
static u_int info_maxcg;

static int
pass5_info(char *buf, size_t buflen)
{
	return (snprintf(buf, buflen, "phase 5, cg %u/%u",
	    info_cg, info_maxcg) > 0);
}

void
pass5(void)
{
	int blk, frags, basesize, sumsize, mapsize, savednrpos=0;
	u_int c;
	int inomapsize, blkmapsize;
	struct fs *fs = &sblock;
	daddr_t dbase, dmax;
	daddr_t d;
	long i, rewritecg = 0;
	ino_t j;
	struct csum *cs;
	struct csum_total cstotal;
	struct inodesc idesc[3];
	char buf[MAXBSIZE];
	struct cg *newcg = (struct cg *)buf;
	struct ocg *ocg = (struct ocg *)buf;
	struct cg *cg;
	struct bufarea *cgbp;

	memset(newcg, 0, (size_t)fs->fs_cgsize);
	if (cvtlevel >= 3) {
		if (fs->fs_maxcontig < 2 && fs->fs_contigsumsize > 0) {
			if (preen)
				pwarn("DELETING CLUSTERING MAPS\n");
			if (preen || reply("DELETE CLUSTERING MAPS")) {
				fs->fs_contigsumsize = 0;
				rewritecg = 1;
				sbdirty();
			}
		}
		if (fs->fs_maxcontig > 1) {
			char *doit = 0;

			if (fs->fs_contigsumsize < 1) {
				doit = "CREAT";
			} else if (fs->fs_contigsumsize < fs->fs_maxcontig &&
				   fs->fs_contigsumsize < FS_MAXCONTIG) {
				doit = "EXPAND";
			}
			if (doit) {
				i = fs->fs_contigsumsize;
				fs->fs_contigsumsize =
				    MINIMUM(fs->fs_maxcontig, FS_MAXCONTIG);
				if (CGSIZE(fs) > fs->fs_bsize) {
					pwarn("CANNOT %s CLUSTER MAPS\n", doit);
					fs->fs_contigsumsize = i;
				} else if (preen ||
				    reply("CREATE CLUSTER MAPS")) {
					if (preen)
						pwarn("%sING CLUSTER MAPS\n",
						    doit);
					fs->fs_cgsize =
					    fragroundup(fs, CGSIZE(fs));
					rewritecg = 1;
					sbdirty();
				}
			}
		}
	}
	switch ((int)fs->fs_postblformat) {

	case FS_42POSTBLFMT:
		basesize = (char *)(&ocg->cg_btot[0]) -
		    (char *)(&ocg->cg_firstfield);
		sumsize = &ocg->cg_iused[0] - (u_int8_t *)(&ocg->cg_btot[0]);
		mapsize = &ocg->cg_free[howmany(fs->fs_fpg, NBBY)] -
			(u_char *)&ocg->cg_iused[0];
		blkmapsize = howmany(fs->fs_fpg, NBBY);
		inomapsize = sizeof(ocg->cg_iused);
		ocg->cg_magic = CG_MAGIC;
		savednrpos = fs->fs_nrpos;
		fs->fs_nrpos = 8;
		break;

	case FS_DYNAMICPOSTBLFMT:
		if (sblock.fs_magic == FS_UFS2_MAGIC) {
			newcg->cg_iusedoff = sizeof(struct cg);
		} else {
			newcg->cg_btotoff = sizeof(struct cg);
			newcg->cg_boff = newcg->cg_btotoff +
			    fs->fs_cpg * sizeof(int32_t);
			newcg->cg_iusedoff = newcg->cg_boff + fs->fs_cpg *
			    fs->fs_nrpos * sizeof(int16_t);
		}
		inomapsize = howmany(fs->fs_ipg, CHAR_BIT);
		newcg->cg_freeoff = newcg->cg_iusedoff + inomapsize;
		blkmapsize = howmany(fs->fs_fpg, CHAR_BIT);
		newcg->cg_nextfreeoff = newcg->cg_freeoff + blkmapsize;
		if (fs->fs_contigsumsize > 0) {
			newcg->cg_clustersumoff = newcg->cg_nextfreeoff -
			    sizeof(int32_t);
			newcg->cg_clustersumoff =
			    roundup(newcg->cg_clustersumoff, sizeof(int32_t));
			newcg->cg_clusteroff = newcg->cg_clustersumoff +
			    (fs->fs_contigsumsize + 1) * sizeof(int32_t);
			newcg->cg_nextfreeoff = newcg->cg_clusteroff +
			    howmany(fragstoblks(fs, fs->fs_fpg), CHAR_BIT);
		}
		newcg->cg_magic = CG_MAGIC;
		basesize = sizeof(struct cg);
		sumsize = newcg->cg_iusedoff - newcg->cg_btotoff;
		mapsize = newcg->cg_nextfreeoff - newcg->cg_iusedoff;
		break;

	default:
		inomapsize = blkmapsize = sumsize = 0;
		errexit("UNKNOWN ROTATIONAL TABLE FORMAT %d\n",
			fs->fs_postblformat);
	}
	memset(&idesc[0], 0, sizeof idesc);
	for (i = 0; i < 3; i++)
		idesc[i].id_type = ADDR;
	memset(&cstotal, 0, sizeof(struct csum_total));
	dmax = blknum(fs, fs->fs_size + fs->fs_frag - 1);
	for (d = fs->fs_size; d < dmax; d++)
		setbmap(d);
	info_cg = 0;
	info_maxcg = fs->fs_ncg;
	info_fn = pass5_info;
	for (c = 0; c < fs->fs_ncg; c++) {
		info_cg = c;
		cgbp = cglookup(c);
		cg = cgbp->b_un.b_cg;
		if (!cg_chkmagic(cg))
			pfatal("CG %u: BAD MAGIC NUMBER\n", c);
		dbase = cgbase(fs, c);
		dmax = dbase + fs->fs_fpg;
		if (dmax > fs->fs_size)
			dmax = fs->fs_size;
		newcg->cg_time = cg->cg_time;
		newcg->cg_ffs2_time = cg->cg_ffs2_time;
		newcg->cg_cgx = c;
		if (c == fs->fs_ncg - 1)
			newcg->cg_ncyl = fs->fs_ncyl % fs->fs_cpg;
		else
			newcg->cg_ncyl = fs->fs_cpg;
		newcg->cg_ndblk = dmax - dbase;
		if (fs->fs_contigsumsize > 0)
			newcg->cg_nclusterblks = newcg->cg_ndblk / fs->fs_frag;
		newcg->cg_cs.cs_ndir = 0;
		newcg->cg_cs.cs_nffree = 0;
		newcg->cg_cs.cs_nbfree = 0;
		newcg->cg_cs.cs_nifree = fs->fs_ipg;
		if (cg->cg_rotor < newcg->cg_ndblk)
			newcg->cg_rotor = cg->cg_rotor;
		else
			newcg->cg_rotor = 0;
		if (cg->cg_frotor >= 0 && cg->cg_frotor < newcg->cg_ndblk)
			newcg->cg_frotor = cg->cg_frotor;
		else
			newcg->cg_frotor = 0;
		newcg->cg_irotor = 0;
		if (fs->fs_magic == FS_UFS1_MAGIC) {
			newcg->cg_initediblk = 0;
			newcg->cg_niblk = cg->cg_niblk;
			if (cg->cg_irotor >= 0 &&
			    cg->cg_irotor < fs->fs_ipg)
				newcg->cg_irotor = cg->cg_irotor;
		} else {
			newcg->cg_ncyl = 0;
			if (cg->cg_initediblk > fs->fs_ipg)
				newcg->cg_initediblk = fs->fs_ipg;
			else
				newcg->cg_initediblk = cg->cg_initediblk;
			newcg->cg_ffs2_niblk = fs->fs_ipg;
			if (cg->cg_irotor >= 0 &&
			    cg->cg_irotor < newcg->cg_ffs2_niblk)
				newcg->cg_irotor = cg->cg_irotor;
		}
		memset(&newcg->cg_frsum[0], 0, sizeof newcg->cg_frsum);
		memset(cg_inosused(newcg), 0, (size_t)(mapsize));
		if (fs->fs_postblformat == FS_42POSTBLFMT)
			ocg->cg_magic = CG_MAGIC;
		j = fs->fs_ipg * c;
		for (i = 0; i < inostathead[c].il_numalloced; j++, i++) {
			switch (GET_ISTATE(j)) {

			case USTATE:
				break;

			case DSTATE:
			case DCLEAR:
			case DFOUND:
				newcg->cg_cs.cs_ndir++;
				/* FALLTHROUGH */

			case FSTATE:
			case FCLEAR:
				newcg->cg_cs.cs_nifree--;
				setbit(cg_inosused(newcg), i);
				break;

			default:
				if (j < ROOTINO)
					break;
				errexit("BAD STATE %d FOR INODE I=%llu\n",
				    GET_ISTATE(j), (unsigned long long)j);
			}
		}
		if (c == 0)
			for (i = 0; i < ROOTINO; i++) {
				setbit(cg_inosused(newcg), i);
				newcg->cg_cs.cs_nifree--;
			}
		for (i = 0, d = dbase;
		     d < dmax;
		     d += fs->fs_frag, i += fs->fs_frag) {
			frags = 0;
			for (j = 0; j < fs->fs_frag; j++) {
				if (testbmap(d + j))
					continue;
				setbit(cg_blksfree(newcg), i + j);
				frags++;
			}
			if (frags == fs->fs_frag) {
				newcg->cg_cs.cs_nbfree++;
				if (fs->fs_contigsumsize > 0)
					setbit(cg_clustersfree(newcg),
					    i / fs->fs_frag);
			} else if (frags > 0) {
				newcg->cg_cs.cs_nffree += frags;
				blk = blkmap(fs, cg_blksfree(newcg), i);
				ffs_fragacct(fs, blk, newcg->cg_frsum, 1);
			}
		}
		if (fs->fs_contigsumsize > 0) {
			int32_t *sump = cg_clustersum(newcg);
			u_char *mapp = cg_clustersfree(newcg);
			int map = *mapp++;
			int bit = 1;
			int run = 0;

			for (i = 0; i < newcg->cg_nclusterblks; i++) {
				if ((map & bit) != 0) {
					run++;
				} else if (run != 0) {
					if (run > fs->fs_contigsumsize)
						run = fs->fs_contigsumsize;
					sump[run]++;
					run = 0;
				}
				if ((i & (NBBY - 1)) != (NBBY - 1)) {
					bit <<= 1;
				} else {
					map = *mapp++;
					bit = 1;
				}
			}
			if (run != 0) {
				if (run > fs->fs_contigsumsize)
					run = fs->fs_contigsumsize;
				sump[run]++;
			}
		}
		cstotal.cs_nffree += newcg->cg_cs.cs_nffree;
		cstotal.cs_nbfree += newcg->cg_cs.cs_nbfree;
		cstotal.cs_nifree += newcg->cg_cs.cs_nifree;
		cstotal.cs_ndir += newcg->cg_cs.cs_ndir;
		cs = &fs->fs_cs(fs, c);
		if (memcmp(&newcg->cg_cs, cs, sizeof *cs) != 0 &&
		    dofix(&idesc[0], "FREE BLK COUNT(S) WRONG IN SUPERBLK")) {
			memcpy(cs, &newcg->cg_cs, sizeof *cs);
			sbdirty();
		}
		if (rewritecg) {
			memcpy(cg, newcg, (size_t)fs->fs_cgsize);
			dirty(cgbp);
			continue;
		}
		if (memcmp(newcg, cg, basesize) &&
		    dofix(&idesc[2], "SUMMARY INFORMATION BAD")) {
			memcpy(cg, newcg, (size_t)basesize);
			dirty(cgbp);
		}
		if (memcmp(cg_inosused(newcg), cg_inosused(cg),
			   mapsize) != 0 &&
		    dofix(&idesc[1], "BLK(S) MISSING IN BIT MAPS")) {
			memmove(cg_inosused(cg), cg_inosused(newcg),
				(size_t)mapsize);
			dirty(cgbp);
		}
	}
	info_fn = NULL;
	if (fs->fs_postblformat == FS_42POSTBLFMT)
		fs->fs_nrpos = savednrpos;

	sumsize = sizeof(cstotal) - sizeof(cstotal.cs_spare);
	if (memcmp(&cstotal, &fs->fs_cstotal, sumsize) != 0
	    && dofix(&idesc[0], "FREE BLK COUNT(S) WRONG IN SUPERBLK")) {
		memcpy(&fs->fs_cstotal, &cstotal, sumsize);
		fs->fs_ronly = 0;
		fs->fs_fmod = 0;
		sbdirty();
	}
}
