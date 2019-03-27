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
static char sccsid[] = "@(#)symtab.c	8.3 (Berkeley) 4/28/95";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

/*
 * These routines maintain the symbol table which tracks the state
 * of the file system being restored. They provide lookup by either
 * name or inode number. They also provide for creation, deletion,
 * and renaming of entries. Because of the dynamic nature of pathnames,
 * names should not be saved, but always constructed just before they
 * are needed, by calling "myname".
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <ufs/ufs/dinode.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "restore.h"
#include "extern.h"

/*
 * The following variables define the inode symbol table.
 * The primary hash table is dynamically allocated based on
 * the number of inodes in the file system (maxino), scaled by
 * HASHFACTOR. The variable "entry" points to the hash table;
 * the variable "entrytblsize" indicates its size (in entries).
 */
#define HASHFACTOR 5
static struct entry **entry;
static long entrytblsize;

static void		 addino(ino_t, struct entry *);
static struct entry	*lookupparent(char *);
static void		 removeentry(struct entry *);

/*
 * Look up an entry by inode number
 */
struct entry *
lookupino(ino_t inum)
{
	struct entry *ep;

	if (inum < UFS_WINO || inum >= maxino)
		return (NULL);
	for (ep = entry[inum % entrytblsize]; ep != NULL; ep = ep->e_next)
		if (ep->e_ino == inum)
			return (ep);
	return (NULL);
}

/*
 * Add an entry into the entry table
 */
static void
addino(ino_t inum, struct entry *np)
{
	struct entry **epp;

	if (inum < UFS_WINO || inum >= maxino)
		panic("addino: out of range %ju\n", (uintmax_t)inum);
	epp = &entry[inum % entrytblsize];
	np->e_ino = inum;
	np->e_next = *epp;
	*epp = np;
	if (dflag)
		for (np = np->e_next; np != NULL; np = np->e_next)
			if (np->e_ino == inum)
				badentry(np, "duplicate inum");
}

/*
 * Delete an entry from the entry table
 */
void
deleteino(ino_t inum)
{
	struct entry *next;
	struct entry **prev;

	if (inum < UFS_WINO || inum >= maxino)
		panic("deleteino: out of range %ju\n", (uintmax_t)inum);
	prev = &entry[inum % entrytblsize];
	for (next = *prev; next != NULL; next = next->e_next) {
		if (next->e_ino == inum) {
			next->e_ino = 0;
			*prev = next->e_next;
			return;
		}
		prev = &next->e_next;
	}
	panic("deleteino: %ju not found\n", (uintmax_t)inum);
}

/*
 * Look up an entry by name
 */
struct entry *
lookupname(char *name)
{
	struct entry *ep;
	char *np, *cp;
	char buf[MAXPATHLEN];

	cp = name;
	for (ep = lookupino(UFS_ROOTINO); ep != NULL; ep = ep->e_entries) {
		for (np = buf; *cp != '/' && *cp != '\0' &&
				np < &buf[sizeof(buf)]; )
			*np++ = *cp++;
		if (np == &buf[sizeof(buf)])
			break;
		*np = '\0';
		for ( ; ep != NULL; ep = ep->e_sibling)
			if (strcmp(ep->e_name, buf) == 0)
				break;
		if (ep == NULL)
			break;
		if (*cp++ == '\0')
			return (ep);
	}
	return (NULL);
}

/*
 * Look up the parent of a pathname
 */
static struct entry *
lookupparent(char *name)
{
	struct entry *ep;
	char *tailindex;

	tailindex = strrchr(name, '/');
	if (tailindex == NULL)
		return (NULL);
	*tailindex = '\0';
	ep = lookupname(name);
	*tailindex = '/';
	if (ep == NULL)
		return (NULL);
	if (ep->e_type != NODE)
		panic("%s is not a directory\n", name);
	return (ep);
}

/*
 * Determine the current pathname of a node or leaf
 */
char *
myname(struct entry *ep)
{
	char *cp;
	static char namebuf[MAXPATHLEN];

	for (cp = &namebuf[MAXPATHLEN - 2]; cp > &namebuf[ep->e_namlen]; ) {
		cp -= ep->e_namlen;
		memmove(cp, ep->e_name, (long)ep->e_namlen);
		if (ep == lookupino(UFS_ROOTINO))
			return (cp);
		*(--cp) = '/';
		ep = ep->e_parent;
	}
	panic("%s: pathname too long\n", cp);
	return(cp);
}

