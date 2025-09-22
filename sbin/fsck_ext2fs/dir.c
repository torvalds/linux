/*	$OpenBSD: dir.c,v 1.20 2015/01/16 06:39:57 deraadt Exp $	*/
/*	$NetBSD: dir.c,v 1.5 2000/01/28 16:01:46 bouyer Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
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

#include <sys/param.h>	/* DEV_BSIZE roundup */
#include <sys/time.h>
#include <ufs/ufs/dir.h>
#include <ufs/ext2fs/ext2fs_dinode.h>
#include <ufs/ext2fs/ext2fs_dir.h>
#include <ufs/ext2fs/ext2fs.h>

#include <ufs/ufs/dinode.h> /* for IFMT & friends */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "fsck.h"
#include "fsutil.h"
#include "extern.h"

char	*lfname = "lost+found";
int	lfmode = 01777;
struct	ext2fs_dirtemplate emptydir = { 0, DIRBLKSIZ };
struct	ext2fs_dirtemplate dirhead = {
	0, 12, 1, EXT2_FT_DIR, ".",
	0, DIRBLKSIZ - 12, 2, EXT2_FT_DIR, ".."
};
#undef DIRBLKSIZ

static int expanddir(struct ext2fs_dinode *, char *);
static void freedir(ino_t, ino_t);
static struct ext2fs_direct *fsck_readdir(struct inodesc *);
static struct bufarea *getdirblk(daddr32_t, long);
static int lftempname(char *, ino_t);
static int mkentry(struct inodesc *);
static int chgino(struct  inodesc *);

/*
 * Propagate connected state through the tree.
 */
void
propagate(void)
{
	struct inoinfo **inpp, *inp, *pinp;
	struct inoinfo **inpend;

	/*
	 * Create a list of children for each directory.
	 */
	inpend = &inpsort[inplast];
	for (inpp = inpsort; inpp < inpend; inpp++) {
		inp = *inpp;
		if (inp->i_parent == 0 ||
		    inp->i_number == EXT2_ROOTINO)
			continue;
		pinp = getinoinfo(inp->i_parent);
		inp->i_parentp = pinp;
		inp->i_sibling = pinp->i_child;
		pinp->i_child = inp;
	}
	inp = getinoinfo(EXT2_ROOTINO);
	while (inp) {
		statemap[inp->i_number] = DFOUND;
		if (inp->i_child &&
		    statemap[inp->i_child->i_number] == DSTATE)
			inp = inp->i_child;
		else if (inp->i_sibling)
			inp = inp->i_sibling;
		else
			inp = inp->i_parentp;
	}
}

/*
 * Scan each entry in a directory block.
 */
int
dirscan(struct inodesc *idesc)
{
	struct ext2fs_direct *dp;
	struct bufarea *bp;
	int dsize, n;
	long blksiz;
	char *dbuf = NULL;

	if ((dbuf = malloc(sblock.e2fs_bsize)) == NULL) {
		fprintf(stderr, "out of memory");
		exit(8);
	}

	if (idesc->id_type != DATA)
		errexit("wrong type to dirscan %d\n", idesc->id_type);
	if (idesc->id_entryno == 0 &&
	    (idesc->id_filesize & (sblock.e2fs_bsize - 1)) != 0)
		idesc->id_filesize = roundup(idesc->id_filesize, sblock.e2fs_bsize);
	blksiz = idesc->id_numfrags * sblock.e2fs_bsize;
	if (chkrange(idesc->id_blkno, idesc->id_numfrags)) {
		idesc->id_filesize -= blksiz;
		free(dbuf);
		return (SKIP);
	}
	idesc->id_loc = 0;
	for (dp = fsck_readdir(idesc); dp != NULL; dp = fsck_readdir(idesc)) {
		dsize = letoh16(dp->e2d_reclen);
		memcpy(dbuf, dp, (size_t)dsize);
		idesc->id_dirp = (struct ext2fs_direct *)dbuf;
		if ((n = (*idesc->id_func)(idesc)) & ALTERED) {
			bp = getdirblk(idesc->id_blkno, blksiz);
			memcpy(bp->b_un.b_buf + idesc->id_loc - dsize, dbuf,
			    (size_t)dsize);
			dirty(bp);
			sbdirty();
		}
		if (n & STOP) {
			free(dbuf);
			return (n);
		}
	}
	free(dbuf);
	return (idesc->id_filesize > 0 ? KEEPON : STOP);
}

