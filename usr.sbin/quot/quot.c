/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1991, 1994 Wolfgang Solfrank.
 * Copyright (C) 1991, 1994 TooLs GmbH.
 * All rights reserved.
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
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stdint.h>
#include <sys/mount.h>
#include <sys/disklabel.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <fcntl.h>
#include <fstab.h>
#include <errno.h>
#include <libufs.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* some flags of what to do: */
static char estimate;
static char count;
static char unused;
static void (*func)(int, struct fs *, char *);
static long blocksize;
static char *header;
static int headerlen;

static union dinode *get_inode(int, struct fs *, ino_t);
static int	virtualblocks(struct fs *, union dinode *);
static int	isfree(struct fs *, union dinode *);
static void	inituser(void);
static void	usrrehash(void);
static struct user *user(uid_t);
static int	cmpusers(const void *, const void *);
static void	uses(uid_t, daddr_t, time_t);
static void	initfsizes(void);
static void	dofsizes(int, struct fs *, char *);
static void	douser(int, struct fs *, char *);
static void	donames(int, struct fs *, char *);
static void	usage(void);
static void	quot(char *, char *);

/*
 * Original BSD quot doesn't round to number of frags/blocks,
 * doesn't account for indirection blocks and gets it totally
 * wrong if the	size is a multiple of the blocksize.
 * The new code always counts the number of 512 byte blocks
 * instead of the number of kilobytes and converts them	to
 * kByte when done (on request).
 *
 * Due to the size of modern disks, we must cast intermediate
 * values to 64 bits to prevent potential overflows.
 */
#ifdef	COMPAT
#define	SIZE(n)	(n)
#else
#define	SIZE(n) ((int)(((quad_t)(n) * 512 + blocksize - 1)/blocksize))
#endif

#define	INOCNT(fs)	((fs)->fs_ipg)
#define	INOSZ(fs) \
	(((fs)->fs_magic == FS_UFS1_MAGIC ? sizeof(struct ufs1_dinode) : \
	sizeof(struct ufs2_dinode)) * INOCNT(fs))

union dinode {
	struct ufs1_dinode dp1;
	struct ufs2_dinode dp2;
};
#define	DIP(fs, dp, field) \
	(((fs)->fs_magic == FS_UFS1_MAGIC) ? \
	(dp)->dp1.field : (dp)->dp2.field)

static union dinode *
get_inode(int fd, struct fs *super, ino_t ino)
{
	static caddr_t ipbuf;
	static struct cg *cgp;
	static ino_t last;
	static int cg;
	struct ufs2_dinode *di2;

	if (fd < 0) {		/* flush cache */
		if (ipbuf) {
			free(ipbuf);
			ipbuf = 0;
			if (super != NULL && super->fs_magic == FS_UFS2_MAGIC) {
				free(cgp);
				cgp = 0;
			}
		}
		return 0;
	}

	if (!ipbuf || ino < last || ino >= last + INOCNT(super)) {
		if (super->fs_magic == FS_UFS2_MAGIC &&
		    (!cgp || cg != ino_to_cg(super, ino))) {
			cg = ino_to_cg(super, ino);
			if (!cgp && !(cgp = malloc(super->fs_cgsize)))
				errx(1, "allocate cg");
			if (lseek(fd, (off_t)cgtod(super, cg) << super->fs_fshift, 0) < 0)
				err(1, "lseek cg");
			if (read(fd, cgp, super->fs_cgsize) != super->fs_cgsize)
				err(1, "read cg");
			if (!cg_chkmagic(cgp))
				errx(1, "cg has bad magic");
		}
		if (!ipbuf
		    && !(ipbuf = malloc(INOSZ(super))))
			errx(1, "allocate inodes");
		last = rounddown(ino, INOCNT(super));
		if (lseek(fd, (off_t)ino_to_fsba(super, last) << super->fs_fshift, 0) < (off_t)0
		    || read(fd, ipbuf, INOSZ(super)) != (ssize_t)INOSZ(super))
			err(1, "read inodes");
	}

	if (super->fs_magic == FS_UFS1_MAGIC)
		return ((union dinode *)
		    &((struct ufs1_dinode *)ipbuf)[ino % INOCNT(super)]);
	di2 = &((struct ufs2_dinode *)ipbuf)[ino % INOCNT(super)];
	/* If the inode is unused, it might be unallocated too, so zero it. */
	if (isclr(cg_inosused(cgp), ino % super->fs_ipg))
		bzero(di2, sizeof (*di2));
	return ((union dinode *)di2);
}