/*
 * Unused symbol table entries are linked together on a free list
 * headed by the following pointer.
 */
static struct entry *freelist = NULL;

/*
 * add an entry to the symbol table
 */
struct entry *
addentry(char *name, ino_t inum, int type)
{
	struct entry *np, *ep;

	if (freelist != NULL) {
		np = freelist;
		freelist = np->e_next;
		memset(np, 0, (long)sizeof(struct entry));
	} else {
		np = (struct entry *)calloc(1, sizeof(struct entry));
		if (np == NULL)
			panic("no memory to extend symbol table\n");
	}
	np->e_type = type & ~LINK;
	ep = lookupparent(name);
	if (ep == NULL) {
		if (inum != UFS_ROOTINO || lookupino(UFS_ROOTINO) != NULL)
			panic("bad name to addentry %s\n", name);
		np->e_name = savename(name);
		np->e_namlen = strlen(name);
		np->e_parent = np;
		addino(UFS_ROOTINO, np);
		return (np);
	}
	np->e_name = savename(strrchr(name, '/') + 1);
	np->e_namlen = strlen(np->e_name);
	np->e_parent = ep;
	np->e_sibling = ep->e_entries;
	ep->e_entries = np;
	if (type & LINK) {
		ep = lookupino(inum);
		if (ep == NULL)
			panic("link to non-existent name\n");
		np->e_ino = inum;
		np->e_links = ep->e_links;
		ep->e_links = np;
	} else if (inum != 0) {
		if (lookupino(inum) != NULL)
			panic("duplicate entry\n");
		addino(inum, np);
	}
	return (np);
}

/*
 * delete an entry from the symbol table
 */
void
freeentry(struct entry *ep)
{
	struct entry *np;
	ino_t inum;

	if (ep->e_flags != REMOVED)
		badentry(ep, "not marked REMOVED");
	if (ep->e_type == NODE) {
		if (ep->e_links != NULL)
			badentry(ep, "freeing referenced directory");
		if (ep->e_entries != NULL)
			badentry(ep, "freeing non-empty directory");
	}
	if (ep->e_ino != 0) {
		np = lookupino(ep->e_ino);
		if (np == NULL)
			badentry(ep, "lookupino failed");
		if (np == ep) {
			inum = ep->e_ino;
			deleteino(inum);
			if (ep->e_links != NULL)
				addino(inum, ep->e_links);
		} else {
			for (; np != NULL; np = np->e_links) {
				if (np->e_links == ep) {
					np->e_links = ep->e_links;
					break;
				}
			}
			if (np == NULL)
				badentry(ep, "link not found");
		}
	}
	removeentry(ep);
	freename(ep->e_name);
	ep->e_next = freelist;
	freelist = ep;
}

/*
 * Relocate an entry in the tree structure
 */
void
moveentry(struct entry *ep, char *newname)
{
	struct entry *np;
	char *cp;

	np = lookupparent(newname);
	if (np == NULL)
		badentry(ep, "cannot move ROOT");
	if (np != ep->e_parent) {
		removeentry(ep);
		ep->e_parent = np;
		ep->e_sibling = np->e_entries;
		np->e_entries = ep;
	}
	cp = strrchr(newname, '/') + 1;
	freename(ep->e_name);
	ep->e_name = savename(cp);
	ep->e_namlen = strlen(cp);
	if (strcmp(gentempname(ep), ep->e_name) == 0)
		ep->e_flags |= TMPNAME;
	else
		ep->e_flags &= ~TMPNAME;
}

/*
 * Remove an entry in the tree structure
 */
static void
removeentry(struct entry *ep)
{
	struct entry *np;

	np = ep->e_parent;
	if (np->e_entries == ep) {
		np->e_entries = ep->e_sibling;
	} else {
		for (np = np->e_entries; np != NULL; np = np->e_sibling) {
			if (np->e_sibling == ep) {
				np->e_sibling = ep->e_sibling;
				break;
			}
		}
		if (np == NULL)
			badentry(ep, "cannot find entry in parent list");
	}
}

