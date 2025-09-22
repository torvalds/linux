/*	$OpenBSD: pass2.c,v 1.16 2019/12/17 19:41:51 millert Exp $	*/
/*	$NetBSD: pass2.c,v 1.6 2000/01/28 16:01:46 bouyer Exp $	*/

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

#include <sys/param.h>	/* roundup */
#include <sys/time.h>
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

#define MINDIRSIZE	(sizeof (struct ext2fs_dirtemplate))

static int pass2check(struct inodesc *);
static int blksort(const void *, const void *);

void
pass2(void)
{
	struct ext2fs_dinode *dp;
	struct inoinfo **inpp, *inp;
	struct inoinfo **inpend;
	struct inodesc curino;
	struct ext2fs_dinode dino;
	char pathbuf[PATH_MAX + 1];

	switch (statemap[EXT2_ROOTINO]) {

	case USTATE:
		pfatal("ROOT INODE UNALLOCATED");
		if (reply("ALLOCATE") == 0)
			errexit("%s\n", "");
		if (allocdir(EXT2_ROOTINO, EXT2_ROOTINO, 0755) != EXT2_ROOTINO)
			errexit("CANNOT ALLOCATE ROOT INODE\n");
		break;

	case DCLEAR:
		pfatal("DUPS/BAD IN ROOT INODE");
		if (reply("REALLOCATE")) {
			freeino(EXT2_ROOTINO);
			if (allocdir(EXT2_ROOTINO, EXT2_ROOTINO, 0755) != EXT2_ROOTINO)
				errexit("CANNOT ALLOCATE ROOT INODE\n");
			break;
		}
		if (reply("CONTINUE") == 0)
			errexit("%s\n", "");
		break;

	case FSTATE:
	case FCLEAR:
		pfatal("ROOT INODE NOT DIRECTORY");
		if (reply("REALLOCATE")) {
			freeino(EXT2_ROOTINO);
			if (allocdir(EXT2_ROOTINO, EXT2_ROOTINO, 0755) != EXT2_ROOTINO)
				errexit("CANNOT ALLOCATE ROOT INODE\n");
			break;
		}
		if (reply("FIX") == 0)
			errexit("%s\n", "");
		dp = ginode(EXT2_ROOTINO);
		dp->e2di_mode = htole16((letoh16(dp->e2di_mode) & ~IFMT) | IFDIR);
		inodirty();
		break;

	case DSTATE:
		break;

	default:
		errexit("BAD STATE %d FOR ROOT INODE\n", statemap[EXT2_ROOTINO]);
	}

	/*
	 * Sort the directory list into disk block order.
	 */
	qsort((char *)inpsort, (size_t)inplast, sizeof *inpsort, blksort);
	/*
	 * Check the integrity of each directory.
	 */
	memset(&curino, 0, sizeof(struct inodesc));
	curino.id_type = DATA;
	curino.id_func = pass2check;
	inpend = &inpsort[inplast];
	for (inpp = inpsort; inpp < inpend; inpp++) {
		inp = *inpp;
		if (inp->i_isize == 0)
			continue;
		if (inp->i_isize < MINDIRSIZE) {
			direrror(inp->i_number, "DIRECTORY TOO SHORT");
			inp->i_isize = roundup(MINDIRSIZE, sblock.e2fs_bsize);
			if (reply("FIX") == 1) {
				dp = ginode(inp->i_number);
				inossize(dp, inp->i_isize);
				inodirty();
			}
		} else if ((inp->i_isize & (sblock.e2fs_bsize - 1)) != 0) {
			getpathname(pathbuf, sizeof pathbuf, inp->i_number,
			    inp->i_number);
			pwarn("DIRECTORY %s: LENGTH %lu NOT MULTIPLE OF %d",
			    pathbuf, (u_long)inp->i_isize, sblock.e2fs_bsize);
			if (preen)
				printf(" (ADJUSTED)\n");
			inp->i_isize = roundup(inp->i_isize, sblock.e2fs_bsize);
			if (preen || reply("ADJUST") == 1) {
				dp = ginode(inp->i_number);
				inossize(dp, inp->i_isize);
				inodirty();
			}
		}
		memset(&dino, 0, sizeof(dino));
		dino.e2di_mode = htole16(IFDIR);
		inossize(&dino, inp->i_isize);
		memcpy(&dino.e2di_blocks[0], &inp->i_blks[0], (size_t)inp->i_numblks);
		curino.id_number = inp->i_number;
		curino.id_parent = inp->i_parent;
		(void)ckinode(&dino, &curino);
	}
	/*
	 * Now that the parents of all directories have been found,
	 * make another pass to verify the value of `..'
	 */
	for (inpp = inpsort; inpp < inpend; inpp++) {
		inp = *inpp;
		if (inp->i_parent == 0 || inp->i_isize == 0)
			continue;
		if (inp->i_dotdot == inp->i_parent ||
		    inp->i_dotdot == (ino_t)-1)
			continue;
		if (inp->i_dotdot == 0) {
			inp->i_dotdot = inp->i_parent;
			fileerror(inp->i_parent, inp->i_number, "MISSING '..'");
			if (reply("FIX") == 0)
				continue;
			(void)makeentry(inp->i_number, inp->i_parent, "..");
			lncntp[inp->i_parent]--;
			continue;
		}
		fileerror(inp->i_parent, inp->i_number,
		    "BAD INODE NUMBER FOR '..'");
		if (reply("FIX") == 0)
			continue;
		lncntp[inp->i_dotdot]++;
		lncntp[inp->i_parent]--;
		inp->i_dotdot = inp->i_parent;
		(void)changeino(inp->i_number, "..", inp->i_parent);
	}
	/*
	 * Mark all the directories that can be found from the root.
	 */
	propagate();
}

