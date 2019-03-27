/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000 Christoph Herrmann, Thomas-Henning von Kamptz
 * Copyright (c) 1980, 1989, 1993 The Regents of the University of California.
 * All rights reserved.
 * 
 * This code is derived from software contributed to Berkeley by
 * Christoph Herrmann and Thomas-Henning von Kamptz, Munich and Frankfurt.
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
 *    must display the following acknowledgment:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors, as well as Christoph
 *      Herrmann and Thomas-Henning von Kamptz.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $TSHeader: src/sbin/growfs/debug.c,v 1.3 2000/12/12 19:31:00 tomsoft Exp $
 *
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include "debug.h"

#ifdef FS_DEBUG

static FILE		*dbg_log = NULL;
static unsigned int	indent = 0;

/*
 * prototypes not done here, as they come with debug.h
 */

/*
 * Open the filehandle where all debug output has to go.
 */
void
dbg_open(const char *fn)
{

	if (strcmp(fn, "-") == 0)
		dbg_log = fopen("/dev/stdout", "a");
	else
		dbg_log = fopen(fn, "a");

	return;
}

/*
 * Close the filehandle where all debug output went to.
 */
void
dbg_close(void)
{

	if (dbg_log) {
		fclose(dbg_log);
		dbg_log = NULL;
	}

	return;
}

/*
 * Dump out a full file system block in hex.
 */
void
dbg_dump_hex(struct fs *sb, const char *comment, unsigned char *mem)
{
	int i, j, k;

	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START HEXDUMP =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)mem, comment);
	indent++;
	for (i = 0; i < sb->fs_bsize; i += 24) {
		for (j = 0; j < 3; j++) {
			for (k = 0; k < 8; k++)
				fprintf(dbg_log, "%02x ", *mem++);
			fprintf(dbg_log, "  ");
		}
		fprintf(dbg_log, "\n");
	}
	indent--;
	fprintf(dbg_log, "===== END HEXDUMP =====\n");

	return;
}

/*
 * Dump the superblock.
 */