/*
 * Table of unused string entries, sorted by length.
 *
 * Entries are allocated in STRTBLINCR sized pieces so that names
 * of similar lengths can use the same entry. The value of STRTBLINCR
 * is chosen so that every entry has at least enough space to hold
 * a "struct strtbl" header. Thus every entry can be linked onto an
 * appropriate free list.
 *
 * NB. The macro "allocsize" below assumes that "struct strhdr"
 *     has a size that is a power of two.
 */
struct strhdr {
	struct strhdr *next;
};

#define STRTBLINCR	(sizeof(struct strhdr))
#define	allocsize(size)	roundup2((size) + 1, STRTBLINCR)

static struct strhdr strtblhdr[allocsize(NAME_MAX) / STRTBLINCR];

/*
 * Allocate space for a name. It first looks to see if it already
 * has an appropriate sized entry, and if not allocates a new one.
 */
char *
savename(char *name)
{
	struct strhdr *np;
	size_t len;
	char *cp;

	if (name == NULL)
		panic("bad name\n");
	len = strlen(name);
	np = strtblhdr[len / STRTBLINCR].next;
	if (np != NULL) {
		strtblhdr[len / STRTBLINCR].next = np->next;
		cp = (char *)np;
	} else {
		cp = malloc(allocsize(len));
		if (cp == NULL)
			panic("no space for string table\n");
	}
	(void) strcpy(cp, name);
	return (cp);
}

/*
 * Free space for a name. The resulting entry is linked onto the
 * appropriate free list.
 */
void
freename(char *name)
{
	struct strhdr *tp, *np;

	tp = &strtblhdr[strlen(name) / STRTBLINCR];
	np = (struct strhdr *)name;
	np->next = tp->next;
	tp->next = np;
}

/*
 * Useful quantities placed at the end of a dumped symbol table.
 */
struct symtableheader {
	int32_t	volno;
	int32_t	stringsize;
	int32_t	entrytblsize;
	time_t	dumptime;
	time_t	dumpdate;
	ino_t	maxino;
	int32_t	ntrec;
};

/*
 * dump a snapshot of the symbol table
 */
void
dumpsymtable(char *filename, long checkpt)
{
	struct entry *ep, *tep;
	ino_t i;
	struct entry temp, *tentry;
	long mynum = 1, stroff = 0;
	FILE *fd;
	struct symtableheader hdr;

	vprintf(stdout, "Checkpointing the restore\n");
	if (Nflag)
		return;
	if ((fd = fopen(filename, "w")) == NULL) {
		fprintf(stderr, "fopen: %s\n", strerror(errno));
		panic("cannot create save file %s for symbol table\n",
			filename);
		done(1);
	}
	clearerr(fd);
	/*
	 * Assign indices to each entry
	 * Write out the string entries
	 */
	for (i = UFS_WINO; i <= maxino; i++) {
		for (ep = lookupino(i); ep != NULL; ep = ep->e_links) {
			ep->e_index = mynum++;
			(void) fwrite(ep->e_name, sizeof(char),
			       (int)allocsize(ep->e_namlen), fd);
		}
	}
	/*
	 * Convert pointers to indexes, and output
	 */
	tep = &temp;
	stroff = 0;
	for (i = UFS_WINO; i <= maxino; i++) {
		for (ep = lookupino(i); ep != NULL; ep = ep->e_links) {
			memmove(tep, ep, (long)sizeof(struct entry));
			tep->e_name = (char *)stroff;
			stroff += allocsize(ep->e_namlen);
			tep->e_parent = (struct entry *)ep->e_parent->e_index;
			if (ep->e_links != NULL)
				tep->e_links =
					(struct entry *)ep->e_links->e_index;
			if (ep->e_sibling != NULL)
				tep->e_sibling =
					(struct entry *)ep->e_sibling->e_index;
			if (ep->e_entries != NULL)
				tep->e_entries =
					(struct entry *)ep->e_entries->e_index;
			if (ep->e_next != NULL)
				tep->e_next =
					(struct entry *)ep->e_next->e_index;
			(void) fwrite((char *)tep, sizeof(struct entry), 1, fd);
		}
	}
	/*
	 * Convert entry pointers to indexes, and output
	 */
	for (i = 0; i < entrytblsize; i++) {
		if (entry[i] == NULL)
			tentry = NULL;
		else
			tentry = (struct entry *)entry[i]->e_index;
		(void) fwrite((char *)&tentry, sizeof(struct entry *), 1, fd);
	}
	hdr.volno = checkpt;
	hdr.maxino = maxino;
	hdr.entrytblsize = entrytblsize;
	hdr.stringsize = stroff;
	hdr.dumptime = dumptime;
	hdr.dumpdate = dumpdate;
	hdr.ntrec = ntrec;
	(void) fwrite((char *)&hdr, sizeof(struct symtableheader), 1, fd);
	if (ferror(fd)) {
		fprintf(stderr, "fwrite: %s\n", strerror(errno));
		panic("output error to file %s writing symbol table\n",
			filename);
	}
	(void) fclose(fd);
}