static int
pass2check(struct inodesc *idesc)
{
	struct ext2fs_direct *dirp = idesc->id_dirp;
	struct inoinfo *inp;
	int n, entrysize, ret = 0;
	struct ext2fs_dinode *dp;
	char *errmsg;
	struct ext2fs_direct proto;
	char namebuf[PATH_MAX + 1];
	char pathbuf[PATH_MAX + 1];

	/*
	 * check for "."
	 */
	if (idesc->id_entryno != 0)
		goto chk1;
	if (letoh32(dirp->e2d_ino) != 0 && dirp->e2d_namlen == 1 &&
		dirp->e2d_name[0] == '.') {
		if (letoh32(dirp->e2d_ino) != idesc->id_number) {
			direrror(idesc->id_number, "BAD INODE NUMBER FOR '.'");
			dirp->e2d_ino = htole32(idesc->id_number);
			if (reply("FIX") == 1)
				ret |= ALTERED;
		}
		if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
		    (sblock.e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE)
		    && (dirp->e2d_type != EXT2_FT_DIR)) {
			direrror(idesc->id_number, "BAD TYPE VALUE FOR '.'");
			dirp->e2d_type = EXT2_FT_DIR;
			if (reply("FIX") == 1)
				ret |= ALTERED;
		}
		goto chk1;
	}
	direrror(idesc->id_number, "MISSING '.'");
	proto.e2d_ino = htole32(idesc->id_number);
	proto.e2d_namlen = 1;
	if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
	    (sblock.e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE))
		proto.e2d_type = EXT2_FT_DIR;
	else
		proto.e2d_type = 0;
	(void)strlcpy(proto.e2d_name, ".", sizeof proto.e2d_name);
	entrysize = EXT2FS_DIRSIZ(proto.e2d_namlen);
	if (letoh32(dirp->e2d_ino) != 0 && strcmp(dirp->e2d_name, "..") != 0) {
		pfatal("CANNOT FIX, FIRST ENTRY IN DIRECTORY CONTAINS %s\n",
			dirp->e2d_name);
	} else if (letoh16(dirp->e2d_reclen) < entrysize) {
		pfatal("CANNOT FIX, INSUFFICIENT SPACE TO ADD '.'\n");
	} else if (letoh16(dirp->e2d_reclen) < 2 * entrysize) {
		proto.e2d_reclen = dirp->e2d_reclen;
		memcpy(dirp, &proto, (size_t)entrysize);
		if (reply("FIX") == 1)
			ret |= ALTERED;
	} else {
		n = letoh16(dirp->e2d_reclen) - entrysize;
		proto.e2d_reclen = htole16(entrysize);
		memcpy(dirp, &proto, (size_t)entrysize);
		idesc->id_entryno++;
		lncntp[letoh32(dirp->e2d_ino)]--;
		dirp = (struct ext2fs_direct *)((char *)(dirp) + entrysize);
		memset(dirp, 0, (size_t)n);
		dirp->e2d_reclen = htole16(n);
		if (reply("FIX") == 1)
			ret |= ALTERED;
	}