void
dbg_dump_fs(struct fs *sb, const char *comment)
{
	int j;

	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START SUPERBLOCK =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)sb, comment);
	indent++;

	fprintf(dbg_log, "sblkno            int32_t          0x%08x\n",
	    sb->fs_sblkno);
	fprintf(dbg_log, "cblkno            int32_t          0x%08x\n",
	    sb->fs_cblkno);
	fprintf(dbg_log, "iblkno            int32_t          0x%08x\n",
	    sb->fs_iblkno);
	fprintf(dbg_log, "dblkno            int32_t          0x%08x\n",
	    sb->fs_dblkno);

	fprintf(dbg_log, "old_cgoffset      int32_t          0x%08x\n",
	    sb->fs_old_cgoffset);
	fprintf(dbg_log, "old_cgmask        int32_t          0x%08x\n",
	    sb->fs_old_cgmask);
	fprintf(dbg_log, "old_time          int32_t          %10u\n",
	    (unsigned int)sb->fs_old_time);
	fprintf(dbg_log, "old_size          int32_t          0x%08x\n",
	    sb->fs_old_size);
	fprintf(dbg_log, "old_dsize         int32_t          0x%08x\n",
	    sb->fs_old_dsize);
	fprintf(dbg_log, "ncg               int32_t          0x%08x\n",
	    sb->fs_ncg);
	fprintf(dbg_log, "bsize             int32_t          0x%08x\n",
	    sb->fs_bsize);
	fprintf(dbg_log, "fsize             int32_t          0x%08x\n",
	    sb->fs_fsize);
	fprintf(dbg_log, "frag              int32_t          0x%08x\n",
	    sb->fs_frag);

	fprintf(dbg_log, "minfree           int32_t          0x%08x\n",
	    sb->fs_minfree);
	fprintf(dbg_log, "old_rotdelay      int32_t          0x%08x\n",
	    sb->fs_old_rotdelay);
	fprintf(dbg_log, "old_rps           int32_t          0x%08x\n",
	    sb->fs_old_rps);

	fprintf(dbg_log, "bmask             int32_t          0x%08x\n",
	    sb->fs_bmask);
	fprintf(dbg_log, "fmask             int32_t          0x%08x\n",
	    sb->fs_fmask);
	fprintf(dbg_log, "bshift            int32_t          0x%08x\n",
	    sb->fs_bshift);
	fprintf(dbg_log, "fshift            int32_t          0x%08x\n",
	    sb->fs_fshift);

	fprintf(dbg_log, "maxcontig         int32_t          0x%08x\n",
	    sb->fs_maxcontig);
	fprintf(dbg_log, "maxbpg            int32_t          0x%08x\n",
	    sb->fs_maxbpg);

	fprintf(dbg_log, "fragshift         int32_t          0x%08x\n",
	    sb->fs_fragshift);
	fprintf(dbg_log, "fsbtodb           int32_t          0x%08x\n",
	    sb->fs_fsbtodb);
	fprintf(dbg_log, "sbsize            int32_t          0x%08x\n",
	    sb->fs_sbsize);
	fprintf(dbg_log, "spare1            int32_t[2]       0x%08x 0x%08x\n",
	    sb->fs_spare1[0], sb->fs_spare1[1]);
	fprintf(dbg_log, "nindir            int32_t          0x%08x\n",
	    sb->fs_nindir);
	fprintf(dbg_log, "inopb             int32_t          0x%08x\n",
	    sb->fs_inopb);
	fprintf(dbg_log, "old_nspf          int32_t          0x%08x\n",
	    sb->fs_old_nspf);

	fprintf(dbg_log, "optim             int32_t          0x%08x\n",
	    sb->fs_optim);

	fprintf(dbg_log, "old_npsect        int32_t          0x%08x\n",
	    sb->fs_old_npsect);
	fprintf(dbg_log, "old_interleave    int32_t          0x%08x\n",
	    sb->fs_old_interleave);
	fprintf(dbg_log, "old_trackskew     int32_t          0x%08x\n",
	    sb->fs_old_trackskew);

	fprintf(dbg_log, "id                int32_t[2]       0x%08x 0x%08x\n",
	    sb->fs_id[0], sb->fs_id[1]);

	fprintf(dbg_log, "old_csaddr        int32_t          0x%08x\n",
	    sb->fs_old_csaddr);
	fprintf(dbg_log, "cssize            int32_t          0x%08x\n",
	    sb->fs_cssize);
	fprintf(dbg_log, "cgsize            int32_t          0x%08x\n",
	    sb->fs_cgsize);

	fprintf(dbg_log, "spare2            int32_t          0x%08x\n",
	    sb->fs_spare2);
	fprintf(dbg_log, "old_nsect         int32_t          0x%08x\n",
	    sb->fs_old_nsect);
	fprintf(dbg_log, "old_spc           int32_t          0x%08x\n",
	    sb->fs_old_spc);

	fprintf(dbg_log, "old_ncyl          int32_t          0x%08x\n",
	    sb->fs_old_ncyl);

	fprintf(dbg_log, "old_cpg           int32_t          0x%08x\n",
	    sb->fs_old_cpg);
	fprintf(dbg_log, "ipg               int32_t          0x%08x\n",
	    sb->fs_ipg);
	fprintf(dbg_log, "fpg               int32_t          0x%08x\n",
	    sb->fs_fpg);

	dbg_dump_csum("internal old_cstotal", &sb->fs_old_cstotal);

	fprintf(dbg_log, "fmod              int8_t           0x%02x\n",
	    sb->fs_fmod);
	fprintf(dbg_log, "clean             int8_t           0x%02x\n",
	    sb->fs_clean);
	fprintf(dbg_log, "ronly             int8_t           0x%02x\n",
	    sb->fs_ronly);
	fprintf(dbg_log, "old_flags         int8_t           0x%02x\n",
	    sb->fs_old_flags);
	fprintf(dbg_log, "fsmnt             u_char[MAXMNTLEN] \"%s\"\n",
	    sb->fs_fsmnt);
	fprintf(dbg_log, "volname           u_char[MAXVOLLEN] \"%s\"\n",
	    sb->fs_volname);
	fprintf(dbg_log, "swuid             u_int64_t        0x%08x%08x\n",
	    ((unsigned int *)&(sb->fs_swuid))[1],
		((unsigned int *)&(sb->fs_swuid))[0]);

	fprintf(dbg_log, "pad               int32_t          0x%08x\n",
	    sb->fs_pad);

	fprintf(dbg_log, "cgrotor           int32_t          0x%08x\n",
	    sb->fs_cgrotor);