/*
 * Initialize a symbol table from a file
 */
void
initsymtable(char *filename)
{
	char *base;
	long tblsize;
	struct entry *ep;
	struct entry *baseep, *lep;
	struct symtableheader hdr;
	struct stat stbuf;
	long i;
	int fd;

	vprintf(stdout, "Initialize symbol table.\n");
	if (filename == NULL) {
		entrytblsize = maxino / HASHFACTOR;
		entry = calloc((unsigned)entrytblsize, sizeof(struct entry *));
		if (entry == NULL)
			panic("no memory for entry table\n");
		ep = addentry(".", UFS_ROOTINO, NODE);
		ep->e_flags |= NEW;
		return;
	}
	if ((fd = open(filename, O_RDONLY, 0)) < 0) {
		fprintf(stderr, "open: %s\n", strerror(errno));
		panic("cannot open symbol table file %s\n", filename);
	}
	if (fstat(fd, &stbuf) < 0) {
		fprintf(stderr, "stat: %s\n", strerror(errno));
		panic("cannot stat symbol table file %s\n", filename);
	}
	tblsize = stbuf.st_size - sizeof(struct symtableheader);
	base = calloc(sizeof(char), (unsigned)tblsize);
	if (base == NULL)
		panic("cannot allocate space for symbol table\n");
	if (read(fd, base, (int)tblsize) < 0 ||
	    read(fd, (char *)&hdr, sizeof(struct symtableheader)) < 0) {
		fprintf(stderr, "read: %s\n", strerror(errno));
		panic("cannot read symbol table file %s\n", filename);
	}
	(void)close(fd);
	switch (command) {
	case 'r':
		/*
		 * For normal continuation, insure that we are using
		 * the next incremental tape
		 */
		if (hdr.dumpdate != dumptime) {
			if (hdr.dumpdate < dumptime)
				fprintf(stderr, "Incremental tape too low\n");
			else
				fprintf(stderr, "Incremental tape too high\n");
			done(1);
		}
		break;
	case 'R':
		/*
		 * For restart, insure that we are using the same tape
		 */
		curfile.action = SKIP;
		dumptime = hdr.dumptime;
		dumpdate = hdr.dumpdate;
		if (!bflag)
			newtapebuf(hdr.ntrec);
		getvol(hdr.volno);
		break;
	default:
		panic("initsymtable called from command %c\n", command);
		break;
	}
	maxino = hdr.maxino;
	entrytblsize = hdr.entrytblsize;
	entry = (struct entry **)
		(base + tblsize - (entrytblsize * sizeof(struct entry *)));
	baseep = (struct entry *)(base + hdr.stringsize - sizeof(struct entry));
	lep = (struct entry *)entry;
	for (i = 0; i < entrytblsize; i++) {
		if (entry[i] == NULL)
			continue;
		entry[i] = &baseep[(long)entry[i]];
	}
	for (ep = &baseep[1]; ep < lep; ep++) {
		ep->e_name = base + (long)ep->e_name;
		ep->e_parent = &baseep[(long)ep->e_parent];
		if (ep->e_sibling != NULL)
			ep->e_sibling = &baseep[(long)ep->e_sibling];
		if (ep->e_links != NULL)
			ep->e_links = &baseep[(long)ep->e_links];
		if (ep->e_entries != NULL)
			ep->e_entries = &baseep[(long)ep->e_entries];
		if (ep->e_next != NULL)
			ep->e_next = &baseep[(long)ep->e_next];
	}
}
