/*	$OpenBSD: dir.c,v 1.35 2024/02/03 18:51:57 beck Exp $	*/
/*	$NetBSD: dir.c,v 1.20 1996/09/27 22:45:11 christos Exp $	*/

/*
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

#include <sys/param.h>	/* DEV_BSIZE roundup btodb */
#include <sys/time.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "fsck.h"
#include "fsutil.h"
#include "extern.h"

char	*lfname = "lost+found";
int	lfmode = 01700;
struct	dirtemplate emptydir = { 0, DIRBLKSIZ };
struct	dirtemplate dirhead = {
	0, 12, DT_DIR, 1, ".",
	0, DIRBLKSIZ - 12, DT_DIR, 2, ".."
};

static int expanddir(union dinode *, char *);
static void freedir(ino_t, ino_t);
static struct direct *fsck_readdir(struct inodesc *);
static struct bufarea *getdirblk(daddr_t, long);
static int lftempname(char *, ino_t);
static int mkentry(struct inodesc *);
static int chgino(struct  inodesc *);

/*
 * Propagate connected state through the tree.
 */
void
propagate(ino_t inumber)
{
	struct	inoinfo *inp;
	char	state;

	inp = getinoinfo(inumber);
	state = GET_ISTATE(inp->i_number);
	for (;;) {
		SET_ISTATE(inp->i_number, state);
		if (inp->i_child &&
		    GET_ISTATE(inp->i_child->i_number) != state)
			inp = inp->i_child;
		else if (inp->i_number == inumber)
			break;
		else if (inp->i_sibling)
			inp = inp->i_sibling;
		else
			inp = getinoinfo(inp->i_parent);
	}
}

/*
 * Scan each entry in a directory block.
 */
int
dirscan(struct inodesc *idesc)
{
	struct direct *dp;
	struct bufarea *bp;
	int dsize, n;
	long blksiz;
	char dbuf[DIRBLKSIZ];

	if (idesc->id_type != DATA)
		errexit("wrong type to dirscan %d\n", idesc->id_type);
	if (idesc->id_entryno == 0 &&
	    (idesc->id_filesize & (DIRBLKSIZ - 1)) != 0)
		idesc->id_filesize = roundup(idesc->id_filesize, DIRBLKSIZ);
	blksiz = idesc->id_numfrags * sblock.fs_fsize;
	if (chkrange(idesc->id_blkno, idesc->id_numfrags)) {
		idesc->id_filesize -= blksiz;
		return (SKIP);
	}
	idesc->id_loc = 0;
	for (dp = fsck_readdir(idesc); dp != NULL; dp = fsck_readdir(idesc)) {
		dsize = dp->d_reclen;
		memcpy(dbuf, dp, (size_t)dsize);
		idesc->id_dirp = (struct direct *)dbuf;
		if ((n = (*idesc->id_func)(idesc)) & ALTERED) {
			bp = getdirblk(idesc->id_blkno, blksiz);
			memcpy(bp->b_un.b_buf + idesc->id_loc - dsize, dbuf,
			    (size_t)dsize);
			dirty(bp);
			sbdirty();
		}
		if (n & STOP)
			return (n);
	}
	return (idesc->id_filesize > 0 ? KEEPON : STOP);
}

/*
 * get next entry in a directory.
 */
