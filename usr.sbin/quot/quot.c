/*	$OpenBSD: quot.c,v 1.33 2024/04/23 13:34:51 jsg Exp $	*/

/*
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

#include <sys/param.h>	/* DEV_BSIZE MAXBSIZE */
#include <sys/mount.h>
#include <sys/time.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pwd.h>
#include <unistd.h>

/* some flags of what to do: */
static char estimate;
static char count;
static char unused;
static void (*func)(int, struct fs *, char *);
static int cmpusers(const void *, const void *);
void	quot(char *, char *);
static long blocksize;
static char *header;
static int headerlen;

#define	SIZE(n)	(howmany(((off_t)(n)) * DEV_BSIZE, blocksize))

#define	INOCNT(fs)	((fs)->fs_ipg)
#define	INOSZ(fs)	(((fs)->fs_magic == FS_UFS1_MAGIC ? \
			    sizeof(struct ufs1_dinode) : \
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
			ipbuf = NULL;
			if (super != NULL && super->fs_magic == FS_UFS2_MAGIC) {
				free(cgp);
				cgp = NULL;
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
			if (pread(fd, cgp, super->fs_cgsize,
			    (off_t)cgtod(super, cg) << super->fs_fshift)
			    != super->fs_cgsize)
				if (read(fd, cgp, super->fs_cgsize) !=
				    super->fs_cgsize)
					err(1, "read cg");
			if (!cg_chkmagic(cgp))
				errx(1, "cg has bad magic");
		}
		if (!ipbuf && !(ipbuf = malloc(INOSZ(super))))
			err(1, "allocate inodes");
		last = (ino / INOCNT(super)) * INOCNT(super);
		if (lseek(fd, (off_t)ino_to_fsba(super, last)
		    << super->fs_fshift, SEEK_SET) < 0 ||
		    read(fd, ipbuf, INOSZ(super)) != INOSZ(super)) {
			err(1, "read inodes");
		}
	}

	if (super->fs_magic == FS_UFS1_MAGIC)
		return ((union dinode *)
		    &((struct ufs1_dinode *)ipbuf)[ino % INOCNT(super)]);
	di2 = &((struct ufs2_dinode *)ipbuf)[ino % INOCNT(super)];
	/* If the inode is unused, it might be unallocated too, so zero it. */
	if (isclr(cg_inosused(cgp), ino % super->fs_ipg))
		memset(di2, 0, sizeof(*di2));
	return ((union dinode *)di2);
}

#define	actualblocks(fs, ip)	DIP(fs, dp, di_blocks)

static int
virtualblocks(struct fs *super, union dinode *dp)
{
	off_t nblk, sz;

	sz = DIP(super, dp, di_size);

	if (lblkno(super, sz) >= NDADDR) {
		nblk = blkroundup(super, sz);
		sz = lblkno(super, nblk);
		sz = howmany(sz - NDADDR, NINDIR(super));
		while (sz > 0) {
			nblk += sz * super->fs_bsize;
			/* One block on this level is in the inode itself */
			sz = howmany(sz - 1, NINDIR(super));
		}
	} else
		nblk = fragroundup(super, sz);

	return nblk / DEV_BSIZE;
}