#ifdef	COMPAT
#define	actualblocks(fs, dp)	(DIP(fs, dp, di_blocks) / 2)
#else
#define	actualblocks(fs, dp)	DIP(fs, dp, di_blocks)
#endif

static int virtualblocks(struct fs *super, union dinode *dp)
{
	off_t nblk, sz;

	sz = DIP(super, dp, di_size);
#ifdef	COMPAT
	if (lblkno(super,sz) >= UFS_NDADDR) {
		nblk = blkroundup(super,sz);
		if (sz == nblk)
			nblk += super->fs_bsize;
	}

	return sz / 1024;

#else	/* COMPAT */

	if (lblkno(super,sz) >= UFS_NDADDR) {
		nblk = blkroundup(super,sz);
		sz = lblkno(super,nblk);
		sz = (sz - UFS_NDADDR + NINDIR(super) - 1) / NINDIR(super);
		while (sz > 0) {
			nblk += sz * super->fs_bsize;
			/* sz - 1 rounded up */
			sz = (sz - 1 + NINDIR(super) - 1) / NINDIR(super);
		}
	} else
		nblk = fragroundup(super,sz);

	return nblk / 512;
#endif	/* COMPAT */
}

static int
isfree(struct fs *super, union dinode *dp)
{
#ifdef	COMPAT
	return (DIP(super, dp, di_mode) & IFMT) == 0;
#else	/* COMPAT */

	switch (DIP(super, dp, di_mode) & IFMT) {
	case IFIFO:
	case IFLNK:		/* should check FASTSYMLINK? */
	case IFDIR:
	case IFREG:
		return 0;
	case IFCHR:
	case IFBLK:
	case IFSOCK:
	case IFWHT:
	case 0:
		return 1;
	default:
		errx(1, "unknown IFMT 0%o", DIP(super, dp, di_mode) & IFMT);
	}
#endif
}

static struct user {
	uid_t uid;
	char *name;
	daddr_t space;
	long count;
	daddr_t spc30;
	daddr_t spc60;
	daddr_t spc90;
} *users;
static int nusers;

static void
inituser(void)
{
	int i;
	struct user *usr;

	if (!nusers) {
		nusers = 8;
		if (!(users =
		    (struct user *)calloc(nusers,sizeof(struct user))))
			errx(1, "allocate users");
	} else {
		for (usr = users, i = nusers; --i >= 0; usr++) {
			usr->space = usr->spc30 = usr->spc60 = usr->spc90 = 0;
			usr->count = 0;
		}
	}
}

static void
usrrehash(void)
{
	int i;
	struct user *usr, *usrn;
	struct user *svusr;

	svusr = users;
	nusers <<= 1;
	if (!(users = (struct user *)calloc(nusers,sizeof(struct user))))
		errx(1, "allocate users");
	for (usr = svusr, i = nusers >> 1; --i >= 0; usr++) {
		for (usrn = users + (usr->uid&(nusers - 1)); usrn->name;
		    usrn--) {
			if (usrn <= users)
				usrn = users + nusers;
		}
		*usrn = *usr;
	}
}

static struct user *
user(uid_t uid)
{
	struct user *usr;
	int i;
	struct passwd *pwd;

	while (1) {
		for (usr = users + (uid&(nusers - 1)), i = nusers; --i >= 0;
		    usr--) {
			if (!usr->name) {
				usr->uid = uid;

				if (!(pwd = getpwuid(uid))) {
					if ((usr->name = (char *)malloc(7)))
						sprintf(usr->name,"#%d",uid);
				} else {
					if ((usr->name = (char *)
					    malloc(strlen(pwd->pw_name) + 1)))
						strcpy(usr->name,pwd->pw_name);
				}
				if (!usr->name)
					errx(1, "allocate users");

				return usr;

			} else if (usr->uid == uid)
				return usr;

			if (usr <= users)
				usr = users + nusers;
		}
		usrrehash();
	}
}

static int
cmpusers(const void *v1, const void *v2)
{
	const struct user *u1, *u2;
	u1 = (const struct user *)v1;
	u2 = (const struct user *)v2;

	return u2->space - u1->space;
}

#define	sortusers(users)	(qsort((users),nusers,sizeof(struct user), \
				    cmpusers))

