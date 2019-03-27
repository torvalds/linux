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
 * $TSHeader: src/sbin/ffsinfo/ffsinfo.c,v 1.4 2000/12/12 19:30:55 tomsoft Exp $
 *
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 2000 Christoph Herrmann, Thomas-Henning von Kamptz\n\
Copyright (c) 1980, 1989, 1993 The Regents of the University of California.\n\
All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

/* ********************************************************** INCLUDES ***** */
#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libufs.h>
#include <paths.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"

/* *********************************************************** GLOBALS ***** */
#ifdef FS_DEBUG
int	_dbg_lvl_ = (DL_INFO); /* DL_TRC */
#endif /* FS_DEBUG */

static struct uufsd disk;

#define sblock disk.d_fs
#define acg    disk.d_cg

static union {
	struct fs fs;
	char pad[SBLOCKSIZE];
} fsun;

#define osblock fsun.fs

static char	i1blk[MAXBSIZE];
static char	i2blk[MAXBSIZE];
static char	i3blk[MAXBSIZE];

static struct csum	*fscs;

/* ******************************************************** PROTOTYPES ***** */
static void	usage(void);
static void	dump_whole_ufs1_inode(ino_t, int);
static void	dump_whole_ufs2_inode(ino_t, int);

#define DUMP_WHOLE_INODE(A,B) \
	( disk.d_ufs == 1 \
		? dump_whole_ufs1_inode((A),(B)) : dump_whole_ufs2_inode((A),(B)) )

/* ************************************************************** main ***** */
/*
 * ffsinfo(8) is a tool to dump all metadata of a file system. It helps to find
 * errors is the file system much easier. You can run ffsinfo before and  after
 * an  fsck(8),  and compare the two ascii dumps easy with diff, and  you  see
 * directly where the problem is. You can control how much detail you want  to
 * see  with some command line arguments. You can also easy check  the  status
 * of  a file system, like is there is enough space for growing  a  file system,
 * or  how  many active snapshots do we have. It provides much  more  detailed
 * information  then dumpfs. Snapshots, as they are very new, are  not  really
 * supported.  They  are just mentioned currently, but it is  planned  to  run
 * also over active snapshots, to even get that output.
 */
