/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)restore.c	8.3 (Berkeley) 9/13/94";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ufs/ufs/dinode.h>

#include "restore.h"
#include "extern.h"

static char *keyval(int);

/*
 * This implements the 't' option.
 * List entries on the tape.
 */
long
listfile(char *name, ino_t ino, int type)
{
	long descend = hflag ? GOOD : FAIL;

	if (TSTINO(ino, dumpmap) == 0)
		return (descend);
	vprintf(stdout, "%s", type == LEAF ? "leaf" : "dir ");
	fprintf(stdout, "%10ju\t%s\n", (uintmax_t)ino, name);
	return (descend);
}

/*
 * This implements the 'x' option.
 * Request that new entries be extracted.
 */
long
addfile(char *name, ino_t ino, int type)
{
	struct entry *ep;
	long descend = hflag ? GOOD : FAIL;
	char buf[100];

	if (TSTINO(ino, dumpmap) == 0) {
		dprintf(stdout, "%s: not on the tape\n", name);
		return (descend);
	}
	if (ino == UFS_WINO && command == 'i' && !vflag)
		return (descend);
	if (!mflag) {
		(void) sprintf(buf, "./%ju", (uintmax_t)ino);
		name = buf;
		if (type == NODE) {
			(void) genliteraldir(name, ino);
			return (descend);
		}
	}
	ep = lookupino(ino);
	if (ep != NULL) {
		if (strcmp(name, myname(ep)) == 0) {
			ep->e_flags |= NEW;
			return (descend);
		}
		type |= LINK;
	}
	ep = addentry(name, ino, type);
	if (type == NODE)
		newnode(ep);
	ep->e_flags |= NEW;
	return (descend);
}

/*
 * This is used by the 'i' option to undo previous requests made by addfile.
 * Delete entries from the request queue.
 */
/* ARGSUSED */
long
deletefile(char *name, ino_t ino, int type)
{
	long descend = hflag ? GOOD : FAIL;
	struct entry *ep;

	if (TSTINO(ino, dumpmap) == 0)
		return (descend);
	ep = lookupname(name);
	if (ep != NULL) {
		ep->e_flags &= ~NEW;
		ep->e_flags |= REMOVED;
		if (ep->e_type != NODE)
			freeentry(ep);
	}
	return (descend);
}

/*
 * The following four routines implement the incremental
 * restore algorithm. The first removes old entries, the second
 * does renames and calculates the extraction list, the third
 * cleans up link names missed by the first two, and the final
 * one deletes old directories.
 *
 * Directories cannot be immediately deleted, as they may have
 * other files in them which need to be moved out first. As
 * directories to be deleted are found, they are put on the
 * following deletion list. After all deletions and renames
 * are done, this list is actually deleted.
 */
static struct entry *removelist;

/*
 *	Remove invalid whiteouts from the old tree.
 *	Remove unneeded leaves from the old tree.
 *	Remove directories from the lookup chains.
 */
void
removeoldleaves(void)
{
	struct entry *ep, *nextep;
	ino_t i, mydirino;

	vprintf(stdout, "Mark entries to be removed.\n");
	if ((ep = lookupino(UFS_WINO))) {
		vprintf(stdout, "Delete whiteouts\n");
		for ( ; ep != NULL; ep = nextep) {
			nextep = ep->e_links;
			mydirino = ep->e_parent->e_ino;
			/*
			 * We remove all whiteouts that are in directories
			 * that have been removed or that have been dumped.
			 */
			if (TSTINO(mydirino, usedinomap) &&
			    !TSTINO(mydirino, dumpmap))
				continue;
			delwhiteout(ep);
			freeentry(ep);
		}
	}
	for (i = UFS_ROOTINO + 1; i < maxino; i++) {
		ep = lookupino(i);
		if (ep == NULL)
			continue;
		if (TSTINO(i, usedinomap))
			continue;
		for ( ; ep != NULL; ep = ep->e_links) {
			dprintf(stdout, "%s: REMOVE\n", myname(ep));
			if (ep->e_type == LEAF) {
				removeleaf(ep);
				freeentry(ep);
			} else {
				mktempname(ep);
				deleteino(ep->e_ino);
				ep->e_next = removelist;
				removelist = ep;
			}
		}
	}
}