static int
isfree(struct fs *super, union dinode *dp)
{
	switch (DIP(super, dp, di_mode) & IFMT) {
	case IFIFO:
	case IFLNK:		/* should check FASTSYMLINK? */
	case IFDIR:
	case IFREG:
		return 0;
	case IFCHR:
	case IFBLK:
	case IFSOCK:
	case 0:
		return 1;
	default:
		errx(1, "unknown IFMT 0%o", DIP(super, dp, di_mode) & IFMT);
	}
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
		if (!(users = calloc(nusers, sizeof(struct user)))) {
			err(1, "allocate users");
		}
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
	if (!(users = calloc(nusers, sizeof(struct user))))
		err(1, "allocate users");
	for (usr = svusr, i = nusers >> 1; --i >= 0; usr++) {
		for (usrn = users + (usr->uid&(nusers - 1));
		     usrn->name;
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
	int i;
	struct user *usr;
	const char *name;

	while (1) {
		for (usr = users + (uid&(nusers - 1)), i = nusers;
		     --i >= 0;
		    usr--) {
			if (!usr->name) {
				usr->uid = uid;

				if ((name = user_from_uid(uid, 1)) == NULL)
					asprintf(&usr->name, "#%u", uid);
				else
					usr->name = strdup(name);
				if (!usr->name)
					err(1, "allocate users");
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
	const struct user *u1 = v1, *u2 = v2;

	return u2->space - u1->space;
}

#define	sortusers(users)	(qsort((users), nusers, sizeof(struct user), \
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

#define	FSZCNT	512
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
	for (inode = 0; inode < maxino; inode++) {
		errno = 0;
		if ((dp = get_inode(fd, super, inode))
		    && !isfree(super, dp)
		    ) {
			sz = estimate ? virtualblocks(super, dp) :
			    actualblocks(super, dp);
			ksz = SIZE(sz);
			for (fsp = &fsizes; (fp = *fsp); fsp = &fp->fsz_next) {
				if (ksz < fp->fsz_last)
					break;
			}
			if (!fp || ksz < fp->fsz_first) {
				if (!(fp = (struct fsizes *)
				    malloc(sizeof(struct fsizes)))) {
					err(1, "alloc fsize structure");
				}
				fp->fsz_next = *fsp;
				*fsp = fp;
				fp->fsz_first = (ksz / FSZCNT) * FSZCNT;
				fp->fsz_last = fp->fsz_first + FSZCNT;
				for (i = FSZCNT; --i >= 0;) {
					fp->fsz_count[i] = 0;
					fp->fsz_sz[i] = 0;
				}
			}
			fp->fsz_count[ksz % FSZCNT]++;
			fp->fsz_sz[ksz % FSZCNT] += sz;
		} else if (errno)
			err(1, "%s", name);
	}
	sz = 0;
	for (fp = fsizes; fp; fp = fp->fsz_next) {
		for (i = 0; i < FSZCNT; i++) {
			if (fp->fsz_count[i])
				printf("%lld\t%llu\t%lld\n",
				    (long long)fp->fsz_first + i,
				    (unsigned long long)fp->fsz_count[i],
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

	setpassent(1);

	maxino = super->fs_ncg * super->fs_ipg - 1;
	for (inode = 0; inode < maxino; inode++) {
		errno = 0;
		if ((dp = get_inode(fd,super,inode))
		    && !isfree(super, dp))
			uses(DIP(super, dp, di_uid),
			    estimate ? virtualblocks(super, dp) :
				actualblocks(super, dp),
			    DIP(super, dp, di_atime));
		else if (errno)
			err(1, "%s", name);
	}
	if (!(usrs = calloc(nusers, sizeof(struct user))))
		err(1, "allocate users");
	memcpy(usrs, users, nusers * sizeof(struct user));
	sortusers(usrs);
	for (usr = usrs, n = nusers; --n >= 0 && usr->count; usr++) {
		printf("%14lld", SIZE(usr->space));
		if (count)
			printf("\t%5ld", usr->count);
		printf("\t%-8s", usr->name);
		if (unused)
			printf("\t%14lld\t%14lld\t%14lld",
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
	unsigned long long inode;
	ino_t inode1;
	ino_t maxino;
	union dinode *dp;

	maxino = super->fs_ncg * super->fs_ipg - 1;
	/* first skip the name of the filesystem */
	while ((c = getchar()) != EOF && (c < '0' || c > '9'))
		while ((c = getchar()) != EOF && c != '\n');
	ungetc(c, stdin);
	inode1 = -1;
	while (scanf("%llu", &inode) == 1) {
		if (inode > maxino) {
			fprintf(stderr, "invalid inode %llu\n",
			    (unsigned long long)inode);
			return;
		}
		errno = 0;
		if ((dp = get_inode(fd, super, inode)) && !isfree(super, dp)) {
			printf("%s\t", user(DIP(super, dp, di_uid))->name);
			/* now skip whitespace */
			while ((c = getchar()) == ' ' || c == '\t');
			/* and print out the remainder of the input line */
			while (c != EOF && c != '\n') {
				putchar(c);
				c = getchar();
			}
			putchar('\n');
			inode1 = inode;
		} else {
			if (errno)
				err(1, "%s", name);
			/* skip this line */
			while ((c = getchar()) != EOF && c != '\n')
				;
		}
		if (c == EOF)
			break;
	}
}

static void
usage(void)
{
	fprintf(stderr, "usage: quot [-acfhknv] [filesystem ...]\n");
	exit(1);
}

/*
 * Possible superblock locations ordered from most to least likely.
 */
static int sblock_try[] = SBLOCKSEARCH;
static char superblock[SBLOCKSIZE];

#define	max(a,b)	MAX((a),(b))
/*
 * Sanity checks for old file systems.
 * Stolen from <sys/lib/libsa/ufs.c>
 */
static void
ffs_oldfscompat(struct fs *fs)
{
	int i;

	fs->fs_npsect = max(fs->fs_npsect, fs->fs_nsect);	/* XXX */
	fs->fs_interleave = max(fs->fs_interleave, 1);		/* XXX */
	if (fs->fs_postblformat == FS_42POSTBLFMT)		/* XXX */
		fs->fs_nrpos = 8;				/* XXX */
	if (fs->fs_inodefmt < FS_44INODEFMT) {			/* XXX */
		quad_t sizepb = fs->fs_bsize;			/* XXX */
								/* XXX */
		fs->fs_maxfilesize = fs->fs_bsize * NDADDR - 1;	/* XXX */
		for (i = 0; i < NIADDR; i++) {			/* XXX */
			sizepb *= NINDIR(fs);			/* XXX */
			fs->fs_maxfilesize += sizepb;		/* XXX */
		}						/* XXX */
		fs->fs_qbmask = ~fs->fs_bmask;			/* XXX */
		fs->fs_qfmask = ~fs->fs_fmask;			/* XXX */
	}							/* XXX */
}

void
quot(char *name, char *mp)
{
	int i, fd;
	struct fs *fs;

	get_inode(-1, NULL, 0);		/* flush cache */
	inituser();
	initfsizes();
	/*
	 * XXX this is completely broken.  Of course you can't read a
	 * directory, well, not anymore.  How to fix this, though...
	 */
	if ((fd = open(name, O_RDONLY)) < 0) {
		warn("%s", name);
		return;
	}
	for (i = 0; sblock_try[i] != -1; i++) {
		if (lseek(fd, sblock_try[i], 0) != sblock_try[i]) {
			close(fd);
			return;
		}
		if (read(fd, superblock, SBLOCKSIZE) != SBLOCKSIZE) {
			close(fd);
			return;
		}
		fs = (struct fs *)superblock;
		if ((fs->fs_magic == FS_UFS1_MAGIC ||
		    (fs->fs_magic == FS_UFS2_MAGIC &&
		    fs->fs_sblockloc == sblock_try[i])) &&
		    fs->fs_bsize <= MAXBSIZE &&
		    fs->fs_bsize >= sizeof(struct fs))
			break;
	}
	if (sblock_try[i] == -1) {
		warnx("%s: not a BSD filesystem", name);
		close(fd);
		return;
	}
	ffs_oldfscompat(fs);
	printf("%s:", name);
	if (mp)
		printf(" (%s)", mp);
	putchar('\n');
	(*func)(fd, fs, name);
	close(fd);
}

int
main(int argc, char *argv[])
{
	int cnt, all, i;
	char dev[MNAMELEN], *nm, *mountpoint, *cp;
	struct statfs *mp;

	all = 0;
	func = douser;
	header = getbsize(&headerlen, &blocksize);
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
			case 'k':
				blocksize = 1024;
				break;
			case 'v':
				unused = 1;
				break;
			default:
				usage();
			}
		}
	}

	if (pledge("stdio rpath getpw", NULL) == -1)
		err(1, "pledge");

	cnt = getmntinfo(&mp, MNT_NOWAIT);
	if (all) {
		for (; --cnt >= 0; mp++) {
			if (strcmp(mp->f_fstypename, MOUNT_FFS) == 0 ||
			    strcmp(mp->f_fstypename, "ufs") == 0) {
				if ((nm = strrchr(mp->f_mntfromname, '/'))) {
					snprintf(dev, sizeof(dev), "%sr%s",
					    _PATH_DEV, nm + 1);
					nm = dev;
				} else
					nm = mp->f_mntfromname;
				quot(nm, mp->f_mntonname);
			}
		}
	}
	for (; --argc >= 0; argv++) {
		mountpoint = NULL;
		nm = *argv;

		/* Remove trailing slashes from name. */
		cp = nm + strlen(nm);
		while (*(--cp) == '/' && cp != nm)
			*cp = '\0';

		/* Look up the name in the mount table. */
		for (i = 0; i < cnt; i++) {
			/* Remove trailing slashes from name. */
			cp = mp[i].f_mntonname + strlen(mp[i].f_mntonname);
			while (*(--cp) == '/' && cp != mp[i].f_mntonname)
				*cp = '\0';

			if ((!strcmp(mp->f_fstypename, MOUNT_FFS) ||
			     !strcmp(mp->f_fstypename, MOUNT_MFS) ||
			     !strcmp(mp->f_fstypename, "ufs")) &&
			    strcmp(nm, mp[i].f_mntonname) == 0) {
				nm = mp[i].f_mntfromname;
				mountpoint = mp[i].f_mntonname;
				break;
			}
		}

		/* Make sure we have the raw device... */
		if (strncmp(nm, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0 &&
		    nm[sizeof(_PATH_DEV) - 1] != 'r') {
			snprintf(dev, sizeof(dev), "%sr%s", _PATH_DEV,
			    nm + sizeof(_PATH_DEV) - 1);
			nm = dev;
		}
		quot(nm, mountpoint);
	}
	exit(0);
}