int
main(int argc, char **argv)
{
	DBG_FUNC("main")
	char	*device, *special;
	int	ch;
	size_t	len;
	struct stat	st;
	struct csum	*dbg_csp;
	int	dbg_csc;
	char	dbg_line[80];
	int	cylno,i;
	int	cfg_cg, cfg_in, cfg_lv;
	int	cg_start, cg_stop;
	ino_t	in;
	char	*out_file;

	DBG_ENTER;

	cfg_lv = 0xff;
	cfg_in = -2;
	cfg_cg = -2;
	out_file = strdup("-");

	while ((ch = getopt(argc, argv, "g:i:l:o:")) != -1) {
		switch (ch) {
		case 'g':
			cfg_cg = strtol(optarg, NULL, 0);
			if (errno == EINVAL || errno == ERANGE)
				err(1, "%s", optarg);
			if (cfg_cg < -1)
				usage();
			break;
		case 'i':
			cfg_in = strtol(optarg, NULL, 0);
			if (errno == EINVAL || errno == ERANGE)
				err(1, "%s", optarg);
			if (cfg_in < 0)
				usage();
			break; 
		case 'l':
			cfg_lv = strtol(optarg, NULL, 0);
			if (errno == EINVAL||errno == ERANGE)
				err(1, "%s", optarg);
			if (cfg_lv < 0x1 || cfg_lv > 0x3ff)
				usage();
			break;
		case 'o':
			free(out_file);
			out_file = strdup(optarg);
			if (out_file == NULL)
				errx(1, "strdup failed");
			break;
		case '?':
			/* FALLTHROUGH */
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();
	device = *argv;

	/*
	 * Now we try to guess the (raw)device name.
	 */
	if (0 == strrchr(device, '/') && stat(device, &st) == -1) {
		/*-
		 * No path prefix was given, so try in this order:
		 *     /dev/r%s
		 *     /dev/%s
		 *     /dev/vinum/r%s
		 *     /dev/vinum/%s.
		 * 
		 * FreeBSD now doesn't distinguish between raw and  block
		 * devices any longer, but it should still work this way.
		 */
		len = strlen(device) + strlen(_PATH_DEV) + 2 + strlen("vinum/");
		special = (char *)malloc(len);
		if (special == NULL)
			errx(1, "malloc failed");
		snprintf(special, len, "%sr%s", _PATH_DEV, device);
		if (stat(special, &st) == -1) {
			snprintf(special, len, "%s%s", _PATH_DEV, device);
			if (stat(special, &st) == -1) {
				snprintf(special, len, "%svinum/r%s",
				    _PATH_DEV, device);
				if (stat(special, &st) == -1)
					/* For now this is the 'last resort' */
					snprintf(special, len, "%svinum/%s",
					    _PATH_DEV, device);
			}
		}
		device = special;
	}

	if (ufs_disk_fillout(&disk, device) == -1)
		err(1, "ufs_disk_fillout(%s) failed: %s", device, disk.d_error);

	DBG_OPEN(out_file);	/* already here we need a superblock */

	if (cfg_lv & 0x001)
		DBG_DUMP_FS(&sblock, "primary sblock");

	/* Determine here what cylinder groups to dump */
	if (cfg_cg==-2) {
		cg_start = 0;
		cg_stop = sblock.fs_ncg;
	} else if (cfg_cg == -1) {
		cg_start = sblock.fs_ncg - 1;
		cg_stop = sblock.fs_ncg;
	} else if (cfg_cg < sblock.fs_ncg) {
		cg_start = cfg_cg;
		cg_stop = cfg_cg + 1;
	} else {
		cg_start = sblock.fs_ncg;
		cg_stop = sblock.fs_ncg;
	}

	if (cfg_lv & 0x004) {
		fscs = (struct csum *)calloc((size_t)1,
		    (size_t)sblock.fs_cssize);
		if (fscs == NULL)
			errx(1, "calloc failed");

		/* get the cylinder summary into the memory ... */
		for (i = 0; i < sblock.fs_cssize; i += sblock.fs_bsize) {
			if (bread(&disk, fsbtodb(&sblock,
			    sblock.fs_csaddr + numfrags(&sblock, i)), 
			    (void *)(((char *)fscs)+i), 
			    (size_t)(sblock.fs_cssize-i < sblock.fs_bsize ?
			    sblock.fs_cssize - i : sblock.fs_bsize)) == -1)
				err(1, "bread: %s", disk.d_error);
		}

		dbg_csp = fscs;
		/* ... and dump it */
		for (dbg_csc = 0; dbg_csc < sblock.fs_ncg; dbg_csc++) {
			snprintf(dbg_line, sizeof(dbg_line),
			    "%d. csum in fscs", dbg_csc);
			DBG_DUMP_CSUM(&sblock,
			    dbg_line,
			    dbg_csp++);
		}
	}

	if (cfg_lv & 0xf8) {
		/* for each requested cylinder group ... */
		for (cylno = cg_start; cylno < cg_stop; cylno++) {
			snprintf(dbg_line, sizeof(dbg_line), "cgr %d", cylno);
			if (cfg_lv & 0x002) {
				/* dump the superblock copies */
				if (bread(&disk, fsbtodb(&sblock,
				    cgsblock(&sblock, cylno)), 
				    (void *)&osblock, SBLOCKSIZE) == -1)
					err(1, "bread: %s", disk.d_error);
				DBG_DUMP_FS(&osblock, dbg_line);
			}

			/*
			 * Read the cylinder group and dump whatever was
			 * requested.
			 */
			if (bread(&disk, fsbtodb(&sblock,
			    cgtod(&sblock, cylno)), (void *)&acg,
			    (size_t)sblock.fs_cgsize) == -1)
				err(1, "bread: %s", disk.d_error);

			if (cfg_lv & 0x008)
				DBG_DUMP_CG(&sblock, dbg_line, &acg);
			if (cfg_lv & 0x010)
				DBG_DUMP_INMAP(&sblock, dbg_line, &acg);
			if (cfg_lv & 0x020)
				DBG_DUMP_FRMAP(&sblock, dbg_line, &acg);
			if (cfg_lv & 0x040) {
				DBG_DUMP_CLMAP(&sblock, dbg_line, &acg);
				DBG_DUMP_CLSUM(&sblock, dbg_line, &acg);
			}
	#ifdef NOT_CURRENTLY
			/*
			 * See the comment in sbin/growfs/debug.c for why this
			 * is currently disabled, and what needs to be done to
			 * re-enable it.
			 */
			if (disk.d_ufs == 1 && cfg_lv & 0x080)
				DBG_DUMP_SPTBL(&sblock, dbg_line, &acg);
	#endif
		}
	}

	if (cfg_lv & 0x300) {
		/* Dump the requested inode(s) */
		if (cfg_in != -2)
			DUMP_WHOLE_INODE((ino_t)cfg_in, cfg_lv);
		else {
			for (in = cg_start * sblock.fs_ipg;
			    in < (ino_t)cg_stop * sblock.fs_ipg; 
			    in++)
				DUMP_WHOLE_INODE(in, cfg_lv);
		}
	}

	DBG_CLOSE;
	DBG_LEAVE;

	return 0;
}

/* ********************************************** dump_whole_ufs1_inode ***** */
/*
 * Here we dump a list of all blocks allocated by this inode. We follow
 * all indirect blocks.
 */
void
dump_whole_ufs1_inode(ino_t inode, int level)
{
	DBG_FUNC("dump_whole_ufs1_inode")
	union dinodep dp;
	int	rb;
	unsigned int	ind2ctr, ind3ctr;
	ufs1_daddr_t	*ind2ptr, *ind3ptr;
	char	comment[80];
	
	DBG_ENTER;

	/*
	 * Read the inode from disk/cache.
	 */
	if (getinode(&disk, &dp, inode) == -1)
		err(1, "getinode: %s", disk.d_error);

	if (dp.dp1->di_nlink == 0) {
		DBG_LEAVE;
		return;	/* inode not in use */
	}

	/*
	 * Dump the main inode structure.
	 */
	snprintf(comment, sizeof(comment), "Inode 0x%08jx", (uintmax_t)inode);
	if (level & 0x100) {
		DBG_DUMP_INO(&sblock,
		    comment,
		    dp.dp1);
	}

	if (!(level & 0x200)) {
		DBG_LEAVE;
		return;
	}

	/*
	 * Ok, now prepare for dumping all direct and indirect pointers.
	 */
	rb = howmany(dp.dp1->di_size, sblock.fs_bsize) - UFS_NDADDR;
	if (rb > 0) {
		/*
		 * Dump single indirect block.
		 */
		if (bread(&disk, fsbtodb(&sblock, dp.dp1->di_ib[0]),
		    (void *)&i1blk, (size_t)sblock.fs_bsize) == -1) {
			err(1, "bread: %s", disk.d_error);
		}
		snprintf(comment, sizeof(comment), "Inode 0x%08jx: indirect 0",
		    (uintmax_t)inode);
		DBG_DUMP_IBLK(&sblock,
		    comment,
		    i1blk,
		    (size_t)rb);
		rb -= howmany(sblock.fs_bsize, sizeof(ufs1_daddr_t));
	}
	if (rb > 0) {
		/*
		 * Dump double indirect blocks.
		 */
		if (bread(&disk, fsbtodb(&sblock, dp.dp1->di_ib[1]),
		    (void *)&i2blk, (size_t)sblock.fs_bsize) == -1) {
			err(1, "bread: %s", disk.d_error);
		}
		snprintf(comment, sizeof(comment), "Inode 0x%08jx: indirect 1",
		    (uintmax_t)inode);
		DBG_DUMP_IBLK(&sblock,
		    comment,
		    i2blk,
		    howmany(rb, howmany(sblock.fs_bsize, sizeof(ufs1_daddr_t))));
		for (ind2ctr = 0; ((ind2ctr < howmany(sblock.fs_bsize,
			sizeof(ufs1_daddr_t))) && (rb > 0)); ind2ctr++) {
			ind2ptr = &((ufs1_daddr_t *)(void *)&i2blk)[ind2ctr];

			if (bread(&disk, fsbtodb(&sblock, *ind2ptr),
			    (void *)&i1blk, (size_t)sblock.fs_bsize) == -1) {
				err(1, "bread: %s", disk.d_error);
			}
			snprintf(comment, sizeof(comment),
			    "Inode 0x%08jx: indirect 1->%d", (uintmax_t)inode,
			    ind2ctr);
			DBG_DUMP_IBLK(&sblock,
			    comment,
			    i1blk,
			    (size_t)rb);
			rb -= howmany(sblock.fs_bsize, sizeof(ufs1_daddr_t));
		}
	}
	if (rb > 0) {
		/*
		 * Dump triple indirect blocks.
		 */
		if (bread(&disk, fsbtodb(&sblock, dp.dp1->di_ib[2]),
		    (void *)&i3blk, (size_t)sblock.fs_bsize) == -1) {
			err(1, "bread: %s", disk.d_error);
		}
		snprintf(comment, sizeof(comment), "Inode 0x%08jx: indirect 2",
		    (uintmax_t)inode);
#define SQUARE(a) ((a)*(a))
		DBG_DUMP_IBLK(&sblock,
		    comment,
		    i3blk,
		    howmany(rb,
		      SQUARE(howmany(sblock.fs_bsize, sizeof(ufs1_daddr_t)))));
#undef SQUARE
		for (ind3ctr = 0; ((ind3ctr < howmany(sblock.fs_bsize,
			sizeof(ufs1_daddr_t))) && (rb > 0)); ind3ctr++) {
			ind3ptr = &((ufs1_daddr_t *)(void *)&i3blk)[ind3ctr];

			if (bread(&disk, fsbtodb(&sblock, *ind3ptr),
			    (void *)&i2blk, (size_t)sblock.fs_bsize) == -1) {
				err(1, "bread: %s", disk.d_error);
			}
			snprintf(comment, sizeof(comment),
			    "Inode 0x%08jx: indirect 2->%d", (uintmax_t)inode,
			    ind3ctr);
			DBG_DUMP_IBLK(&sblock,
			    comment,
			    i2blk,
			    howmany(rb,
			      howmany(sblock.fs_bsize, sizeof(ufs1_daddr_t))));
			for (ind2ctr = 0; ((ind2ctr < howmany(sblock.fs_bsize,
			     sizeof(ufs1_daddr_t))) && (rb > 0)); ind2ctr++) {
				ind2ptr=&((ufs1_daddr_t *)(void *)&i2blk)
				    [ind2ctr];
				if (bread(&disk, fsbtodb(&sblock, *ind2ptr),
				    (void *)&i1blk, (size_t)sblock.fs_bsize)
				    == -1) {
					err(1, "bread: %s", disk.d_error);
				}
				snprintf(comment, sizeof(comment),
				    "Inode 0x%08jx: indirect 2->%d->%d",
				    (uintmax_t)inode, ind3ctr, ind3ctr);
				DBG_DUMP_IBLK(&sblock,
				    comment,
				    i1blk,
				    (size_t)rb);
				rb -= howmany(sblock.fs_bsize,
				    sizeof(ufs1_daddr_t));
			}
		}
	}

	DBG_LEAVE;
	return;
}

/* ********************************************** dump_whole_ufs2_inode ***** */
/*
 * Here we dump a list of all blocks allocated by this inode. We follow
 * all indirect blocks.
 */
void
dump_whole_ufs2_inode(ino_t inode, int level)
{
	DBG_FUNC("dump_whole_ufs2_inode")
	union dinodep dp;
	int	rb;
	unsigned int	ind2ctr, ind3ctr;
	ufs2_daddr_t	*ind2ptr, *ind3ptr;
	char	comment[80];
	
	DBG_ENTER;

	/*
	 * Read the inode from disk/cache.
	 */
	if (getinode(&disk, &dp, inode) == -1)
		err(1, "getinode: %s", disk.d_error);

	if (dp.dp2->di_nlink == 0) {
		DBG_LEAVE;
		return;	/* inode not in use */
	}

	/*
	 * Dump the main inode structure.
	 */
	snprintf(comment, sizeof(comment), "Inode 0x%08jx", (uintmax_t)inode);
	if (level & 0x100) {
		DBG_DUMP_INO(&sblock, comment, dp.dp2);
	}

	if (!(level & 0x200)) {
		DBG_LEAVE;
		return;
	}

	/*
	 * Ok, now prepare for dumping all direct and indirect pointers.
	 */
	rb = howmany(dp.dp2->di_size, sblock.fs_bsize) - UFS_NDADDR;
	if (rb > 0) {
		/*
		 * Dump single indirect block.
		 */
		if (bread(&disk, fsbtodb(&sblock, dp.dp2->di_ib[0]),
		    (void *)&i1blk, (size_t)sblock.fs_bsize) == -1) {
			err(1, "bread: %s", disk.d_error);
		}
		snprintf(comment, sizeof(comment), "Inode 0x%08jx: indirect 0",
		    (uintmax_t)inode);
		DBG_DUMP_IBLK(&sblock, comment, i1blk, (size_t)rb);
		rb -= howmany(sblock.fs_bsize, sizeof(ufs2_daddr_t));
	}
	if (rb > 0) {
		/*
		 * Dump double indirect blocks.
		 */
		if (bread(&disk, fsbtodb(&sblock, dp.dp2->di_ib[1]),
		    (void *)&i2blk, (size_t)sblock.fs_bsize) == -1) {
			err(1, "bread: %s", disk.d_error);
		}
		snprintf(comment, sizeof(comment), "Inode 0x%08jx: indirect 1",
		    (uintmax_t)inode);
		DBG_DUMP_IBLK(&sblock,
			comment,
			i2blk,
			howmany(rb, howmany(sblock.fs_bsize, sizeof(ufs2_daddr_t))));
		for (ind2ctr = 0; ((ind2ctr < howmany(sblock.fs_bsize,
			sizeof(ufs2_daddr_t))) && (rb>0)); ind2ctr++) {
			ind2ptr = &((ufs2_daddr_t *)(void *)&i2blk)[ind2ctr];

			if (bread(&disk, fsbtodb(&sblock, *ind2ptr),
			    (void *)&i1blk, (size_t)sblock.fs_bsize) == -1) {
				err(1, "bread: %s", disk.d_error);
			}
			snprintf(comment, sizeof(comment),
				"Inode 0x%08jx: indirect 1->%d",
				(uintmax_t)inode, ind2ctr);
			DBG_DUMP_IBLK(&sblock, comment, i1blk, (size_t)rb);
			rb -= howmany(sblock.fs_bsize, sizeof(ufs2_daddr_t));
		}
	}
	if (rb > 0) {
		/*
		 * Dump triple indirect blocks.
		 */
		if (bread(&disk, fsbtodb(&sblock, dp.dp2->di_ib[2]),
		    (void *)&i3blk, (size_t)sblock.fs_bsize) == -1) {
			err(1, "bread: %s", disk.d_error);
		}
		snprintf(comment, sizeof(comment), "Inode 0x%08jx: indirect 2",
		    (uintmax_t)inode);
#define SQUARE(a) ((a)*(a))
		DBG_DUMP_IBLK(&sblock,
			comment,
			i3blk,
			howmany(rb,
				SQUARE(howmany(sblock.fs_bsize, sizeof(ufs2_daddr_t)))));
#undef SQUARE
		for (ind3ctr = 0; ((ind3ctr < howmany(sblock.fs_bsize,
			sizeof(ufs2_daddr_t))) && (rb > 0)); ind3ctr++) {
			ind3ptr = &((ufs2_daddr_t *)(void *)&i3blk)[ind3ctr];

			if (bread(&disk, fsbtodb(&sblock, *ind3ptr),
			    (void *)&i2blk, (size_t)sblock.fs_bsize) == -1) {
				err(1, "bread: %s", disk.d_error);
			}
			snprintf(comment, sizeof(comment),
				"Inode 0x%08jx: indirect 2->%d",
				(uintmax_t)inode, ind3ctr);
			DBG_DUMP_IBLK(&sblock,
				comment,
				i2blk,
				howmany(rb,
					howmany(sblock.fs_bsize, sizeof(ufs2_daddr_t))));
			for (ind2ctr = 0; ((ind2ctr < howmany(sblock.fs_bsize,
				sizeof(ufs2_daddr_t))) && (rb > 0)); ind2ctr++) {
				ind2ptr = &((ufs2_daddr_t *)(void *)&i2blk) [ind2ctr];
				if (bread(&disk, fsbtodb(&sblock, *ind2ptr),
				    (void *)&i1blk, (size_t)sblock.fs_bsize)
				    == -1) {
					err(1, "bread: %s", disk.d_error);
				}
				snprintf(comment, sizeof(comment),
					"Inode 0x%08jx: indirect 2->%d->%d",
					(uintmax_t)inode, ind3ctr, ind3ctr);
				DBG_DUMP_IBLK(&sblock, comment, i1blk, (size_t)rb);
				rb -= howmany(sblock.fs_bsize, sizeof(ufs2_daddr_t));
			}
		}
	}

	DBG_LEAVE;
	return;
}

/* ************************************************************* usage ***** */
/*
 * Dump a line of usage.
 */
void
usage(void)
{
	DBG_FUNC("usage")	

	DBG_ENTER;

	fprintf(stderr,
	    "usage: ffsinfo [-g cylinder_group] [-i inode] [-l level] "
	    "[-o outfile]\n"
	    "               special | file\n");

	DBG_LEAVE;
	exit(1);
}
