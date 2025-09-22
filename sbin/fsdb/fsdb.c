/*	$OpenBSD: fsdb.c,v 1.36 2024/01/09 03:16:00 guenther Exp $	*/
/*	$NetBSD: fsdb.c,v 1.7 1997/01/11 06:50:53 lukem Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by John T. Kohl.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <grp.h>
#include <histedit.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>

#include "fsdb.h"
#include "fsck.h"
#include "extern.h"

extern char *__progname;	/* from crt0.o */

int main(int, char *[]);
static void usage(void);
static int cmdloop(void);
static int helpfn(int, char *[]);
static char *prompt(EditLine *);
static int scannames(struct inodesc *);
static int dolookup(char *);
static int chinumfunc(struct inodesc *);
static int chnamefunc(struct inodesc *);
static int dotime(char *, time_t *, int32_t *);

int returntosingle = 0;
union dinode *curinode;
ino_t curinum;

struct inostatlist *inostathead;

struct bufarea bufhead;		/* head of list of other blks in filesys */
struct bufarea sblk;		/* file system superblock */
struct bufarea asblk;		/* alternate file system superblock */
struct bufarea *pdirbp;		/* current directory contents */
struct bufarea *pbp;		/* current inode block */

struct dups *duplist;		/* head of dup list */
struct dups *muldup;		/* end of unique duplicate dup block numbers */

struct zlncnt *zlnhead;		/* head of zero link count list */

struct inoinfo **inphead, **inpsort;

extern long numdirs, listmax, inplast;

long	secsize;		/* actual disk sector size */
char	nflag;			/* assume a no response */
char	yflag;			/* assume a yes response */
daddr_t	bflag;			/* location of alternate super block */
int	debug;			/* output debugging info */
int	cvtlevel;		/* convert to newer file system format */
char    usedsoftdep;            /* just fix soft dependency inconsistencies */
int	preen;			/* just fix normal inconsistencies */
char    resolved;               /* cleared if unresolved changes => not clean */
char	havesb;			/* superblock has been read */
char	skipclean;		/* skip clean file systems if preening */
int	fsmodified;		/* 1 => write done to file system */
int	fsreadfd;		/* file descriptor for reading file system */
int	fswritefd;		/* file descriptor for writing file system */
int	rerun;			/* rerun fsck.  Only used in non-preen mode */

daddr_t	maxfsblock;		/* number of blocks in the file system */
char	*blockmap;		/* ptr to primary blk allocation map */
ino_t	maxino;			/* number of inodes in file system */
ino_t	lastino;		/* last inode in use */

ino_t	lfdir;			/* lost & found directory inode number */

daddr_t	n_blks;			/* number of blocks in use */
int64_t	n_files;		/* number of files in use */

struct ufs1_dinode ufs1_zino;
struct ufs2_dinode ufs2_zino;


static void
usage(void)
{
	fprintf(stderr, "usage: %s [-d] -f fsname\n", __progname);
	exit(1);
}

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

	while (-1 != (ch = getopt(argc, argv, "f:d"))) {
		switch (ch) {
		case 'f':
			fsys = optarg;
			break;
		case 'd':
			debug++;
			break;
		default:
			usage();
		}
	}
	if (fsys == NULL)
		usage();
	if (!setup(fsys, 1))
		errx(1, "cannot set up file system `%s'", fsys);
	printf("Editing file system `%s'\nLast Mounted on %s\n", fsys,
	    sblock.fs_fsmnt);
	rval = cmdloop();
	sblock.fs_clean = 0;		/* mark it dirty */
	sbdirty();
	ckfini(0);
	printf("*** FILE SYSTEM MARKED DIRTY\n");
	printf("*** BE SURE TO RUN FSCK TO CLEAN UP ANY DAMAGE\n");
	printf("*** IF IT WAS MOUNTED, RE-MOUNT WITH -u -o reload\n");
	exit(rval);
}

#define CMDFUNC(func) static int func(int argc, char *argv[])
#define CMDFUNCSTART(func) static int func(int argc, char *argv[])

CMDFUNC(helpfn);
CMDFUNC(focus);				/* focus on inode */
CMDFUNC(active);			/* print active inode */
CMDFUNC(focusname);			/* focus by name */
CMDFUNC(zapi);				/* clear inode */
CMDFUNC(uplink);			/* incr link */
CMDFUNC(downlink);			/* decr link */
CMDFUNC(linkcount);			/* set link count */
CMDFUNC(quit);				/* quit */
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
CMDFUNC(chmtime);			/* Change mtime */
CMDFUNC(chctime);			/* Change ctime */
CMDFUNC(chatime);			/* Change atime */
CMDFUNC(chinum);			/* Change inode # of dirent */
CMDFUNC(chname);			/* Change dirname of dirent */

