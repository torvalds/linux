/*	$NetBSD: fsdb.c,v 1.2 1995/10/08 23:18:10 thorpej Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 1995 John T. Kohl
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <ctype.h>
#include <err.h>
#include <grp.h>
#include <histedit.h>
#include <pwd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <timeconv.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>

#include "fsdb.h"
#include "fsck.h"

static void usage(void) __dead2;
int cmdloop(void);
static int compare_blk32(uint32_t *wantedblk, uint32_t curblk);
static int compare_blk64(uint64_t *wantedblk, uint64_t curblk);
static int founddatablk(uint64_t blk);
static int find_blks32(uint32_t *buf, int size, uint32_t *blknum);
static int find_blks64(uint64_t *buf, int size, uint64_t *blknum);
static int find_indirblks32(uint32_t blk, int ind_level, uint32_t *blknum);
static int find_indirblks64(uint64_t blk, int ind_level, uint64_t *blknum);

static void 
usage(void)
{
	fprintf(stderr, "usage: fsdb [-d] [-f] [-r] fsname\n");
	exit(1);
}

int returntosingle;
char nflag;

/*
 * We suck in lots of fsck code, and just pick & choose the stuff we want.
 *
 * fsreadfd is set up to read from the file system, fswritefd to write to
 * the file system.
 */