/*
 *	For each directory entry on the incremental tape, determine which
 *	category it falls into as follows:
 *	KEEP - entries that are to be left alone.
 *	NEW - new entries to be added.
 *	EXTRACT - files that must be updated with new contents.
 *	LINK - new links to be added.
 *	Renames are done at the same time.
 */
long
nodeupdates(char *name, ino_t ino, int type)
{
	struct entry *ep, *np, *ip;
	long descend = GOOD;
	int lookuptype = 0;
	int key = 0;
		/* key values */
#		define ONTAPE	0x1	/* inode is on the tape */
#		define INOFND	0x2	/* inode already exists */
#		define NAMEFND	0x4	/* name already exists */
#		define MODECHG	0x8	/* mode of inode changed */

	/*
	 * This routine is called once for each element in the
	 * directory hierarchy, with a full path name.
	 * The "type" value is incorrectly specified as LEAF for
	 * directories that are not on the dump tape.
	 *
	 * Check to see if the file is on the tape.
	 */
	if (TSTINO(ino, dumpmap))
		key |= ONTAPE;
	/*
	 * Check to see if the name exists, and if the name is a link.
	 */
	np = lookupname(name);
	if (np != NULL) {
		key |= NAMEFND;
		ip = lookupino(np->e_ino);
		if (ip == NULL)
			panic("corrupted symbol table\n");
		if (ip != np)
			lookuptype = LINK;
	}
	/*
	 * Check to see if the inode exists, and if one of its links
	 * corresponds to the name (if one was found).
	 */
	ip = lookupino(ino);
	if (ip != NULL) {
		key |= INOFND;
		for (ep = ip->e_links; ep != NULL; ep = ep->e_links) {
			if (ep == np) {
				ip = ep;
				break;
			}
		}
	}
	/*
	 * If both a name and an inode are found, but they do not
	 * correspond to the same file, then both the inode that has
	 * been found and the inode corresponding to the name that
	 * has been found need to be renamed. The current pathname
	 * is the new name for the inode that has been found. Since
	 * all files to be deleted have already been removed, the
	 * named file is either a now unneeded link, or it must live
	 * under a new name in this dump level. If it is a link, it
	 * can be removed. If it is not a link, it is given a
	 * temporary name in anticipation that it will be renamed
	 * when it is later found by inode number.
	 */
	if (((key & (INOFND|NAMEFND)) == (INOFND|NAMEFND)) && ip != np) {
		if (lookuptype == LINK) {
			removeleaf(np);
			freeentry(np);
		} else {
			dprintf(stdout, "name/inode conflict, mktempname %s\n",
				myname(np));
			mktempname(np);
		}
		np = NULL;
		key &= ~NAMEFND;
	}
	if ((key & ONTAPE) &&
	  (((key & INOFND) && ip->e_type != type) ||
	   ((key & NAMEFND) && np->e_type != type)))
		key |= MODECHG;

	/*
	 * Decide on the disposition of the file based on its flags.
	 * Note that we have already handled the case in which
	 * a name and inode are found that correspond to different files.
	 * Thus if both NAMEFND and INOFND are set then ip == np.
	 */
	switch (key) {

	/*
	 * A previously existing file has been found.
	 * Mark it as KEEP so that other links to the inode can be
	 * detected, and so that it will not be reclaimed by the search
	 * for unreferenced names.
	 */
	case INOFND|NAMEFND:
		ip->e_flags |= KEEP;
		dprintf(stdout, "[%s] %s: %s\n", keyval(key), name,
			flagvalues(ip));
		break;

	/*
	 * A file on the tape has a name which is the same as a name
	 * corresponding to a different file in the previous dump.
	 * Since all files to be deleted have already been removed,
	 * this file is either a now unneeded link, or it must live
	 * under a new name in this dump level. If it is a link, it
	 * can simply be removed. If it is not a link, it is given a
	 * temporary name in anticipation that it will be renamed
	 * when it is later found by inode number (see INOFND case
	 * below). The entry is then treated as a new file.
	 */
	case ONTAPE|NAMEFND:
	case ONTAPE|NAMEFND|MODECHG:
		if (lookuptype == LINK) {
			removeleaf(np);
			freeentry(np);
		} else {
			mktempname(np);
		}
		/* FALLTHROUGH */

	/*
	 * A previously non-existent file.
	 * Add it to the file system, and request its extraction.
	 * If it is a directory, create it immediately.
	 * (Since the name is unused there can be no conflict)
	 */
	case ONTAPE:
		ep = addentry(name, ino, type);
		if (type == NODE)
			newnode(ep);
		ep->e_flags |= NEW|KEEP;
		dprintf(stdout, "[%s] %s: %s\n", keyval(key), name,
			flagvalues(ep));
		break;

	/*
	 * A file with the same inode number, but a different
	 * name has been found. If the other name has not already
	 * been found (indicated by the KEEP flag, see above) then
	 * this must be a new name for the file, and it is renamed.
	 * If the other name has been found then this must be a
	 * link to the file. Hard links to directories are not
	 * permitted, and are either deleted or converted to
	 * symbolic links. Finally, if the file is on the tape,
	 * a request is made to extract it.
	 */
	case ONTAPE|INOFND:
		if (type == LEAF && (ip->e_flags & KEEP) == 0)
			ip->e_flags |= EXTRACT;
		/* FALLTHROUGH */
	case INOFND:
		if ((ip->e_flags & KEEP) == 0) {
			renameit(myname(ip), name);
			moveentry(ip, name);
			ip->e_flags |= KEEP;
			dprintf(stdout, "[%s] %s: %s\n", keyval(key), name,
				flagvalues(ip));
			break;
		}
		if (ip->e_type == NODE) {
			descend = FAIL;
			fprintf(stderr,
				"deleted hard link %s to directory %s\n",
				name, myname(ip));
			break;
		}
		ep = addentry(name, ino, type|LINK);
		ep->e_flags |= NEW;
		dprintf(stdout, "[%s] %s: %s|LINK\n", keyval(key), name,
			flagvalues(ep));
		break;

	/*
	 * A previously known file which is to be updated. If it is a link,
	 * then all names referring to the previous file must be removed
	 * so that the subset of them that remain can be recreated.
	 */
	case ONTAPE|INOFND|NAMEFND:
		if (lookuptype == LINK) {
			removeleaf(np);
			freeentry(np);
			ep = addentry(name, ino, type|LINK);
			if (type == NODE)
			        newnode(ep);
			ep->e_flags |= NEW|KEEP;
			dprintf(stdout, "[%s] %s: %s|LINK\n", keyval(key), name,
				flagvalues(ep));
			break;
		}
		if (type == LEAF && lookuptype != LINK)
			np->e_flags |= EXTRACT;
		np->e_flags |= KEEP;
		dprintf(stdout, "[%s] %s: %s\n", keyval(key), name,
			flagvalues(np));
		break;

	/*
	 * An inode is being reused in a completely different way.
	 * Normally an extract can simply do an "unlink" followed
	 * by a "creat". Here we must do effectively the same
	 * thing. The complications arise because we cannot really
	 * delete a directory since it may still contain files
	 * that we need to rename, so we delete it from the symbol
	 * table, and put it on the list to be deleted eventually.
	 * Conversely if a directory is to be created, it must be
	 * done immediately, rather than waiting until the
	 * extraction phase.
	 */
	case ONTAPE|INOFND|MODECHG:
	case ONTAPE|INOFND|NAMEFND|MODECHG:
		if (ip->e_flags & KEEP) {
			badentry(ip, "cannot KEEP and change modes");
			break;
		}
		if (ip->e_type == LEAF) {
			/* changing from leaf to node */
			for (ip = lookupino(ino); ip != NULL; ip = ip->e_links) {
				if (ip->e_type != LEAF)
					badentry(ip, "NODE and LEAF links to same inode");
				removeleaf(ip);
				freeentry(ip);
			}
			ip = addentry(name, ino, type);
			newnode(ip);
		} else {
			/* changing from node to leaf */
			if ((ip->e_flags & TMPNAME) == 0)
				mktempname(ip);
			deleteino(ip->e_ino);
			ip->e_next = removelist;
			removelist = ip;
			ip = addentry(name, ino, type);
		}
		ip->e_flags |= NEW|KEEP;
		dprintf(stdout, "[%s] %s: %s\n", keyval(key), name,
			flagvalues(ip));
		break;

	/*
	 * A hard link to a directory that has been removed.
	 * Ignore it.
	 */
	case NAMEFND:
		dprintf(stdout, "[%s] %s: Extraneous name\n", keyval(key),
			name);
		descend = FAIL;
		break;

	/*
	 * If we find a directory entry for a file that is not on
	 * the tape, then we must have found a file that was created
	 * while the dump was in progress. Since we have no contents
	 * for it, we discard the name knowing that it will be on the
	 * next incremental tape.
	 */
	case 0:
		fprintf(stderr, "%s: (inode %ju) not found on tape\n",
		    name, (uintmax_t)ino);
		break;

	/*
	 * If any of these arise, something is grievously wrong with
	 * the current state of the symbol table.
	 */
	case INOFND|NAMEFND|MODECHG:
	case NAMEFND|MODECHG:
	case INOFND|MODECHG:
		fprintf(stderr, "[%s] %s: inconsistent state\n", keyval(key),
			name);
		break;

	/*
	 * These states "cannot" arise for any state of the symbol table.
	 */
	case ONTAPE|MODECHG:
	case MODECHG:
	default:
		panic("[%s] %s: impossible state\n", keyval(key), name);
		break;
	}
	return (descend);
}

