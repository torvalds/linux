/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#if 0
#ifndef lint
static const char sccsid[] = "@(#)pass2.c	8.9 (Berkeley) 4/28/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/sysctl.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "fsck.h"

#define MINDIRSIZE	(sizeof (struct dirtemplate))

static int fix_extraneous(struct inoinfo *, struct inodesc *);
static int deleteentry(struct inodesc *);
static int blksort(const void *, const void *);
static int pass2check(struct inodesc *);

void
pass2(void)
{
	union dinode *dp;
	struct inoinfo **inpp, *inp;
	struct inoinfo **inpend;
	struct inodesc curino;
	union dinode dino;
	int i;
	char pathbuf[MAXPATHLEN + 1];

	switch (inoinfo(UFS_ROOTINO)->ino_state) {

	case USTATE:
		pfatal("ROOT INODE UNALLOCATED");
		if (reply("ALLOCATE") == 0) {
			ckfini(0);
			exit(EEXIT);
		}
		if (allocdir(UFS_ROOTINO, UFS_ROOTINO, 0755) != UFS_ROOTINO)
			errx(EEXIT, "CANNOT ALLOCATE ROOT INODE");
		break;

	case DCLEAR:
		pfatal("DUPS/BAD IN ROOT INODE");
		if (reply("REALLOCATE")) {
			freeino(UFS_ROOTINO);
			if (allocdir(UFS_ROOTINO, UFS_ROOTINO, 0755) !=
			    UFS_ROOTINO)
				errx(EEXIT, "CANNOT ALLOCATE ROOT INODE");
			break;
		}
		if (reply("CONTINUE") == 0) {
			ckfini(0);
			exit(EEXIT);
		}
		break;

	case FSTATE:
	case FCLEAR:
	case FZLINK:
		pfatal("ROOT INODE NOT DIRECTORY");
		if (reply("REALLOCATE")) {
			freeino(UFS_ROOTINO);
			if (allocdir(UFS_ROOTINO, UFS_ROOTINO, 0755) !=
			    UFS_ROOTINO)
				errx(EEXIT, "CANNOT ALLOCATE ROOT INODE");
			break;
		}
		if (reply("FIX") == 0) {
			ckfini(0);
			exit(EEXIT);
		}
		dp = ginode(UFS_ROOTINO);
		DIP_SET(dp, di_mode, DIP(dp, di_mode) & ~IFMT);
		DIP_SET(dp, di_mode, DIP(dp, di_mode) | IFDIR);
		inodirty(dp);
		break;

	case DSTATE:
	case DZLINK:
		break;

	default:
		errx(EEXIT, "BAD STATE %d FOR ROOT INODE",
		    inoinfo(UFS_ROOTINO)->ino_state);
	}
	inoinfo(UFS_ROOTINO)->ino_state = DFOUND;
	inoinfo(UFS_WINO)->ino_state = FSTATE;
	inoinfo(UFS_WINO)->ino_type = DT_WHT;
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
		if (got_siginfo) {
			printf("%s: phase 2: dir %td of %d (%d%%)\n", cdevname,
			    inpp - inpsort, (int)inplast,
			    (int)((inpp - inpsort) * 100 / inplast));
			got_siginfo = 0;
		}
		if (got_sigalarm) {
			setproctitle("%s p2 %d%%", cdevname,
			    (int)((inpp - inpsort) * 100 / inplast));
			got_sigalarm = 0;
		}
		inp = *inpp;
		if (inp->i_isize == 0)
			continue;
		if (inp->i_isize < MINDIRSIZE) {
			direrror(inp->i_number, "DIRECTORY TOO SHORT");
			inp->i_isize = roundup(MINDIRSIZE, DIRBLKSIZ);
			if (reply("FIX") == 1) {
				dp = ginode(inp->i_number);
				DIP_SET(dp, di_size, inp->i_isize);
				inodirty(dp);
			}
		} else if ((inp->i_isize & (DIRBLKSIZ - 1)) != 0) {
			getpathname(pathbuf, inp->i_number, inp->i_number);
			if (usedsoftdep)
				pfatal("%s %s: LENGTH %jd NOT MULTIPLE OF %d",
					"DIRECTORY", pathbuf,
					(intmax_t)inp->i_isize, DIRBLKSIZ);
			else
				pwarn("%s %s: LENGTH %jd NOT MULTIPLE OF %d",
					"DIRECTORY", pathbuf,
					(intmax_t)inp->i_isize, DIRBLKSIZ);
			if (preen)
				printf(" (ADJUSTED)\n");
			inp->i_isize = roundup(inp->i_isize, DIRBLKSIZ);
			if (preen || reply("ADJUST") == 1) {
				dp = ginode(inp->i_number);
				DIP_SET(dp, di_size,
				    roundup(inp->i_isize, DIRBLKSIZ));
				inodirty(dp);
			}
		}
		dp = &dino;
		memset(dp, 0, sizeof(struct ufs2_dinode));
		DIP_SET(dp, di_mode, IFDIR);
		DIP_SET(dp, di_size, inp->i_isize);
		for (i = 0; i < MIN(inp->i_numblks, UFS_NDADDR); i++)
			DIP_SET(dp, di_db[i], inp->i_blks[i]);
		if (inp->i_numblks > UFS_NDADDR)
			for (i = 0; i < UFS_NIADDR; i++)
				DIP_SET(dp, di_ib[i],
				    inp->i_blks[UFS_NDADDR + i]);
		curino.id_number = inp->i_number;
		curino.id_parent = inp->i_parent;
		(void)ckinode(dp, &curino);
	}
	/*
	 * Now that the parents of all directories have been found,
	 * make another pass to verify the value of `..'
	 */
	for (inpp = inpsort; inpp < inpend; inpp++) {
		inp = *inpp;
		if (inp->i_parent == 0 || inp->i_isize == 0)
			continue;
		if (inoinfo(inp->i_parent)->ino_state == DFOUND &&
		    INO_IS_DUNFOUND(inp->i_number))
			inoinfo(inp->i_number)->ino_state = DFOUND;
		if (inp->i_dotdot == inp->i_parent ||
		    inp->i_dotdot == (ino_t)-1)
			continue;
		if (inp->i_dotdot == 0) {
			inp->i_dotdot = inp->i_parent;
			fileerror(inp->i_parent, inp->i_number, "MISSING '..'");
			if (reply("FIX") == 0)
				continue;
			(void)makeentry(inp->i_number, inp->i_parent, "..");
			inoinfo(inp->i_parent)->ino_linkcnt--;
			continue;
		}
		/*
		 * Here we have:
		 *    inp->i_number is directory with bad ".." in it.
		 *    inp->i_dotdot is current value of "..".
		 *    inp->i_parent is directory to which ".." should point.
		 */
		getpathname(pathbuf, inp->i_parent, inp->i_number);
		printf("BAD INODE NUMBER FOR '..' in DIR I=%ju (%s)\n",
		    (uintmax_t)inp->i_number, pathbuf);
		getpathname(pathbuf, inp->i_dotdot, inp->i_dotdot);
		printf("CURRENTLY POINTS TO I=%ju (%s), ",
		    (uintmax_t)inp->i_dotdot, pathbuf);
		getpathname(pathbuf, inp->i_parent, inp->i_parent);
		printf("SHOULD POINT TO I=%ju (%s)",
		    (uintmax_t)inp->i_parent, pathbuf);
		if (cursnapshot != 0) {
			/*
			 * We need to:
			 *    setcwd(inp->i_number);
			 *    setdotdot(inp->i_dotdot, inp->i_parent);
			 */
			cmd.value = inp->i_number;
			if (sysctlbyname("vfs.ffs.setcwd", 0, 0,
			    &cmd, sizeof cmd) == -1) {
				/* kernel lacks support for these functions */
				printf(" (IGNORED)\n");
				continue;
			}
			cmd.value = inp->i_dotdot; /* verify same value */
			cmd.size = inp->i_parent;  /* new parent */
			if (sysctlbyname("vfs.ffs.setdotdot", 0, 0,
			    &cmd, sizeof cmd) == -1) {
				printf(" (FIX FAILED: %s)\n", strerror(errno));
				continue;
			}
			printf(" (FIXED)\n");
			inoinfo(inp->i_parent)->ino_linkcnt--;
			inp->i_dotdot = inp->i_parent;
			continue;
		}
		if (preen)
			printf(" (FIXED)\n");
		else if (reply("FIX") == 0)
			continue;
		inoinfo(inp->i_dotdot)->ino_linkcnt++;
		inoinfo(inp->i_parent)->ino_linkcnt--;
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
	struct direct *dirp = idesc->id_dirp;
	char dirname[MAXPATHLEN + 1];
	struct inoinfo *inp;
	int n, entrysize, ret = 0;
	union dinode *dp;
	const char *errmsg;
	struct direct proto;

	/*
	 * check for "."
	 */
	if (dirp->d_ino > maxino)
		goto chk2;
	if (idesc->id_entryno != 0)
		goto chk1;
	if (dirp->d_ino != 0 && strcmp(dirp->d_name, ".") == 0) {
		if (dirp->d_ino != idesc->id_number) {
			direrror(idesc->id_number, "BAD INODE NUMBER FOR '.'");
			dirp->d_ino = idesc->id_number;
			if (reply("FIX") == 1)
				ret |= ALTERED;
		}
		if (dirp->d_type != DT_DIR) {
			direrror(idesc->id_number, "BAD TYPE VALUE FOR '.'");
			dirp->d_type = DT_DIR;
			if (reply("FIX") == 1)
				ret |= ALTERED;
		}
		goto chk1;
	}
	direrror(idesc->id_number, "MISSING '.'");
	proto.d_ino = idesc->id_number;
	proto.d_type = DT_DIR;
	proto.d_namlen = 1;
	(void)strcpy(proto.d_name, ".");
	entrysize = DIRSIZ(0, &proto);
	if (dirp->d_ino != 0 && strcmp(dirp->d_name, "..") != 0) {
		pfatal("CANNOT FIX, FIRST ENTRY IN DIRECTORY CONTAINS %s\n",
			dirp->d_name);
	} else if (dirp->d_reclen < entrysize) {
		pfatal("CANNOT FIX, INSUFFICIENT SPACE TO ADD '.'\n");
	} else if (dirp->d_reclen < 2 * entrysize) {
		proto.d_reclen = dirp->d_reclen;
		memmove(dirp, &proto, (size_t)entrysize);
		if (reply("FIX") == 1)
			ret |= ALTERED;
	} else {
		n = dirp->d_reclen - entrysize;
		proto.d_reclen = entrysize;
		memmove(dirp, &proto, (size_t)entrysize);
		idesc->id_entryno++;
		inoinfo(dirp->d_ino)->ino_linkcnt--;
		dirp = (struct direct *)((char *)(dirp) + entrysize);
		memset(dirp, 0, (size_t)n);
		dirp->d_reclen = n;
		if (reply("FIX") == 1)
			ret |= ALTERED;
	}
chk1:
	if (idesc->id_entryno > 1)
		goto chk2;
	inp = getinoinfo(idesc->id_number);
	proto.d_ino = inp->i_parent;
	proto.d_type = DT_DIR;
	proto.d_namlen = 2;
	(void)strcpy(proto.d_name, "..");
	entrysize = DIRSIZ(0, &proto);
	if (idesc->id_entryno == 0) {
		n = DIRSIZ(0, dirp);
		if (dirp->d_reclen < n + entrysize)
			goto chk2;
		proto.d_reclen = dirp->d_reclen - n;
		dirp->d_reclen = n;
		idesc->id_entryno++;
		inoinfo(dirp->d_ino)->ino_linkcnt--;
		dirp = (struct direct *)((char *)(dirp) + n);
		memset(dirp, 0, (size_t)proto.d_reclen);
		dirp->d_reclen = proto.d_reclen;
	}
	if (dirp->d_ino != 0 && strcmp(dirp->d_name, "..") == 0) {
		inp->i_dotdot = dirp->d_ino;
		if (dirp->d_type != DT_DIR) {
			direrror(idesc->id_number, "BAD TYPE VALUE FOR '..'");
			dirp->d_type = DT_DIR;
			if (reply("FIX") == 1)
				ret |= ALTERED;
		}
		goto chk2;
	}
	if (dirp->d_ino != 0 && strcmp(dirp->d_name, ".") != 0) {
		fileerror(inp->i_parent, idesc->id_number, "MISSING '..'");
		pfatal("CANNOT FIX, SECOND ENTRY IN DIRECTORY CONTAINS %s\n",
			dirp->d_name);
		inp->i_dotdot = (ino_t)-1;
	} else if (dirp->d_reclen < entrysize) {
		fileerror(inp->i_parent, idesc->id_number, "MISSING '..'");
		pfatal("CANNOT FIX, INSUFFICIENT SPACE TO ADD '..'\n");
		inp->i_dotdot = (ino_t)-1;
	} else if (inp->i_parent != 0) {
		/*
		 * We know the parent, so fix now.
		 */
		inp->i_dotdot = inp->i_parent;
		fileerror(inp->i_parent, idesc->id_number, "MISSING '..'");
		proto.d_reclen = dirp->d_reclen;
		memmove(dirp, &proto, (size_t)entrysize);
		if (reply("FIX") == 1)
			ret |= ALTERED;
	}
	idesc->id_entryno++;
	if (dirp->d_ino != 0)
		inoinfo(dirp->d_ino)->ino_linkcnt--;
	return (ret|KEEPON);
chk2:
	if (dirp->d_ino == 0)
		return (ret|KEEPON);
	if (dirp->d_namlen <= 2 &&
	    dirp->d_name[0] == '.' &&
	    idesc->id_entryno >= 2) {
		if (dirp->d_namlen == 1) {
			direrror(idesc->id_number, "EXTRA '.' ENTRY");
			dirp->d_ino = 0;
			if (reply("FIX") == 1)
				ret |= ALTERED;
			return (KEEPON | ret);
		}
		if (dirp->d_name[1] == '.') {
			direrror(idesc->id_number, "EXTRA '..' ENTRY");
			dirp->d_ino = 0;
			if (reply("FIX") == 1)
				ret |= ALTERED;
			return (KEEPON | ret);
		}
	}
	idesc->id_entryno++;
	n = 0;
	if (dirp->d_ino > maxino) {
		fileerror(idesc->id_number, dirp->d_ino, "I OUT OF RANGE");
		n = reply("REMOVE");
	} else if (((dirp->d_ino == UFS_WINO && dirp->d_type != DT_WHT) ||
		    (dirp->d_ino != UFS_WINO && dirp->d_type == DT_WHT))) {
		fileerror(idesc->id_number, dirp->d_ino, "BAD WHITEOUT ENTRY");
		dirp->d_ino = UFS_WINO;
		dirp->d_type = DT_WHT;
		if (reply("FIX") == 1)
			ret |= ALTERED;
	} else {
again:
		switch (inoinfo(dirp->d_ino)->ino_state) {
		case USTATE:
			if (idesc->id_entryno <= 2)
				break;
			fileerror(idesc->id_number, dirp->d_ino, "UNALLOCATED");
			n = reply("REMOVE");
			break;

		case DCLEAR:
		case FCLEAR:
			if (idesc->id_entryno <= 2)
				break;
			if (inoinfo(dirp->d_ino)->ino_state == FCLEAR)
				errmsg = "DUP/BAD";
			else if (!preen && !usedsoftdep)
				errmsg = "ZERO LENGTH DIRECTORY";
			else if (cursnapshot == 0) {
				n = 1;
				break;
			} else {
				getpathname(dirname, idesc->id_number,
				    dirp->d_ino);
				pwarn("ZERO LENGTH DIRECTORY %s I=%ju",
				    dirname, (uintmax_t)dirp->d_ino);
				/*
				 * We need to:
				 *    setcwd(idesc->id_parent);
				 *    rmdir(dirp->d_name);
				 */
				cmd.value = idesc->id_number;
				if (sysctlbyname("vfs.ffs.setcwd", 0, 0,
				    &cmd, sizeof cmd) == -1) {
					/* kernel lacks support */
					printf(" (IGNORED)\n");
					n = 1;
					break;
				}
				if (rmdir(dirp->d_name) == -1) {
					printf(" (REMOVAL FAILED: %s)\n",
					    strerror(errno));
					n = 1;
					break;
				}
				/* ".." reference to parent is removed */
				inoinfo(idesc->id_number)->ino_linkcnt--;
				printf(" (REMOVED)\n");
				break;
			}
			fileerror(idesc->id_number, dirp->d_ino, errmsg);
			if ((n = reply("REMOVE")) == 1)
				break;
			dp = ginode(dirp->d_ino);
			inoinfo(dirp->d_ino)->ino_state =
			   (DIP(dp, di_mode) & IFMT) == IFDIR ? DSTATE : FSTATE;
			inoinfo(dirp->d_ino)->ino_linkcnt = DIP(dp, di_nlink);
			goto again;

		case DSTATE:
		case DZLINK:
			if (inoinfo(idesc->id_number)->ino_state == DFOUND)
				inoinfo(dirp->d_ino)->ino_state = DFOUND;
			/* FALLTHROUGH */

		case DFOUND:
			inp = getinoinfo(dirp->d_ino);
			if (idesc->id_entryno > 2) {
				if (inp->i_parent == 0)
					inp->i_parent = idesc->id_number;
				else if ((n = fix_extraneous(inp, idesc)) == 1)
					break;
			}
			/* FALLTHROUGH */

		case FSTATE:
		case FZLINK:
			if (dirp->d_type != inoinfo(dirp->d_ino)->ino_type) {
				fileerror(idesc->id_number, dirp->d_ino,
				    "BAD TYPE VALUE");
				dirp->d_type = inoinfo(dirp->d_ino)->ino_type;
				if (reply("FIX") == 1)
					ret |= ALTERED;
			}
			inoinfo(dirp->d_ino)->ino_linkcnt--;
			break;

		default:
			errx(EEXIT, "BAD STATE %d FOR INODE I=%ju",
			    inoinfo(dirp->d_ino)->ino_state,
			    (uintmax_t)dirp->d_ino);
		}
	}
	if (n == 0)
		return (ret|KEEPON);
	dirp->d_ino = 0;
	return (ret|KEEPON|ALTERED);
}