/*
 * struct csum[MAXCSBUFS] - is only maintained in memory
 */
/*	fprintf(dbg_log, " int32_t\n", sb->*fs_maxcluster);*/
	fprintf(dbg_log, "old_cpc           int32_t          0x%08x\n",
	    sb->fs_old_cpc);
/*
 * int16_t fs_opostbl[16][8] - is dumped when used in dbg_dump_sptbl
 */
	fprintf(dbg_log, "maxbsize          int32_t          0x%08x\n",
	    sb->fs_maxbsize);
	fprintf(dbg_log, "unrefs            int64_t          0x%08jx\n",
	    sb->fs_unrefs);
	fprintf(dbg_log, "sblockloc         int64_t          0x%08x%08x\n",
		((unsigned int *)&(sb->fs_sblockloc))[1],
		((unsigned int *)&(sb->fs_sblockloc))[0]);

	dbg_dump_csum_total("internal cstotal", &sb->fs_cstotal);

	fprintf(dbg_log, "time              ufs_time_t       %10u\n",
	    (unsigned int)sb->fs_time);

	fprintf(dbg_log, "size              int64_t          0x%08x%08x\n",
		((unsigned int *)&(sb->fs_size))[1],
		((unsigned int *)&(sb->fs_size))[0]);
	fprintf(dbg_log, "dsize             int64_t          0x%08x%08x\n",
		((unsigned int *)&(sb->fs_dsize))[1],
		((unsigned int *)&(sb->fs_dsize))[0]);
	fprintf(dbg_log, "csaddr            ufs2_daddr_t     0x%08x%08x\n",
		((unsigned int *)&(sb->fs_csaddr))[1],
		((unsigned int *)&(sb->fs_csaddr))[0]);
	fprintf(dbg_log, "pendingblocks     int64_t          0x%08x%08x\n",
		((unsigned int *)&(sb->fs_pendingblocks))[1],
		((unsigned int *)&(sb->fs_pendingblocks))[0]);
	fprintf(dbg_log, "pendinginodes     int32_t          0x%08x\n",
	    sb->fs_pendinginodes);

	for (j = 0; j < FSMAXSNAP; j++) {
		fprintf(dbg_log, "snapinum          int32_t[%2d]      0x%08x\n",
		    j, sb->fs_snapinum[j]);
		if (!sb->fs_snapinum[j]) { /* list is dense */
			break;
		}
	}
	fprintf(dbg_log, "avgfilesize       int32_t          0x%08x\n",
	    sb->fs_avgfilesize);
	fprintf(dbg_log, "avgfpdir          int32_t          0x%08x\n",
	    sb->fs_avgfpdir);
	fprintf(dbg_log, "save_cgsize       int32_t          0x%08x\n",
	    sb->fs_save_cgsize);
	fprintf(dbg_log, "flags             int32_t          0x%08x\n",
	    sb->fs_flags);
	fprintf(dbg_log, "contigsumsize     int32_t          0x%08x\n",
	    sb->fs_contigsumsize);
	fprintf(dbg_log, "maxsymlinklen     int32_t          0x%08x\n",
	    sb->fs_maxsymlinklen);
	fprintf(dbg_log, "old_inodefmt      int32_t          0x%08x\n",
	    sb->fs_old_inodefmt);
	fprintf(dbg_log, "maxfilesize       u_int64_t        0x%08x%08x\n",
	    ((unsigned int *)&(sb->fs_maxfilesize))[1],
	    ((unsigned int *)&(sb->fs_maxfilesize))[0]);
	fprintf(dbg_log, "qbmask            int64_t          0x%08x%08x\n",
	    ((unsigned int *)&(sb->fs_qbmask))[1],
	    ((unsigned int *)&(sb->fs_qbmask))[0]);
	fprintf(dbg_log, "qfmask            int64_t          0x%08x%08x\n",
	    ((unsigned int *)&(sb->fs_qfmask))[1],
	    ((unsigned int *)&(sb->fs_qfmask))[0]);
	fprintf(dbg_log, "state             int32_t          0x%08x\n",
	    sb->fs_state);
	fprintf(dbg_log, "old_postblformat  int32_t          0x%08x\n",
	    sb->fs_old_postblformat);
	fprintf(dbg_log, "old_nrpos         int32_t          0x%08x\n",
	    sb->fs_old_nrpos);
	fprintf(dbg_log, "spare5            int32_t[2]       0x%08x 0x%08x\n",
	    sb->fs_spare5[0], sb->fs_spare5[1]);
	fprintf(dbg_log, "magic             int32_t          0x%08x\n",
	    sb->fs_magic);

	indent--;
	fprintf(dbg_log, "===== END SUPERBLOCK =====\n");

	return;
}