static void
uses(uid_t uid, daddr_t blks, time_t act)
{
	static time_t today;
	struct user *usr;

	if (!today)
		time(&today);

	usr = user(uid);
	usr->count++;
	usr->space += blks;

	if (today - act > 90L * 24L * 60L * 60L)
		usr->spc90 += blks;
	if (today - act > 60L * 24L * 60L * 60L)
		usr->spc60 += blks;
	if (today - act > 30L * 24L * 60L * 60L)
		usr->spc30 += blks;
}

#ifdef	COMPAT
#define	FSZCNT	500
#else
#define	FSZCNT	512
#endif
struct fsizes {
	struct fsizes *fsz_next;
	daddr_t fsz_first, fsz_last;
	ino_t fsz_count[FSZCNT];
	daddr_t fsz_sz[FSZCNT];
} *fsizes;

static void
initfsizes(void)
{
	struct fsizes *fp;
	int i;

	for (fp = fsizes; fp; fp = fp->fsz_next) {
		for (i = FSZCNT; --i >= 0;) {
			fp->fsz_count[i] = 0;
			fp->fsz_sz[i] = 0;
		}
	}
}

static void
dofsizes(int fd, struct fs *super, char *name)
{
	ino_t inode, maxino;
	union dinode *dp;
	daddr_t sz, ksz;
	struct fsizes *fp, **fsp;
	int i;

	maxino = super->fs_ncg * super->fs_ipg - 1;
#ifdef	COMPAT
	if (!(fsizes = (struct fsizes *)malloc(sizeof(struct fsizes))))
		errx(1, "allocate fsize structure");
#endif	/* COMPAT */
	for (inode = 0; inode < maxino; inode++) {
		errno = 0;
		if ((dp = get_inode(fd,super,inode))
#ifdef	COMPAT
		    && ((DIP(super, dp, di_mode) & IFMT) == IFREG
			|| (DIP(super, dp, di_mode) & IFMT) == IFDIR)
#else	/* COMPAT */
		    && !isfree(super, dp)
#endif	/* COMPAT */
		    ) {
			sz = estimate ? virtualblocks(super, dp) :
			    actualblocks(super, dp);
#ifdef	COMPAT
			if (sz >= FSZCNT) {
				fsizes->fsz_count[FSZCNT-1]++;
				fsizes->fsz_sz[FSZCNT-1] += sz;
			} else {
				fsizes->fsz_count[sz]++;
				fsizes->fsz_sz[sz] += sz;
			}
#else	/* COMPAT */
			ksz = SIZE(sz);
			for (fsp = &fsizes; (fp = *fsp); fsp = &fp->fsz_next) {
				if (ksz < fp->fsz_last)
					break;
			}
			if (!fp || ksz < fp->fsz_first) {
				if (!(fp = (struct fsizes *)
				    malloc(sizeof(struct fsizes))))
					errx(1, "allocate fsize structure");
				fp->fsz_next = *fsp;
				*fsp = fp;
				fp->fsz_first = rounddown(ksz, FSZCNT);
				fp->fsz_last = fp->fsz_first + FSZCNT;
				for (i = FSZCNT; --i >= 0;) {
					fp->fsz_count[i] = 0;
					fp->fsz_sz[i] = 0;
				}
			}
			fp->fsz_count[ksz % FSZCNT]++;
			fp->fsz_sz[ksz % FSZCNT] += sz;
#endif	/* COMPAT */
		} else if (errno) {
			err(1, "%s", name);
		}
	}
	sz = 0;
	for (fp = fsizes; fp; fp = fp->fsz_next) {
		for (i = 0; i < FSZCNT; i++) {
			if (fp->fsz_count[i])
				printf("%jd\t%jd\t%d\n",
				    (intmax_t)(fp->fsz_first + i),
				    (intmax_t)fp->fsz_count[i],
				    SIZE(sz += fp->fsz_sz[i]));
		}
	}
}

