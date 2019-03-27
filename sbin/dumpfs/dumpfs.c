/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc.
 *
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program.
 *
 * Copyright (c) 1983, 1992, 1993
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)dumpfs.c	8.5 (Berkeley) 4/29/95";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/disklabel.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <libufs.h>
#include <paths.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define	afs	disk.d_fs
#define	acg	disk.d_cg

static struct uufsd disk;

static int	dumpfs(const char *);
static int	dumpfsid(void);
static int	dumpcg(void);
static int	dumpfreespace(const char *, int);
static void	dumpfreespacecg(int);
static int	marshal(const char *);
static void	pbits(void *, int);
static void	pblklist(void *, int, off_t, int);
static void	ufserr(const char *);
static void	usage(void) __dead2;

int
main(int argc, char *argv[])
{
	const char *name;
	int ch, dofreespace, domarshal, dolabel, eval;

	dofreespace = domarshal = dolabel = eval = 0;

	while ((ch = getopt(argc, argv, "lfm")) != -1) {
		switch (ch) {
		case 'f':
			dofreespace++;
			break;
		case 'm':
			domarshal = 1;
			break;
		case 'l':
			dolabel = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();
	if (dofreespace && domarshal)
		usage();
	if (dofreespace > 2)
		usage();

	while ((name = *argv++) != NULL) {
		if (ufs_disk_fillout(&disk, name) == -1) {
			ufserr(name);
			eval |= 1;
			continue;
		}
		if (dofreespace)
			eval |= dumpfreespace(name, dofreespace);
		else if (domarshal)
			eval |= marshal(name);
		else if (dolabel)
			eval |= dumpfsid();
		else
			eval |= dumpfs(name);
		ufs_disk_close(&disk);
	}
	exit(eval);
}

static int
dumpfsid(void)
{

	printf("%sufsid/%08x%08x\n", _PATH_DEV, afs.fs_id[0], afs.fs_id[1]);
	return 0;
}

static int
dumpfs(const char *name)
{
	time_t fstime;
	int64_t fssize;
	int32_t fsflags;
	int i;

	switch (disk.d_ufs) {
	case 2:
		fssize = afs.fs_size;
		fstime = afs.fs_time;
		printf("magic\t%x (UFS2)\ttime\t%s",
		    afs.fs_magic, ctime(&fstime));
		printf("superblock location\t%jd\tid\t[ %08x %08x ]\n",
		    (intmax_t)afs.fs_sblockloc, afs.fs_id[0], afs.fs_id[1]);
		printf("ncg\t%d\tsize\t%jd\tblocks\t%jd\n",
		    afs.fs_ncg, (intmax_t)fssize, (intmax_t)afs.fs_dsize);
		break;
	case 1:
		fssize = afs.fs_old_size;
		fstime = afs.fs_old_time;
		printf("magic\t%x (UFS1)\ttime\t%s",
		    afs.fs_magic, ctime(&fstime));
		printf("id\t[ %08x %08x ]\n", afs.fs_id[0], afs.fs_id[1]);
		printf("ncg\t%d\tsize\t%jd\tblocks\t%jd\n",
		    afs.fs_ncg, (intmax_t)fssize, (intmax_t)afs.fs_dsize);
		break;
	default:
		goto err;
	}
	printf("bsize\t%d\tshift\t%d\tmask\t0x%08x\n",
	    afs.fs_bsize, afs.fs_bshift, afs.fs_bmask);
	printf("fsize\t%d\tshift\t%d\tmask\t0x%08x\n",
	    afs.fs_fsize, afs.fs_fshift, afs.fs_fmask);
	printf("frag\t%d\tshift\t%d\tfsbtodb\t%d\n",
	    afs.fs_frag, afs.fs_fragshift, afs.fs_fsbtodb);
	printf("minfree\t%d%%\toptim\t%s\tsymlinklen %d\n",
	    afs.fs_minfree, afs.fs_optim == FS_OPTSPACE ? "space" : "time",
	    afs.fs_maxsymlinklen);
	switch (disk.d_ufs) {
	case 2:
		printf("%s %d\tmaxbpg\t%d\tmaxcontig %d\tcontigsumsize %d\n",
		    "maxbsize", afs.fs_maxbsize, afs.fs_maxbpg,
		    afs.fs_maxcontig, afs.fs_contigsumsize);
		printf("nbfree\t%jd\tndir\t%jd\tnifree\t%jd\tnffree\t%jd\n",
		    (intmax_t)afs.fs_cstotal.cs_nbfree,
		    (intmax_t)afs.fs_cstotal.cs_ndir,
		    (intmax_t)afs.fs_cstotal.cs_nifree,
		    (intmax_t)afs.fs_cstotal.cs_nffree);
		printf("bpg\t%d\tfpg\t%d\tipg\t%d\tunrefs\t%jd\n",
		    afs.fs_fpg / afs.fs_frag, afs.fs_fpg, afs.fs_ipg,
		    (intmax_t)afs.fs_unrefs);
		printf("nindir\t%d\tinopb\t%d\tmaxfilesize\t%ju\n",
		    afs.fs_nindir, afs.fs_inopb,
		    (uintmax_t)afs.fs_maxfilesize);
		printf("sbsize\t%d\tcgsize\t%d\tcsaddr\t%jd\tcssize\t%d\n",
		    afs.fs_sbsize, afs.fs_cgsize, (intmax_t)afs.fs_csaddr,
		    afs.fs_cssize);
		break;
	case 1:
		printf("maxbpg\t%d\tmaxcontig %d\tcontigsumsize %d\n",
		    afs.fs_maxbpg, afs.fs_maxcontig, afs.fs_contigsumsize);
		printf("nbfree\t%d\tndir\t%d\tnifree\t%d\tnffree\t%d\n",
		    afs.fs_old_cstotal.cs_nbfree, afs.fs_old_cstotal.cs_ndir,
		    afs.fs_old_cstotal.cs_nifree, afs.fs_old_cstotal.cs_nffree);
		printf("cpg\t%d\tbpg\t%d\tfpg\t%d\tipg\t%d\n",
		    afs.fs_old_cpg, afs.fs_fpg / afs.fs_frag, afs.fs_fpg,
		    afs.fs_ipg);
		printf("nindir\t%d\tinopb\t%d\tnspf\t%d\tmaxfilesize\t%ju\n",
		    afs.fs_nindir, afs.fs_inopb, afs.fs_old_nspf,
		    (uintmax_t)afs.fs_maxfilesize);
		printf("sbsize\t%d\tcgsize\t%d\tcgoffset %d\tcgmask\t0x%08x\n",
		    afs.fs_sbsize, afs.fs_cgsize, afs.fs_old_cgoffset,
		    afs.fs_old_cgmask);
		printf("csaddr\t%d\tcssize\t%d\n",
		    afs.fs_old_csaddr, afs.fs_cssize);
		printf("rotdelay %dms\trps\t%d\ttrackskew %d\tinterleave %d\n",
		    afs.fs_old_rotdelay, afs.fs_old_rps, afs.fs_old_trackskew,
		    afs.fs_old_interleave);
		printf("nsect\t%d\tnpsect\t%d\tspc\t%d\n",
		    afs.fs_old_nsect, afs.fs_old_npsect, afs.fs_old_spc);
		break;
	default:
		goto err;
	}
	printf("sblkno\t%d\tcblkno\t%d\tiblkno\t%d\tdblkno\t%d\n",
	    afs.fs_sblkno, afs.fs_cblkno, afs.fs_iblkno, afs.fs_dblkno);
	printf("cgrotor\t%d\tfmod\t%d\tronly\t%d\tclean\t%d\n",
	    afs.fs_cgrotor, afs.fs_fmod, afs.fs_ronly, afs.fs_clean);
	printf("metaspace %jd\tavgfpdir %d\tavgfilesize %d\n",
	    afs.fs_metaspace, afs.fs_avgfpdir, afs.fs_avgfilesize);
	printf("flags\t");
	if (afs.fs_old_flags & FS_FLAGS_UPDATED)
		fsflags = afs.fs_flags;
	else
		fsflags = afs.fs_old_flags;
	if (fsflags == 0)
		printf("none");
	if (fsflags & FS_UNCLEAN)
		printf("unclean ");
	if (fsflags & FS_DOSOFTDEP)
		printf("soft-updates%s ", (fsflags & FS_SUJ) ? "+journal" : "");
	if (fsflags & FS_NEEDSFSCK)
		printf("needs-fsck-run ");
	if (fsflags & FS_INDEXDIRS)
		printf("indexed-directories ");
	if (fsflags & FS_ACLS)
		printf("acls ");
	if (fsflags & FS_MULTILABEL)
		printf("multilabel ");
	if (fsflags & FS_GJOURNAL)
		printf("gjournal ");
	if (fsflags & FS_FLAGS_UPDATED)
		printf("fs_flags-expanded ");
	if (fsflags & FS_NFS4ACLS)
		printf("nfsv4acls ");
	if (fsflags & FS_TRIM)
		printf("trim ");
	fsflags &= ~(FS_UNCLEAN | FS_DOSOFTDEP | FS_NEEDSFSCK | FS_METACKHASH |
		     FS_ACLS | FS_MULTILABEL | FS_GJOURNAL | FS_FLAGS_UPDATED |
		     FS_NFS4ACLS | FS_SUJ | FS_TRIM | FS_INDEXDIRS);
	if (fsflags != 0)
		printf("unknown-flags (%#x)", fsflags);
	putchar('\n');
	if (afs.fs_flags & FS_METACKHASH) {
		printf("check hashes\t");
		fsflags = afs.fs_metackhash;
		if (fsflags == 0)
			printf("none");
		if (fsflags & CK_SUPERBLOCK)
			printf("superblock ");
		if (fsflags & CK_CYLGRP)
			printf("cylinder-groups ");
		if (fsflags & CK_INODE)
			printf("inodes ");
		if (fsflags & CK_INDIR)
			printf("indirect-blocks ");
		if (fsflags & CK_DIR)
			printf("directories ");
	}
	fsflags &= ~(CK_SUPERBLOCK | CK_CYLGRP | CK_INODE | CK_INDIR | CK_DIR);
	if (fsflags != 0)
		printf("unknown flags (%#x)", fsflags);
	putchar('\n');
	printf("fsmnt\t%s\n", afs.fs_fsmnt);
	printf("volname\t%s\tswuid\t%ju\tprovidersize\t%ju\n",
		afs.fs_volname, (uintmax_t)afs.fs_swuid,
		(uintmax_t)afs.fs_providersize);
	printf("\ncs[].cs_(nbfree,ndir,nifree,nffree):\n\t");
	afs.fs_csp = calloc(1, afs.fs_cssize);
	if (bread(&disk, fsbtodb(&afs, afs.fs_csaddr), afs.fs_csp, afs.fs_cssize) == -1)
		goto err;
	for (i = 0; i < afs.fs_ncg; i++) {
		struct csum *cs = &afs.fs_cs(&afs, i);
		if (i && i % 4 == 0)
			printf("\n\t");
		printf("(%d,%d,%d,%d) ",
		    cs->cs_nbfree, cs->cs_ndir, cs->cs_nifree, cs->cs_nffree);
	}
	printf("\n");
	if (fssize % afs.fs_fpg) {
		if (disk.d_ufs == 1)
			printf("cylinders in last group %d\n",
			    howmany(afs.fs_old_size % afs.fs_fpg,
			    afs.fs_old_spc / afs.fs_old_nspf));
		printf("blocks in last group %ld\n\n",
		    (long)((fssize % afs.fs_fpg) / afs.fs_frag));
	}
	while ((i = cgread(&disk)) != 0) {
		if (i == -1 || dumpcg())
			goto err;
	}
	return (0);

err:	ufserr(name);
	return (1);
}

static int
dumpcg(void)
{
	time_t cgtime;
	off_t cur;
	int i, j;

	printf("\ncg %d:\n", disk.d_lcg);
	cur = fsbtodb(&afs, cgtod(&afs, disk.d_lcg)) * disk.d_bsize;
	switch (disk.d_ufs) {
	case 2:
		cgtime = acg.cg_time;
		printf("magic\t%x\ttell\t%jx\ttime\t%s",
		    acg.cg_magic, (intmax_t)cur, ctime(&cgtime));
		printf("cgx\t%d\tndblk\t%d\tniblk\t%d\tinitiblk %d\tunrefs %d\n",
		    acg.cg_cgx, acg.cg_ndblk, acg.cg_niblk, acg.cg_initediblk,
		    acg.cg_unrefs);
		break;
	case 1:
		cgtime = acg.cg_old_time;
		printf("magic\t%x\ttell\t%jx\ttime\t%s",
		    acg.cg_magic, (intmax_t)cur, ctime(&cgtime));
		printf("cgx\t%d\tncyl\t%d\tniblk\t%d\tndblk\t%d\n",
		    acg.cg_cgx, acg.cg_old_ncyl, acg.cg_old_niblk,
		    acg.cg_ndblk);
		break;
	default:
		break;
	}
	printf("nbfree\t%d\tndir\t%d\tnifree\t%d\tnffree\t%d\n",
	    acg.cg_cs.cs_nbfree, acg.cg_cs.cs_ndir,
	    acg.cg_cs.cs_nifree, acg.cg_cs.cs_nffree);
	printf("rotor\t%d\tirotor\t%d\tfrotor\t%d\nfrsum",
	    acg.cg_rotor, acg.cg_irotor, acg.cg_frotor);
	for (i = 1, j = 0; i < afs.fs_frag; i++) {
		printf("\t%d", acg.cg_frsum[i]);
		j += i * acg.cg_frsum[i];
	}
	printf("\nsum of frsum: %d", j);
	if (afs.fs_contigsumsize > 0) {
		for (i = 1; i < afs.fs_contigsumsize; i++) {
			if ((i - 1) % 8 == 0)
				printf("\nclusters %d-%d:", i,
				    MIN(afs.fs_contigsumsize - 1, i + 7));
			printf("\t%d", cg_clustersum(&acg)[i]);
		}
		printf("\nclusters size %d and over: %d\n",
		    afs.fs_contigsumsize,
		    cg_clustersum(&acg)[afs.fs_contigsumsize]);
		printf("clusters free:\t");
		pbits(cg_clustersfree(&acg), acg.cg_nclusterblks);
	} else
		printf("\n");
	printf("inodes used:\t");
	pbits(cg_inosused(&acg), afs.fs_ipg);
	printf("blks free:\t");
	pbits(cg_blksfree(&acg), afs.fs_fpg);
	return (0);
}

static int
dumpfreespace(const char *name, int fflag)
{
	int i;

	while ((i = cgread(&disk)) != 0) {
		if (i == -1)
			goto err;
		dumpfreespacecg(fflag);
	}
	return (0);
err:
	ufserr(name);
	return (1);
}

static void
dumpfreespacecg(int fflag)
{

	pblklist(cg_blksfree(&acg), afs.fs_fpg, disk.d_lcg * afs.fs_fpg,
	    fflag);
}

static int
marshal(const char *name)
{
	struct fs *fs;

	fs = &disk.d_fs;

	printf("# newfs command for %s (%s)\n", name, disk.d_name);
	printf("newfs ");
	if (fs->fs_volname[0] != '\0')
		printf("-L %s ", fs->fs_volname);
	printf("-O %d ", disk.d_ufs);
	if (fs->fs_flags & FS_DOSOFTDEP)
		printf("-U ");
	printf("-a %d ", fs->fs_maxcontig);
	printf("-b %d ", fs->fs_bsize);
	/* -c is dumb */
	printf("-d %d ", fs->fs_maxbsize);
	printf("-e %d ", fs->fs_maxbpg);
	printf("-f %d ", fs->fs_fsize);
	printf("-g %d ", fs->fs_avgfilesize);
	printf("-h %d ", fs->fs_avgfpdir);
	printf("-i %jd ", fragroundup(fs, lblktosize(fs, fragstoblks(fs,
	    fs->fs_fpg)) / fs->fs_ipg));
	if (fs->fs_flags & FS_SUJ)
		printf("-j ");
	if (fs->fs_flags & FS_GJOURNAL)
		printf("-J ");
	printf("-k %jd ", fs->fs_metaspace);
	if (fs->fs_flags & FS_MULTILABEL)
		printf("-l ");
	printf("-m %d ", fs->fs_minfree);
	/* -n unimplemented */
	printf("-o ");
	switch (fs->fs_optim) {
	case FS_OPTSPACE:
		printf("space ");
		break;
	case FS_OPTTIME:
		printf("time ");
		break;
	default:
		printf("unknown ");
		break;
	}
	/* -p..r unimplemented */
	printf("-s %jd ", (intmax_t)fsbtodb(fs, fs->fs_size));
	if (fs->fs_flags & FS_TRIM)
		printf("-t ");
	printf("%s ", disk.d_name);
	printf("\n");

	return 0;
}

static void
pbits(void *vp, int max)
{
	int i;
	char *p;
	int count, j;

	for (count = i = 0, p = vp; i < max; i++)
		if (isset(p, i)) {
			if (count)
				printf(",%s", count % 6 ? " " : "\n\t");
			count++;
			printf("%d", i);
			j = i;
			while ((i+1)<max && isset(p, i+1))
				i++;
			if (i != j)
				printf("-%d", i);
		}
	printf("\n");
}

static void
pblklist(void *vp, int max, off_t offset, int fflag)
{
	int i, j;
	char *p;

	for (i = 0, p = vp; i < max; i++) {
		if (isset(p, i)) {
			printf("%jd", (intmax_t)(i + offset));
			if (fflag < 2) {
				j = i;
				while ((i+1)<max && isset(p, i+1))
					i++;
				if (i != j)
					printf("-%jd", (intmax_t)(i + offset));
			}
			printf("\n");
		}
	}
}

static void
ufserr(const char *name)
{
	if (disk.d_error != NULL)
		warnx("%s: %s", name, disk.d_error);
	else if (errno)
		warn("%s", name);
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: dumpfs [-flm] filesys | device\n");
	exit(1);
}