/*
 * get next entry in a directory.
 */
static struct ext2fs_direct *
fsck_readdir(struct inodesc *idesc)
{
	struct ext2fs_direct *dp, *ndp;
	struct bufarea *bp;
	long size, blksiz, fix, dploc;

	blksiz = idesc->id_numfrags * sblock.e2fs_bsize;
	bp = getdirblk(idesc->id_blkno, blksiz);
	if (idesc->id_loc % sblock.e2fs_bsize == 0 && idesc->id_filesize > 0 &&
	    idesc->id_loc < blksiz) {
		dp = (struct ext2fs_direct *)(bp->b_un.b_buf + idesc->id_loc);
		if (dircheck(idesc, dp))
			goto dpok;
		if (idesc->id_fix == IGNORE)
			return (0);
		fix = dofix(idesc, "DIRECTORY CORRUPTED");
		bp = getdirblk(idesc->id_blkno, blksiz);
		dp = (struct ext2fs_direct *)(bp->b_un.b_buf + idesc->id_loc);
		dp->e2d_reclen = htole16(sblock.e2fs_bsize);
		dp->e2d_ino = 0;
		dp->e2d_namlen = 0;
		dp->e2d_type = 0;
		dp->e2d_name[0] = '\0';
		if (fix)
			dirty(bp);
		idesc->id_loc += sblock.e2fs_bsize;
		idesc->id_filesize -= sblock.e2fs_bsize;
		return (dp);
	}
dpok:
	if (idesc->id_filesize <= 0 || idesc->id_loc >= blksiz)
		return NULL;
	dploc = idesc->id_loc;
	dp = (struct ext2fs_direct *)(bp->b_un.b_buf + dploc);
	idesc->id_loc += letoh16(dp->e2d_reclen);
	idesc->id_filesize -= letoh16(dp->e2d_reclen);
	if ((idesc->id_loc % sblock.e2fs_bsize) == 0)
		return (dp);
	ndp = (struct ext2fs_direct *)(bp->b_un.b_buf + idesc->id_loc);
	if (idesc->id_loc < blksiz && idesc->id_filesize > 0 &&
	    dircheck(idesc, ndp) == 0) {
		size = sblock.e2fs_bsize - (idesc->id_loc % sblock.e2fs_bsize);
		idesc->id_loc += size;
		idesc->id_filesize -= size;
		if (idesc->id_fix == IGNORE)
			return (0);
		fix = dofix(idesc, "DIRECTORY CORRUPTED");
		bp = getdirblk(idesc->id_blkno, blksiz);
		dp = (struct ext2fs_direct *)(bp->b_un.b_buf + dploc);
		dp->e2d_reclen = htole16(letoh16(dp->e2d_reclen) + size);
		if (fix)
			dirty(bp);
	}
	return (dp);
}

/*
 * Verify that a directory entry is valid.
 * This is a superset of the checks made in the kernel.
 */
int
dircheck(struct inodesc *idesc, struct ext2fs_direct *dp)
{
	int size;
	char *cp;
	int spaceleft;
	u_int16_t reclen = letoh16(dp->e2d_reclen);

	spaceleft = sblock.e2fs_bsize - (idesc->id_loc % sblock.e2fs_bsize);
	if (letoh32(dp->e2d_ino) > maxino ||
	    reclen == 0 ||
	    reclen > spaceleft ||
	    (reclen & 0x3) != 0)
		return (0);
	if (dp->e2d_ino == 0)
		return (1);
	if (sblock.e2fs.e2fs_rev < E2FS_REV1 ||
	    (sblock.e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE) == 0)
		if (dp->e2d_type != 0)
			return (1);
	size = EXT2FS_DIRSIZ(dp->e2d_namlen);
	if (reclen < size ||
	    idesc->id_filesize < size ||
	    dp->e2d_namlen > EXT2FS_MAXNAMLEN)
		return (0);
	for (cp = dp->e2d_name, size = 0; size < dp->e2d_namlen; size++)
		if (*cp == '\0' || (*cp++ == '/'))
			return (0);
	return (1);
}

