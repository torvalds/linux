/*	$OpenBSD: ncheck_ffs.c,v 1.56 2024/04/23 13:34:50 jsg Exp $	*/

/*-
 * Copyright (c) 1995, 1996 SigmaSoft, Th. Lockert <tholo@sigmasoft.com>
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
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1980, 1988, 1991, 1993
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

#include <sys/param.h>	/* MAXBSIZE DEV_BSIZE */
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <ufs/ffs/fs.h>
#include <ufs/ufs/dir.h>
#include <ufs/ufs/dinode.h>

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <fstab.h>
#include <errno.h>
#include <err.h>
#include <util.h>

#define DIP(dp, field) \
    ((sblock->fs_magic == FS_UFS1_MAGIC) ? \
    ((struct ufs1_dinode *)(dp))->field : \
    ((struct ufs2_dinode *)(dp))->field)

char	*disk;		/* name of the disk file */
char	rdisk[PATH_MAX];/* resolved name of the disk file */
int	diskfd;		/* disk file descriptor */
struct	fs *sblock;	/* the file system super block */
char	sblock_buf[MAXBSIZE];
int	sblock_try[] = SBLOCKSEARCH; /* possible superblock locations */
ufsino_t *ilist;	/* list of inodes to check */
int	ninodes;	/* number of inodes in list */
int	sflag;		/* only suid and special files */
int	aflag;		/* print the . and .. entries too */
int	mflag;		/* verbose output */
int	iflag;		/* specific inode */
char	*format;	/* output format */

struct disklabel lab;

struct icache_s {
	ufsino_t	ino;
	union {
		struct ufs1_dinode dp1;
		struct ufs2_dinode dp2;
	} di;
} *icache;
int	nicache;
int	maxicache;

void addinode(ufsino_t inum);
void *getino(ufsino_t inum);
void findinodes(ufsino_t);
void bread(daddr_t, char *, int);
__dead void usage(void);
void scanonedir(ufsino_t, const char *);
void dirindir(ufsino_t, daddr_t, int, off_t *, const char *);
void searchdir(ufsino_t, daddr_t, long, off_t, const char *);
int matchino(const void *, const void *);
int matchcache(const void *, const void *);
void cacheino(ufsino_t, void *);
void *cached(ufsino_t);
int main(int, char *[]);
char *rawname(char *);
void format_entry(const char *, struct direct *);

/*
 * Check to see if the indicated inodes are the same
 */
int
matchino(const void *key, const void *val)
{
	ufsino_t k = *(ufsino_t *)key;
	ufsino_t v = *(ufsino_t *)val;

	if (k < v)
		return -1;
	else if (k > v)
		return 1;
	return 0;
}

/*
 * Check if the indicated inode match the entry in the cache
 */
int
matchcache(const void *key, const void *val)
{
	ufsino_t	ino = *(ufsino_t *)key;
	struct icache_s	*ic = (struct icache_s *)val;

	if (ino < ic->ino)
		return -1;
	else if (ino > ic->ino)
		return 1;
	return 0;
}

/*
 * Add an inode to the cached entries
 */
void
cacheino(ufsino_t ino, void *dp)
{
	if (nicache == maxicache) {
		struct icache_s *newicache;

		/* grow exponentially */
		maxicache += 10 + maxicache/2;
		newicache = reallocarray(icache, maxicache, sizeof(*icache));
		if (newicache == NULL)
			errx(1, "malloc");
		icache = newicache;

	}
	icache[nicache].ino = ino;
	if (sblock->fs_magic == FS_UFS1_MAGIC)
		icache[nicache++].di.dp1 = *(struct ufs1_dinode *)dp;
	else
		icache[nicache++].di.dp2 = *(struct ufs2_dinode *)dp;
}

/*
 * Get a cached inode
 */
