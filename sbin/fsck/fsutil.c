/*	$OpenBSD: fsutil.c,v 1.25 2025/02/26 06:18:56 otto Exp $	*/
/*	$NetBSD: fsutil.c,v 1.2 1996/10/03 20:06:31 christos Exp $	*/

/*
 * Copyright (c) 1990, 1993
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <fstab.h>
#include <limits.h>
#include <err.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "fsutil.h"

static const char *dev = NULL;
static const char *origdev = NULL;
static int hot = 0;
static int preen = 0;

extern char *__progname;

static void vmsg(int, const char *, va_list);

struct stat stslash;

void
checkroot(void)
{
	if (stat("/", &stslash) == -1) {
		xperror("/");
		printf("Can't stat root\n");
	}
}

void
setcdevname(const char *cd, const char *ocd, int pr)
{
	dev = cd;
	origdev = ocd;
	preen = pr;
}

const char *
cdevname(void)
{
	return dev;
}

int
hotroot(void)
{
	return hot;
}

void
errexit(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(8);
}

static void
vmsg(int fatal, const char *fmt, va_list ap)
{
	if (!fatal && preen) {
		if (origdev)
			printf("%s (%s): ", dev, origdev);
		else
			printf("%s: ", dev);
	}

	(void) vprintf(fmt, ap);
	
	if (fatal && preen) {
		printf("\n");
		if (origdev)
			printf("%s (%s): ", dev, origdev);
		else
			printf("%s: ", dev);
		printf("UNEXPECTED INCONSISTENCY; RUN %s MANUALLY.\n",
		    __progname);
		exit(8);
	}
}

void
pfatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmsg(1, fmt, ap);
	va_end(ap);
}

void
pwarn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmsg(0, fmt, ap);
	va_end(ap);
}

void
xperror(const char *s)
{
	pfatal("%s (%s)", s, strerror(errno));
}

void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmsg(1, fmt, ap);
	va_end(ap);
	exit(8);
}

char *
unrawname(char *name)
{
	char *dp;
	struct stat stb;

	if ((dp = strrchr(name, '/')) == NULL)
		return (name);
	if (stat(name, &stb) == -1)
		return (name);
	if (!S_ISCHR(stb.st_mode))
		return (name);
	if (dp[1] != 'r')
		return (name);
	(void)memmove(&dp[1], &dp[2], strlen(&dp[2]) + 1);
	return (name);
}

char *
rawname(char *name)
{
	static char rawbuf[PATH_MAX];
	char *dp;

	if ((dp = strrchr(name, '/')) == NULL)
		return (0);
	*dp = 0;
	(void)strlcpy(rawbuf, name, sizeof rawbuf);
	*dp = '/';
	(void)strlcat(rawbuf, "/r", sizeof rawbuf);
	(void)strlcat(rawbuf, &dp[1], sizeof rawbuf);
	return (rawbuf);
}

char *
blockcheck(char *origname)
{
	struct stat stblock, stchar;
	char *newname, *raw;
	struct fstab *fsp;
	int retried = 0;

	hot = 0;
	newname = origname;
retry:
	if (stat(newname, &stblock) == -1)
		return (origname);

	if (S_ISBLK(stblock.st_mode)) {
		if (stslash.st_dev == stblock.st_rdev)
			hot++;
		raw = rawname(newname);
		if (raw == NULL) {
			printf("Can't get raw name of %s\n", newname);
			return (origname);
		}
		if (stat(raw, &stchar) == -1) {
			xperror(raw);
			printf("Can't stat %s\n", raw);
			return (origname);
		}
		if (S_ISCHR(stchar.st_mode)) {
			return (raw);
		} else {
			printf("%s is not a character device\n", raw);
			return (origname);
		}
	} else if (S_ISCHR(stblock.st_mode) && !retried) {
		newname = unrawname(newname);
		retried++;
		goto retry;
	} else if ((fsp = getfsfile(newname)) != 0 && !retried) {
		newname = fsp->fs_spec;
		retried++;
		goto retry;
	}
	/*
	 * Not a block or character device, just return name and
	 * let the user decide whether to use it.
	 */
	return (origname);
}


void *
emalloc(size_t s)
{
	void *p;

	if (s == 0)
		err(1, "malloc failed");
	p = malloc(s);
	if (p == NULL)
		err(1, "malloc failed");
	return p;
}


void *
ereallocarray(void *p, size_t n, size_t s)
{
	void *newp;

	if (n == 0 || s == 0) {
		free(p);
		err(1, "realloc failed");
	}
	newp = reallocarray(p, n, s);
	if (newp == NULL) {
		free(p);
		err(1, "realloc failed");
	}
	return newp;
}


char *
estrdup(const char *s)
{
	char *p = strdup(s);
	if (p == NULL)
		err(1, "strdup failed");
	return p;
}