void
direrror(ino_t ino, char *errmesg)
{

	fileerror(ino, ino, errmesg);
}

void
fileerror(ino_t cwd, ino_t ino, char *errmesg)
{
	struct ext2fs_dinode *dp;
	char pathbuf[PATH_MAX + 1];

	pwarn("%s ", errmesg);
	pinode(ino);
	printf("\n");
	getpathname(pathbuf, sizeof pathbuf, cwd, ino);
	if ((ino < EXT2_FIRSTINO && ino != EXT2_ROOTINO) || ino > maxino) {
		pfatal("NAME=%s\n", pathbuf);
		return;
	}
	dp = ginode(ino);
	if (ftypeok(dp))
		pfatal("%s=%s\n",
		    (letoh16(dp->e2di_mode) & IFMT) == IFDIR ? "DIR" : "FILE", pathbuf);
	else
		pfatal("NAME=%s\n", pathbuf);
}

void
adjust(struct inodesc *idesc, short lcnt)
{
	struct ext2fs_dinode *dp;

	dp = ginode(idesc->id_number);
	if (letoh16(dp->e2di_nlink) == lcnt) {
		if (linkup(idesc->id_number, (ino_t)0) == 0)
			clri(idesc, "UNREF", 0);
	} else {
		pwarn("LINK COUNT %s", (lfdir == idesc->id_number) ? lfname :
			((letoh16(dp->e2di_mode) & IFMT) == IFDIR ? "DIR" : "FILE"));
		pinode(idesc->id_number);
		printf(" COUNT %d SHOULD BE %d",
			letoh16(dp->e2di_nlink), letoh16(dp->e2di_nlink) - lcnt);
		if (preen) {
			if (lcnt < 0) {
				printf("\n");
				pfatal("LINK COUNT INCREASING");
			}
			printf(" (ADJUSTED)\n");
		}
		if (preen || reply("ADJUST") == 1) {
			dp->e2di_nlink = htole16(letoh16(dp->e2di_nlink) - lcnt);
			inodirty();
		}
	}
}

static int
mkentry(struct inodesc *idesc)
{
	struct ext2fs_direct *dirp = idesc->id_dirp;
	struct ext2fs_direct newent;
	int newlen, oldlen;

	newent.e2d_type = EXT2_FT_UNKNOWN;
	newent.e2d_namlen = strlen(idesc->id_name);
	if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
	    (sblock.e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE))
		newent.e2d_type = inot2ext2dt(typemap[idesc->id_parent]);
	newlen = EXT2FS_DIRSIZ(newent.e2d_namlen);
	if (dirp->e2d_ino != 0)
		oldlen = EXT2FS_DIRSIZ(dirp->e2d_namlen);
	else
		oldlen = 0;
	if (letoh16(dirp->e2d_reclen) - oldlen < newlen)
		return (KEEPON);
	newent.e2d_reclen = htole16(letoh16(dirp->e2d_reclen) - oldlen);
	dirp->e2d_reclen = htole16(oldlen);
	dirp = (struct ext2fs_direct *)(((char *)dirp) + oldlen);
	dirp->e2d_ino = htole32(idesc->id_parent); /* ino to be entered is in id_parent */
	dirp->e2d_reclen = newent.e2d_reclen;
	dirp->e2d_namlen = newent.e2d_namlen;
	dirp->e2d_type = newent.e2d_type;
	memcpy(dirp->e2d_name, idesc->id_name, (size_t)(dirp->e2d_namlen));
	return (ALTERED|STOP);
}