/*
 * Dump a cylinder group.
 */
void
dbg_dump_cg(const char *comment, struct cg *cgr)
{
	int j;

	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START CYLINDER GROUP =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)cgr, comment);
	indent++;

	fprintf(dbg_log, "magic         int32_t    0x%08x\n", cgr->cg_magic);
	fprintf(dbg_log, "old_time      int32_t    0x%08x\n", cgr->cg_old_time);
	fprintf(dbg_log, "cgx           int32_t    0x%08x\n", cgr->cg_cgx);
	fprintf(dbg_log, "old_ncyl      int16_t    0x%04x\n", cgr->cg_old_ncyl);
	fprintf(dbg_log, "old_niblk     int16_t    0x%04x\n", cgr->cg_old_niblk);
	fprintf(dbg_log, "ndblk         int32_t    0x%08x\n", cgr->cg_ndblk);
	dbg_dump_csum("internal cs", &cgr->cg_cs);
	fprintf(dbg_log, "rotor         int32_t    0x%08x\n", cgr->cg_rotor);
	fprintf(dbg_log, "frotor        int32_t    0x%08x\n", cgr->cg_frotor);
	fprintf(dbg_log, "irotor        int32_t    0x%08x\n", cgr->cg_irotor);
	for (j = 0; j < MAXFRAG; j++) {
		fprintf(dbg_log, "frsum         int32_t[%d] 0x%08x\n", j,
		    cgr->cg_frsum[j]);
	}
	fprintf(dbg_log, "old_btotoff   int32_t    0x%08x\n", cgr->cg_old_btotoff);
	fprintf(dbg_log, "old_boff      int32_t    0x%08x\n", cgr->cg_old_boff);
	fprintf(dbg_log, "iusedoff      int32_t    0x%08x\n", cgr->cg_iusedoff);
	fprintf(dbg_log, "freeoff       int32_t    0x%08x\n", cgr->cg_freeoff);
	fprintf(dbg_log, "nextfreeoff   int32_t    0x%08x\n",
	    cgr->cg_nextfreeoff);
	fprintf(dbg_log, "clustersumoff int32_t    0x%08x\n",
	    cgr->cg_clustersumoff);
	fprintf(dbg_log, "clusteroff    int32_t    0x%08x\n",
	    cgr->cg_clusteroff);
	fprintf(dbg_log, "nclusterblks  int32_t    0x%08x\n",
	    cgr->cg_nclusterblks);
	fprintf(dbg_log, "niblk         int32_t    0x%08x\n", cgr->cg_niblk);
	fprintf(dbg_log, "initediblk    int32_t    0x%08x\n", cgr->cg_initediblk);
	fprintf(dbg_log, "unrefs        int32_t    0x%08x\n", cgr->cg_unrefs);
	fprintf(dbg_log, "time          ufs_time_t %10u\n", 
		(unsigned int)cgr->cg_initediblk);

	indent--;
	fprintf(dbg_log, "===== END CYLINDER GROUP =====\n");

	return;
}

/*
 * Dump a cylinder summary.
 */