void *
cached(ufsino_t ino)
{
	struct icache_s *ic;
	void *dp = NULL;

	ic = bsearch(&ino, icache, nicache, sizeof(*icache), matchcache);
	if (ic != NULL) {
		if (sblock->fs_magic == FS_UFS1_MAGIC)
			dp = &ic->di.dp1;
		else
			dp = &ic->di.dp2;
	}
	return (dp);
}

/*
 * Walk the inode list for a filesystem to find all allocated inodes
 * Remember inodes we want to give information about and cache all
 * inodes pointing to directories
 */
void
findinodes(ufsino_t maxino)
{
	ufsino_t ino;
	void *dp;
	mode_t mode;

	for (ino = ROOTINO; ino < maxino; ino++) {
		dp = getino(ino);
		mode = DIP(dp, di_mode) & IFMT;
		if (!mode)
			continue;
		if (mode == IFDIR)
			cacheino(ino, dp);
		if (iflag ||
		    (sflag && (mode == IFDIR ||
		     ((DIP(dp, di_mode) & (ISGID | ISUID)) == 0 &&
		      (mode == IFREG || mode == IFLNK)))))
			continue;
		addinode(ino);
	}
}

/*
 * Get a specified inode from disk.  Attempt to minimize reads to once
 * per cylinder group
 */
void *
getino(ufsino_t inum)
{
	static char *itab = NULL;
	static daddr_t iblk = -1;
	void *dp;
	size_t dsize;

	if (inum < ROOTINO || inum >= sblock->fs_ncg * sblock->fs_ipg)
		return NULL;
	if ((dp = cached(inum)) != NULL)
		return dp;
	if (sblock->fs_magic == FS_UFS1_MAGIC)
		dsize = sizeof(struct ufs1_dinode);
	else
		dsize = sizeof(struct ufs2_dinode);
	if ((inum / sblock->fs_ipg) != iblk || itab == NULL) {
		iblk = inum / sblock->fs_ipg;
		if (itab == NULL &&
		    (itab = reallocarray(NULL, sblock->fs_ipg, dsize)) == NULL)
			errx(1, "no memory for inodes");
		bread(fsbtodb(sblock, cgimin(sblock, iblk)), itab,
		      sblock->fs_ipg * dsize);
	}
	return itab + (inum % sblock->fs_ipg) * dsize;
}

/*
 * Read a chunk of data from the disk.
 * Try to recover from hard errors by reading in sector sized pieces.
 * Error recovery is attempted at most BREADEMAX times before seeking
 * consent from the operator to continue.
 */
int	breaderrors = 0;
#define	BREADEMAX 32

void
bread(daddr_t blkno, char *buf, int size)
{
	off_t offset;
 	int cnt, i;
	u_int32_t secsize = lab.d_secsize;

	offset = blkno * DEV_BSIZE;

loop:
	if ((cnt = pread(diskfd, buf, size, offset)) == size)
		return;
	if (blkno + (size / DEV_BSIZE) >
	    fsbtodb(sblock, sblock->fs_ffs1_size)) {
		/*
		 * Trying to read the final fragment.
		 *
		 * NB - dump only works in TP_BSIZE blocks, hence
		 * rounds `DEV_BSIZE' fragments up to TP_BSIZE pieces.
		 * It should be smarter about not actually trying to
		 * read more than it can get, but for the time being
		 * we punt and scale back the read only when it gets
		 * us into trouble. (mkm 9/25/83)
		 */
		size -= secsize;
		goto loop;
	}
	if (cnt == -1)
		warnx("read error from %s: %s: [block %lld]: count=%d",
		    disk, strerror(errno), (long long)blkno, size);
	else
		warnx("short read error from %s: [block %lld]: count=%d, "
		    "got=%d", disk, (long long)blkno, size, cnt);
	if (++breaderrors > BREADEMAX)
		errx(1, "More than %d block read errors from %s", BREADEMAX,
		    disk);
	/*
	 * Zero buffer, then try to read each sector of buffer separately.
	 */
	memset(buf, 0, size);
	for (i = 0; i < size; i += secsize, buf += secsize) {
		if ((cnt = pread(diskfd, buf, secsize, offset + i)) ==
		    secsize)
			continue;
		if (cnt == -1) {
			warnx("read error from %s: %s: [sector %lld]: "
			    "count=%u", disk, strerror(errno),
			    (long long)(offset + i) / DEV_BSIZE, secsize);
			continue;
		}
		warnx("short read error from %s: [sector %lld]: count=%u, "
		    "got=%d", disk, (long long)(offset + i) / DEV_BSIZE,
		    secsize, cnt);
	}
}