static int
chgino(struct inodesc *idesc)
{
	struct ext2fs_direct *dirp = idesc->id_dirp;
	u_int16_t namlen = dirp->e2d_namlen;

	if (strlen(idesc->id_name) != namlen ||
		strncmp(dirp->e2d_name, idesc->id_name, (int)namlen))
		return (KEEPON);
	dirp->e2d_ino = htole32(idesc->id_parent);
	if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
	    (sblock.e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE))
		dirp->e2d_type = inot2ext2dt(typemap[idesc->id_parent]);
	else
		dirp->e2d_type = 0;
	return (ALTERED|STOP);
}

int
linkup(ino_t orphan, ino_t parentdir)
{
	struct ext2fs_dinode *dp;
	int lostdir;
	ino_t oldlfdir;
	struct inodesc idesc;
	char tempname[BUFSIZ];

	memset(&idesc, 0, sizeof(struct inodesc));
	dp = ginode(orphan);
	lostdir = (letoh16(dp->e2di_mode) & IFMT) == IFDIR;
	pwarn("UNREF %s ", lostdir ? "DIR" : "FILE");
	pinode(orphan);
	if (preen && inosize(dp) == 0)
		return (0);
	if (preen)
		printf(" (RECONNECTED)\n");
	else
		if (reply("RECONNECT") == 0)
			return (0);
	if (lfdir == 0) {
		dp = ginode(EXT2_ROOTINO);
		idesc.id_name = lfname;
		idesc.id_type = DATA;
		idesc.id_func = findino;
		idesc.id_number = EXT2_ROOTINO;
		if ((ckinode(dp, &idesc) & FOUND) != 0) {
			lfdir = idesc.id_parent;
		} else {
			pwarn("NO lost+found DIRECTORY");
			if (preen || reply("CREATE")) {
				lfdir = allocdir(EXT2_ROOTINO, (ino_t)0, lfmode);
				if (lfdir != 0) {
					if (makeentry(EXT2_ROOTINO, lfdir, lfname) != 0) {
						if (preen)
							printf(" (CREATED)\n");
					} else {
						freedir(lfdir, EXT2_ROOTINO);
						lfdir = 0;
						if (preen)
							printf("\n");
					}
				}
			}
		}
		if (lfdir == 0) {
			pfatal("SORRY. CANNOT CREATE lost+found DIRECTORY");
			printf("\n\n");
			return (0);
		}
	}
	dp = ginode(lfdir);
	if ((letoh16(dp->e2di_mode) & IFMT) != IFDIR) {
		pfatal("lost+found IS NOT A DIRECTORY");
		if (reply("REALLOCATE") == 0)
			return (0);
		oldlfdir = lfdir;
		if ((lfdir = allocdir(EXT2_ROOTINO, (ino_t)0, lfmode)) == 0) {
			pfatal("SORRY. CANNOT CREATE lost+found DIRECTORY\n\n");
			return (0);
		}
		if ((changeino(EXT2_ROOTINO, lfname, lfdir) & ALTERED) == 0) {
			pfatal("SORRY. CANNOT CREATE lost+found DIRECTORY\n\n");
			return (0);
		}
		inodirty();
		idesc.id_type = ADDR;
		idesc.id_func = pass4check;
		idesc.id_number = oldlfdir;
		adjust(&idesc, lncntp[oldlfdir] + 1);
		lncntp[oldlfdir] = 0;
		dp = ginode(lfdir);
	}
	if (statemap[lfdir] != DFOUND) {
		pfatal("SORRY. NO lost+found DIRECTORY\n\n");
		return (0);
	}
	(void)lftempname(tempname, orphan);
	if (makeentry(lfdir, orphan, tempname) == 0) {
		pfatal("SORRY. NO SPACE IN lost+found DIRECTORY");
		printf("\n\n");
		return (0);
	}
	lncntp[orphan]--;
	if (lostdir) {
		if ((changeino(orphan, "..", lfdir) & ALTERED) == 0 &&
		    parentdir != (ino_t)-1)
			(void)makeentry(orphan, lfdir, "..");
		dp = ginode(lfdir);
		dp->e2di_nlink = htole16(letoh16(dp->e2di_nlink) +1);
		inodirty();
		lncntp[lfdir]++;
		pwarn("DIR I=%llu CONNECTED. ", (unsigned long long)orphan);
		if (parentdir != (ino_t)-1)
			printf("PARENT WAS I=%llu\n",
			    (unsigned long long)parentdir);
		if (preen == 0)
			printf("\n");
	}
	return (1);
}

