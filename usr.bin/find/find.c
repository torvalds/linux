/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Cimarron D. Taylor of the University of California, Berkeley.
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
static char sccsid[] = "@(#)find.c	8.5 (Berkeley) 8/5/94";
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fts.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "find.h"

static int find_compare(const FTSENT * const *s1, const FTSENT * const *s2);

/*
 * find_compare --
 *	tell fts_open() how to order the traversal of the hierarchy. 
 *	This variant gives lexicographical order, i.e., alphabetical
 *	order within each directory.
 */
static int
find_compare(const FTSENT * const *s1, const FTSENT * const *s2)
{

	return (strcoll((*s1)->fts_name, (*s2)->fts_name));
}

/*
 * find_formplan --
 *	process the command line and create a "plan" corresponding to the
 *	command arguments.
 */
PLAN *
find_formplan(char *argv[])
{
	PLAN *plan, *tail, *new;

	/*
	 * for each argument in the command line, determine what kind of node
	 * it is, create the appropriate node type and add the new plan node
	 * to the end of the existing plan.  The resulting plan is a linked
	 * list of plan nodes.  For example, the string:
	 *
	 *	% find . -name foo -newer bar -print
	 *
	 * results in the plan:
	 *
	 *	[-name foo]--> [-newer bar]--> [-print]
	 *
	 * in this diagram, `[-name foo]' represents the plan node generated
	 * by c_name() with an argument of foo and `-->' represents the
	 * plan->next pointer.
	 */
	for (plan = tail = NULL; *argv;) {
		if (!(new = find_create(&argv)))
			continue;
		if (plan == NULL)
			tail = plan = new;
		else {
			tail->next = new;
			tail = new;
		}
	}

	/*
	 * if the user didn't specify one of -print, -ok or -exec, then -print
	 * is assumed so we bracket the current expression with parens, if
	 * necessary, and add a -print node on the end.
	 */
	if (!isoutput) {
		OPTION *p;
		char **argv1 = 0;

		if (plan == NULL) {
			p = lookup_option("-print");
			new = (p->create)(p, &argv1);
			tail = plan = new;
		} else {
			p = lookup_option("(");
			new = (p->create)(p, &argv1);
			new->next = plan;
			plan = new;
			p = lookup_option(")");
			new = (p->create)(p, &argv1);
			tail->next = new;
			tail = new;
			p = lookup_option("-print");
			new = (p->create)(p, &argv1);
			tail->next = new;
			tail = new;
		}
	}

	/*
	 * the command line has been completely processed into a search plan
	 * except for the (, ), !, and -o operators.  Rearrange the plan so
	 * that the portions of the plan which are affected by the operators
	 * are moved into operator nodes themselves.  For example:
	 *
	 *	[!]--> [-name foo]--> [-print]
	 *
	 * becomes
	 *
	 *	[! [-name foo] ]--> [-print]
	 *
	 * and
	 *
	 *	[(]--> [-depth]--> [-name foo]--> [)]--> [-print]
	 *
	 * becomes
	 *
	 *	[expr [-depth]-->[-name foo] ]--> [-print]
	 *
	 * operators are handled in order of precedence.
	 */

	plan = paren_squish(plan);		/* ()'s */
	plan = not_squish(plan);		/* !'s */
	plan = or_squish(plan);			/* -o's */
	return (plan);
}

FTS *tree;			/* pointer to top of FTS hierarchy */

/*
 * find_execute --
 *	take a search plan and an array of search paths and executes the plan
 *	over all FTSENT's returned for the given search paths.
 */
int
find_execute(PLAN *plan, char *paths[])
{
	FTSENT *entry;
	PLAN *p;
	int e;

	tree = fts_open(paths, ftsoptions, (issort ? find_compare : NULL));
	if (tree == NULL)
		err(1, "ftsopen");

	exitstatus = 0;
	while (errno = 0, (entry = fts_read(tree)) != NULL) {
		if (maxdepth != -1 && entry->fts_level >= maxdepth) {
			if (fts_set(tree, entry, FTS_SKIP))
				err(1, "%s", entry->fts_path);
		}

		switch (entry->fts_info) {
		case FTS_D:
			if (isdepth)
				continue;
			break;
		case FTS_DP:
			if (!isdepth)
				continue;
			break;
		case FTS_DNR:
		case FTS_NS:
			if (ignore_readdir_race &&
			    entry->fts_errno == ENOENT && entry->fts_level > 0)
				continue;
			/* FALLTHROUGH */
		case FTS_ERR:
			(void)fflush(stdout);
			warnx("%s: %s",
			    entry->fts_path, strerror(entry->fts_errno));
			exitstatus = 1;
			continue;
#if defined(FTS_W) && defined(FTS_WHITEOUT)
		case FTS_W:
			if (ftsoptions & FTS_WHITEOUT)
				break;
			continue;
#endif /* FTS_W */
		}
#define	BADCH	" \t\n\\'\""
		if (isxargs && strpbrk(entry->fts_path, BADCH)) {
			(void)fflush(stdout);
			warnx("%s: illegal path", entry->fts_path);
			exitstatus = 1;
			continue;
		}

		if (mindepth != -1 && entry->fts_level < mindepth)
			continue;

		/*
		 * Call all the functions in the execution plan until one is
		 * false or all have been executed.  This is where we do all
		 * the work specified by the user on the command line.
		 */
		for (p = plan; p && (p->execute)(p, entry); p = p->next);
	}
	e = errno;
	finish_execplus();
	if (e && (!ignore_readdir_race || e != ENOENT))
		errc(1, e, "fts_read");
	return (exitstatus);
}