/*
 * Add an inode to the in-memory list of inodes to dump
 */
void
addinode(ufsino_t ino)
{
	ufsino_t *newilist;

	newilist = reallocarray(ilist, ninodes + 1, sizeof(*ilist));
	if (newilist == NULL)
		errx(4, "not enough memory to allocate tables");
	ilist = newilist;
	ilist[ninodes] = ino;
	ninodes++;
}

/*
 * Scan the directory pointer at by ino
 */
void
scanonedir(ufsino_t ino, const char *path)
{
	void *dp;
	off_t filesize;
	int i;

	if ((dp = cached(ino)) == NULL)
		return;
	filesize = (off_t)DIP(dp, di_size);
	for (i = 0; filesize > 0 && i < NDADDR; i++) {
		if (DIP(dp, di_db[i]) != 0) {
			searchdir(ino, DIP(dp, di_db[i]),
			    sblksize(sblock, DIP(dp, di_size), i),
			    filesize, path);
		}
		filesize -= sblock->fs_bsize;
	}
	for (i = 0; filesize > 0 && i < NIADDR; i++) {
		if (DIP(dp, di_ib[i]))
			dirindir(ino, DIP(dp, di_ib[i]), i, &filesize, path);
	}
}

/*
 * Read indirect blocks, and pass the data blocks to be searched
 * as directories.
 */
void
dirindir(ufsino_t ino, daddr_t blkno, int ind_level, off_t *filesizep,
    const char *path)
{
	int i;
	void *idblk;

	if ((idblk = malloc(sblock->fs_bsize)) == NULL)
		errx(1, "dirindir: cannot allocate indirect memory.\n");
	bread(fsbtodb(sblock, blkno), idblk, (int)sblock->fs_bsize);
	if (ind_level <= 0) {
		for (i = 0; *filesizep > 0 && i < NINDIR(sblock); i++) {
			if (sblock->fs_magic == FS_UFS1_MAGIC)
				blkno = ((int32_t *)idblk)[i];
			else
				blkno = ((int64_t *)idblk)[i];
			if (blkno != 0)
				searchdir(ino, blkno, sblock->fs_bsize,
				    *filesizep, path);
			*filesizep -= sblock->fs_bsize;
		}
	} else {
		ind_level--;
		for (i = 0; *filesizep > 0 && i < NINDIR(sblock); i++) {
			if (sblock->fs_magic == FS_UFS1_MAGIC)
				blkno = ((int32_t *)idblk)[i];
			else
				blkno = ((int64_t *)idblk)[i];
			if (blkno != 0)
				dirindir(ino, blkno, ind_level, filesizep,
				    path);
		}
	}
	free(idblk);
}

/*
 * Scan a disk block containing directory information looking to see if
 * any of the entries are on the dump list and to see if the directory
 * contains any subdirectories.
 */