/*
 * fix an entry in a directory.
 */
int
changeino(ino_t dir, char *name, ino_t newnum)
{
	struct inodesc idesc;

	memset(&idesc, 0, sizeof(struct inodesc));
	idesc.id_type = DATA;
	idesc.id_func = chgino;
	idesc.id_number = dir;
	idesc.id_fix = DONTKNOW;
	idesc.id_name = name;
	idesc.id_parent = newnum;	/* new value for name */
	return (ckinode(ginode(dir), &idesc));
}

/*
 * make an entry in a directory
 */
int
makeentry(ino_t parent, ino_t ino, char *name)
{
	struct ext2fs_dinode *dp;
	struct inodesc idesc;
	char pathbuf[PATH_MAX + 1];

	if ((parent < EXT2_FIRSTINO && parent != EXT2_ROOTINO)
		|| parent >= maxino ||
	    (ino < EXT2_FIRSTINO && ino < EXT2_ROOTINO) || ino >= maxino)
		return (0);
	memset(&idesc, 0, sizeof(struct inodesc));
	idesc.id_type = DATA;
	idesc.id_func = mkentry;
	idesc.id_number = parent;
	idesc.id_parent = ino;	/* this is the inode to enter */
	idesc.id_fix = DONTKNOW;
	idesc.id_name = name;
	dp = ginode(parent);
	if (inosize(dp) % sblock.e2fs_bsize) {
		inossize(dp, roundup(inosize(dp), sblock.e2fs_bsize));
		inodirty();
	}
	if ((ckinode(dp, &idesc) & ALTERED) != 0)
		return (1);
	getpathname(pathbuf, sizeof pathbuf, parent, parent);
	dp = ginode(parent);
	if (expanddir(dp, pathbuf) == 0)
		return (0);
	return (ckinode(dp, &idesc) & ALTERED);
}

/*
 * Attempt to expand the size of a directory
 */
static int
expanddir(struct ext2fs_dinode *dp, char *name)
{
	daddr32_t lastbn, newblk;
	struct bufarea *bp;
	char *firstblk;

	lastbn = lblkno(&sblock, inosize(dp));
	if (lastbn >= NDADDR - 1 || letoh32(dp->e2di_blocks[lastbn]) == 0 ||
		inosize(dp) == 0)
		return (0);
	if ((newblk = allocblk()) == 0)
		return (0);
	dp->e2di_blocks[lastbn + 1] = dp->e2di_blocks[lastbn];
	dp->e2di_blocks[lastbn] = htole32(newblk);
	inossize(dp, inosize(dp) + sblock.e2fs_bsize);
	dp->e2di_nblock = htole32(letoh32(dp->e2di_nblock) + 1);
	bp = getdirblk(letoh32(dp->e2di_blocks[lastbn + 1]),
		sblock.e2fs_bsize);
	if (bp->b_errs)
		goto bad;
	if ((firstblk = malloc(sblock.e2fs_bsize)) == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(8);
	}
	memcpy(firstblk, bp->b_un.b_buf, sblock.e2fs_bsize);
	bp = getdirblk(newblk, sblock.e2fs_bsize);
	if (bp->b_errs) {
		free(firstblk);
		goto bad;
	}
	memcpy(bp->b_un.b_buf, firstblk, sblock.e2fs_bsize);
	free(firstblk);
	dirty(bp);
	bp = getdirblk(letoh32(dp->e2di_blocks[lastbn + 1]),
		sblock.e2fs_bsize);
	if (bp->b_errs)
		goto bad;
	emptydir.dot_reclen = htole16(sblock.e2fs_bsize);
	memcpy(bp->b_un.b_buf, &emptydir, sizeof emptydir);
	pwarn("NO SPACE LEFT IN %s", name);
	if (preen)
		printf(" (EXPANDED)\n");
	else if (reply("EXPAND") == 0)
		goto bad;
	dirty(bp);
	inodirty();
	return (1);
bad:
	dp->e2di_blocks[lastbn] = dp->e2di_blocks[lastbn + 1];
	dp->e2di_blocks[lastbn + 1] = 0;
	dp->e2di_size = htole32(letoh32(dp->e2di_size) - sblock.e2fs_bsize);
	inossize(dp, inosize(dp) - sblock.e2fs_bsize);
	dp->e2di_nblock = htole32(letoh32(dp->e2di_nblock) - 1);
	freeblk(newblk);
	return (0);
}