void
dbg_dump_csum(const char *comment, struct csum *cs)
{

	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START CYLINDER SUMMARY =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)cs, comment);
	indent++;

	fprintf(dbg_log, "ndir   int32_t 0x%08x\n", cs->cs_ndir);
	fprintf(dbg_log, "nbfree int32_t 0x%08x\n", cs->cs_nbfree);
	fprintf(dbg_log, "nifree int32_t 0x%08x\n", cs->cs_nifree);
	fprintf(dbg_log, "nffree int32_t 0x%08x\n", cs->cs_nffree);

	indent--;
	fprintf(dbg_log, "===== END CYLINDER SUMMARY =====\n");

	return;
}

/*
 * Dump a cylinder summary.
 */
void
dbg_dump_csum_total(const char *comment, struct csum_total *cs)
{

	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START CYLINDER SUMMARY TOTAL =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)cs, comment);
	indent++;

	fprintf(dbg_log, "ndir        int64_t 0x%08x%08x\n", 
		((unsigned int *)&(cs->cs_ndir))[1],
		((unsigned int *)&(cs->cs_ndir))[0]);
	fprintf(dbg_log, "nbfree      int64_t 0x%08x%08x\n", 
		((unsigned int *)&(cs->cs_nbfree))[1],
		((unsigned int *)&(cs->cs_nbfree))[0]);
	fprintf(dbg_log, "nifree      int64_t 0x%08x%08x\n", 
		((unsigned int *)&(cs->cs_nifree))[1],
		((unsigned int *)&(cs->cs_nifree))[0]);
	fprintf(dbg_log, "nffree      int64_t 0x%08x%08x\n", 
		((unsigned int *)&(cs->cs_nffree))[1],
		((unsigned int *)&(cs->cs_nffree))[0]);
	fprintf(dbg_log, "numclusters int64_t 0x%08x%08x\n", 
		((unsigned int *)&(cs->cs_numclusters))[1],
		((unsigned int *)&(cs->cs_numclusters))[0]);

	indent--;
	fprintf(dbg_log, "===== END CYLINDER SUMMARY TOTAL =====\n");

	return;
}
/*
 * Dump the inode allocation map in one cylinder group.
 */
void
dbg_dump_inmap(struct fs *sb, const char *comment, struct cg *cgr)
{
	int j,k,l,e;
	unsigned char *cp;

	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START INODE ALLOCATION MAP =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)cgr, comment);
	indent++;

	cp = (unsigned char *)cg_inosused(cgr);
	e = sb->fs_ipg / 8;
	for (j = 0; j < e; j += 32) {
		fprintf(dbg_log, "%08x: ", j);
		for (k = 0; k < 32; k += 8) {
			if (j + k + 8 < e) {
				fprintf(dbg_log,
				    "%02x%02x%02x%02x%02x%02x%02x%02x ", 
				    cp[0], cp[1], cp[2], cp[3],
				    cp[4], cp[5], cp[6], cp[7]);
			} else {
				for (l = 0; (l < 8) && (j + k + l < e); l++) {
					fprintf(dbg_log, "%02x", cp[l]);
				}
			}
			cp += 8;
		}
		fprintf(dbg_log, "\n");
	}

	indent--;
	fprintf(dbg_log, "===== END INODE ALLOCATION MAP =====\n");

	return;
}


/*
 * Dump the fragment allocation map in one cylinder group.
 */
void
dbg_dump_frmap(struct fs *sb, const char *comment, struct cg *cgr)
{
	int j,k,l,e;
	unsigned char *cp;

	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START FRAGMENT ALLOCATION MAP =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)cgr, comment);
	indent++;

	cp = (unsigned char *)cg_blksfree(cgr);
	if (sb->fs_old_nspf)
		e = howmany(sb->fs_old_cpg * sb->fs_old_spc / sb->fs_old_nspf,
		    CHAR_BIT);
	else
		e = 0;
	for (j = 0; j < e; j += 32) {
		fprintf(dbg_log, "%08x: ", j);
		for (k = 0; k < 32; k += 8) {
			if (j + k + 8 <e) {
				fprintf(dbg_log,
				    "%02x%02x%02x%02x%02x%02x%02x%02x ", 
				    cp[0], cp[1], cp[2], cp[3],
				    cp[4], cp[5], cp[6], cp[7]);
			} else {
				for (l = 0; (l < 8) && (j + k + l < e); l++) {
					fprintf(dbg_log, "%02x", cp[l]);
				}
			}
			cp += 8;
		}
		fprintf(dbg_log, "\n");
	}

	indent--;
	fprintf(dbg_log, "===== END FRAGMENT ALLOCATION MAP =====\n");

	return;
}

