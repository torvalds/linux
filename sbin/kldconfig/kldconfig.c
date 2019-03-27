/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Peter Pentchev
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* the default sysctl name */
#define PATHCTL	"kern.module_path"

/* queue structure for the module path broken down into components */
TAILQ_HEAD(pathhead, pathentry);
struct pathentry {
	char			*path;
	TAILQ_ENTRY(pathentry)	next;
};

/* the Management Information Base entries for the search path sysctl */
static int	 mib[5];
static size_t	 miblen;
/* the sysctl name, defaults to PATHCTL */
static char	*pathctl;
/* the sysctl value - the current module search path */
static char	*modpath;
/* flag whether user actions require changing the sysctl value */
static int	 changed;

/* Top-level path management functions */
static void	 addpath(struct pathhead *, char *, int, int);
static void	 rempath(struct pathhead *, char *, int, int);
static void	 showpath(struct pathhead *);

/* Low-level path management functions */
static char	*qstring(struct pathhead *);

/* sysctl-related functions */
static void	 getmib(void);
static void	 getpath(void);
static void	 parsepath(struct pathhead *, char *, int);
static void	 setpath(struct pathhead *);

static void	 usage(void);

/* Get the MIB entry for our sysctl */
static void
getmib(void)
{

	/* have we already fetched it? */
	if (miblen != 0)
		return;
	
	miblen = nitems(mib);
	if (sysctlnametomib(pathctl, mib, &miblen) != 0)
		err(1, "sysctlnametomib(%s)", pathctl);
}

/* Get the current module search path */
static void
getpath(void)
{
	char *path;
	size_t sz;

	if (modpath != NULL) {
		free(modpath);
		modpath = NULL;
	}

	if (miblen == 0)
		getmib();
	if (sysctl(mib, miblen, NULL, &sz, NULL, 0) == -1)
		err(1, "getting path: sysctl(%s) - size only", pathctl);
	if ((path = malloc(sz + 1)) == NULL) {
		errno = ENOMEM;
		err(1, "allocating %lu bytes for the path",
		    (unsigned long)sz+1);
	}
	if (sysctl(mib, miblen, path, &sz, NULL, 0) == -1)
		err(1, "getting path: sysctl(%s)", pathctl);
	modpath = path;
}

/* Set the module search path after changing it */
static void
setpath(struct pathhead *pathq)
{
	char *newpath;

	if (miblen == 0)
		getmib();
	if ((newpath = qstring(pathq)) == NULL) {
		errno = ENOMEM;
		err(1, "building path string");
	}
	if (sysctl(mib, miblen, NULL, NULL, newpath, strlen(newpath)+1) == -1)
		err(1, "setting path: sysctl(%s)", pathctl);

	if (modpath != NULL)
		free(modpath);
	modpath = newpath;
}

/* Add/insert a new component to the module search path */
static void
addpath(struct pathhead *pathq, char *path, int force, int insert)
{
	struct pathentry *pe, *pskip;
	char pathbuf[MAXPATHLEN+1];
	size_t len;
	static unsigned added = 0;
	unsigned i;

	/*
	 * If the path exists, use it; otherwise, take the user-specified
	 * path at face value - may be a removed directory.
	 */
	if (realpath(path, pathbuf) == NULL)
		strlcpy(pathbuf, path, sizeof(pathbuf));

	len = strlen(pathbuf);
	/* remove a terminating slash if present */
	if ((len > 0) && (pathbuf[len-1] == '/'))
		pathbuf[--len] = '\0';

	/* is it already in there? */
	TAILQ_FOREACH(pe, pathq, next)
		if (!strcmp(pe->path, pathbuf))
			break;
	if (pe != NULL) {
		if (force)
			return;
		errx(1, "already in the module search path: %s", pathbuf);
	}
	
	/* OK, allocate and add it. */
	if (((pe = malloc(sizeof(*pe))) == NULL) ||
	    ((pe->path = strdup(pathbuf)) == NULL)) {
		errno = ENOMEM;
		err(1, "allocating path component");
	}
	if (!insert) {
		TAILQ_INSERT_TAIL(pathq, pe, next);
	} else {
		for (i = 0, pskip = TAILQ_FIRST(pathq); i < added; i++)
			pskip = TAILQ_NEXT(pskip, next);
		if (pskip != NULL)
			TAILQ_INSERT_BEFORE(pskip, pe, next);
		else
			TAILQ_INSERT_TAIL(pathq, pe, next);
		added++;
	}
	changed = 1;
}

/* Remove a path component from the module search path */
static void
rempath(struct pathhead *pathq, char *path, int force, int insert __unused)
{
	char pathbuf[MAXPATHLEN+1];
	struct pathentry *pe;
	size_t len;

	/* same logic as in addpath() */
	if (realpath(path, pathbuf) == NULL)
		strlcpy(pathbuf, path, sizeof(pathbuf));

	len = strlen(pathbuf);
	/* remove a terminating slash if present */
	if ((len > 0) && (pathbuf[len-1] == '/'))
		pathbuf[--len] = '\0';

	/* Is it in there? */
	TAILQ_FOREACH(pe, pathq, next)
		if (!strcmp(pe->path, pathbuf))
			break;
	if (pe == NULL) {
		if (force)
			return;
		errx(1, "not in module search path: %s", pathbuf);
	}

	/* OK, remove it now.. */
	TAILQ_REMOVE(pathq, pe, next);
	changed = 1;
}