void
searchdir(ufsino_t ino, daddr_t blkno, long size, off_t filesize,
    const char *path)
{
	char *dblk;
	struct direct *dp;
	void *di;
	mode_t mode;
	char *npath;
	ufsino_t subino;
	long loc;

	if ((dblk = malloc(sblock->fs_bsize)) == NULL)
		errx(1, "searchdir: cannot allocate directory memory.");
	bread(fsbtodb(sblock, blkno), dblk, (int)size);
	if (filesize < size)
		size = filesize;
	for (loc = 0; loc < size; ) {
		dp = (struct direct *)(dblk + loc);
		if (dp->d_reclen == 0) {
			warnx("corrupted directory, inode %llu",
			    (unsigned long long)ino);
			break;
		}
		loc += dp->d_reclen;
		if (dp->d_ino == 0)
			continue;
		if (dp->d_name[0] == '.') {
			if (!aflag && (dp->d_name[1] == '\0' ||
			    (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
				continue;
		}
		di = getino(dp->d_ino);
		mode = DIP(di, di_mode) & IFMT;
		subino = dp->d_ino;
		if (bsearch(&subino, ilist, ninodes, sizeof(*ilist), matchino)) {
			if (format) {
				format_entry(path, dp);
			} else {
				if (mflag)
					printf("mode %-6o uid %-5u gid %-5u ino ",
					    DIP(di, di_mode), DIP(di, di_uid),
					    DIP(di, di_gid));
				printf("%-7llu %s/%s%s\n",
				    (unsigned long long)dp->d_ino, path,
				    dp->d_name, mode == IFDIR ? "/." : "");
			}
		}
		if (mode == IFDIR) {
			if (dp->d_name[0] == '.') {
				if (dp->d_name[1] == '\0' ||
				    (dp->d_name[1] == '.' && dp->d_name[2] == '\0'))
					continue;
			}
			if (asprintf(&npath, "%s/%s", path, dp->d_name) == -1)
				errx(1, "malloc");
			scanonedir(dp->d_ino, npath);
			free(npath);
		}
	}
	free(dblk);
}

char *
rawname(char *name)
{
	static char newname[PATH_MAX];
	char *p;

	if ((p = strrchr(name, '/')) == NULL)
		return name;
	*p = '\0';
	strlcpy(newname, name, sizeof newname - 2);
	*p++ = '/';
	strlcat(newname, "/r", sizeof newname);
	strlcat(newname, p, sizeof newname);
	return(newname);
}

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [-ams] [-f format] [-i number ...] filesystem\n",
	    __progname);
	exit(3);
}

int
main(int argc, char *argv[])
{
	struct stat stblock;
	struct fstab *fsp;
	unsigned long long ullval;
	ssize_t n;
	char *ep;
	int c, i;

	while ((c = getopt(argc, argv, "af:i:ms")) != -1)
		switch (c) {
		case 'a':
			aflag = 1;
			break;
		case 'i':
			iflag = 1;

			errno = 0;
			ullval = strtoull(optarg, &ep, 10);
			if (optarg[0] == '\0' || *ep != '\0')
				errx(1, "%s is not a number",
				    optarg);
			if ((errno == ERANGE && ullval == ULLONG_MAX) ||
			    (ufsino_t)ullval != ullval)
				errx(1, "%s is out of range",
				    optarg);
			addinode((ufsino_t)ullval);

			while (optind < argc) {
				errno = 0;
				ullval = strtoull(argv[optind], &ep, 10);
				if (argv[optind][0] == '\0' || *ep != '\0')
					break;
				if ((errno == ERANGE && ullval == ULLONG_MAX)
				    || (ufsino_t)ullval != ullval)
					errx(1, "%s is out of range",
					    argv[optind]);
				addinode((ufsino_t)ullval);
				optind++;
			}
			break;
		case 'f':
			format = optarg;
			break;
		case 'm':
			mflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		default:
			usage();
			exit(2);
		}
	if (optind != argc - 1 || (mflag && format))
		usage();

	disk = argv[optind];
	if ((diskfd = opendev(disk, O_RDONLY, 0, NULL)) >= 0) {
		if (fstat(diskfd, &stblock))
			err(1, "cannot stat %s", disk);
		if (S_ISCHR(stblock.st_mode))
			goto gotdev;
		close(diskfd);
	}

	if (realpath(disk, rdisk) == NULL)
		err(1, "cannot find real path for %s", disk);
	disk = rdisk;

	if (stat(disk, &stblock) == -1)
		err(1, "cannot stat %s", disk);

        if (S_ISBLK(stblock.st_mode)) {
		disk = rawname(disk);
	} else if (!S_ISCHR(stblock.st_mode)) {
		if ((fsp = getfsfile(disk)) == NULL)
			err(1, "could not find file system %s", disk);
                disk = rawname(fsp->fs_spec);
        }

	if ((diskfd = opendev(disk, O_RDONLY, 0, NULL)) == -1)
		err(1, "cannot open %s", disk);

gotdev:
	if (ioctl(diskfd, DIOCGDINFO, (char *)&lab) == -1)
		err(1, "ioctl (DIOCGDINFO)");
	if (ioctl(diskfd, DIOCGPDINFO, (char *)&lab) == -1)
		err(1, "ioctl (DIOCGPDINFO)");

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	sblock = (struct fs *)sblock_buf;
	for (i = 0; sblock_try[i] != -1; i++) {
		n = pread(diskfd, sblock, SBLOCKSIZE, (off_t)sblock_try[i]);
		if (n == SBLOCKSIZE && (sblock->fs_magic == FS_UFS1_MAGIC ||
		     (sblock->fs_magic == FS_UFS2_MAGIC &&
		      sblock->fs_sblockloc == sblock_try[i])) &&
		    sblock->fs_bsize <= MAXBSIZE &&
		    sblock->fs_bsize >= sizeof(struct fs))
			break;
	}
	if (sblock_try[i] == -1)
		errx(1, "cannot find filesystem superblock");

	findinodes(sblock->fs_ipg * sblock->fs_ncg);
	if (!format)
		printf("%s:\n", disk);
	scanonedir(ROOTINO, "");
	close(diskfd);
	exit (0);
}

void
format_entry(const char *path, struct direct *dp)
{
	static size_t size;
	static char *buf;
	char *src, *dst, *newbuf;
	int len;

	if (buf == NULL) {
		if ((buf = malloc(LINE_MAX)) == NULL)
			err(1, "malloc");
		size = LINE_MAX;
	}

	for (src = format, dst = buf; *src; src++) {
		/* Need room for at least one character in buf. */
		if (size <= dst - buf) {
		    expand_buf:
			if ((newbuf = reallocarray(buf, size, 2)) == NULL)
				err(1, "realloc");
			buf = newbuf;
			size = size * 2;
		}
		if (src[0] =='\\') {
			switch (src[1]) {
			case 'I':
				len = snprintf(dst, size - (dst - buf), "%llu",
				    (unsigned long long)dp->d_ino);
				if (len < 0 || len >= size - (dst - buf))
					goto expand_buf;
				dst += len;
				break;
			case 'P':
				len = snprintf(dst, size - (dst - buf), "%s/%s",
				    path, dp->d_name);
				if (len < 0 || len >= size - (dst - buf))
					goto expand_buf;
				dst += len;
				break;
			case '\\':
				*dst++ = '\\';
				break;
			case '0':
				/* XXX - support other octal numbers? */
				*dst++ = '\0';
				break;
			case 'a':
				*dst++ = '\a';
				break;
			case 'b':
				*dst++ = '\b';
				break;
			case 'e':
				*dst++ = '\e';
				break;
			case 'f':
				*dst++ = '\f';
				break;
			case 'n':
				*dst++ = '\n';
				break;
			case 'r':
				*dst++ = '\r';
				break;
			case 't':
				*dst++ = '\t';
				break;
			case 'v':
				*dst++ = '\v';
				break;
			default:
				*dst++ = src[1];
				break;
			}
			src++;
		} else
			*dst++ = *src;
	}
	fwrite(buf, dst - buf, 1, stdout);
}