static int
fix_extraneous(struct inoinfo *inp, struct inodesc *idesc)
{
	char *cp;
	struct inodesc dotdesc;
	char oldname[MAXPATHLEN + 1];
	char newname[MAXPATHLEN + 1];

	/*
	 * If we have not yet found "..", look it up now so we know
	 * which inode the directory itself believes is its parent.
	 */
	if (inp->i_dotdot == 0) {
		memset(&dotdesc, 0, sizeof(struct inodesc));
		dotdesc.id_type = DATA;
		dotdesc.id_number = idesc->id_dirp->d_ino;
		dotdesc.id_func = findino;
		dotdesc.id_name = strdup("..");
		if ((ckinode(ginode(dotdesc.id_number), &dotdesc) & FOUND))
			inp->i_dotdot = dotdesc.id_parent;
	}
	/*
	 * We have the previously found old name (inp->i_parent) and the
	 * just found new name (idesc->id_number). We have five cases:
	 * 1)  ".." is missing - can remove either name, choose to delete
	 *     new one and let fsck create ".." pointing to old name.
	 * 2) Both new and old are in same directory, choose to delete
	 *    the new name and let fsck fix ".." if it is wrong.
	 * 3) ".." does not point to the new name, so delete it and let
	 *    fsck fix ".." to point to the old one if it is wrong.
	 * 4) ".." points to the old name only, so delete the new one.
	 * 5) ".." points to the new name only, so delete the old one.
	 *
	 * For cases 1-4 we eliminate the new name;
	 * for case 5 we eliminate the old name.
	 */
	if (inp->i_dotdot == 0 ||		    /* Case 1 */
	    idesc->id_number == inp->i_parent ||    /* Case 2 */
	    inp->i_dotdot != idesc->id_number ||    /* Case 3 */
	    inp->i_dotdot == inp->i_parent) {	    /* Case 4 */
		getpathname(newname, idesc->id_number, idesc->id_number);
		if (strcmp(newname, "/") != 0)
			strcat (newname, "/");
		strcat(newname, idesc->id_dirp->d_name);
		getpathname(oldname, inp->i_number, inp->i_number);
		pwarn("%s IS AN EXTRANEOUS HARD LINK TO DIRECTORY %s",
		    newname, oldname);
		if (cursnapshot != 0) {
			/*
			 * We need to
			 *    setcwd(idesc->id_number);
			 *    unlink(idesc->id_dirp->d_name);
			 */
			cmd.value = idesc->id_number;
			if (sysctlbyname("vfs.ffs.setcwd", 0, 0,
			    &cmd, sizeof cmd) == -1) {
				printf(" (IGNORED)\n");
				return (0);
			}
			cmd.value = (intptr_t)idesc->id_dirp->d_name;
			cmd.size = inp->i_number; /* verify same name */
			if (sysctlbyname("vfs.ffs.unlink", 0, 0,
			    &cmd, sizeof cmd) == -1) {
				printf(" (UNLINK FAILED: %s)\n",
				    strerror(errno));
				return (0);
			}
			printf(" (REMOVED)\n");
			return (0);
		}
		if (preen) {
			printf(" (REMOVED)\n");
			return (1);
		}
		return (reply("REMOVE"));
	}
	/*
	 * None of the first four cases above, so must be case (5).
	 * Eliminate the old name and make the new the name the parent.
	 */
	getpathname(oldname, inp->i_parent, inp->i_number);
	getpathname(newname, inp->i_number, inp->i_number);
	pwarn("%s IS AN EXTRANEOUS HARD LINK TO DIRECTORY %s", oldname,
	    newname);
	if (cursnapshot != 0) {
		/*
		 * We need to
		 *    setcwd(inp->i_parent);
		 *    unlink(last component of oldname pathname);
		 */
		cmd.value = inp->i_parent;
		if (sysctlbyname("vfs.ffs.setcwd", 0, 0,
		    &cmd, sizeof cmd) == -1) {
			printf(" (IGNORED)\n");
			return (0);
		}
		if ((cp = strchr(oldname, '/')) == NULL) {
			printf(" (IGNORED)\n");
			return (0);
		}
		cmd.value = (intptr_t)(cp + 1);
		cmd.size = inp->i_number; /* verify same name */
		if (sysctlbyname("vfs.ffs.unlink", 0, 0,
		    &cmd, sizeof cmd) == -1) {
			printf(" (UNLINK FAILED: %s)\n",
			    strerror(errno));
			return (0);
		}
		printf(" (REMOVED)\n");
		inp->i_parent = idesc->id_number;  /* reparent to correct dir */
		return (0);
	}
	if (!preen && !reply("REMOVE"))
		return (0);
	memset(&dotdesc, 0, sizeof(struct inodesc));
	dotdesc.id_type = DATA;
	dotdesc.id_number = inp->i_parent; /* directory in which name appears */
	dotdesc.id_parent = inp->i_number; /* inode number in entry to delete */
	dotdesc.id_func = deleteentry;
	if ((ckinode(ginode(dotdesc.id_number), &dotdesc) & FOUND) && preen)
		printf(" (REMOVED)\n");
	inp->i_parent = idesc->id_number;  /* reparent to correct directory */
	inoinfo(inp->i_number)->ino_linkcnt++; /* name gone, return reference */
	return (0);
}

static int
deleteentry(struct inodesc *idesc)
{
	struct direct *dirp = idesc->id_dirp;

	if (idesc->id_entryno++ < 2 || dirp->d_ino != idesc->id_parent)
		return (KEEPON);
	dirp->d_ino = 0;
	return (ALTERED|STOP|FOUND);
}

/*
 * Routine to sort disk blocks.
 */
static int
blksort(const void *arg1, const void *arg2)
{

	return ((*(struct inoinfo * const *)arg1)->i_blks[0] -
		(*(struct inoinfo * const *)arg2)->i_blks[0]);
}