/*
 * Calculate the active flags in a key.
 */
static char *
keyval(int key)
{
	static char keybuf[32];

	(void) strcpy(keybuf, "|NIL");
	keybuf[0] = '\0';
	if (key & ONTAPE)
		(void) strcat(keybuf, "|ONTAPE");
	if (key & INOFND)
		(void) strcat(keybuf, "|INOFND");
	if (key & NAMEFND)
		(void) strcat(keybuf, "|NAMEFND");
	if (key & MODECHG)
		(void) strcat(keybuf, "|MODECHG");
	return (&keybuf[1]);
}

/*
 * Find unreferenced link names.
 */
void
findunreflinks(void)
{
	struct entry *ep, *np;
	ino_t i;

	vprintf(stdout, "Find unreferenced names.\n");
	for (i = UFS_ROOTINO; i < maxino; i++) {
		ep = lookupino(i);
		if (ep == NULL || ep->e_type == LEAF || TSTINO(i, dumpmap) == 0)
			continue;
		for (np = ep->e_entries; np != NULL; np = np->e_sibling) {
			if (np->e_flags == 0) {
				dprintf(stdout,
				    "%s: remove unreferenced name\n",
				    myname(np));
				removeleaf(np);
				freeentry(np);
			}
		}
	}
	/*
	 * Any leaves remaining in removed directories is unreferenced.
	 */
	for (ep = removelist; ep != NULL; ep = ep->e_next) {
		for (np = ep->e_entries; np != NULL; np = np->e_sibling) {
			if (np->e_type == LEAF) {
				if (np->e_flags != 0)
					badentry(np, "unreferenced with flags");
				dprintf(stdout,
				    "%s: remove unreferenced name\n",
				    myname(np));
				removeleaf(np);
				freeentry(np);
			}
		}
	}
}

