/*	$OpenBSD: verify.c,v 1.21 2016/08/16 16:41:46 krw Exp $	*/
/*	$NetBSD: verify.c,v 1.10 1995/03/07 21:26:28 cgd Exp $	*/

/*-
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

#include <sys/stat.h>
#include <dirent.h>
#include <fts.h>
#include <fnmatch.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include "mtree.h"
#include "extern.h"

extern u_int32_t crc_total;
extern int ftsoptions;
extern int dflag, eflag, qflag, rflag, sflag, uflag;
extern char fullpath[PATH_MAX];

static NODE *root;
static char path[PATH_MAX];

static void	miss(NODE *, char *, size_t);
static int	vwalk(void);

int
verify(void)
{
	int rval;

	root = spec();
	rval = vwalk();
	miss(root, path, sizeof(path));
	return (rval);
}

static int
vwalk(void)
{
	FTS *t;
	FTSENT *p;
	NODE *ep, *level;
	int specdepth, rval;
	char *argv[2];

	argv[0] = ".";
	argv[1] = NULL;
	if ((t = fts_open(argv, ftsoptions, dsort)) == NULL)
		error("fts_open: %s", strerror(errno));
	level = root;
	specdepth = rval = 0;
	while ((p = fts_read(t))) {
		switch(p->fts_info) {
		case FTS_D:
			break;
		case FTS_DP:
			if (specdepth > p->fts_level) {
				for (level = level->parent; level->prev;
				      level = level->prev);
				--specdepth;
			}
			continue;
		case FTS_DNR:
		case FTS_ERR:
		case FTS_NS:
			(void)fprintf(stderr, "mtree: %s: %s\n",
			    RP(p), strerror(p->fts_errno));
			continue;
		default:
			if (dflag)
				continue;
		}

		if (specdepth != p->fts_level)
			goto extra;
		for (ep = level; ep; ep = ep->next)
			if ((ep->flags & F_MAGIC &&
			    !fnmatch(ep->name, p->fts_name, FNM_PATHNAME)) ||
			    !strcmp(ep->name, p->fts_name)) {
				ep->flags |= F_VISIT;
				if ((ep->flags & F_NOCHANGE) == 0 &&
				    compare(ep->name, ep, p))
					rval = MISMATCHEXIT;
				if (ep->flags & F_IGN)
					(void)fts_set(t, p, FTS_SKIP);
				else if (ep->child && ep->type == F_DIR &&
				    p->fts_info == FTS_D) {
					level = ep->child;
					++specdepth;
				}
				break;
			}

		if (ep)
			continue;
extra:
		if (!eflag) {
			(void)printf("extra: %s", RP(p));
			if (rflag) {
				if ((S_ISDIR(p->fts_statp->st_mode)
				    ? rmdir : unlink)(p->fts_accpath)) {
					(void)printf(", not removed: %s",
					    strerror(errno));
					rval = ERROREXIT;
				} else
					(void)printf(", removed");
			} else
				rval = MISMATCHEXIT;
			(void)putchar('\n');
		}
		(void)fts_set(t, p, FTS_SKIP);
	}
	(void)fts_close(t);
	if (sflag)
		(void)fprintf(stderr,
		    "mtree: %s checksum: %u\n", fullpath, crc_total);
	return (rval);
}

static void
miss(NODE *p, char *tail, size_t len)
{
	int create;
	char *tp;

	for (; p; p = p->next) {
		if ((p->flags & F_OPT) && !(p->flags & F_VISIT))
			continue;
		if (p->type != F_DIR && (dflag || p->flags & F_VISIT))
			continue;
		(void)strlcpy(tail, p->name, len);
		if (!(p->flags & F_VISIT)) {
			/* Don't print missing message if file exists as a
			   symbolic link and the -q flag is set. */
			struct stat statbuf;

			if (qflag && stat(path, &statbuf) == 0)
				p->flags |= F_VISIT;
			else
				(void)printf("missing: %s", path);
		}
		if (p->type != F_DIR) {
			putchar('\n');
			continue;
		}

		create = 0;
		if (!(p->flags & F_VISIT) && uflag) {
			if (!(p->flags & (F_UID | F_UNAME)))
			    (void)printf(" (not created: user not specified)");
			else if (!(p->flags & (F_GID | F_GNAME)))
			    (void)printf(" (not created: group not specified)");
			else if (!(p->flags & F_MODE))
			    (void)printf(" (not created: mode not specified)");
			else if (mkdir(path, S_IRWXU))
				(void)printf(" (not created: %s)",
				    strerror(errno));
			else {
				create = 1;
				(void)printf(" (created)");
			}
		}

		if (!(p->flags & F_VISIT))
			(void)putchar('\n');

		for (tp = tail; *tp; ++tp);
		*tp = '/';
		miss(p->child, tp + 1, len - (tp + 1 - tail));
		*tp = '\0';

		if (!create)
			continue;
		if (chown(path, p->st_uid, p->st_gid)) {
			(void)printf("%s: user/group/mode not modified: %s\n",
			    path, strerror(errno));
			continue;
		}
		if (chmod(path, p->st_mode))
			(void)printf("%s: permissions not set: %s\n",
			    path, strerror(errno));
	}
}