static struct cmdtable cmds[] = {
	{ "help", "Print out help", 1, 1, helpfn },
	{ "?", "Print out help", 1, 1, helpfn },
	{ "inode", "Set active inode to INUM", 2, 2, focus },
	{ "clri", "Clear inode INUM", 2, 2, zapi },
	{ "lookup", "Set active inode by looking up NAME", 2, 2, focusname },
	{ "cd", "Set active inode by looking up NAME", 2, 2, focusname },
	{ "back", "Go to previous active inode", 1, 1, back },
	{ "active", "Print active inode", 1, 1, active },
	{ "print", "Print active inode", 1, 1, active },
	{ "uplink", "Increment link count", 1, 1, uplink },
	{ "downlink", "Decrement link count", 1, 1, downlink },
	{ "linkcount", "Set link count to COUNT", 2, 2, linkcount },
	{ "ls", "List current inode as directory", 1, 1, ls },
	{ "rm", "Remove NAME from current inode directory", 2, 2, rm },
	{ "del", "Remove NAME from current inode directory", 2, 2, rm },
	{ "ln", "Hardlink INO into current inode directory as NAME", 3, 3, ln },
	{ "chinum", "Change dir entry number INDEX to INUM", 3, 3, chinum },
	{ "chname", "Change dir entry number INDEX to NAME", 3, 3, chname },
	{ "chtype", "Change type of current inode to TYPE", 2, 2, newtype },
	{ "chmod", "Change mode of current inode to MODE", 2, 2, chmode },
	{ "chown", "Change owner of current inode to OWNER", 2, 2, chowner },
	{ "chlen", "Change length of current inode to LENGTH", 2, 2, chlen },
	{ "chgrp", "Change group of current inode to GROUP", 2, 2, chgroup },
	{ "chflags", "Change flags of current inode to FLAGS", 2, 2, chaflags },
	{ "chgen", "Change generation number of current inode to GEN", 2, 2, chgen },
	{ "mtime", "Change mtime of current inode to MTIME", 2, 2, chmtime },
	{ "ctime", "Change ctime of current inode to CTIME", 2, 2, chctime },
	{ "atime", "Change atime of current inode to ATIME", 2, 2, chatime },
	{ "quit", "Exit", 1, 1, quit },
	{ "q", "Exit", 1, 1, quit },
	{ "exit", "Exit", 1, 1, quit },
	{ NULL, 0, 0, 0 },
};

static int
helpfn(int argc, char *argv[])
{
	struct cmdtable *cmdtp;

	printf("Commands are:\n%-10s %5s %5s   %s\n",
	    "command", "min argc", "max argc", "what");

	for (cmdtp = cmds; cmdtp->cmd; cmdtp++)
		printf("%-10s %5u %5u   %s\n",
		    cmdtp->cmd, cmdtp->minargc, cmdtp->maxargc, cmdtp->helptxt);
	return 0;
}

static char *
prompt(EditLine *el)
{
	static char pstring[64];

	snprintf(pstring, sizeof(pstring), "fsdb (inum: %llu)> ",
	    (unsigned long long)curinum);
	return pstring;
}