int
main(int argc, char *argv[])
{
	int ch, rval;
	char *fsys = NULL;

	while (-1 != (ch = getopt(argc, argv, "fdr"))) {
		switch (ch) {
		case 'f':
			/* The -f option is left for historical
			 * reasons and has no meaning.
			 */
			break;
		case 'd':
			debug++;
			break;
		case 'r':
			nflag++; /* "no" in fsck, readonly for us */
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();
	else
		fsys = argv[0];

	sblock_init();
	if (!setup(fsys))
		errx(1, "cannot set up file system `%s'", fsys);
	printf("%s file system `%s'\nLast Mounted on %s\n",
	       nflag? "Examining": "Editing", fsys, sblock.fs_fsmnt);
	rval = cmdloop();
	if (!nflag) {
		sblock.fs_clean = 0;	/* mark it dirty */
		sbdirty();
		ckfini(0);
		printf("*** FILE SYSTEM MARKED DIRTY\n");
		printf("*** BE SURE TO RUN FSCK TO CLEAN UP ANY DAMAGE\n");
		printf("*** IF IT WAS MOUNTED, RE-MOUNT WITH -u -o reload\n");
	}
	exit(rval);
}

#define CMDFUNC(func) int func(int argc, char *argv[])
#define CMDFUNCSTART(func) int func(int argc, char *argv[])

CMDFUNC(helpfn);
CMDFUNC(focus);				/* focus on inode */
CMDFUNC(active);			/* print active inode */
CMDFUNC(blocks);			/* print blocks for active inode */
CMDFUNC(focusname);			/* focus by name */
CMDFUNC(zapi);				/* clear inode */
CMDFUNC(uplink);			/* incr link */
CMDFUNC(downlink);			/* decr link */
CMDFUNC(linkcount);			/* set link count */
CMDFUNC(quit);				/* quit */
CMDFUNC(findblk);			/* find block */
CMDFUNC(ls);				/* list directory */
CMDFUNC(rm);				/* remove name */
CMDFUNC(ln);				/* add name */
CMDFUNC(newtype);			/* change type */
CMDFUNC(chmode);			/* change mode */
CMDFUNC(chlen);				/* change length */
CMDFUNC(chaflags);			/* change flags */
CMDFUNC(chgen);				/* change generation */
CMDFUNC(chowner);			/* change owner */
CMDFUNC(chgroup);			/* Change group */
CMDFUNC(back);				/* pop back to last ino */
CMDFUNC(chbtime);			/* Change btime */
CMDFUNC(chmtime);			/* Change mtime */
CMDFUNC(chctime);			/* Change ctime */
CMDFUNC(chatime);			/* Change atime */
CMDFUNC(chinum);			/* Change inode # of dirent */
CMDFUNC(chname);			/* Change dirname of dirent */
CMDFUNC(chsize);			/* Change size */

struct cmdtable cmds[] = {
	{ "help", "Print out help", 1, 1, FL_RO, helpfn },
	{ "?", "Print out help", 1, 1, FL_RO, helpfn },
	{ "inode", "Set active inode to INUM", 2, 2, FL_RO, focus },
	{ "clri", "Clear inode INUM", 2, 2, FL_WR, zapi },
	{ "lookup", "Set active inode by looking up NAME", 2, 2, FL_RO | FL_ST, focusname },
	{ "cd", "Set active inode by looking up NAME", 2, 2, FL_RO | FL_ST, focusname },
	{ "back", "Go to previous active inode", 1, 1, FL_RO, back },
	{ "active", "Print active inode", 1, 1, FL_RO, active },
	{ "print", "Print active inode", 1, 1, FL_RO, active },
	{ "blocks", "Print block numbers of active inode", 1, 1, FL_RO, blocks },
	{ "uplink", "Increment link count", 1, 1, FL_WR, uplink },
	{ "downlink", "Decrement link count", 1, 1, FL_WR, downlink },
	{ "linkcount", "Set link count to COUNT", 2, 2, FL_WR, linkcount },
	{ "findblk", "Find inode owning disk block(s)", 2, 33, FL_RO, findblk},
	{ "ls", "List current inode as directory", 1, 1, FL_RO, ls },
	{ "rm", "Remove NAME from current inode directory", 2, 2, FL_WR | FL_ST, rm },
	{ "del", "Remove NAME from current inode directory", 2, 2, FL_WR | FL_ST, rm },
	{ "ln", "Hardlink INO into current inode directory as NAME", 3, 3, FL_WR | FL_ST, ln },
	{ "chinum", "Change dir entry number INDEX to INUM", 3, 3, FL_WR, chinum },
	{ "chname", "Change dir entry number INDEX to NAME", 3, 3, FL_WR | FL_ST, chname },
	{ "chtype", "Change type of current inode to TYPE", 2, 2, FL_WR, newtype },
	{ "chmod", "Change mode of current inode to MODE", 2, 2, FL_WR, chmode },
	{ "chlen", "Change length of current inode to LENGTH", 2, 2, FL_WR, chlen },
	{ "chown", "Change owner of current inode to OWNER", 2, 2, FL_WR, chowner },
	{ "chgrp", "Change group of current inode to GROUP", 2, 2, FL_WR, chgroup },
	{ "chflags", "Change flags of current inode to FLAGS", 2, 2, FL_WR, chaflags },
	{ "chgen", "Change generation number of current inode to GEN", 2, 2, FL_WR, chgen },
	{ "chsize", "Change size of current inode to SIZE", 2, 2, FL_WR, chsize },
	{ "btime", "Change btime of current inode to BTIME", 2, 2, FL_WR, chbtime },
	{ "mtime", "Change mtime of current inode to MTIME", 2, 2, FL_WR, chmtime },
	{ "ctime", "Change ctime of current inode to CTIME", 2, 2, FL_WR, chctime },
	{ "atime", "Change atime of current inode to ATIME", 2, 2, FL_WR, chatime },
	{ "quit", "Exit", 1, 1, FL_RO, quit },
	{ "q", "Exit", 1, 1, FL_RO, quit },
	{ "exit", "Exit", 1, 1, FL_RO, quit },
	{ NULL, 0, 0, 0, 0, NULL },
};

int
helpfn(int argc, char *argv[])
{
    struct cmdtable *cmdtp;

    printf("Commands are:\n%-10s %5s %5s   %s\n",
	   "command", "min args", "max args", "what");
    
    for (cmdtp = cmds; cmdtp->cmd; cmdtp++)
	printf("%-10s %5u %5u   %s\n",
		cmdtp->cmd, cmdtp->minargc-1, cmdtp->maxargc-1, cmdtp->helptxt);
    return 0;
}

char *
prompt(EditLine *el)
{
    static char pstring[64];
    snprintf(pstring, sizeof(pstring), "fsdb (inum: %ju)> ",
	(uintmax_t)curinum);
    return pstring;
}


int
cmdloop(void)
{
    char *line;
    const char *elline;
    int cmd_argc, rval = 0, known;
#define scratch known
    char **cmd_argv;
    struct cmdtable *cmdp;
    History *hist;
    EditLine *elptr;
    HistEvent he;

    curinode = ginode(UFS_ROOTINO);
    curinum = UFS_ROOTINO;
    printactive(0);

    hist = history_init();
    history(hist, &he, H_SETSIZE, 100);	/* 100 elt history buffer */

    elptr = el_init("fsdb", stdin, stdout, stderr);
    el_set(elptr, EL_EDITOR, "emacs");
    el_set(elptr, EL_PROMPT, prompt);
    el_set(elptr, EL_HIST, history, hist);
    el_source(elptr, NULL);

    while ((elline = el_gets(elptr, &scratch)) != NULL && scratch != 0) {
	if (debug)
	    printf("command `%s'\n", elline);

	history(hist, &he, H_ENTER, elline);

	line = strdup(elline);
	cmd_argv = crack(line, &cmd_argc);
	/*
	 * el_parse returns -1 to signal that it's not been handled
	 * internally.
	 */
	if (el_parse(elptr, cmd_argc, (const char **)cmd_argv) != -1)
	    continue;
	if (cmd_argc) {
	    known = 0;
	    for (cmdp = cmds; cmdp->cmd; cmdp++) {
		if (!strcmp(cmdp->cmd, cmd_argv[0])) {
		    if ((cmdp->flags & FL_WR) == FL_WR && nflag)
			warnx("`%s' requires write access", cmd_argv[0]),
			    rval = 1;
		    else if (cmd_argc >= cmdp->minargc &&
			cmd_argc <= cmdp->maxargc)
			rval = (*cmdp->handler)(cmd_argc, cmd_argv);
		    else if (cmd_argc >= cmdp->minargc &&
			(cmdp->flags & FL_ST) == FL_ST) {
			strcpy(line, elline);
			cmd_argv = recrack(line, &cmd_argc, cmdp->maxargc);
			rval = (*cmdp->handler)(cmd_argc, cmd_argv);
		    } else
			rval = argcount(cmdp, cmd_argc, cmd_argv);
		    known = 1;
		    break;
		}
	    }
	    if (!known)
		warnx("unknown command `%s'", cmd_argv[0]), rval = 1;
	} else
	    rval = 0;
	free(line);
	if (rval < 0)
	    /* user typed "quit" */
	    return 0;
	if (rval)
	    warnx("rval was %d", rval);
    }
    el_end(elptr);
    history_end(hist);
    return rval;
}

union dinode *curinode;
ino_t curinum, ocurrent;

#define GETINUM(ac,inum)    inum = strtoul(argv[ac], &cp, 0); \
if (inum < UFS_ROOTINO || inum > maxino || cp == argv[ac] || *cp != '\0' ) { \
	printf("inode %ju out of range; range is [%ju,%ju]\n",		\
	    (uintmax_t)inum, (uintmax_t)UFS_ROOTINO, (uintmax_t)maxino);\
	return 1; \
}

/*
 * Focus on given inode number
 */
CMDFUNCSTART(focus)
{
    ino_t inum;
    char *cp;

    GETINUM(1,inum);
    curinode = ginode(inum);
    ocurrent = curinum;
    curinum = inum;
    printactive(0);
    return 0;
}

CMDFUNCSTART(back)
{
    curinum = ocurrent;
    curinode = ginode(curinum);
    printactive(0);
    return 0;
}

CMDFUNCSTART(zapi)
{
    ino_t inum;
    union dinode *dp;
    char *cp;

    GETINUM(1,inum);
    dp = ginode(inum);
    clearinode(dp);
    inodirty(dp);
    if (curinode)			/* re-set after potential change */
	curinode = ginode(curinum);
    return 0;
}

CMDFUNCSTART(active)
{
    printactive(0);
    return 0;
}

CMDFUNCSTART(blocks)
{
    printactive(1);
    return 0;
}

CMDFUNCSTART(quit)
{
    return -1;
}

CMDFUNCSTART(uplink)
{
    if (!checkactive())
	return 1;
    DIP_SET(curinode, di_nlink, DIP(curinode, di_nlink) + 1);
    printf("inode %ju link count now %d\n",
	(uintmax_t)curinum, DIP(curinode, di_nlink));
    inodirty(curinode);
    return 0;
}

CMDFUNCSTART(downlink)
{
    if (!checkactive())
	return 1;
    DIP_SET(curinode, di_nlink, DIP(curinode, di_nlink) - 1);
    printf("inode %ju link count now %d\n",
	(uintmax_t)curinum, DIP(curinode, di_nlink));
    inodirty(curinode);
    return 0;
}

const char *typename[] = {
    "unknown",
    "fifo",
    "char special",
    "unregistered #3",
    "directory",
    "unregistered #5",
    "blk special",
    "unregistered #7",
    "regular",
    "unregistered #9",
    "symlink",
    "unregistered #11",
    "socket",
    "unregistered #13",
    "whiteout",
};

int diroff; 
int slot;

int
scannames(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;

	printf("slot %d off %d ino %d reclen %d: %s, `%.*s'\n",
	       slot++, diroff, dirp->d_ino, dirp->d_reclen,
	       typename[dirp->d_type], dirp->d_namlen, dirp->d_name);
	diroff += dirp->d_reclen;
	return (KEEPON);
}

CMDFUNCSTART(ls)
{
    struct inodesc idesc;
    checkactivedir();			/* let it go on anyway */

    slot = 0;
    diroff = 0;
    idesc.id_number = curinum;
    idesc.id_func = scannames;
    idesc.id_type = DATA;
    idesc.id_fix = IGNORE;
    ckinode(curinode, &idesc);
    curinode = ginode(curinum);

    return 0;
}

static int findblk_numtofind;
static int wantedblksize;

CMDFUNCSTART(findblk)
{
    ino_t inum, inosused;
    uint32_t *wantedblk32;
    uint64_t *wantedblk64;
    struct bufarea *cgbp;
    struct cg *cgp;
    int c, i, is_ufs2;

    wantedblksize = (argc - 1);
    is_ufs2 = sblock.fs_magic == FS_UFS2_MAGIC;
    ocurrent = curinum;

    if (is_ufs2) {
	wantedblk64 = calloc(wantedblksize, sizeof(uint64_t));
	if (wantedblk64 == NULL)
	    err(1, "malloc");
	for (i = 1; i < argc; i++)
	    wantedblk64[i - 1] = dbtofsb(&sblock, strtoull(argv[i], NULL, 0));
    } else {
	wantedblk32 = calloc(wantedblksize, sizeof(uint32_t));
	if (wantedblk32 == NULL)
	    err(1, "malloc");
	for (i = 1; i < argc; i++)
	    wantedblk32[i - 1] = dbtofsb(&sblock, strtoull(argv[i], NULL, 0));
    }
    findblk_numtofind = wantedblksize;
    /*
     * sblock.fs_ncg holds a number of cylinder groups.
     * Iterate over all cylinder groups.
     */
    for (c = 0; c < sblock.fs_ncg; c++) {
	/*
	 * sblock.fs_ipg holds a number of inodes per cylinder group.
	 * Calculate a highest inode number for a given cylinder group.
	 */
	inum = c * sblock.fs_ipg;
	/* Read cylinder group. */
	cgbp = cglookup(c);
	cgp = cgbp->b_un.b_cg;
	/*
	 * Get a highest used inode number for a given cylinder group.
	 * For UFS1 all inodes initialized at the newfs stage.
	 */
	if (is_ufs2)
	    inosused = cgp->cg_initediblk;
	else
	    inosused = sblock.fs_ipg;

	for (; inosused > 0; inum++, inosused--) {
	    /* Skip magic inodes: 0, UFS_WINO, UFS_ROOTINO. */
	    if (inum < UFS_ROOTINO)
		continue;
	    /*
	     * Check if the block we are looking for is just an inode block.
	     *
	     * ino_to_fsba() - get block containing inode from its number.
	     * INOPB() - get a number of inodes in one disk block.
	     */
	    if (is_ufs2 ?
		compare_blk64(wantedblk64, ino_to_fsba(&sblock, inum)) :
		compare_blk32(wantedblk32, ino_to_fsba(&sblock, inum))) {
		printf("block %llu: inode block (%ju-%ju)\n",
		    (unsigned long long)fsbtodb(&sblock,
			ino_to_fsba(&sblock, inum)),
		    (uintmax_t)(inum / INOPB(&sblock)) * INOPB(&sblock),
		    (uintmax_t)(inum / INOPB(&sblock) + 1) * INOPB(&sblock));
		findblk_numtofind--;
		if (findblk_numtofind == 0)
		    goto end;
	    }
	    /* Get on-disk inode aka dinode. */
	    curinum = inum;
	    curinode = ginode(inum);
	    /* Find IFLNK dinode with allocated data blocks. */
	    switch (DIP(curinode, di_mode) & IFMT) {
	    case IFDIR:
	    case IFREG:
		if (DIP(curinode, di_blocks) == 0)
		    continue;
		break;
	    case IFLNK:
		{
		    uint64_t size = DIP(curinode, di_size);
		    if (size > 0 && size < sblock.fs_maxsymlinklen &&
			DIP(curinode, di_blocks) == 0)
			continue;
		    else
			break;
		}
	    default:
		continue;
	    }
	    /* Look through direct data blocks. */
	    if (is_ufs2 ?
		find_blks64(curinode->dp2.di_db, UFS_NDADDR, wantedblk64) :
		find_blks32(curinode->dp1.di_db, UFS_NDADDR, wantedblk32))
		goto end;
	    for (i = 0; i < UFS_NIADDR; i++) {
		/*
		 * Does the block we are looking for belongs to the
		 * indirect blocks?
		 */
		if (is_ufs2 ?
		    compare_blk64(wantedblk64, curinode->dp2.di_ib[i]) :
		    compare_blk32(wantedblk32, curinode->dp1.di_ib[i]))
		    if (founddatablk(is_ufs2 ? curinode->dp2.di_ib[i] :
			curinode->dp1.di_ib[i]))
			goto end;
		/*
		 * Search through indirect, double and triple indirect
		 * data blocks.
		 */
		if (is_ufs2 ? (curinode->dp2.di_ib[i] != 0) :
		    (curinode->dp1.di_ib[i] != 0))
		    if (is_ufs2 ?
			find_indirblks64(curinode->dp2.di_ib[i], i,
			    wantedblk64) :
			find_indirblks32(curinode->dp1.di_ib[i], i,
			    wantedblk32))
			goto end;
	    }
	}
    }
end:
    curinum = ocurrent;
    curinode = ginode(curinum);
    if (is_ufs2)
	free(wantedblk64);
    else
	free(wantedblk32);
    return 0;
}

static int
compare_blk32(uint32_t *wantedblk, uint32_t curblk)
{
    int i;

    for (i = 0; i < wantedblksize; i++) {
	if (wantedblk[i] != 0 && wantedblk[i] == curblk) {
	    wantedblk[i] = 0;
	    return 1;
	}
    }
    return 0;
}

static int
compare_blk64(uint64_t *wantedblk, uint64_t curblk)
{
    int i;

    for (i = 0; i < wantedblksize; i++) {
	if (wantedblk[i] != 0 && wantedblk[i] == curblk) {
	    wantedblk[i] = 0;
	    return 1;
	}
    }
    return 0;
}

static int
founddatablk(uint64_t blk)
{

    printf("%llu: data block of inode %ju\n",
	(unsigned long long)fsbtodb(&sblock, blk), (uintmax_t)curinum);
    findblk_numtofind--;
    if (findblk_numtofind == 0)
	return 1;
    return 0;
}

static int
find_blks32(uint32_t *buf, int size, uint32_t *wantedblk)
{
    int blk;
    for (blk = 0; blk < size; blk++) {
	if (buf[blk] == 0)
	    continue;
	if (compare_blk32(wantedblk, buf[blk])) {
	    if (founddatablk(buf[blk]))
		return 1;
	}
    }
    return 0;
}

static int
find_indirblks32(uint32_t blk, int ind_level, uint32_t *wantedblk)
{
#define MAXNINDIR      (MAXBSIZE / sizeof(uint32_t))
    uint32_t idblk[MAXNINDIR];
    int i;

    blread(fsreadfd, (char *)idblk, fsbtodb(&sblock, blk), (int)sblock.fs_bsize);
    if (ind_level <= 0) {
	if (find_blks32(idblk, sblock.fs_bsize / sizeof(uint32_t), wantedblk))
	    return 1;
    } else {
	ind_level--;
	for (i = 0; i < sblock.fs_bsize / sizeof(uint32_t); i++) {
	    if (compare_blk32(wantedblk, idblk[i])) {
		if (founddatablk(idblk[i]))
		    return 1;
	    }
	    if (idblk[i] != 0)
		if (find_indirblks32(idblk[i], ind_level, wantedblk))
		    return 1;
	}
    }
#undef MAXNINDIR
    return 0;
}

static int
find_blks64(uint64_t *buf, int size, uint64_t *wantedblk)
{
    int blk;
    for (blk = 0; blk < size; blk++) {
	if (buf[blk] == 0)
	    continue;
	if (compare_blk64(wantedblk, buf[blk])) {
	    if (founddatablk(buf[blk]))
		return 1;
	}
    }
    return 0;
}

static int
find_indirblks64(uint64_t blk, int ind_level, uint64_t *wantedblk)
{
#define MAXNINDIR      (MAXBSIZE / sizeof(uint64_t))
    uint64_t idblk[MAXNINDIR];
    int i;

    blread(fsreadfd, (char *)idblk, fsbtodb(&sblock, blk), (int)sblock.fs_bsize);
    if (ind_level <= 0) {
	if (find_blks64(idblk, sblock.fs_bsize / sizeof(uint64_t), wantedblk))
	    return 1;
    } else {
	ind_level--;
	for (i = 0; i < sblock.fs_bsize / sizeof(uint64_t); i++) {
	    if (compare_blk64(wantedblk, idblk[i])) {
		if (founddatablk(idblk[i]))
		    return 1;
	    }
	    if (idblk[i] != 0)
		if (find_indirblks64(idblk[i], ind_level, wantedblk))
		    return 1;
	}
    }
#undef MAXNINDIR
    return 0;
}

int findino(struct inodesc *idesc); /* from fsck */
static int dolookup(char *name);

static int
dolookup(char *name)
{
    struct inodesc idesc;

    if (!checkactivedir())
	    return 0;
    idesc.id_number = curinum;
    idesc.id_func = findino;
    idesc.id_name = name;
    idesc.id_type = DATA;
    idesc.id_fix = IGNORE;
    if (ckinode(curinode, &idesc) & FOUND) {
	curinum = idesc.id_parent;
	curinode = ginode(curinum);
	printactive(0);
	return 1;
    } else {
	warnx("name `%s' not found in current inode directory", name);
	return 0;
    }
}

CMDFUNCSTART(focusname)
{
    char *p, *val;

    if (!checkactive())
	return 1;

    ocurrent = curinum;
    
    if (argv[1][0] == '/') {
	curinum = UFS_ROOTINO;
	curinode = ginode(UFS_ROOTINO);
    } else {
	if (!checkactivedir())
	    return 1;
    }
    for (p = argv[1]; p != NULL;) {
	while ((val = strsep(&p, "/")) != NULL && *val == '\0');
	if (val) {
	    printf("component `%s': ", val);
	    fflush(stdout);
	    if (!dolookup(val)) {
		curinode = ginode(curinum);
		return(1);
	    }
	}
    }
    return 0;
}

CMDFUNCSTART(ln)
{
    ino_t inum;
    int rval;
    char *cp;

    GETINUM(1,inum);

    if (!checkactivedir())
	return 1;
    rval = makeentry(curinum, inum, argv[2]);
    if (rval)
	    printf("Ino %ju entered as `%s'\n", (uintmax_t)inum, argv[2]);
    else
	printf("could not enter name? weird.\n");
    curinode = ginode(curinum);
    return rval;
}

CMDFUNCSTART(rm)
{
    int rval;

    if (!checkactivedir())
	return 1;
    rval = changeino(curinum, argv[1], 0);
    if (rval & ALTERED) {
	printf("Name `%s' removed\n", argv[1]);
	return 0;
    } else {
	printf("could not remove name ('%s')? weird.\n", argv[1]);
	return 1;
    }
}

long slotcount, desired;

int
chinumfunc(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;

	if (slotcount++ == desired) {
	    dirp->d_ino = idesc->id_parent;
	    return STOP|ALTERED|FOUND;
	}
	return KEEPON;
}

CMDFUNCSTART(chinum)
{
    char *cp;
    ino_t inum;
    struct inodesc idesc;
    
    slotcount = 0;
    if (!checkactivedir())
	return 1;
    GETINUM(2,inum);

    desired = strtol(argv[1], &cp, 0);
    if (cp == argv[1] || *cp != '\0' || desired < 0) {
	printf("invalid slot number `%s'\n", argv[1]);
	return 1;
    }

    idesc.id_number = curinum;
    idesc.id_func = chinumfunc;
    idesc.id_fix = IGNORE;
    idesc.id_type = DATA;
    idesc.id_parent = inum;		/* XXX convenient hiding place */

    if (ckinode(curinode, &idesc) & FOUND)
	return 0;
    else {
	warnx("no %sth slot in current directory", argv[1]);
	return 1;
    }
}

int
chnamefunc(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;
	struct direct testdir;

	if (slotcount++ == desired) {
	    /* will name fit? */
	    testdir.d_namlen = strlen(idesc->id_name);
	    if (DIRSIZ(NEWDIRFMT, &testdir) <= dirp->d_reclen) {
		dirp->d_namlen = testdir.d_namlen;
		strcpy(dirp->d_name, idesc->id_name);
		return STOP|ALTERED|FOUND;
	    } else
		return STOP|FOUND;	/* won't fit, so give up */
	}
	return KEEPON;
}

CMDFUNCSTART(chname)
{
    int rval;
    char *cp;
    struct inodesc idesc;
    
    slotcount = 0;
    if (!checkactivedir())
	return 1;

    desired = strtoul(argv[1], &cp, 0);
    if (cp == argv[1] || *cp != '\0') {
	printf("invalid slot number `%s'\n", argv[1]);
	return 1;
    }

    idesc.id_number = curinum;
    idesc.id_func = chnamefunc;
    idesc.id_fix = IGNORE;
    idesc.id_type = DATA;
    idesc.id_name = argv[2];

    rval = ckinode(curinode, &idesc);
    if ((rval & (FOUND|ALTERED)) == (FOUND|ALTERED))
	return 0;
    else if (rval & FOUND) {
	warnx("new name `%s' does not fit in slot %s\n", argv[2], argv[1]);
	return 1;
    } else {
	warnx("no %sth slot in current directory", argv[1]);
	return 1;
    }
}

struct typemap {
    const char *typename;
    int typebits;
} typenamemap[]  = {
    {"file", IFREG},
    {"dir", IFDIR},
    {"socket", IFSOCK},
    {"fifo", IFIFO},
};

CMDFUNCSTART(newtype)
{
    int type;
    struct typemap *tp;

    if (!checkactive())
	return 1;
    type = DIP(curinode, di_mode) & IFMT;
    for (tp = typenamemap;
	 tp < &typenamemap[nitems(typenamemap)];
	 tp++) {
	if (!strcmp(argv[1], tp->typename)) {
	    printf("setting type to %s\n", tp->typename);
	    type = tp->typebits;
	    break;
	}
    }
    if (tp == &typenamemap[nitems(typenamemap)]) {
	warnx("type `%s' not known", argv[1]);
	warnx("try one of `file', `dir', `socket', `fifo'");
	return 1;
    }
    DIP_SET(curinode, di_mode, DIP(curinode, di_mode) & ~IFMT);
    DIP_SET(curinode, di_mode, DIP(curinode, di_mode) | type);
    inodirty(curinode);
    printactive(0);
    return 0;
}

CMDFUNCSTART(chlen)
{
    int rval = 1;
    long len;
    char *cp;

    if (!checkactive())
	return 1;

    len = strtol(argv[1], &cp, 0);
    if (cp == argv[1] || *cp != '\0' || len < 0) { 
	warnx("bad length `%s'", argv[1]);
	return 1;
    }
    
    DIP_SET(curinode, di_size, len);
    inodirty(curinode);
    printactive(0);
    return rval;
}

CMDFUNCSTART(chmode)
{
    int rval = 1;
    long modebits;
    char *cp;

    if (!checkactive())
	return 1;

    modebits = strtol(argv[1], &cp, 8);
    if (cp == argv[1] || *cp != '\0' || (modebits & ~07777)) { 
	warnx("bad modebits `%s'", argv[1]);
	return 1;
    }
    
    DIP_SET(curinode, di_mode, DIP(curinode, di_mode) & ~07777);
    DIP_SET(curinode, di_mode, DIP(curinode, di_mode) | modebits);
    inodirty(curinode);
    printactive(0);
    return rval;
}

CMDFUNCSTART(chaflags)
{
    int rval = 1;
    u_long flags;
    char *cp;

    if (!checkactive())
	return 1;

    flags = strtoul(argv[1], &cp, 0);
    if (cp == argv[1] || *cp != '\0' ) { 
	warnx("bad flags `%s'", argv[1]);
	return 1;
    }
    
    if (flags > UINT_MAX) {
	warnx("flags set beyond 32-bit range of field (%lx)\n", flags);
	return(1);
    }
    DIP_SET(curinode, di_flags, flags);
    inodirty(curinode);
    printactive(0);
    return rval;
}

CMDFUNCSTART(chgen)
{
    int rval = 1;
    long gen;
    char *cp;

    if (!checkactive())
	return 1;

    gen = strtol(argv[1], &cp, 0);
    if (cp == argv[1] || *cp != '\0' ) { 
	warnx("bad gen `%s'", argv[1]);
	return 1;
    }
    
    if (gen > INT_MAX || gen < INT_MIN) {
	warnx("gen set beyond 32-bit range of field (%lx)\n", gen);
	return(1);
    }
    DIP_SET(curinode, di_gen, gen);
    inodirty(curinode);
    printactive(0);
    return rval;
}

CMDFUNCSTART(chsize)
{
    int rval = 1;
    off_t size;
    char *cp;

    if (!checkactive())
	return 1;

    size = strtoll(argv[1], &cp, 0);
    if (cp == argv[1] || *cp != '\0') {
	warnx("bad size `%s'", argv[1]);
	return 1;
    }

    if (size < 0) {
	warnx("size set to negative (%jd)\n", (intmax_t)size);
	return(1);
    }
    DIP_SET(curinode, di_size, size);
    inodirty(curinode);
    printactive(0);
    return rval;
}

CMDFUNCSTART(linkcount)
{
    int rval = 1;
    int lcnt;
    char *cp;

    if (!checkactive())
	return 1;

    lcnt = strtol(argv[1], &cp, 0);
    if (cp == argv[1] || *cp != '\0' ) { 
	warnx("bad link count `%s'", argv[1]);
	return 1;
    }
    if (lcnt > USHRT_MAX || lcnt < 0) {
	warnx("max link count is %d\n", USHRT_MAX);
	return 1;
    }
    
    DIP_SET(curinode, di_nlink, lcnt);
    inodirty(curinode);
    printactive(0);
    return rval;
}

CMDFUNCSTART(chowner)
{
    int rval = 1;
    unsigned long uid;
    char *cp;
    struct passwd *pwd;

    if (!checkactive())
	return 1;

    uid = strtoul(argv[1], &cp, 0);
    if (cp == argv[1] || *cp != '\0' ) { 
	/* try looking up name */
	if ((pwd = getpwnam(argv[1]))) {
	    uid = pwd->pw_uid;
	} else {
	    warnx("bad uid `%s'", argv[1]);
	    return 1;
	}
    }
    
    DIP_SET(curinode, di_uid, uid);
    inodirty(curinode);
    printactive(0);
    return rval;
}

CMDFUNCSTART(chgroup)
{
    int rval = 1;
    unsigned long gid;
    char *cp;
    struct group *grp;

    if (!checkactive())
	return 1;

    gid = strtoul(argv[1], &cp, 0);
    if (cp == argv[1] || *cp != '\0' ) { 
	if ((grp = getgrnam(argv[1]))) {
	    gid = grp->gr_gid;
	} else {
	    warnx("bad gid `%s'", argv[1]);
	    return 1;
	}
    }
    
    DIP_SET(curinode, di_gid, gid);
    inodirty(curinode);
    printactive(0);
    return rval;
}

int
dotime(char *name, time_t *secp, int32_t *nsecp)
{
    char *p, *val;
    struct tm t;
    int32_t nsec;
    p = strchr(name, '.');
    if (p) {
	*p = '\0';
	nsec = strtoul(++p, &val, 0);
	if (val == p || *val != '\0' || nsec >= 1000000000 || nsec < 0) {
		warnx("invalid nanoseconds");
		goto badformat;
	}
    } else
	nsec = 0;
    if (strlen(name) != 14) {
badformat:
	warnx("date format: YYYYMMDDHHMMSS[.nsec]");
	return 1;
    }
    *nsecp = nsec;

    for (p = name; *p; p++)
	if (*p < '0' || *p > '9')
	    goto badformat;
    
    p = name;
#define VAL() ((*p++) - '0')
    t.tm_year = VAL();
    t.tm_year = VAL() + t.tm_year * 10;
    t.tm_year = VAL() + t.tm_year * 10;
    t.tm_year = VAL() + t.tm_year * 10 - 1900;
    t.tm_mon = VAL();
    t.tm_mon = VAL() + t.tm_mon * 10 - 1;
    t.tm_mday = VAL();
    t.tm_mday = VAL() + t.tm_mday * 10;
    t.tm_hour = VAL();
    t.tm_hour = VAL() + t.tm_hour * 10;
    t.tm_min = VAL();
    t.tm_min = VAL() + t.tm_min * 10;
    t.tm_sec = VAL();
    t.tm_sec = VAL() + t.tm_sec * 10;
    t.tm_isdst = -1;

    *secp = mktime(&t);
    if (*secp == -1) {
	warnx("date/time out of range");
	return 1;
    }
    return 0;
}

CMDFUNCSTART(chbtime)
{
    time_t secs;
    int32_t nsecs;

    if (dotime(argv[1], &secs, &nsecs))
	return 1;
    if (sblock.fs_magic == FS_UFS1_MAGIC)
	return 1;
    curinode->dp2.di_birthtime = _time_to_time64(secs);
    curinode->dp2.di_birthnsec = nsecs;
    inodirty(curinode);
    printactive(0);
    return 0;
}

CMDFUNCSTART(chmtime)
{
    time_t secs;
    int32_t nsecs;

    if (dotime(argv[1], &secs, &nsecs))
	return 1;
    if (sblock.fs_magic == FS_UFS1_MAGIC)
	curinode->dp1.di_mtime = _time_to_time32(secs);
    else
	curinode->dp2.di_mtime = _time_to_time64(secs);
    DIP_SET(curinode, di_mtimensec, nsecs);
    inodirty(curinode);
    printactive(0);
    return 0;
}

CMDFUNCSTART(chatime)
{
    time_t secs;
    int32_t nsecs;

    if (dotime(argv[1], &secs, &nsecs))
	return 1;
    if (sblock.fs_magic == FS_UFS1_MAGIC)
	curinode->dp1.di_atime = _time_to_time32(secs);
    else
	curinode->dp2.di_atime = _time_to_time64(secs);
    DIP_SET(curinode, di_atimensec, nsecs);
    inodirty(curinode);
    printactive(0);
    return 0;
}

CMDFUNCSTART(chctime)
{
    time_t secs;
    int32_t nsecs;

    if (dotime(argv[1], &secs, &nsecs))
	return 1;
    if (sblock.fs_magic == FS_UFS1_MAGIC)
	curinode->dp1.di_ctime = _time_to_time32(secs);
    else
	curinode->dp2.di_ctime = _time_to_time64(secs);
    DIP_SET(curinode, di_ctimensec, nsecs);
    inodirty(curinode);
    printactive(0);
    return 0;
}