/*
 * Dump the cluster allocation map in one cylinder group.
 */
void
dbg_dump_clmap(struct fs *sb, const char *comment, struct cg *cgr)
{
	int j,k,l,e;
	unsigned char *cp;

	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START CLUSTER ALLOCATION MAP =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)cgr, comment);
	indent++;

	cp = (unsigned char *)cg_clustersfree(cgr);
	if (sb->fs_old_nspf)
		e = howmany(sb->fs_old_cpg * sb->fs_old_spc / (sb->fs_old_nspf << sb->fs_fragshift), CHAR_BIT);
	else
		e = 0;
	for (j = 0; j < e; j += 32) {
		fprintf(dbg_log, "%08x: ", j);
		for (k = 0; k < 32; k += 8) {
			if (j + k + 8 < e) {
				fprintf(dbg_log,
				    "%02x%02x%02x%02x%02x%02x%02x%02x ", 
				    cp[0], cp[1], cp[2], cp[3],
				    cp[4], cp[5], cp[6], cp[7]);
			} else {
				for (l = 0; (l < 8) && (j + k + l <e); l++) {
					fprintf(dbg_log, "%02x", cp[l]);
				}
			}
			cp += 8;
		}
		fprintf(dbg_log, "\n");
	}

	indent--;
	fprintf(dbg_log, "===== END CLUSTER ALLOCATION MAP =====\n");

	return;
}

/*
 * Dump the cluster availability summary of one cylinder group.
 */
void
dbg_dump_clsum(struct fs *sb, const char *comment, struct cg *cgr)
{
	int j;
	int *ip;

	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START CLUSTER SUMMARY =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)cgr, comment);
	indent++;

	ip = (int *)cg_clustersum(cgr);
	for (j = 0; j <= sb->fs_contigsumsize; j++) {
		fprintf(dbg_log, "%02d: %8d\n", j, *ip++);
	}

	indent--;
	fprintf(dbg_log, "===== END CLUSTER SUMMARY =====\n");

	return;
}

#ifdef NOT_CURRENTLY
/*
 * This code dates from before the UFS2 integration, and doesn't compile
 * post-UFS2 due to the use of cg_blks().  I'm not sure how best to update
 * this for UFS2, where the rotational bits of UFS no longer apply, so
 * will leave it disabled for now; it should probably be re-enabled
 * specifically for UFS1.
 */
/*
 * Dump the block summary, and the rotational layout table.
 */