/*
 * allocate a new directory
 */
int
allocdir(ino_t parent, ino_t request, int mode)
{
	ino_t ino;
	struct ext2fs_dinode *dp;
	struct bufarea *bp;
	struct ext2fs_dirtemplate *dirp;

	ino = allocino(request, IFDIR|mode);
	dirhead.dot_reclen = htole16(12); /* XXX */
	dirhead.dotdot_reclen = htole16(sblock.e2fs_bsize - 12); /* XXX */
	dirhead.dot_namlen = 1;
	if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
	    (sblock.e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE))
		dirhead.dot_type = EXT2_FT_DIR;
	else
		dirhead.dot_type = 0;
	dirhead.dotdot_namlen = 2;
	if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
	    (sblock.e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE))
		dirhead.dotdot_type = EXT2_FT_DIR;
	else
		dirhead.dotdot_type = 0;
	dirp = &dirhead;
	dirp->dot_ino = htole32(ino);
	dirp->dotdot_ino = htole32(parent);
	dp = ginode(ino);
	bp = getdirblk(letoh32(dp->e2di_blocks[0]), sblock.e2fs_bsize);
	if (bp->b_errs) {
		freeino(ino);
		return (0);
	}
	memcpy(bp->b_un.b_buf, dirp, sizeof(struct ext2fs_dirtemplate));
	dirty(bp);
	dp->e2di_nlink = htole16(2);
	inodirty();
	if (ino == EXT2_ROOTINO) {
		lncntp[ino] = letoh16(dp->e2di_nlink);
		cacheino(dp, ino);
		return(ino);
	}
	if (statemap[parent] != DSTATE && statemap[parent] != DFOUND) {
		freeino(ino);
		return (0);
	}
	cacheino(dp, ino);
	statemap[ino] = statemap[parent];
	if (statemap[ino] == DSTATE) {
		lncntp[ino] = letoh16(dp->e2di_nlink);
		lncntp[parent]++;
	}
	dp = ginode(parent);
	dp->e2di_nlink = htole16(letoh16(dp->e2di_nlink) + 1);
	inodirty();
	return (ino);
}

/*
 * free a directory inode
 */
static void
freedir(ino_t ino, ino_t parent)
{
	struct ext2fs_dinode *dp;

	if (ino != parent) {
		dp = ginode(parent);
		dp->e2di_nlink = htole16(letoh16(dp->e2di_nlink) - 1);
		inodirty();
	}
	freeino(ino);
}

/*
 * generate a temporary name for the lost+found directory.
 */
static int
lftempname(char *bufp, ino_t ino)
{
	ino_t in;
	char *cp;
	int namlen;

	cp = bufp + 2;
	for (in = maxino; in > 0; in /= 10)
		cp++;
	*--cp = 0;
	namlen = cp - bufp;
	in = ino;
	while (cp > bufp) {
		*--cp = (in % 10) + '0';
		in /= 10;
	}
	*cp = '#';
	return (namlen);
}

/*
 * Get a directory block.
 * Insure that it is held until another is requested.
 */
static struct bufarea *
getdirblk(daddr32_t blkno, long size)
{

	if (pdirbp != 0)
		pdirbp->b_flags &= ~B_INUSE;
	pdirbp = getdatablk(blkno, size);
	return (pdirbp);
}