chk1:
	if (idesc->id_entryno > 1)
		goto chk2;
	inp = getinoinfo(idesc->id_number);
	proto.e2d_ino = htole32(inp->i_parent);
	proto.e2d_namlen = 2;
	if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
	    (sblock.e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE))
		proto.e2d_type = EXT2_FT_DIR;
	else
		proto.e2d_type = 0;
	(void)strlcpy(proto.e2d_name, "..", sizeof proto.e2d_name);
	entrysize = EXT2FS_DIRSIZ(2);
	if (idesc->id_entryno == 0) {
		n = EXT2FS_DIRSIZ(dirp->e2d_namlen);
		if (letoh16(dirp->e2d_reclen) < n + entrysize)
			goto chk2;
		proto.e2d_reclen = htole16(letoh16(dirp->e2d_reclen) - n);
		dirp->e2d_reclen = htole16(n);
		idesc->id_entryno++;
		lncntp[letoh32(dirp->e2d_ino)]--;
		dirp = (struct ext2fs_direct *)((char *)(dirp) + n);
		memset(dirp, 0, (size_t)letoh16(proto.e2d_reclen));
		dirp->e2d_reclen = proto.e2d_reclen;
	}
	if (letoh32(dirp->e2d_ino) != 0 &&
	    dirp->e2d_namlen == 2 &&
	    strncmp(dirp->e2d_name, "..", 2) == 0) {
		inp->i_dotdot = letoh32(dirp->e2d_ino);
		if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
		    (sblock.e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE)
		    && dirp->e2d_type != EXT2_FT_DIR) {
			direrror(idesc->id_number, "BAD TYPE VALUE FOR '..'");
			dirp->e2d_type = EXT2_FT_DIR;
			if (reply("FIX") == 1)
				ret |= ALTERED;
		}
		goto chk2;
	}
	if (letoh32(dirp->e2d_ino) != 0 &&
		dirp->e2d_namlen == 1 &&
		strncmp(dirp->e2d_name, ".", 1) != 0) {
		fileerror(inp->i_parent, idesc->id_number, "MISSING '..'");
		pfatal("CANNOT FIX, SECOND ENTRY IN DIRECTORY CONTAINS %s\n",
			dirp->e2d_name);
		inp->i_dotdot = (ino_t)-1;
	} else if (letoh16(dirp->e2d_reclen) < entrysize) {
		fileerror(inp->i_parent, idesc->id_number, "MISSING '..'");
		pfatal("CANNOT FIX, INSUFFICIENT SPACE TO ADD '..'\n");
		inp->i_dotdot = (ino_t)-1;
	} else if (inp->i_parent != 0) {
		/*
		 * We know the parent, so fix now.
		 */
		inp->i_dotdot = inp->i_parent;
		fileerror(inp->i_parent, idesc->id_number, "MISSING '..'");
		proto.e2d_reclen = dirp->e2d_reclen;
		memcpy(dirp, &proto, (size_t)entrysize);
		if (reply("FIX") == 1)
			ret |= ALTERED;
	}
	idesc->id_entryno++;
	if (letoh32(dirp->e2d_ino) != 0)
		lncntp[letoh32(dirp->e2d_ino)]--;
	return (ret|KEEPON);