/*
 * Remove old nodes (directories).
 * Note that this routine runs in O(N*D) where:
 *	N is the number of directory entries to be removed.
 *	D is the maximum depth of the tree.
 * If N == D this can be quite slow. If the list were
 * topologically sorted, the deletion could be done in
 * time O(N).
 */
void
removeoldnodes(void)
{
	struct entry *ep, **prev;
	long change;

	vprintf(stdout, "Remove old nodes (directories).\n");
	do	{
		change = 0;
		prev = &removelist;
		for (ep = removelist; ep != NULL; ep = *prev) {
			if (ep->e_entries != NULL) {
				prev = &ep->e_next;
				continue;
			}
			*prev = ep->e_next;
			removenode(ep);
			freeentry(ep);
			change++;
		}
	} while (change);
	for (ep = removelist; ep != NULL; ep = ep->e_next)
		badentry(ep, "cannot remove, non-empty");
}

/*
 * This is the routine used to extract files for the 'r' command.
 * Extract new leaves.
 */
void
createleaves(char *symtabfile)
{
	struct entry *ep;
	ino_t first;
	long curvol;

	if (command == 'R') {
		vprintf(stdout, "Continue extraction of new leaves\n");
	} else {
		vprintf(stdout, "Extract new leaves.\n");
		dumpsymtable(symtabfile, volno);
	}
	first = lowerbnd(UFS_ROOTINO);
	curvol = volno;
	while (curfile.ino < maxino) {
		first = lowerbnd(first);
		/*
		 * If the next available file is not the one which we
		 * expect then we have missed one or more files. Since
		 * we do not request files that were not on the tape,
		 * the lost files must have been due to a tape read error,
		 * or a file that was removed while the dump was in progress.
		 */
		while (first < curfile.ino) {
			ep = lookupino(first);
			if (ep == NULL)
				panic("%ju: bad first\n", (uintmax_t)first);
			fprintf(stderr, "%s: not found on tape\n", myname(ep));
			ep->e_flags &= ~(NEW|EXTRACT);
			first = lowerbnd(first);
		}
		/*
		 * If we find files on the tape that have no corresponding
		 * directory entries, then we must have found a file that
		 * was created while the dump was in progress. Since we have
		 * no name for it, we discard it knowing that it will be
		 * on the next incremental tape.
		 */
		if (first != curfile.ino) {
			fprintf(stderr, "expected next file %ju, got %ju\n",
			    (uintmax_t)first, (uintmax_t)curfile.ino);
			skipfile();
			goto next;
		}
		ep = lookupino(curfile.ino);
		if (ep == NULL)
			panic("unknown file on tape\n");
		if ((ep->e_flags & (NEW|EXTRACT)) == 0)
			badentry(ep, "unexpected file on tape");
		/*
		 * If the file is to be extracted, then the old file must
		 * be removed since its type may change from one leaf type
		 * to another (e.g. "file" to "character special").
		 */
		if ((ep->e_flags & EXTRACT) != 0) {
			removeleaf(ep);
			ep->e_flags &= ~REMOVED;
		}
		(void) extractfile(myname(ep));
		ep->e_flags &= ~(NEW|EXTRACT);
		/*
		 * We checkpoint the restore after every tape reel, so
		 * as to simplify the amount of work required by the
		 * 'R' command.
		 */
	next:
		if (curvol != volno) {
			dumpsymtable(symtabfile, volno);
			skipmaps();
			curvol = volno;
		}
	}
}

