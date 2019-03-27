/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#if 0
#ifndef lint
static const char sccsid[] = "@(#)pass5.c	8.9 (Berkeley) 4/28/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/sysctl.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <libufs.h>

#include "fsck.h"

static void check_maps(u_char *, u_char *, int, ufs2_daddr_t, const char *,
			int *, int, int, int);
static void clear_blocks(ufs2_daddr_t start, ufs2_daddr_t end);

void
pass5(void)
{
	int c, i, j, blk, frags, basesize, mapsize;
	int inomapsize, blkmapsize;
	struct fs *fs = &sblock;
	ufs2_daddr_t d, dbase, dmax, start;
	int rewritecg = 0;
	struct csum *cs;
	struct csum_total cstotal;
	struct inodesc idesc[3];
	char buf[MAXBSIZE];
	struct cg *cg, *newcg = (struct cg *)buf;
	struct bufarea *cgbp;

	inoinfo(UFS_WINO)->ino_state = USTATE;
	memset(newcg, 0, (size_t)fs->fs_cgsize);
	newcg->cg_niblk = fs->fs_ipg;
	/* check to see if we are to add a cylinder group check hash */
	if ((ckhashadd & CK_CYLGRP) != 0)
		rewritecg = 1;
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
			const char *doit = NULL;

			if (fs->fs_contigsumsize < 1) {
				doit = "CREAT";
			} else if (fs->fs_contigsumsize < fs->fs_maxcontig &&
				   fs->fs_contigsumsize < FS_MAXCONTIG) {
				doit = "EXPAND";
			}
			if (doit) {
				i = fs->fs_contigsumsize;
				fs->fs_contigsumsize =
				    MIN(fs->fs_maxcontig, FS_MAXCONTIG);
				if (CGSIZE(fs) > (u_int)fs->fs_bsize) {
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
	basesize = &newcg->cg_space[0] - (u_char *)(&newcg->cg_firstfield);
	if (sblock.fs_magic == FS_UFS2_MAGIC) {
		newcg->cg_iusedoff = basesize;
	} else {
		/*
		 * We reserve the space for the old rotation summary
		 * tables for the benefit of old kernels, but do not
		 * maintain them in modern kernels. In time, they can
		 * go away.
		 */
		newcg->cg_old_btotoff = basesize;
		newcg->cg_old_boff = newcg->cg_old_btotoff +
		    fs->fs_old_cpg * sizeof(int32_t);
		newcg->cg_iusedoff = newcg->cg_old_boff +
		    fs->fs_old_cpg * fs->fs_old_nrpos * sizeof(u_int16_t);
		memset(&newcg->cg_space[0], 0, newcg->cg_iusedoff - basesize);
	}
	inomapsize = howmany(fs->fs_ipg, CHAR_BIT);
	newcg->cg_freeoff = newcg->cg_iusedoff + inomapsize;
	blkmapsize = howmany(fs->fs_fpg, CHAR_BIT);
	newcg->cg_nextfreeoff = newcg->cg_freeoff + blkmapsize;
	if (fs->fs_contigsumsize > 0) {
		newcg->cg_clustersumoff = newcg->cg_nextfreeoff -
		    sizeof(u_int32_t);
		newcg->cg_clustersumoff =
		    roundup(newcg->cg_clustersumoff, sizeof(u_int32_t));
		newcg->cg_clusteroff = newcg->cg_clustersumoff +
		    (fs->fs_contigsumsize + 1) * sizeof(u_int32_t);
		newcg->cg_nextfreeoff = newcg->cg_clusteroff +
		    howmany(fragstoblks(fs, fs->fs_fpg), CHAR_BIT);
	}
	newcg->cg_magic = CG_MAGIC;
	mapsize = newcg->cg_nextfreeoff - newcg->cg_iusedoff;
	memset(&idesc[0], 0, sizeof idesc);
	for (i = 0; i < 3; i++)
		idesc[i].id_type = ADDR;
	memset(&cstotal, 0, sizeof(struct csum_total));
	dmax = blknum(fs, fs->fs_size + fs->fs_frag - 1);
	for (d = fs->fs_size; d < dmax; d++)
		setbmap(d);
	for (c = 0; c < fs->fs_ncg; c++) {
		if (got_siginfo) {
			printf("%s: phase 5: cyl group %d of %d (%d%%)\n",
			    cdevname, c, sblock.fs_ncg,
			    c * 100 / sblock.fs_ncg);
			got_siginfo = 0;
		}
		if (got_sigalarm) {
			setproctitle("%s p5 %d%%", cdevname,
			    c * 100 / sblock.fs_ncg);
			got_sigalarm = 0;
		}
		cgbp = cglookup(c);
		cg = cgbp->b_un.b_cg;
		if (!cg_chkmagic(cg))
			pfatal("CG %d: BAD MAGIC NUMBER\n", c);
		/*
		 * If we have a cylinder group check hash and are not adding
		 * it for the first time, verify that it is good.
		 */
		if ((fs->fs_metackhash & CK_CYLGRP) != 0 &&
		    (ckhashadd & CK_CYLGRP) == 0) {
			uint32_t ckhash, thishash;

			ckhash = cg->cg_ckhash;
			cg->cg_ckhash = 0;
			thishash = calculate_crc32c(~0L, cg, fs->fs_cgsize);
			if (ckhash == thishash) {
				cg->cg_ckhash = ckhash;
			} else {
				pwarn("CG %d: BAD CHECK-HASH %#x vs %#x\n",
				    c, ckhash, thishash);
				cg->cg_ckhash = thishash;
				cgdirty(cgbp);
			}
		}
		newcg->cg_time = cg->cg_time;
		newcg->cg_old_time = cg->cg_old_time;
		newcg->cg_unrefs = cg->cg_unrefs;
		newcg->cg_ckhash = cg->cg_ckhash;
		newcg->cg_cgx = c;
		dbase = cgbase(fs, c);
		dmax = dbase + fs->fs_fpg;
		if (dmax > fs->fs_size)
			dmax = fs->fs_size;
		newcg->cg_ndblk = dmax - dbase;
		if (fs->fs_magic == FS_UFS1_MAGIC) {
			if (c == fs->fs_ncg - 1)
				newcg->cg_old_ncyl = howmany(newcg->cg_ndblk,
				    fs->fs_fpg / fs->fs_old_cpg);
			else
				newcg->cg_old_ncyl = fs->fs_old_cpg;
			newcg->cg_old_niblk = fs->fs_ipg;
			newcg->cg_niblk = 0;
		}
		if (fs->fs_contigsumsize > 0)
			newcg->cg_nclusterblks = newcg->cg_ndblk / fs->fs_frag;
		newcg->cg_cs.cs_ndir = 0;
		newcg->cg_cs.cs_nffree = 0;
		newcg->cg_cs.cs_nbfree = 0;
		newcg->cg_cs.cs_nifree = fs->fs_ipg;
		if (cg->cg_rotor >= 0 && cg->cg_rotor < newcg->cg_ndblk)
			newcg->cg_rotor = cg->cg_rotor;
		else
			newcg->cg_rotor = 0;
		if (cg->cg_frotor >= 0 && cg->cg_frotor < newcg->cg_ndblk)
			newcg->cg_frotor = cg->cg_frotor;
		else
			newcg->cg_frotor = 0;
		if (cg->cg_irotor >= 0 && cg->cg_irotor < fs->fs_ipg)
			newcg->cg_irotor = cg->cg_irotor;
		else
			newcg->cg_irotor = 0;
		if (fs->fs_magic == FS_UFS1_MAGIC) {
			newcg->cg_initediblk = 0;
		} else {
			if ((unsigned)cg->cg_initediblk > fs->fs_ipg)
				newcg->cg_initediblk = fs->fs_ipg;
			else
				newcg->cg_initediblk = cg->cg_initediblk;
		}
		memset(&newcg->cg_frsum[0], 0, sizeof newcg->cg_frsum);
		memset(cg_inosused(newcg), 0, (size_t)(mapsize));
		j = fs->fs_ipg * c;
		for (i = 0; i < inostathead[c].il_numalloced; j++, i++) {
			switch (inoinfo(j)->ino_state) {

			case USTATE:
				break;

			case DSTATE:
			case DCLEAR:
			case DFOUND:
			case DZLINK:
				newcg->cg_cs.cs_ndir++;
				/* FALLTHROUGH */

			case FSTATE:
			case FCLEAR:
			case FZLINK:
				newcg->cg_cs.cs_nifree--;
				setbit(cg_inosused(newcg), i);
				break;

			default:
				if (j < (int)UFS_ROOTINO)
					break;
				errx(EEXIT, "BAD STATE %d FOR INODE I=%d",
				    inoinfo(j)->ino_state, j);
			}
		}
		if (c == 0)
			for (i = 0; i < (int)UFS_ROOTINO; i++) {
				setbit(cg_inosused(newcg), i);
				newcg->cg_cs.cs_nifree--;
			}
		start = -1;
		for (i = 0, d = dbase;
		     d < dmax;
		     d += fs->fs_frag, i += fs->fs_frag) {
			frags = 0;
			for (j = 0; j < fs->fs_frag; j++) {
				if (testbmap(d + j)) {
					if ((Eflag || Zflag) && start != -1) {
						clear_blocks(start, d + j - 1);
						start = -1;
					}
					continue;
				}
				if (start == -1)
					start = d + j;
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
		if ((Eflag || Zflag) && start != -1)
			clear_blocks(start, d - 1);
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
				if ((i & (CHAR_BIT - 1)) != (CHAR_BIT - 1)) {
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

		if (bkgrdflag != 0) {
			cstotal.cs_nffree += cg->cg_cs.cs_nffree;
			cstotal.cs_nbfree += cg->cg_cs.cs_nbfree;
			cstotal.cs_nifree += cg->cg_cs.cs_nifree;
			cstotal.cs_ndir += cg->cg_cs.cs_ndir;
		} else {
			cstotal.cs_nffree += newcg->cg_cs.cs_nffree;
			cstotal.cs_nbfree += newcg->cg_cs.cs_nbfree;
			cstotal.cs_nifree += newcg->cg_cs.cs_nifree;
			cstotal.cs_ndir += newcg->cg_cs.cs_ndir;
		}
		cs = &fs->fs_cs(fs, c);
		if (cursnapshot == 0 &&
		    memcmp(&newcg->cg_cs, cs, sizeof *cs) != 0 &&
		    dofix(&idesc[0], "FREE BLK COUNT(S) WRONG IN SUPERBLK")) {
			memmove(cs, &newcg->cg_cs, sizeof *cs);
			sbdirty();
		}
		if (rewritecg) {
			memmove(cg, newcg, (size_t)fs->fs_cgsize);
			cgdirty(cgbp);
			continue;
		}
		if (cursnapshot == 0 &&
		    memcmp(newcg, cg, basesize) != 0 &&
		    dofix(&idesc[2], "SUMMARY INFORMATION BAD")) {
			memmove(cg, newcg, (size_t)basesize);
			cgdirty(cgbp);
		}
		if (bkgrdflag != 0 || usedsoftdep || debug)
			update_maps(cg, newcg, bkgrdflag);
		if (cursnapshot == 0 &&
		    memcmp(cg_inosused(newcg), cg_inosused(cg), mapsize) != 0 &&
		    dofix(&idesc[1], "BLK(S) MISSING IN BIT MAPS")) {
			memmove(cg_inosused(cg), cg_inosused(newcg),
			      (size_t)mapsize);
			cgdirty(cgbp);
		}
	}
	if (cursnapshot == 0 &&
	    memcmp(&cstotal, &fs->fs_cstotal, sizeof cstotal) != 0
	    && dofix(&idesc[0], "SUMMARY BLK COUNT(S) WRONG IN SUPERBLK")) {
		memmove(&fs->fs_cstotal, &cstotal, sizeof cstotal);
		fs->fs_ronly = 0;
		fs->fs_fmod = 0;
		sbdirty();
	}

	/*
	 * When doing background fsck on a snapshot, figure out whether
	 * the superblock summary is inaccurate and correct it when
	 * necessary.
	 */
	if (cursnapshot != 0) {
		cmd.size = 1;

		cmd.value = cstotal.cs_ndir - fs->fs_cstotal.cs_ndir;
		if (cmd.value != 0) {
			if (debug)
				printf("adjndir by %+" PRIi64 "\n", cmd.value);
			if (bkgrdsumadj == 0 || sysctl(adjndir, MIBSIZE, 0, 0,
			    &cmd, sizeof cmd) == -1)
				rwerror("ADJUST NUMBER OF DIRECTORIES", cmd.value);
		}

		cmd.value = cstotal.cs_nbfree - fs->fs_cstotal.cs_nbfree;
		if (cmd.value != 0) {
			if (debug)
				printf("adjnbfree by %+" PRIi64 "\n", cmd.value);
			if (bkgrdsumadj == 0 || sysctl(adjnbfree, MIBSIZE, 0, 0,
			    &cmd, sizeof cmd) == -1)
				rwerror("ADJUST NUMBER OF FREE BLOCKS", cmd.value);
		}

		cmd.value = cstotal.cs_nifree - fs->fs_cstotal.cs_nifree;
		if (cmd.value != 0) {
			if (debug)
				printf("adjnifree by %+" PRIi64 "\n", cmd.value);
			if (bkgrdsumadj == 0 || sysctl(adjnifree, MIBSIZE, 0, 0,
			    &cmd, sizeof cmd) == -1)
				rwerror("ADJUST NUMBER OF FREE INODES", cmd.value);
		}

		cmd.value = cstotal.cs_nffree - fs->fs_cstotal.cs_nffree;
		if (cmd.value != 0) {
			if (debug)
				printf("adjnffree by %+" PRIi64 "\n", cmd.value);
			if (bkgrdsumadj == 0 || sysctl(adjnffree, MIBSIZE, 0, 0,
			    &cmd, sizeof cmd) == -1)
				rwerror("ADJUST NUMBER OF FREE FRAGS", cmd.value);
		}

		cmd.value = cstotal.cs_numclusters - fs->fs_cstotal.cs_numclusters;
		if (cmd.value != 0) {
			if (debug)
				printf("adjnumclusters by %+" PRIi64 "\n", cmd.value);
			if (bkgrdsumadj == 0 || sysctl(adjnumclusters, MIBSIZE, 0, 0,
			    &cmd, sizeof cmd) == -1)
				rwerror("ADJUST NUMBER OF FREE CLUSTERS", cmd.value);
		}
	}
}

/*
 * Compare the original cylinder group inode and block bitmaps with the
 * updated cylinder group inode and block bitmaps. Free inodes and blocks
 * that have been added. Complain if any previously freed inodes blocks
 * are now allocated.
 */
void
update_maps(
	struct cg *oldcg,	/* cylinder group of claimed allocations */
	struct cg *newcg,	/* cylinder group of determined allocations */
	int usesysctl)		/* 1 => use sysctl interface to update maps */
{
	int inomapsize, excessdirs;
	struct fs *fs = &sblock;

	inomapsize = howmany(fs->fs_ipg, CHAR_BIT);
	excessdirs = oldcg->cg_cs.cs_ndir - newcg->cg_cs.cs_ndir;
	if (excessdirs < 0) {
		pfatal("LOST %d DIRECTORIES\n", -excessdirs);
		excessdirs = 0;
	}
	if (excessdirs > 0)
		check_maps(cg_inosused(newcg), cg_inosused(oldcg), inomapsize,
		    oldcg->cg_cgx * (ufs2_daddr_t)fs->fs_ipg, "DIR", freedirs,
		    0, excessdirs, usesysctl);
	check_maps(cg_inosused(newcg), cg_inosused(oldcg), inomapsize,
	    oldcg->cg_cgx * (ufs2_daddr_t)fs->fs_ipg, "FILE", freefiles,
	    excessdirs, fs->fs_ipg, usesysctl);
	check_maps(cg_blksfree(oldcg), cg_blksfree(newcg),
	    howmany(fs->fs_fpg, CHAR_BIT),
	    oldcg->cg_cgx * (ufs2_daddr_t)fs->fs_fpg, "FRAG",
	    freeblks, 0, fs->fs_fpg, usesysctl);
}

static void
check_maps(
	u_char *map1,	/* map of claimed allocations */
	u_char *map2,	/* map of determined allocations */
	int mapsize,	/* size of above two maps */
	ufs2_daddr_t startvalue, /* resource value for first element in map */
	const char *name,	/* name of resource found in maps */
	int *opcode,	/* sysctl opcode to free resource */
	int skip,	/* number of entries to skip before starting to free */
	int limit,	/* limit on number of entries to free */
	int usesysctl)	/* 1 => use sysctl interface to update maps */
{
#	define BUFSIZE 16
	char buf[BUFSIZE];
	long i, j, k, l, m, size;
	ufs2_daddr_t n, astart, aend, ustart, uend;
	void (*msg)(const char *fmt, ...);

	if (usesysctl)
		msg = pfatal;
	else
		msg = pwarn;
	astart = ustart = aend = uend = -1;
	for (i = 0; i < mapsize; i++) {
		j = *map1++;
		k = *map2++;
		if (j == k)
			continue;
		for (m = 0, l = 1; m < CHAR_BIT; m++, l <<= 1) {
			if ((j & l) == (k & l))
				continue;
			n = startvalue + i * CHAR_BIT + m;
			if ((j & l) != 0) {
				if (astart == -1) {
					astart = aend = n;
					continue;
				}
				if (aend + 1 == n) {
					aend = n;
					continue;
				}
				if (astart == aend)
					(*msg)("ALLOCATED %s %" PRId64
					    " MARKED FREE\n",
					    name, astart);
				else
					(*msg)("%s %sS %" PRId64 "-%" PRId64
					    " MARKED FREE\n",
					    "ALLOCATED", name, astart, aend);
				astart = aend = n;
			} else {
				if (ustart == -1) {
					ustart = uend = n;
					continue;
				}
				if (uend + 1 == n) {
					uend = n;
					continue;
				}
				size = uend - ustart + 1;
				if (size <= skip) {
					skip -= size;
					ustart = uend = n;
					continue;
				}
				if (skip > 0) {
					ustart += skip;
					size -= skip;
					skip = 0;
				}
				if (size > limit)
					size = limit;
				if (debug && size == 1)
					pwarn("%s %s %" PRId64
					    " MARKED USED\n",
					    "UNALLOCATED", name, ustart);
				else if (debug)
					pwarn("%s %sS %" PRId64 "-%" PRId64
					    " MARKED USED\n",
					    "UNALLOCATED", name, ustart,
					    ustart + size - 1);
				if (usesysctl != 0) {
					cmd.value = ustart;
					cmd.size = size;
					if (sysctl(opcode, MIBSIZE, 0, 0,
					    &cmd, sizeof cmd) == -1) {
						snprintf(buf, BUFSIZE,
						    "FREE %s", name);
						rwerror(buf, cmd.value);
					}
				}
				limit -= size;
				if (limit <= 0)
					return;
				ustart = uend = n;
			}
		}
	}
	if (astart != -1) {
		if (astart == aend)
			(*msg)("ALLOCATED %s %" PRId64
			    " MARKED FREE\n", name, astart);
		else
			(*msg)("ALLOCATED %sS %" PRId64 "-%" PRId64
			    " MARKED FREE\n",
			    name, astart, aend);
	}
	if (ustart != -1) {
		size = uend - ustart + 1;
		if (size <= skip)
			return;
		if (skip > 0) {
			ustart += skip;
			size -= skip;
		}
		if (size > limit)
			size = limit;
		if (debug) {
			if (size == 1)
				pwarn("UNALLOCATED %s %" PRId64
				    " MARKED USED\n",
				    name, ustart);
			else
				pwarn("UNALLOCATED %sS %" PRId64 "-%" PRId64
				    " MARKED USED\n",
				    name, ustart, ustart + size - 1);
		}
		if (usesysctl != 0) {
			cmd.value = ustart;
			cmd.size = size;
			if (sysctl(opcode, MIBSIZE, 0, 0, &cmd,
			    sizeof cmd) == -1) {
				snprintf(buf, BUFSIZE, "FREE %s", name);
				rwerror(buf, cmd.value);
			}
		}
	}
}

static void
clear_blocks(ufs2_daddr_t start, ufs2_daddr_t end)
{

	if (debug)
		printf("Zero frags %jd to %jd\n", start, end);
	if (Zflag)
		blzero(fswritefd, fsbtodb(&sblock, start),
		    lfragtosize(&sblock, end - start + 1));
	if (Eflag)
		blerase(fswritefd, fsbtodb(&sblock, start),
		    lfragtosize(&sblock, end - start + 1));
}