static int
cmdloop(void)
{
	char *line = NULL;
	const char *elline;
	int cmd_argc, rval = 0, known;
#define scratch known
	char **cmd_argv;
	struct cmdtable *cmdp;
	History *hist;
	EditLine *elptr;
	HistEvent hev;

	curinode = ginode(ROOTINO);
	curinum = ROOTINO;
	printactive();

	hist = history_init();
	history(hist, &hev, H_SETSIZE, 100);	/* 100 elt history buffer */

	elptr = el_init(__progname, stdin, stdout, stderr);
	el_set(elptr, EL_EDITOR, "emacs");
	el_set(elptr, EL_PROMPT, prompt);
	el_set(elptr, EL_HIST, history, hist);
	el_source(elptr, NULL);

	while ((elline = el_gets(elptr, &scratch)) != NULL && scratch != 0) {
		if (debug)
			printf("command `%s'\n", elline);

		history(hist, &hev, H_ENTER, elline);

		line = strdup(elline);
		if (line == NULL)
			errx(1, "out of memory");
		cmd_argv = crack(line, &cmd_argc);
		if (cmd_argc) {
			/*
			 * el_parse returns -1 to signal that it's not been handled
			 * internally.
			 */
			if (el_parse(elptr, cmd_argc, (const char **)cmd_argv) != -1)
				continue;
			known = 0;
			for (cmdp = cmds; cmdp->cmd; cmdp++) {
				if (!strcmp(cmdp->cmd, cmd_argv[0])) {
					if (cmd_argc >= cmdp->minargc &&
					    cmd_argc <= cmdp->maxargc)
						rval = (*cmdp->handler)(cmd_argc,
						    cmd_argv);
					else
						rval = argcount(cmdp,
						    cmd_argc, cmd_argv);
					known = 1;
					break;
				}
			}
			if (!known) {
				warnx("unknown command `%s'", cmd_argv[0]);
				rval = 1;
			}
		} else
			rval = 0;
		free(line);
		if (rval < 0)
			return rval;
		if (rval)
			warnx("rval was %d", rval);
	}
	el_end(elptr);
	history_end(hist);
	return rval;
}

static ino_t ocurrent;

#define GETINUM(ac,inum)    inum = strtoull(argv[ac], &cp, 0); \
	if (inum < ROOTINO || inum > maxino || cp == argv[ac] || *cp != '\0' ) { \
		printf("inode %llu out of range; range is [%llu,%llu]\n", \
		    (unsigned long long)inum, (unsigned long long)ROOTINO, \
		    (unsigned long long)maxino); \
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
	printactive();
	return 0;
}

CMDFUNCSTART(back)
{
	curinum = ocurrent;
	curinode = ginode(curinum);
	printactive();
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
	inodirty();
	if (curinode)			/* re-set after potential change */
		curinode = ginode(curinum);
	return 0;
}

CMDFUNCSTART(active)
{
	printactive();
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
	printf("inode %llu link count now %d\n",
	    (unsigned long long)curinum, DIP(curinode, di_nlink));
	inodirty();
	return 0;
}

CMDFUNCSTART(downlink)
{
	if (!checkactive())
		return 1;
	DIP_SET(curinode, di_nlink, DIP(curinode, di_nlink) - 1);
	printf("inode %llu link count now %d\n",
	    (unsigned long long)curinum, DIP(curinode, di_nlink));
	inodirty();
	return 0;
}

static const char *typename[] = {
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

static int slot;

static int
scannames(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;

	printf("slot %d ino %llu reclen %d: %s, `%.*s'\n",
	    slot++, (unsigned long long)dirp->d_ino, dirp->d_reclen,
	    typename[dirp->d_type], dirp->d_namlen, dirp->d_name);
	return (KEEPON);
}

CMDFUNCSTART(ls)
{
	struct inodesc idesc;
	checkactivedir();			/* let it go on anyway */

	slot = 0;
	idesc.id_number = curinum;
	idesc.id_func = scannames;
	idesc.id_type = DATA;
	idesc.id_fix = IGNORE;
	ckinode(curinode, &idesc);
	curinode = ginode(curinum);

	return 0;
}

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
		printactive();
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
		curinum = ROOTINO;
		curinode = ginode(ROOTINO);
	} else {
		if (!checkactivedir())
		    return 1;
	}
	for (p = argv[1]; p != NULL;) {
		while ((val = strsep(&p, "/")) != NULL && *val == '\0')
			continue;
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
		printf("Ino %llu entered as `%s'\n",
		    (unsigned long long)inum, argv[2]);
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
		printf("could not remove name? weird.\n");
		return 1;
	}
}

static long slotcount, desired;

static int
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

static int
chnamefunc(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;
	struct direct testdir;

	if (slotcount++ == desired) {
		/* will name fit? */
		testdir.d_namlen = strlen(idesc->id_name);
		if (DIRSIZ(&testdir) <= dirp->d_reclen) {
			dirp->d_namlen = testdir.d_namlen;
			strlcpy(dirp->d_name, idesc->id_name, sizeof dirp->d_name);
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
		warnx("new name `%s' does not fit in slot %s", argv[2], argv[1]);
		return 1;
	} else {
		warnx("no %sth slot in current directory", argv[1]);
		return 1;
	}
}

static struct typemap {
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
	    tp < &typenamemap[sizeof(typenamemap)/sizeof(*typenamemap)];
	    tp++) {
		if (!strcmp(argv[1], tp->typename)) {
			printf("setting type to %s\n", tp->typename);
			type = tp->typebits;
			break;
		}
	}
	if (tp == &typenamemap[sizeof(typenamemap)/sizeof(*typenamemap)]) {
		warnx("type `%s' not known", argv[1]);
		warnx("try one of `file', `dir', `socket', `fifo'");
		return 1;
	}
	DIP_SET(curinode, di_mode, DIP(curinode, di_mode) & ~IFMT);
	DIP_SET(curinode, di_mode, DIP(curinode, di_mode) | type);
	inodirty();
	printactive();
	return 0;
}

