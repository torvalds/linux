/*	$OpenBSD: utilities.c,v 1.20 2019/06/28 13:32:46 deraadt Exp $	*/
/*	$NetBSD: utilities.c,v 1.11 1997/03/19 08:42:56 lukem Exp $	*/

/*
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

#include <sys/stat.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>

#include <err.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "restore.h"
#include "extern.h"

/*
 * Insure that all the components of a pathname exist.
 */
void
pathcheck(char *name)
{
	char *cp;
	struct entry *ep;
	char *start;

	start = strchr(name, '/');
	if (start == 0)
		return;
	for (cp = start; *cp != '\0'; cp++) {
		if (*cp != '/')
			continue;
		*cp = '\0';
		ep = lookupname(name);
		if (ep == NULL) {
			/* Safe; we know the pathname exists in the dump. */
			ep = addentry(name, pathsearch(name)->d_ino, NODE);
			newnode(ep);
		}
		ep->e_flags |= NEW|KEEP;
		*cp = '/';
	}
}

/*
 * Change a name to a unique temporary name.
 */
void
mktempname(struct entry *ep)
{
	char oldname[PATH_MAX];

	if (ep->e_flags & TMPNAME)
		badentry(ep, "mktempname: called with TMPNAME");
	ep->e_flags |= TMPNAME;
	(void)strlcpy(oldname, myname(ep), sizeof oldname);
	freename(ep->e_name);
	ep->e_name = savename(gentempname(ep));
	ep->e_namlen = strlen(ep->e_name);
	renameit(oldname, myname(ep));
}

/*
 * Generate a temporary name for an entry.
 */
char *
gentempname(struct entry *ep)
{
	static char name[PATH_MAX];
	struct entry *np;
	long i = 0;

	for (np = lookupino(ep->e_ino);
	    np != NULL && np != ep; np = np->e_links)
		i++;
	if (np == NULL)
		badentry(ep, "not on ino list");
	(void)snprintf(name, sizeof(name), "%s%ld%llu", TMPHDR, i,
	    (unsigned long long)ep->e_ino);
	return (name);
}

/*
 * Rename a file or directory.
 */
void
renameit(char *from, char *to)
{
	if (!Nflag && rename(from, to) == -1) {
		warn("cannot rename %s to %s", from, to);
		return;
	}
	Vprintf(stdout, "rename %s to %s\n", from, to);
}

/*
 * Create a new node (directory).
 */
void
newnode(struct entry *np)
{
	char *cp;

	if (np->e_type != NODE)
		badentry(np, "newnode: not a node");
	cp = myname(np);
	if (!Nflag && mkdir(cp, 0777) == -1) {
		np->e_flags |= EXISTED;
		warn("%s", cp);
		return;
	}
	Vprintf(stdout, "Make node %s\n", cp);
}

/*
 * Remove an old node (directory).
 */
void
removenode(struct entry *ep)
{
	char *cp;

	if (ep->e_type != NODE)
		badentry(ep, "removenode: not a node");
	if (ep->e_entries != NULL)
		badentry(ep, "removenode: non-empty directory");
	ep->e_flags |= REMOVED;
	ep->e_flags &= ~TMPNAME;
	cp = myname(ep);
	if (!Nflag && rmdir(cp) == -1) {
		warn("%s", cp);
		return;
	}
	Vprintf(stdout, "Remove node %s\n", cp);
}

/*
 * Remove a leaf.
 */
void
removeleaf(struct entry *ep)
{
	char *cp;

	if (ep->e_type != LEAF)
		badentry(ep, "removeleaf: not a leaf");
	ep->e_flags |= REMOVED;
	ep->e_flags &= ~TMPNAME;
	cp = myname(ep);
	if (!Nflag && unlink(cp) == -1) {
		warn("%s", cp);
		return;
	}
	Vprintf(stdout, "Remove leaf %s\n", cp);
}

/*
 * Create a link.
 */
int
linkit(char *existing, char *new, int type)
{

	if (type == SYMLINK) {
		if (!Nflag && symlink(existing, new) == -1) {
			warn("cannot create symbolic link %s->%s",
			    new, existing);
			return (FAIL);
		}
	} else if (type == HARDLINK) {
		if (!Nflag && linkat(AT_FDCWD, existing, AT_FDCWD, new, 0)
		    == -1) {
			warn("cannot create hard link %s->%s",
			    new, existing);
			return (FAIL);
		}
	} else {
		panic("linkit: unknown type %d\n", type);
		return (FAIL);
	}
	Vprintf(stdout, "Create %s link %s->%s\n",
		type == SYMLINK ? "symbolic" : "hard", new, existing);
	return (GOOD);
}