void
dbg_dump_sptbl(struct fs *sb, const char *comment, struct cg *cgr)
{
	int j,k;
	int *ip;

	if (!dbg_log)
		return;

	fprintf(dbg_log,
	    "===== START BLOCK SUMMARY AND POSITION TABLE =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)cgr, comment);
	indent++;

	ip = (int *)cg_blktot(cgr);
	for (j = 0; j < sb->fs_old_cpg; j++) {
		fprintf(dbg_log, "%2d: %5d = ", j, *ip++);
		for (k = 0; k < sb->fs_old_nrpos; k++) {
			fprintf(dbg_log, "%4d", cg_blks(sb, cgr, j)[k]);
			if (k < sb->fs_old_nrpos - 1)
				fprintf(dbg_log, " + ");
		}
		fprintf(dbg_log, "\n");
	}

	indent--;
	fprintf(dbg_log, "===== END BLOCK SUMMARY AND POSITION TABLE =====\n");

	return;
}
#endif

/*
 * Dump a UFS1 inode structure.
 */
void
dbg_dump_ufs1_ino(struct fs *sb, const char *comment, struct ufs1_dinode *ino)
{
	int ictr;
	int remaining_blocks;
	
	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START UFS1 INODE DUMP =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)ino, comment);
	indent++;

	fprintf(dbg_log, "mode       u_int16_t      0%o\n", ino->di_mode);
	fprintf(dbg_log, "nlink      int16_t        0x%04x\n", ino->di_nlink);
	fprintf(dbg_log, "size       u_int64_t      0x%08x%08x\n", 
	    ((unsigned int *)&(ino->di_size))[1],
	    ((unsigned int *)&(ino->di_size))[0]);
	fprintf(dbg_log, "atime      int32_t        0x%08x\n", ino->di_atime);
	fprintf(dbg_log, "atimensec  int32_t        0x%08x\n",
	    ino->di_atimensec);
	fprintf(dbg_log, "mtime      int32_t        0x%08x\n",
	    ino->di_mtime);
	fprintf(dbg_log, "mtimensec  int32_t        0x%08x\n",
	    ino->di_mtimensec);
	fprintf(dbg_log, "ctime      int32_t        0x%08x\n", ino->di_ctime);
	fprintf(dbg_log, "ctimensec  int32_t        0x%08x\n",
	    ino->di_ctimensec);

	remaining_blocks = howmany(ino->di_size, sb->fs_bsize); /* XXX ts - +1? */
	for (ictr = 0; ictr < MIN(UFS_NDADDR, remaining_blocks); ictr++) {
		fprintf(dbg_log, "db         ufs_daddr_t[%x] 0x%08x\n", ictr,
		    ino->di_db[ictr]);
	}
	remaining_blocks -= UFS_NDADDR;
	if (remaining_blocks > 0) {
		fprintf(dbg_log, "ib         ufs_daddr_t[0] 0x%08x\n",
		    ino->di_ib[0]);
	}
	remaining_blocks -= howmany(sb->fs_bsize, sizeof(ufs1_daddr_t));
	if (remaining_blocks > 0) {
		fprintf(dbg_log, "ib         ufs_daddr_t[1] 0x%08x\n",
		    ino->di_ib[1]);
	}
#define SQUARE(a) ((a) * (a))
	remaining_blocks -= SQUARE(howmany(sb->fs_bsize, sizeof(ufs1_daddr_t)));
#undef SQUARE
	if (remaining_blocks > 0) {
		fprintf(dbg_log, "ib         ufs_daddr_t[2] 0x%08x\n",
		    ino->di_ib[2]);
	}

	fprintf(dbg_log, "flags      u_int32_t      0x%08x\n", ino->di_flags);
	fprintf(dbg_log, "blocks     int32_t        0x%08x\n", ino->di_blocks);
	fprintf(dbg_log, "gen        int32_t        0x%08x\n", ino->di_gen);
	fprintf(dbg_log, "uid        u_int32_t      0x%08x\n", ino->di_uid);
	fprintf(dbg_log, "gid        u_int32_t      0x%08x\n", ino->di_gid);

	indent--;
	fprintf(dbg_log, "===== END UFS1 INODE DUMP =====\n");

	return;
}

/*
 * Dump a UFS2 inode structure.
 */
