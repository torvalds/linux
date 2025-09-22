/*	$OpenBSD: dumpfs.c,v 1.39 2024/05/09 08:35:40 florian Exp $	*/

/*
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

#include <sys/param.h>	/* DEV_BSIZE MAXBSIZE isset */
#include <sys/time.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <util.h>

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

union {
	struct fs fs;
	char pad[MAXBSIZE];
} fsun;
#define afs	fsun.fs
 
union {
	struct cg cg;
	char pad[MAXBSIZE];
} cgun;
#define acg	cgun.cg

int	dumpfs(int, const char *);
int	dumpcg(const char *, int, u_int);
int	marshal(const char *);
int	open_disk(const char *);
void	pbits(void *, int);
__dead void	usage(void);

int
main(int argc, char *argv[])
{
	struct fstab *fs;
	const char *name;
	int ch, domarshal, eval, fd;

	domarshal = eval = 0;

	while ((ch = getopt(argc, argv, "m")) != -1) {
		switch (ch) {
		case 'm':
			domarshal = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	if (pledge("stdio rpath disklabel", NULL) == -1)
		err(1, "pledge");

	for (; *argv != NULL; argv++) {
		if ((fs = getfsfile(*argv)) != NULL)
			name = fs->fs_spec;
		else
			name = *argv;
		if ((fd = open_disk(name)) == -1) {
			eval |= 1;
			continue;
		}
		if (domarshal)
			eval |= marshal(name);
		else
			eval |= dumpfs(fd, name);
		close(fd);
	}
	exit(eval);
}

int
open_disk(const char *name)
{
	int fd, i, sbtry[] = SBLOCKSEARCH;
	ssize_t n;

	/* XXX - should retry w/raw device on failure */
	if ((fd = opendev(name, O_RDONLY, 0, NULL)) == -1) {
		warn("%s", name);
		return(-1);
	}

	/* Read superblock, could be UFS1 or UFS2. */
	for (i = 0; sbtry[i] != -1; i++) {
		n = pread(fd, &afs, SBLOCKSIZE, (off_t)sbtry[i]);
		if (n == SBLOCKSIZE && (afs.fs_magic == FS_UFS1_MAGIC ||
		    (afs.fs_magic == FS_UFS2_MAGIC &&
		    afs.fs_sblockloc == sbtry[i])) &&
		    !(afs.fs_magic == FS_UFS1_MAGIC &&
		    sbtry[i] == SBLOCK_UFS2) &&
		    afs.fs_bsize <= MAXBSIZE &&
		    afs.fs_bsize >= sizeof(struct fs))
			break;
	}
	if (sbtry[i] == -1) {
		warnx("cannot find filesystem superblock");
		close(fd);
		return (-1);
	}

	return (fd);
}

int
dumpfs(int fd, const char *name)
{
	time_t fstime;
	int64_t fssize;
	int32_t fsflags;
	size_t size;
	off_t off;
	int i, j;
	u_int cg;
	char *ct;

	switch (afs.fs_magic) {
	case FS_UFS2_MAGIC:
		fssize = afs.fs_size;
		fstime = afs.fs_time;
		ct = ctime(&fstime);
		if (ct)
			printf("magic\t%x (FFS2)\ttime\t%s",
			    afs.fs_magic, ctime(&fstime));
		else
			printf("magic\t%x (FFS2)\ttime\t%lld\n",
			    afs.fs_magic, fstime);
		printf("superblock location\t%jd\tid\t[ %x %x ]\n",
		    (intmax_t)afs.fs_sblockloc, afs.fs_id[0], afs.fs_id[1]);
		printf("ncg\t%u\tsize\t%jd\tblocks\t%jd\n",
		    afs.fs_ncg, (intmax_t)fssize, (intmax_t)afs.fs_dsize);
		break;
	case FS_UFS1_MAGIC:
		fssize = afs.fs_ffs1_size;
		fstime = afs.fs_ffs1_time;
		ct = ctime(&fstime);
		if (ct)
			printf("magic\t%x (FFS1)\ttime\t%s",
			    afs.fs_magic, ctime(&fstime));
		else
			printf("magic\t%x (FFS1)\ttime\t%lld\n",
			    afs.fs_magic, fstime);
		printf("id\t[ %x %x ]\n", afs.fs_id[0], afs.fs_id[1]);
		i = 0;
		if (afs.fs_postblformat != FS_42POSTBLFMT) {
			i++;
			if (afs.fs_inodefmt >= FS_44INODEFMT) {
				size_t max;

				i++;
				max = afs.fs_maxcontig;
				size = afs.fs_contigsumsize;
				if ((max < 2 && size == 0) ||
				    (max > 1 && size >= MINIMUM(max, FS_MAXCONTIG)))
					i++;
			}
		}
		printf("cylgrp\t%s\tinodes\t%s\tfslevel %d\n",
		    i < 1 ? "static" : "dynamic",
		    i < 2 ? "4.2/4.3BSD" : "4.4BSD", i);
		printf("ncg\t%u\tncyl\t%d\tsize\t%d\tblocks\t%d\n",
		    afs.fs_ncg, afs.fs_ncyl, afs.fs_ffs1_size, afs.fs_ffs1_dsize);
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
	switch (afs.fs_magic) {
	case FS_UFS2_MAGIC:
		printf("%s %d\tmaxbpg\t%d\tmaxcontig %d\tcontigsumsize %d\n",
		    "maxbsize", afs.fs_maxbsize, afs.fs_maxbpg,
		    afs.fs_maxcontig, afs.fs_contigsumsize);
		printf("nbfree\t%jd\tndir\t%jd\tnifree\t%jd\tnffree\t%jd\n",
		    (intmax_t)afs.fs_cstotal.cs_nbfree, 
		    (intmax_t)afs.fs_cstotal.cs_ndir,
		    (intmax_t)afs.fs_cstotal.cs_nifree, 
		    (intmax_t)afs.fs_cstotal.cs_nffree);
		printf("bpg\t%d\tfpg\t%d\tipg\t%u\n",
		    afs.fs_fpg / afs.fs_frag, afs.fs_fpg, afs.fs_ipg);
		printf("nindir\t%d\tinopb\t%u\tmaxfilesize\t%ju\n",
		    afs.fs_nindir, afs.fs_inopb, 
		    (uintmax_t)afs.fs_maxfilesize);
		printf("sbsize\t%d\tcgsize\t%d\tcsaddr\t%jd\tcssize\t%d\n",
		    afs.fs_sbsize, afs.fs_cgsize, (intmax_t)afs.fs_csaddr,
		    afs.fs_cssize);
		break;
	case FS_UFS1_MAGIC:
		printf("maxbpg\t%d\tmaxcontig %d\tcontigsumsize %d\n",
		    afs.fs_maxbpg, afs.fs_maxcontig, afs.fs_contigsumsize);
		printf("nbfree\t%d\tndir\t%d\tnifree\t%d\tnffree\t%d\n",
		    afs.fs_ffs1_cstotal.cs_nbfree, afs.fs_ffs1_cstotal.cs_ndir,
		    afs.fs_ffs1_cstotal.cs_nifree, afs.fs_ffs1_cstotal.cs_nffree);
		printf("cpg\t%d\tbpg\t%d\tfpg\t%d\tipg\t%u\n",
		    afs.fs_cpg, afs.fs_fpg / afs.fs_frag, afs.fs_fpg,
		    afs.fs_ipg);
		printf("nindir\t%d\tinopb\t%u\tnspf\t%d\tmaxfilesize\t%ju\n",
		    afs.fs_nindir, afs.fs_inopb, afs.fs_nspf,
		    (uintmax_t)afs.fs_maxfilesize);
		printf("sbsize\t%d\tcgsize\t%d\tcgoffset %d\tcgmask\t0x%08x\n",
		    afs.fs_sbsize, afs.fs_cgsize, afs.fs_cgoffset,
		    afs.fs_cgmask);
		printf("csaddr\t%d\tcssize\t%d\n",
		    afs.fs_ffs1_csaddr, afs.fs_cssize);
		printf("rotdelay %dms\trps\t%d\tinterleave %d\n",
		    afs.fs_rotdelay, afs.fs_rps, afs.fs_interleave);
		printf("nsect\t%d\tnpsect\t%d\tspc\t%d\n",
		    afs.fs_nsect, afs.fs_npsect, afs.fs_spc);
		break;
	default:
		goto err;
	}
	printf("sblkno\t%d\tcblkno\t%d\tiblkno\t%d\tdblkno\t%d\n",
	    afs.fs_sblkno, afs.fs_cblkno, afs.fs_iblkno, afs.fs_dblkno);
	printf("cgrotor\t%d\tfmod\t%d\tronly\t%d\tclean\t%d\n",
	    afs.fs_cgrotor, afs.fs_fmod, afs.fs_ronly, afs.fs_clean);
	printf("avgfpdir %u\tavgfilesize %u\n",
	    afs.fs_avgfpdir, afs.fs_avgfilesize);
	printf("flags\t");
	if (afs.fs_magic == FS_UFS2_MAGIC ||
	    afs.fs_ffs1_flags & FS_FLAGS_UPDATED)
		fsflags = afs.fs_flags;
	else
		fsflags = afs.fs_ffs1_flags;
	if (fsflags == 0)
		printf("none");
	if (fsflags & FS_UNCLEAN)
		printf("unclean ");
	if (fsflags & FS_FLAGS_UPDATED)
		printf("updated ");
#if 0
	fsflags &= ~(FS_UNCLEAN | FS_FLAGS_UPDATED);
	if (fsflags != 0)
		printf("unknown flags (%#x)", fsflags);
#endif
	putchar('\n');
	printf("fsmnt\t%s\n", afs.fs_fsmnt);
	printf("volname\t%s\tswuid\t%ju\n",
		afs.fs_volname, (uintmax_t)afs.fs_swuid);
	printf("\ncs[].cs_(nbfree,ndir,nifree,nffree):\n\t");
	afs.fs_csp = calloc(1, afs.fs_cssize);
	for (i = 0, j = 0; i < afs.fs_cssize; i += afs.fs_bsize, j++) {
		size = afs.fs_cssize - i < afs.fs_bsize ?
		    afs.fs_cssize - i : afs.fs_bsize;
		off = (off_t)(fsbtodb(&afs, (afs.fs_csaddr + j *
		    afs.fs_frag))) * DEV_BSIZE;
		if (pread(fd, (char *)afs.fs_csp + i, size, off) != size)
			goto err;
	}
	for (cg = 0; cg < afs.fs_ncg; cg++) {
		struct csum *cs = &afs.fs_cs(&afs, cg);
		if (i && i % 4 == 0)
			printf("\n\t");
		printf("(%d,%d,%d,%d) ",
		    cs->cs_nbfree, cs->cs_ndir, cs->cs_nifree, cs->cs_nffree);
	}
	printf("\n");
	if (fssize % afs.fs_fpg) {
		if (afs.fs_magic == FS_UFS1_MAGIC)
			printf("cylinders in last group %d\n",
			    howmany(afs.fs_ffs1_size % afs.fs_fpg,
			    afs.fs_spc / afs.fs_nspf));
		printf("blocks in last group %ld\n\n",
		    (long)((fssize % afs.fs_fpg) / afs.fs_frag));
	}
	for (cg = 0; cg < afs.fs_ncg; cg++)
		if (dumpcg(name, fd, cg))
			goto err;
	return (0);

err:	warn("%s", name);
	return (1);
}

int
dumpcg(const char *name, int fd, u_int c)
{
	time_t cgtime;
	off_t cur;
	int i, j;
	char *ct;

	printf("\ncg %u:\n", c);
	cur = (off_t)fsbtodb(&afs, cgtod(&afs, c)) * DEV_BSIZE;
	if (pread(fd, &acg, afs.fs_bsize, cur) != afs.fs_bsize) {
		warn("%s: error reading cg", name);
		return(1);
	}
	switch (afs.fs_magic) {
	case FS_UFS2_MAGIC:
		cgtime = acg.cg_ffs2_time;
		ct = ctime(&cgtime);
		if (ct)
			printf("magic\t%x\ttell\t%jx\ttime\t%s",
			    acg.cg_magic, (intmax_t)cur, ct);
		else
			printf("magic\t%x\ttell\t%jx\ttime\t%lld\n",
			    acg.cg_magic, (intmax_t)cur, cgtime);
		printf("cgx\t%u\tndblk\t%u\tniblk\t%u\tinitiblk %u\n",
		    acg.cg_cgx, acg.cg_ndblk, acg.cg_ffs2_niblk,
		    acg.cg_initediblk);
		break;
	case FS_UFS1_MAGIC:
		cgtime = acg.cg_time;
		ct = ctime(&cgtime);
		if (ct)
			printf("magic\t%x\ttell\t%jx\ttime\t%s",
			    afs.fs_postblformat == FS_42POSTBLFMT ?
			    ((struct ocg *)&acg)->cg_magic : acg.cg_magic,
			    (intmax_t)cur, ct);
		else
			printf("magic\t%x\ttell\t%jx\ttime\t%lld\n",
			    afs.fs_postblformat == FS_42POSTBLFMT ?
			    ((struct ocg *)&acg)->cg_magic : acg.cg_magic,
			    (intmax_t)cur, cgtime);
		printf("cgx\t%u\tncyl\t%d\tniblk\t%d\tndblk\t%u\n",
		    acg.cg_cgx, acg.cg_ncyl, acg.cg_niblk, acg.cg_ndblk);
		break;
	default:
		break;
	}
	printf("nbfree\t%d\tndir\t%d\tnifree\t%d\tnffree\t%d\n",
	    acg.cg_cs.cs_nbfree, acg.cg_cs.cs_ndir,
	    acg.cg_cs.cs_nifree, acg.cg_cs.cs_nffree);
	printf("rotor\t%u\tirotor\t%u\tfrotor\t%u\nfrsum",
	    acg.cg_rotor, acg.cg_irotor, acg.cg_frotor);
	for (i = 1, j = 0; i < afs.fs_frag; i++) {
		printf("\t%u", acg.cg_frsum[i]);
		j += i * acg.cg_frsum[i];
	}
	printf("\nsum of frsum: %d", j);
	if (afs.fs_contigsumsize > 0) {
		for (i = 1; i < afs.fs_contigsumsize; i++) {
			if ((i - 1) % 8 == 0)
				printf("\nclusters %d-%d:", i,
				    afs.fs_contigsumsize - 1 < i + 7 ?
				    afs.fs_contigsumsize - 1 : i + 7);
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
#if 0
	/* XXX - keep this? */
	if (afs.fs_magic == FS_UFS1_MAGIC) {
		printf("b:\n");
		for (i = 0; i < afs.fs_cpg; i++) {
			if (cg_blktot(&acg)[i] == 0)
				continue;
			printf("   c%d:\t(%d)\t", i, cg_blktot(&acg)[i]);
			printf("\n");
		}
	}
#endif
	return (0);
}

int
marshal(const char *name)
{
	int Oflag;

	printf("# newfs command for %s\n", name);
	printf("newfs ");
	if (afs.fs_volname[0] != '\0')
		printf("-L %s ", afs.fs_volname);

	Oflag = (afs.fs_magic == FS_UFS2_MAGIC) +
	    (afs.fs_inodefmt == FS_44INODEFMT);
	printf("-O %d ", Oflag);
	printf("-b %d ", afs.fs_bsize);
	/* -c unimplemented */
	printf("-e %d ", afs.fs_maxbpg);
	printf("-f %d ", afs.fs_fsize);
	printf("-g %u ", afs.fs_avgfilesize);
	printf("-h %u ", afs.fs_avgfpdir);
	/* -i unimplemented */
	printf("-m %d ", afs.fs_minfree);
	printf("-o ");
	switch (afs.fs_optim) {
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
	/* -S unimplemented */
	printf("-s %jd ", (intmax_t)afs.fs_size * (afs.fs_fsize / DEV_BSIZE));
	printf("%s ", name);
	printf("\n");

	return 0;
}

void
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

__dead void
usage(void)
{
	(void)fprintf(stderr, "usage: dumpfs [-m] filesys | device\n");
	exit(1);
}