/*
 * This is the routine used to extract files for the 'x' and 'i' commands.
 * Efficiently extract a subset of the files on a tape.
 */
void
createfiles(void)
{
	ino_t first, next, last;
	struct entry *ep;
	long curvol;

	vprintf(stdout, "Extract requested files\n");
	curfile.action = SKIP;
	getvol((long)1);
	skipmaps();
	skipdirs();
	first = lowerbnd(UFS_ROOTINO);
	last = upperbnd(maxino - 1);
	for (;;) {
		curvol = volno;
		first = lowerbnd(first);
		last = upperbnd(last);
		/*
		 * Check to see if any files remain to be extracted
		 */
		if (first > last)
			return;
		if (Dflag) {
			if (curfile.ino == maxino)
				return;
			if((ep = lookupino(curfile.ino)) != NULL &&
			    (ep->e_flags & (NEW|EXTRACT))) {
				goto justgetit;
			} else {
				skipfile();
				continue;
			}
		}
		/*
		 * Reject any volumes with inodes greater than the last
		 * one needed, so that we can quickly skip backwards to
		 * a volume containing useful inodes. We can't do this
		 * if there are no further volumes available (curfile.ino
		 * >= maxino) or if we are already at the first tape.
		 */
		if (curfile.ino > last && curfile.ino < maxino && volno > 1) {
			curfile.action = SKIP;
			getvol((long)0);
			skipmaps();
			skipdirs();
			continue;
		}
		/*
		 * Decide on the next inode needed.
		 * Skip across the inodes until it is found
		 * or a volume change is encountered
		 */
		if (curfile.ino < maxino) {
			next = lowerbnd(curfile.ino);
			while (next > curfile.ino && volno == curvol)
				skipfile();
			if (volno != curvol) {
				skipmaps();
				skipdirs();
				continue;
			}
		} else {
			/*
			 * No further volumes or inodes available. Set
			 * `next' to the first inode, so that a warning
			 * is emitted below for each missing file.
			 */
			next = first;
		}
		/*
		 * If the current inode is greater than the one we were
		 * looking for then we missed the one we were looking for.
		 * Since we only attempt to extract files listed in the
		 * dump map, the lost files must have been due to a tape
		 * read error, or a file that was removed while the dump
		 * was in progress. Thus we report all requested files
		 * between the one we were looking for, and the one we
		 * found as missing, and delete their request flags.
		 */
		while (next < curfile.ino) {
			ep = lookupino(next);
			if (ep == NULL)
				panic("corrupted symbol table\n");
			fprintf(stderr, "%s: not found on tape\n", myname(ep));
			ep->e_flags &= ~NEW;
			next = lowerbnd(next);
		}
		/*
		 * The current inode is the one that we are looking for,
		 * so extract it per its requested name.
		 */
		if (next == curfile.ino && next <= last) {
			ep = lookupino(next);
			if (ep == NULL)
				panic("corrupted symbol table\n");
justgetit:
			(void) extractfile(myname(ep));
			ep->e_flags &= ~NEW;
			if (volno != curvol)
				skipmaps();
		}
	}
}