/*
 * find lowest number file (above "start") that needs to be extracted
 */
ino_t
lowerbnd(ino_t start)
{
	struct entry *ep;

	for ( ; start < maxino; start++) {
		ep = lookupino(start);
		if (ep == NULL || ep->e_type == NODE)
			continue;
		if (ep->e_flags & (NEW|EXTRACT))
			return (start);
	}
	return (start);
}

/*
 * find highest number file (below "start") that needs to be extracted
 */
ino_t
upperbnd(ino_t start)
{
	struct entry *ep;

	for ( ; start > ROOTINO; start--) {
		ep = lookupino(start);
		if (ep == NULL || ep->e_type == NODE)
			continue;
		if (ep->e_flags & (NEW|EXTRACT))
			return (start);
	}
	return (start);
}

/*
 * report on a badly formed entry
 */
void
badentry(struct entry *ep, char *msg)
{

	fprintf(stderr, "bad entry: %s\n", msg);
	fprintf(stderr, "name: %s\n", myname(ep));
	fprintf(stderr, "parent name %s\n", myname(ep->e_parent));
	if (ep->e_sibling != NULL)
		fprintf(stderr, "sibling name: %s\n", myname(ep->e_sibling));
	if (ep->e_entries != NULL)
		fprintf(stderr, "next entry name: %s\n", myname(ep->e_entries));
	if (ep->e_links != NULL)
		fprintf(stderr, "next link name: %s\n", myname(ep->e_links));
	if (ep->e_next != NULL)
		fprintf(stderr,
		    "next hashchain name: %s\n", myname(ep->e_next));
	fprintf(stderr, "entry type: %s\n",
		ep->e_type == NODE ? "NODE" : "LEAF");
	fprintf(stderr, "inode number: %llu\n",
	    (unsigned long long)ep->e_ino);
	panic("flags: %s\n", flagvalues(ep));
}

/*
 * Construct a string indicating the active flag bits of an entry.
 */
char *
flagvalues(struct entry *ep)
{
	static char flagbuf[BUFSIZ];

	(void)strlcpy(flagbuf, "|NIL", sizeof flagbuf);
	flagbuf[0] = '\0';
	if (ep->e_flags & REMOVED)
		(void)strlcat(flagbuf, "|REMOVED", sizeof flagbuf);
	if (ep->e_flags & TMPNAME)
		(void)strlcat(flagbuf, "|TMPNAME", sizeof flagbuf);
	if (ep->e_flags & EXTRACT)
		(void)strlcat(flagbuf, "|EXTRACT", sizeof flagbuf);
	if (ep->e_flags & NEW)
		(void)strlcat(flagbuf, "|NEW", sizeof flagbuf);
	if (ep->e_flags & KEEP)
		(void)strlcat(flagbuf, "|KEEP", sizeof flagbuf);
	if (ep->e_flags & EXISTED)
		(void)strlcat(flagbuf, "|EXISTED", sizeof flagbuf);
	return (&flagbuf[1]);
}

/*
 * Check to see if a name is on a dump tape.
 */
ino_t
dirlookup(const char *name)
{
	struct direct *dp;
	ino_t ino;

	ino = ((dp = pathsearch(name)) == NULL) ? 0 : dp->d_ino;

	if (ino == 0 || TSTINO(ino, dumpmap) == 0)
		fprintf(stderr, "%s is not on the tape\n", name);
	return (ino);
}

/*
 * Elicit a reply.
 */
int
reply(char *question)
{
	int c;

	do	{
		fprintf(stderr, "%s? [yn] ", question);
		(void)fflush(stderr);
		c = getc(terminal);
		while (c != '\n' && getc(terminal) != '\n')
			if (feof(terminal))
				return (FAIL);
	} while (c != 'y' && c != 'n');
	if (c == 'y')
		return (GOOD);
	return (FAIL);
}

/*
 * handle unexpected inconsistencies
 */
void
panic(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (yflag)
		return;
	if (reply("abort") == GOOD) {
		if (reply("dump core") == GOOD)
			abort();
		exit(1);
	}
}