void
dbg_dump_ufs2_ino(struct fs *sb, const char *comment, struct ufs2_dinode *ino)
{
	int ictr;
	int remaining_blocks;
	
	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START UFS2 INODE DUMP =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)ino, comment);
	indent++;

	fprintf(dbg_log, "mode       u_int16_t      0%o\n", ino->di_mode);
	fprintf(dbg_log, "nlink      int16_t        0x%04x\n", ino->di_nlink);
	fprintf(dbg_log, "uid        u_int32_t      0x%08x\n", ino->di_uid);
	fprintf(dbg_log, "gid        u_int32_t      0x%08x\n", ino->di_gid);
	fprintf(dbg_log, "blksize    u_int32_t      0x%08x\n", ino->di_blksize);
	fprintf(dbg_log, "size       u_int64_t      0x%08x%08x\n", 
	    ((unsigned int *)&(ino->di_size))[1],
	    ((unsigned int *)&(ino->di_size))[0]);
	fprintf(dbg_log, "blocks     u_int64_t      0x%08x%08x\n", 
	    ((unsigned int *)&(ino->di_blocks))[1],
	    ((unsigned int *)&(ino->di_blocks))[0]);
	fprintf(dbg_log, "atime      ufs_time_t     %10jd\n", ino->di_atime);
	fprintf(dbg_log, "mtime      ufs_time_t     %10jd\n", ino->di_mtime);
	fprintf(dbg_log, "ctime      ufs_time_t     %10jd\n", ino->di_ctime);
	fprintf(dbg_log, "birthtime  ufs_time_t     %10jd\n", ino->di_birthtime);
	fprintf(dbg_log, "mtimensec  int32_t        0x%08x\n", ino->di_mtimensec);
	fprintf(dbg_log, "atimensec  int32_t        0x%08x\n", ino->di_atimensec);
	fprintf(dbg_log, "ctimensec  int32_t        0x%08x\n", ino->di_ctimensec);
	fprintf(dbg_log, "birthnsec  int32_t        0x%08x\n", ino->di_birthnsec);
	fprintf(dbg_log, "gen        int32_t        0x%08x\n", ino->di_gen);
	fprintf(dbg_log, "kernflags  u_int32_t      0x%08x\n", ino->di_kernflags);
	fprintf(dbg_log, "flags      u_int32_t      0x%08x\n", ino->di_flags);
	fprintf(dbg_log, "extsize    u_int32_t      0x%08x\n", ino->di_extsize);

	/* XXX: What do we do with di_extb[UFS_NXADDR]? */

	remaining_blocks = howmany(ino->di_size, sb->fs_bsize); /* XXX ts - +1? */
	for (ictr = 0; ictr < MIN(UFS_NDADDR, remaining_blocks); ictr++) {
		fprintf(dbg_log, "db         ufs2_daddr_t[%x] 0x%16jx\n", ictr,
		    ino->di_db[ictr]);
	}
	remaining_blocks -= UFS_NDADDR;
	if (remaining_blocks > 0) {
		fprintf(dbg_log, "ib         ufs2_daddr_t[0] 0x%16jx\n",
		    ino->di_ib[0]);
	}
	remaining_blocks -= howmany(sb->fs_bsize, sizeof(ufs2_daddr_t));
	if (remaining_blocks > 0) {
		fprintf(dbg_log, "ib         ufs2_daddr_t[1] 0x%16jx\n",
		    ino->di_ib[1]);
	}
#define SQUARE(a) ((a) * (a))
	remaining_blocks -= SQUARE(howmany(sb->fs_bsize, sizeof(ufs2_daddr_t)));
#undef SQUARE
	if (remaining_blocks > 0) {
		fprintf(dbg_log, "ib         ufs2_daddr_t[2] 0x%16jx\n",
		    ino->di_ib[2]);
	}

	indent--;
	fprintf(dbg_log, "===== END UFS2 INODE DUMP =====\n");

	return;
}

/*
 * Dump an indirect block. The iteration to dump a full file has to be
 * written around.
 */
void
dbg_dump_iblk(struct fs *sb, const char *comment, char *block, size_t length)
{
	unsigned int *mem, i, j, size;

	if (!dbg_log)
		return;

	fprintf(dbg_log, "===== START INDIRECT BLOCK DUMP =====\n");
	fprintf(dbg_log, "# %d@%lx: %s\n", indent, (unsigned long)block,
	    comment);
	indent++;

	if (sb->fs_magic == FS_UFS1_MAGIC)
		size = sizeof(ufs1_daddr_t);
	else
		size = sizeof(ufs2_daddr_t);

	mem = (unsigned int *)block;
	for (i = 0; (size_t)i < MIN(howmany(sb->fs_bsize, size), length);
	    i += 8) {
		fprintf(dbg_log, "%04x: ", i);
		for (j = 0; j < 8; j++) {
			if ((size_t)(i + j) < length)
				fprintf(dbg_log, "%08X ", *mem++);
		}
		fprintf(dbg_log, "\n");
	}

	indent--;
	fprintf(dbg_log, "===== END INDIRECT BLOCK DUMP =====\n");

	return;
}

#endif /* FS_DEBUG */