/*
 * Add links.
 */
void
createlinks(void)
{
	struct entry *np, *ep;
	ino_t i;
	char name[BUFSIZ];

	if ((ep = lookupino(UFS_WINO))) {
		vprintf(stdout, "Add whiteouts\n");
		for ( ; ep != NULL; ep = ep->e_links) {
			if ((ep->e_flags & NEW) == 0)
				continue;
			(void) addwhiteout(myname(ep));
			ep->e_flags &= ~NEW;
		}
	}
	vprintf(stdout, "Add links\n");
	for (i = UFS_ROOTINO; i < maxino; i++) {
		ep = lookupino(i);
		if (ep == NULL)
			continue;
		for (np = ep->e_links; np != NULL; np = np->e_links) {
			if ((np->e_flags & NEW) == 0)
				continue;
			(void) strcpy(name, myname(ep));
			if (ep->e_type == NODE) {
				(void) linkit(name, myname(np), SYMLINK);
			} else {
				(void) linkit(name, myname(np), HARDLINK);
			}
			np->e_flags &= ~NEW;
		}
	}
}

/*
 * Check the symbol table.
 * We do this to insure that all the requested work was done, and
 * that no temporary names remain.
 */
void
checkrestore(void)
{
	struct entry *ep;
	ino_t i;

	vprintf(stdout, "Check the symbol table.\n");
	for (i = UFS_WINO; i < maxino; i++) {
		for (ep = lookupino(i); ep != NULL; ep = ep->e_links) {
			ep->e_flags &= ~KEEP;
			if (ep->e_type == NODE)
				ep->e_flags &= ~(NEW|EXISTED);
			if (ep->e_flags != 0)
				badentry(ep, "incomplete operations");
		}
	}
}

/*
 * Compare with the directory structure on the tape
 * A paranoid check that things are as they should be.
 */
long
verifyfile(char *name, ino_t ino, int type)
{
	struct entry *np, *ep;
	long descend = GOOD;

	ep = lookupname(name);
	if (ep == NULL) {
		fprintf(stderr, "Warning: missing name %s\n", name);
		return (FAIL);
	}
	np = lookupino(ino);
	if (np != ep)
		descend = FAIL;
	for ( ; np != NULL; np = np->e_links)
		if (np == ep)
			break;
	if (np == NULL)
		panic("missing inumber %ju\n", (uintmax_t)ino);
	if (ep->e_type == LEAF && type != LEAF)
		badentry(ep, "type should be LEAF");
	return (descend);
}