/* Display the retrieved module search path */
static void
showpath(struct pathhead *pathq)
{
	char *s;

	if ((s = qstring(pathq)) == NULL) {
		errno = ENOMEM;
		err(1, "building path string");
	}
	printf("%s\n", s);
	free(s);
}

/* Break a string down into path components, store them into a queue */
static void
parsepath(struct pathhead *pathq, char *path, int uniq)
{
	char *p;
	struct pathentry *pe;
	
	while ((p = strsep(&path, ";")) != NULL)
		if (!uniq) {
			if (((pe = malloc(sizeof(*pe))) == NULL) ||
			    ((pe->path = strdup(p)) == NULL)) {
				errno = ENOMEM;
				err(1, "allocating path element");
			}
			TAILQ_INSERT_TAIL(pathq, pe, next);
		} else {
			addpath(pathq, p, 1, 0);
		}
}

/* Recreate a path string from a components queue */
static char *
qstring(struct pathhead *pathq)
{
	char *s, *p;
	struct pathentry *pe;
	
	s = strdup("");
	TAILQ_FOREACH(pe, pathq, next) {
		asprintf(&p, "%s%s%s",
		    s, pe->path, (TAILQ_NEXT(pe, next) != NULL? ";": ""));
		free(s);
		if (p == NULL)
			return (NULL);
		s = p;
	}

	return (s);
}

/* Usage message */
static void
usage(void)
{

	fprintf(stderr, "%s\n%s\n",
	    "usage:\tkldconfig [-dfimnUv] [-S sysctlname] [path ...]",
	    "\tkldconfig -r");
	exit(1);
}

/* Main function */
int
main(int argc, char *argv[])
{
	/* getopt() iterator */
	int c;
	/* iterator over argv[] path components */
	int i;
	/* Command-line flags: */
	/* "-f" - no diagnostic messages */
	int fflag;
	/* "-i" - insert before the first element */
	int iflag;
	/* "-m" - merge into the existing path, do not replace it */
	int mflag;
	/* "-n" - do not actually set the new module path */
	int nflag;
	/* "-r" - print out the current search path */
	int rflag;
	/* "-U" - remove duplicate values from the path */
	int uniqflag;
	/* "-v" - verbose operation (currently a no-op) */
	int vflag;
	/* The higher-level function to call - add/remove */
	void (*act)(struct pathhead *, char *, int, int);
	/* The original path */
	char *origpath;
	/* The module search path broken down into components */
	struct pathhead pathq;

	fflag = iflag = mflag = nflag = rflag = uniqflag = vflag = 0;
	act = addpath;
	origpath = NULL;
	if ((pathctl = strdup(PATHCTL)) == NULL) {
		/* this is just too paranoid ;) */
		errno = ENOMEM;
		err(1, "initializing sysctl name %s", PATHCTL);
	}

	/* If no arguments and no options are specified, force '-m' */
	if (argc == 1)
		mflag = 1;

	while ((c = getopt(argc, argv, "dfimnrS:Uv")) != -1)
		switch (c) {
			case 'd':
				if (iflag || mflag)
					usage();
				act = rempath;
				break;
			case 'f':
				fflag = 1;
				break;
			case 'i':
				if (act != addpath)
					usage();
				iflag = 1;
				break;
			case 'm':
				if (act != addpath)
					usage();
				mflag = 1;
				break;
			case 'n':
				nflag = 1;
				break;
			case 'r':
				rflag = 1;
				break;
			case 'S':
				free(pathctl);
				if ((pathctl = strdup(optarg)) == NULL) {
					errno = ENOMEM;
					err(1, "sysctl name %s", optarg);
				}
				break;
			case 'U':
				uniqflag = 1;
				break;
			case 'v':
				vflag++;
				break;
			default:
				usage();
		}

	argc -= optind;
	argv += optind;

	/* The '-r' flag cannot be used when paths are also specified */
	if (rflag && (argc > 0))
		usage();

	TAILQ_INIT(&pathq);

	/* Retrieve and store the path from the sysctl value */
	getpath();
	if ((origpath = strdup(modpath)) == NULL) {
		errno = ENOMEM;
		err(1, "saving the original search path");
	}

	/*
	 * Break down the path into the components queue if:
	 * - we are NOT adding paths, OR
	 * - the 'merge' flag is specified, OR
	 * - the 'print only' flag is specified, OR
	 * - the 'unique' flag is specified.
	 */
	if ((act != addpath) || mflag || rflag || uniqflag)
		parsepath(&pathq, modpath, uniqflag);
	else if (modpath[0] != '\0')
		changed = 1;

	/* Process the path arguments */
	for (i = 0; i < argc; i++)
		act(&pathq, argv[i], fflag, iflag);

	if (changed && !nflag)
		setpath(&pathq);

	if (rflag || (changed && vflag)) {
		if (changed && (vflag > 1))
			printf("%s -> ", origpath);
		showpath(&pathq);
	}

	return (0);
}