CMDFUNCSTART(chmode)
{
	int rval = 1;
	long modebits;
	char *cp;

	if (!checkactive())
		return 1;

	modebits = strtol(argv[1], &cp, 8);
	if (cp == argv[1] || *cp != '\0' ) {
		warnx("bad modebits `%s'", argv[1]);
		return 1;
	}

	DIP_SET(curinode, di_mode, DIP(curinode, di_mode) & ~07777);
	DIP_SET(curinode, di_mode, DIP(curinode, di_mode) | modebits);
	inodirty();
	printactive();
	return rval;
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
		warnx("bad length '%s'", argv[1]);
		return 1;
	}

	DIP_SET(curinode, di_size, len);
	inodirty();
	printactive();
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
		warnx("flags set beyond 32-bit range of field (%lx)", flags);
		return(1);
	}
	DIP_SET(curinode, di_flags, flags);
	inodirty();
	printactive();
	return rval;
}

CMDFUNCSTART(chgen)
{
	int rval = 1;
	long long gen;
	char *cp;

	if (!checkactive())
		return 1;

	gen = strtoll(argv[1], &cp, 0);
	if (cp == argv[1] || *cp != '\0' ) {
		warnx("bad gen `%s'", argv[1]);
		return 1;
	}

	if (gen > UINT_MAX || gen < 0) {
		warnx("gen set beyond 32-bit range of field (%llx)", gen);
		return(1);
	}
	DIP_SET(curinode, di_gen, gen);
	inodirty();
	printactive();
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
		warnx("max link count is %d", USHRT_MAX);
		return 1;
	}

	DIP_SET(curinode, di_nlink, lcnt);
	inodirty();
	printactive();
	return rval;
}

CMDFUNCSTART(chowner)
{
	int rval = 1;
	uid_t uid;
	char *cp;

	if (!checkactive())
		return 1;

	uid = strtoul(argv[1], &cp, 0);
	if (cp == argv[1] || *cp != '\0' ) {
		/* try looking up name */
		if (uid_from_user(argv[1], &uid) == -1) {
			warnx("bad uid `%s'", argv[1]);
			return 1;
		}
	}

	DIP_SET(curinode, di_uid, uid);
	inodirty();
	printactive();
	return rval;
}

CMDFUNCSTART(chgroup)
{
	int rval = 1;
	gid_t gid;
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
	inodirty();
	printactive();
	return rval;
}

static int
dotime(char *name, time_t *rsec, int32_t *rnsec)
{
	char *p, *val;
	struct tm t;
	time_t sec;
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

	for (p = name; *p; p++)
		if (*p < '0' || *p > '9')
			    goto badformat;

	p = name;
#define VAL() ((*p++) - '0')
	bzero(&t, sizeof t);
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

	sec = mktime(&t);
	if (sec == -1) {
		warnx("date/time out of range");
		return 1;
	}
	*rsec = sec;
	*rnsec = nsec;
	return 0;
}

CMDFUNCSTART(chmtime)
{
	time_t rsec;
	int32_t nsec;

	if (dotime(argv[1], &rsec, &nsec))
		return 1;
	DIP_SET(curinode, di_mtime, rsec);
	DIP_SET(curinode, di_mtimensec, nsec);
	inodirty();
	printactive();
	return 0;
}

CMDFUNCSTART(chatime)
{
	time_t rsec;
	int32_t nsec;

	if (dotime(argv[1], &rsec, &nsec))
		return 1;
	DIP_SET(curinode, di_atime, rsec);
	DIP_SET(curinode, di_atimensec, nsec);
	inodirty();
	printactive();
	return 0;
}

CMDFUNCSTART(chctime)
{
	time_t rsec;
	int32_t nsec;

	if (dotime(argv[1], &rsec, &nsec))
		return 1;
	DIP_SET(curinode, di_ctime, rsec);
	DIP_SET(curinode, di_ctimensec, nsec);
	inodirty();
	printactive();
	return 0;
}