static struct direct *
fsck_readdir(struct inodesc *idesc)
{
	struct direct *dp, *ndp;
	struct bufarea *bp;
	long size, blksiz, fix, dploc;

	blksiz = idesc->id_numfrags * sblock.fs_fsize;
	bp = getdirblk(idesc->id_blkno, blksiz);
	if (idesc->id_loc % DIRBLKSIZ == 0 && idesc->id_filesize > 0 &&
	    idesc->id_loc < blksiz) {
		dp = (struct direct *)(bp->b_un.b_buf + idesc->id_loc);
		if (dircheck(idesc, dp))
			goto dpok;
		if (idesc->id_fix == IGNORE)
			return (0);
		fix = dofix(idesc, "DIRECTORY CORRUPTED");
		bp = getdirblk(idesc->id_blkno, blksiz);
		dp = (struct direct *)(bp->b_un.b_buf + idesc->id_loc);
		dp->d_reclen = DIRBLKSIZ;
		dp->d_ino = 0;
		dp->d_type = 0;
		dp->d_namlen = 0;
		dp->d_name[0] = '\0';
		if (fix)
			dirty(bp);
		idesc->id_loc += DIRBLKSIZ;
		idesc->id_filesize -= DIRBLKSIZ;
		return (dp);
	}
dpok:
	if (idesc->id_filesize <= 0 || idesc->id_loc >= blksiz)
		return NULL;
	dploc = idesc->id_loc;
	dp = (struct direct *)(bp->b_un.b_buf + dploc);
	idesc->id_loc += dp->d_reclen;
	idesc->id_filesize -= dp->d_reclen;
	if ((idesc->id_loc % DIRBLKSIZ) == 0)
		return (dp);
	ndp = (struct direct *)(bp->b_un.b_buf + idesc->id_loc);
	if (idesc->id_loc < blksiz && idesc->id_filesize > 0 &&
	    dircheck(idesc, ndp) == 0) {
		size = DIRBLKSIZ - (idesc->id_loc % DIRBLKSIZ);
		idesc->id_loc += size;
		idesc->id_filesize -= size;
		if (idesc->id_fix == IGNORE)
			return (0);
		fix = dofix(idesc, "DIRECTORY CORRUPTED");
		bp = getdirblk(idesc->id_blkno, blksiz);
		dp = (struct direct *)(bp->b_un.b_buf + dploc);
		dp->d_reclen += size;
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
dircheck(struct inodesc *idesc, struct direct *dp)
{
	int size;
	char *cp;
	u_char namlen, type;
	int spaceleft;

	spaceleft = DIRBLKSIZ - (idesc->id_loc % DIRBLKSIZ);
	if (dp->d_ino >= maxino ||
	    dp->d_reclen == 0 ||
	    dp->d_reclen > spaceleft ||
	    (dp->d_reclen & 0x3) != 0)
		return (0);
	if (dp->d_ino == 0)
		return (1);
	size = DIRSIZ(dp);
	namlen = dp->d_namlen;
	type = dp->d_type;
	if (dp->d_reclen < size ||
	    idesc->id_filesize < size ||
	    type > 15)
		return (0);
	for (cp = dp->d_name, size = 0; size < namlen; size++)
		if (*cp == '\0' || (*cp++ == '/'))
			return (0);
	if (*cp != '\0')
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
	union dinode *dp;
	char pathbuf[PATH_MAX + 1];

	pwarn("%s ", errmesg);
	pinode(ino);
	printf("\n");
	getpathname(pathbuf, sizeof pathbuf, cwd, ino);
	if (ino < ROOTINO || ino > maxino) {
		pfatal("NAME=%s\n", pathbuf);
		return;
	}
	dp = ginode(ino);
	if (ftypeok(dp))
		pfatal("%s=%s\n",
		    (DIP(dp, di_mode) & IFMT) == IFDIR ? "DIR" : "FILE",
		    pathbuf);
	else
		pfatal("NAME=%s\n", pathbuf);
}

void
adjust(struct inodesc *idesc, short lcnt)
{
	union dinode *dp;

	dp = ginode(idesc->id_number);
	if (DIP(dp, di_nlink) == lcnt) {
		if (linkup(idesc->id_number, 0) == 0)
			clri(idesc, "UNREF", 0);
	} else {
		pwarn("LINK COUNT %s", (lfdir == idesc->id_number) ? lfname :
			((DIP(dp, di_mode) & IFMT) == IFDIR ? "DIR" : "FILE"));
		pinode(idesc->id_number);
		printf(" COUNT %d SHOULD BE %d", DIP(dp, di_nlink),
		    DIP(dp, di_nlink) - lcnt);
		if (preen) {
			if (lcnt < 0) {
				printf("\n");
				pfatal("LINK COUNT INCREASING");
			}
			if (preen)
				printf(" (ADJUSTED)\n");
		}
		if (preen || reply("ADJUST") == 1) {
			DIP_SET(dp, di_nlink, DIP(dp, di_nlink) - lcnt);
			inodirty();
		}
	}
}

static int
mkentry(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;
	struct direct newent;
	int newlen, oldlen;

	newent.d_namlen = strlen(idesc->id_name);
	newlen = DIRSIZ(&newent);
	if (dirp->d_ino != 0)
		oldlen = DIRSIZ(dirp);
	else
		oldlen = 0;
	if (dirp->d_reclen - oldlen < newlen)
		return (KEEPON);
	newent.d_reclen = dirp->d_reclen - oldlen;
	dirp->d_reclen = oldlen;
	dirp = (struct direct *)(((char *)dirp) + oldlen);
	dirp->d_ino = idesc->id_parent;	/* ino to be entered is in id_parent */
	dirp->d_reclen = newent.d_reclen;
	dirp->d_type = GET_ITYPE(idesc->id_parent);
	dirp->d_namlen = newent.d_namlen;
	memcpy(dirp->d_name, idesc->id_name, (size_t)dirp->d_namlen + 1);
	return (ALTERED|STOP);
}

static int
chgino(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;

	if (memcmp(dirp->d_name, idesc->id_name, (int)dirp->d_namlen + 1))
		return (KEEPON);
	dirp->d_ino = idesc->id_parent;
	dirp->d_type = GET_ITYPE(idesc->id_parent);
	return (ALTERED|STOP);
}

int
linkup(ino_t orphan, ino_t parentdir)
{
	union dinode *dp;
	int lostdir;
	ino_t oldlfdir;
	struct inodesc idesc;
	char tempname[BUFSIZ];

	memset(&idesc, 0, sizeof(struct inodesc));
	dp = ginode(orphan);
	lostdir = (DIP(dp, di_mode) & IFMT) == IFDIR;
	pwarn("UNREF %s ", lostdir ? "DIR" : "FILE");
	pinode(orphan);
	if (preen && DIP(dp, di_size) == 0)
		return (0);
	if (preen)
		printf(" (RECONNECTED)\n");
	else
		if (reply("RECONNECT") == 0)
			return (0);
	if (lfdir == 0) {
		dp = ginode(ROOTINO);
		idesc.id_name = lfname;
		idesc.id_type = DATA;
		idesc.id_func = findino;
		idesc.id_number = ROOTINO;
		if ((ckinode(dp, &idesc) & FOUND) != 0) {
			lfdir = idesc.id_parent;
		} else {
			pwarn("NO lost+found DIRECTORY");
			if (preen || reply("CREATE")) {
				lfdir = allocdir(ROOTINO, 0, lfmode);
				if (lfdir != 0) {
					if (makeentry(ROOTINO, lfdir, lfname) != 0) {
						if (preen)
							printf(" (CREATED)\n");
					} else {
						freedir(lfdir, ROOTINO);
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
	if ((DIP(dp, di_mode) & IFMT) != IFDIR) {
		pfatal("lost+found IS NOT A DIRECTORY");
		if (reply("REALLOCATE") == 0)
			return (0);
		oldlfdir = lfdir;
		if ((lfdir = allocdir(ROOTINO, 0, lfmode)) == 0) {
			pfatal("SORRY. CANNOT CREATE lost+found DIRECTORY\n\n");
			return (0);
		}
		if ((changeino(ROOTINO, lfname, lfdir) & ALTERED) == 0) {
			pfatal("SORRY. CANNOT CREATE lost+found DIRECTORY\n\n");
			return (0);
		}
		inodirty();
		idesc.id_type = ADDR;
		idesc.id_func = pass4check;
		idesc.id_number = oldlfdir;
		adjust(&idesc, ILNCOUNT(oldlfdir) + 1);
		ILNCOUNT(oldlfdir) = 0;
		dp = ginode(lfdir);
	}
	if (GET_ISTATE(lfdir) != DFOUND) {
		pfatal("SORRY. NO lost+found DIRECTORY\n\n");
		return (0);
	}
	(void)lftempname(tempname, orphan);
	if (makeentry(lfdir, orphan, tempname) == 0) {
		pfatal("SORRY. NO SPACE IN lost+found DIRECTORY");
		printf("\n\n");
		return (0);
	}
	ILNCOUNT(orphan)--;
	if (lostdir) {
		if ((changeino(orphan, "..", lfdir) & ALTERED) == 0 &&
		    parentdir != (ino_t)-1)
			(void)makeentry(orphan, lfdir, "..");
		dp = ginode(lfdir);
		DIP_SET(dp, di_nlink, DIP(dp, di_nlink) + 1);
		inodirty();
		ILNCOUNT(lfdir)++;
		pwarn("DIR I=%llu CONNECTED. ",
		    (unsigned long long)orphan);
		if (parentdir != (ino_t)-1) {
			printf("PARENT WAS I=%llu\n",
			    (unsigned long long)parentdir);
			/*
			 * The parent directory, because of the ordering
			 * guarantees, has had the link count incremented
			 * for the child, but no entry was made.  This
			 * fixes the parent link count so that fsck does
			 * not need to be rerun.
			 */
			ILNCOUNT(parentdir)++;
		}
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
	union dinode *dp;
	struct inodesc idesc;
	char pathbuf[PATH_MAX + 1];

	if (parent < ROOTINO || parent >= maxino ||
	    ino < ROOTINO || ino >= maxino)
		return (0);
	memset(&idesc, 0, sizeof(struct inodesc));
	idesc.id_type = DATA;
	idesc.id_func = mkentry;
	idesc.id_number = parent;
	idesc.id_parent = ino;	/* this is the inode to enter */
	idesc.id_fix = DONTKNOW;
	idesc.id_name = name;
	dp = ginode(parent);
	if (DIP(dp, di_size) % DIRBLKSIZ) {
		DIP_SET(dp, di_size, roundup(DIP(dp, di_size), DIRBLKSIZ));
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
expanddir(union dinode *dp, char *name)
{
	daddr_t lastbn, newblk;
	struct bufarea *bp;
	char *cp, firstblk[DIRBLKSIZ];
	u_int64_t dis;

	dis = lblkno(&sblock, DIP(dp, di_size));
	if (dis > (u_int64_t)INT_MAX)
		return (0);
	lastbn = dis;
	if (lastbn >= NDADDR - 1 || DIP(dp, di_db[lastbn]) == 0 ||
	    DIP(dp, di_size) == 0)
		return (0);
	if ((newblk = allocblk(sblock.fs_frag)) == 0)
		return (0);
	DIP_SET(dp, di_db[lastbn + 1], DIP(dp, di_db[lastbn]));
	DIP_SET(dp, di_db[lastbn], newblk);
	DIP_SET(dp, di_size, DIP(dp, di_size) + sblock.fs_bsize);
	DIP_SET(dp, di_blocks, DIP(dp, di_blocks) + btodb(sblock.fs_bsize));
	bp = getdirblk(DIP(dp, di_db[lastbn + 1]),
	    sblksize(&sblock, DIP(dp, di_size), lastbn + 1));
	if (bp->b_errs)
		goto bad;
	memcpy(firstblk, bp->b_un.b_buf, DIRBLKSIZ);
	bp = getdirblk(newblk, sblock.fs_bsize);
	if (bp->b_errs)
		goto bad;
	memcpy(bp->b_un.b_buf, firstblk, DIRBLKSIZ);
	for (cp = &bp->b_un.b_buf[DIRBLKSIZ];
	     cp < &bp->b_un.b_buf[sblock.fs_bsize];
	     cp += DIRBLKSIZ)
		memcpy(cp, &emptydir, sizeof emptydir);
	dirty(bp);
	bp = getdirblk(DIP(dp, di_db[lastbn + 1]),
	    sblksize(&sblock, DIP(dp, di_size), lastbn + 1));
	if (bp->b_errs)
		goto bad;
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
	DIP_SET(dp, di_db[lastbn], DIP(dp, di_db[lastbn + 1]));
	DIP_SET(dp, di_db[lastbn + 1], 0);
	DIP_SET(dp, di_size, DIP(dp, di_size) - sblock.fs_bsize);
	DIP_SET(dp, di_blocks, DIP(dp, di_blocks) - btodb(sblock.fs_bsize));
	freeblk(newblk, sblock.fs_frag);
	return (0);
}

/*
 * allocate a new directory
 */
ino_t
allocdir(ino_t parent, ino_t request, int mode)
{
	ino_t ino;
	uid_t uid;
	gid_t gid;
	char *cp;
	union dinode *dp;
	struct bufarea *bp;
	struct dirtemplate *dirp;
	struct inoinfo *inp;

	ino = allocino(request, IFDIR|mode);
	dirp = &dirhead;
	dirp->dot_ino = ino;
	dirp->dotdot_ino = parent;
	dp = ginode(ino);
	bp = getdirblk(DIP(dp, di_db[0]), sblock.fs_fsize);
	if (bp->b_errs) {
		freeino(ino);
		return (0);
	}
	memcpy(bp->b_un.b_buf, dirp, sizeof(struct dirtemplate));
	for (cp = &bp->b_un.b_buf[DIRBLKSIZ];
	     cp < &bp->b_un.b_buf[sblock.fs_fsize];
	     cp += DIRBLKSIZ)
		memcpy(cp, &emptydir, sizeof emptydir);
	dirty(bp);
	DIP_SET(dp, di_nlink, 2);
	inodirty();
	if (ino == ROOTINO) {
		ILNCOUNT(ino) = DIP(dp, di_nlink);
		cacheino(dp, ino);
		return(ino);
	}
	if (GET_ISTATE(parent) != DSTATE && GET_ISTATE(parent) != DFOUND) {
		freeino(ino);
		return (0);
	}
	cacheino(dp, ino);
	inp = getinoinfo(ino);
	inp->i_parent = parent;
	inp->i_dotdot = parent;
	SET_ISTATE(ino, GET_ISTATE(parent));
	if (GET_ISTATE(ino) == DSTATE) {
		ILNCOUNT(ino) = DIP(dp, di_nlink);
		ILNCOUNT(parent)++;
	}
	dp = ginode(parent);
	DIP_SET(dp, di_nlink, DIP(dp, di_nlink) + 1);
	uid = DIP(dp, di_uid);
	gid = DIP(dp, di_gid);
	inodirty();
	dp = ginode(ino);
	DIP_SET(dp, di_uid, uid);
	DIP_SET(dp, di_gid, gid);
	inodirty();
	return (ino);
}

/*
 * free a directory inode
 */
static void
freedir(ino_t ino, ino_t parent)
{
	union dinode *dp;

	if (ino != parent) {
		dp = ginode(parent);
		DIP_SET(dp, di_nlink, DIP(dp, di_nlink) - 1);
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
getdirblk(daddr_t blkno, long size)
{

	if (pdirbp != 0)
		pdirbp->b_flags &= ~B_INUSE;
	pdirbp = getdatablk(blkno, size);
	return (pdirbp);
}