static void
douser(int fd, struct fs *super, char *name)
{
	ino_t inode, maxino;
	struct user *usr, *usrs;
	union dinode *dp;
	int n;

	maxino = super->fs_ncg * super->fs_ipg - 1;
	for (inode = 0; inode < maxino; inode++) {
		errno = 0;
		if ((dp = get_inode(fd,super,inode))
		    && !isfree(super, dp))
			uses(DIP(super, dp, di_uid),
			    estimate ? virtualblocks(super, dp) :
				actualblocks(super, dp),
			    DIP(super, dp, di_atime));
		else if (errno) {
			err(1, "%s", name);
		}
	}
	if (!(usrs = (struct user *)malloc(nusers * sizeof(struct user))))
		errx(1, "allocate users");
	bcopy(users,usrs,nusers * sizeof(struct user));
	sortusers(usrs);
	for (usr = usrs, n = nusers; --n >= 0 && usr->count; usr++) {
		printf("%5d",SIZE(usr->space));
		if (count)
			printf("\t%5ld",usr->count);
		printf("\t%-8s",usr->name);
		if (unused)
			printf("\t%5d\t%5d\t%5d",
			       SIZE(usr->spc30),
			       SIZE(usr->spc60),
			       SIZE(usr->spc90));
		printf("\n");
	}
	free(usrs);
}

static void
donames(int fd, struct fs *super, char *name)
{
	int c;
	ino_t maxino;
	uintmax_t inode;
	union dinode *dp;

	maxino = super->fs_ncg * super->fs_ipg - 1;
	/* first skip the name of the filesystem */
	while ((c = getchar()) != EOF && (c < '0' || c > '9'))
		while ((c = getchar()) != EOF && c != '\n');
	ungetc(c,stdin);
	while (scanf("%ju", &inode) == 1) {
		if (inode > maxino) {
			warnx("illegal inode %ju", inode);
			return;
		}
		errno = 0;
		if ((dp = get_inode(fd,super,inode))
		    && !isfree(super, dp)) {
			printf("%s\t",user(DIP(super, dp, di_uid))->name);
			/* now skip whitespace */
			while ((c = getchar()) == ' ' || c == '\t');
			/* and print out the remainder of the input line */
			while (c != EOF && c != '\n') {
				putchar(c);
				c = getchar();
			}
			putchar('\n');
		} else {
			if (errno) {
				err(1, "%s", name);
			}
			/* skip this line */
			while ((c = getchar()) != EOF && c != '\n');
		}
		if (c == EOF)
			break;
	}
}

static void
usage(void)
{
#ifdef	COMPAT
	fprintf(stderr,"usage: quot [-nfcvha] [filesystem ...]\n");
#else	/* COMPAT */
	fprintf(stderr,"usage: quot [-acfhknv] [filesystem ...]\n");
#endif	/* COMPAT */
	exit(1);
}

void
quot(char *name, char *mp)
{
	int fd;
	struct fs *fs;

	get_inode(-1, NULL, 0);		/* flush cache */
	inituser();
	initfsizes();
	if ((fd = open(name,0)) < 0) {
		warn("%s", name);
		close(fd);
		return;
	}
	switch (sbget(fd, &fs, STDSB)) {
	case 0:
		break;
	case ENOENT:
		warn("Cannot find file system superblock");
		close(fd);
		return;
	default:
		warn("Unable to read file system superblock");
		close(fd);
		return;
	}
	printf("%s:",name);
	if (mp)
		printf(" (%s)",mp);
	putchar('\n');
	(*func)(fd, fs, name);
	close(fd);
}

int
main(int argc, char *argv[])
{
	char all = 0;
	struct statfs *mp;
	struct fstab *fs;
	int cnt;

	func = douser;
#ifndef	COMPAT
	header = getbsize(&headerlen,&blocksize);
#endif
	while (--argc > 0 && **++argv == '-') {
		while (*++*argv) {
			switch (**argv) {
			case 'n':
				func = donames;
				break;
			case 'c':
				func = dofsizes;
				break;
			case 'a':
				all = 1;
				break;
			case 'f':
				count = 1;
				break;
			case 'h':
				estimate = 1;
				break;
#ifndef	COMPAT
			case 'k':
				blocksize = 1024;
				break;
#endif	/* COMPAT */
			case 'v':
				unused = 1;
				break;
			default:
				usage();
			}
		}
	}
	if (all) {
		cnt = getmntinfo(&mp,MNT_NOWAIT);
		for (; --cnt >= 0; mp++) {
			if (!strncmp(mp->f_fstypename, "ufs", MFSNAMELEN))
				quot(mp->f_mntfromname, mp->f_mntonname);
		}
	}
	while (--argc >= 0) {
		if ((fs = getfsfile(*argv)) != NULL)
			quot(fs->fs_spec, 0);
		else
			quot(*argv,0);
		argv++;
	}
	return 0;
}