chk2:
	if (letoh32(dirp->e2d_ino) == 0)
		return (ret|KEEPON);
	if (dirp->e2d_namlen <= 2 &&
	    dirp->e2d_name[0] == '.' &&
	    idesc->id_entryno >= 2) {
		if (dirp->e2d_namlen == 1) {
			direrror(idesc->id_number, "EXTRA '.' ENTRY");
			dirp->e2d_ino = 0;
			if (reply("FIX") == 1)
				ret |= ALTERED;
			return (KEEPON | ret);
		}
		if (dirp->e2d_name[1] == '.') {
			direrror(idesc->id_number, "EXTRA '..' ENTRY");
			dirp->e2d_ino = 0;
			if (reply("FIX") == 1)
				ret |= ALTERED;
			return (KEEPON | ret);
		}
	}
	idesc->id_entryno++;
	n = 0;
	if (letoh32(dirp->e2d_ino) > maxino ||
		(letoh32(dirp->e2d_ino) < EXT2_FIRSTINO &&
		 letoh32(dirp->e2d_ino) != EXT2_ROOTINO)) {
		fileerror(idesc->id_number, letoh32(dirp->e2d_ino), "I OUT OF RANGE");
		n = reply("REMOVE");
	} else {
again:
		switch (statemap[letoh32(dirp->e2d_ino)]) {
		case USTATE:
			if (idesc->id_entryno <= 2)
				break;
			fileerror(idesc->id_number, letoh32(dirp->e2d_ino), "UNALLOCATED");
			n = reply("REMOVE");
			break;

		case DCLEAR:
		case FCLEAR:
			if (idesc->id_entryno <= 2)
				break;
			if (statemap[letoh32(dirp->e2d_ino)] == FCLEAR)
				errmsg = "DUP/BAD";
			else if (!preen)
				errmsg = "ZERO LENGTH DIRECTORY";
			else {
				n = 1;
				break;
			}
			fileerror(idesc->id_number, letoh32(dirp->e2d_ino), errmsg);
			if ((n = reply("REMOVE")) == 1)
				break;
			dp = ginode(letoh32(dirp->e2d_ino));
			statemap[letoh32(dirp->e2d_ino)] =
			    (letoh16(dp->e2di_mode) & IFMT) == IFDIR ? DSTATE : FSTATE;
			lncntp[letoh32(dirp->e2d_ino)] = letoh16(dp->e2di_nlink);
			goto again;

		case DSTATE:
		case DFOUND:
			inp = getinoinfo(letoh32(dirp->e2d_ino));
			if (inp->i_parent != 0 && idesc->id_entryno > 2) {
				getpathname(pathbuf, sizeof pathbuf,
				    idesc->id_number, idesc->id_number);
				getpathname(namebuf, sizeof namebuf,
				    letoh32(dirp->e2d_ino), letoh32(dirp->e2d_ino));
				pwarn("%s %s %s\n", pathbuf,
				    "IS AN EXTRANEOUS HARD LINK TO DIRECTORY",
				    namebuf);
				if (preen)
					printf(" (IGNORED)\n");
				else if ((n = reply("REMOVE")) == 1)
					break;
			}
			if (idesc->id_entryno > 2)
				inp->i_parent = idesc->id_number;
			/* fall through */

		case FSTATE:
			if (sblock.e2fs.e2fs_rev > E2FS_REV0 &&
			    (sblock.e2fs.e2fs_features_incompat &
				EXT2F_INCOMPAT_FTYPE) &&
			    dirp->e2d_type !=
				inot2ext2dt(typemap[letoh32(dirp->e2d_ino)])) {
				dirp->e2d_type =
				    inot2ext2dt(typemap[letoh32(dirp->e2d_ino)]);
				fileerror(idesc->id_number,
				    letoh32(dirp->e2d_ino),
				    "BAD TYPE VALUE");
				if (reply("FIX") == 1)
					ret |= ALTERED;
			}
			lncntp[letoh32(dirp->e2d_ino)]--;
			break;

		default:
			errexit("BAD STATE %d FOR INODE I=%llu\n",
			    statemap[letoh32(dirp->e2d_ino)],
			    (unsigned long long)letoh32(dirp->e2d_ino));
		}
	}
	if (n == 0)
		return (ret|KEEPON);
	dirp->e2d_ino = 0;
	return (ret|KEEPON|ALTERED);
}

/*
 * Routine to sort disk blocks.
 */
static int
blksort(const void *inpp1, const void *inpp2)
{
	return ((* (struct inoinfo **) inpp1)->i_blks[0] -
		(* (struct inoinfo **) inpp2)->i_blks[0]);
}
