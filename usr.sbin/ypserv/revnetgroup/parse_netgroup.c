/* $OpenBSD: parse_netgroup.c,v 1.15 2024/10/09 01:52:11 jsg Exp $ */
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 *
 *	$FreeBSD: parse_netgroup.c,v 1.5 1997/02/22 14:22:02 peter Exp $
 */

/*
 * This is a specially hacked-up version of getnetgrent.c used to parse
 * data from the stored hash table of netgroup info rather than from a
 * file. It's used mainly for the parse_netgroup() function. All the YP
 * stuff and file support has been stripped out since it isn't needed.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "hash.h"

/*
 * Static Variables and functions used by setnetgrent(), getnetgrent() and
 * __endnetgrent().
 * There are two linked lists:
 * - linelist is just used by setnetgrent() to parse the net group file via.
 *   parse_netgrp()
 * - netgrp is the list of entries for the current netgroup
 */
struct linelist {
	struct linelist	*l_next;	/* Chain ptr. */
	int		l_parsed;	/* Flag for cycles */
	char		*l_groupname;	/* Name of netgroup */
	char		*l_line;	/* Netgroup entrie(s) to be parsed */
};

struct netgrp {
	struct netgrp	*ng_next;	/* Chain ptr */
	char		*ng_str[3];	/* Field pointers, see below */
};
#define NG_HOST		0	/* Host name */
#define NG_USER		1	/* User name */
#define NG_DOM		2	/* and Domain name */

static struct linelist	*linehead = NULL;
static struct netgrp	*nextgrp = NULL;
static struct {
	struct netgrp	*gr;
	char		*grname;
} grouphead = {
	NULL,
	NULL,
};

static int parse_netgrp(char *);
static struct linelist *read_for_group(char *);
void __setnetgrent(char *), __endnetgrent(void);
int __getnetgrent(char **, char **, char **);
extern struct group_entry *gtable[];

/*
 * setnetgrent()
 * Parse the netgroup file looking for the netgroup and build the list
 * of netgrp structures. Let parse_netgrp() and read_for_group() do
 * most of the work.
 */
void
__setnetgrent(char *group)
{
	/* Sanity check */

	if (group == NULL || !strlen(group))
		return;

	if (grouphead.gr == NULL || strcmp(group, grouphead.grname)) {
		__endnetgrent();
		if (parse_netgrp(group))
			__endnetgrent();
		else
			grouphead.grname = strdup(group);
	}
	nextgrp = grouphead.gr;
}

/*
 * Get the next netgroup off the list.
 */
int
__getnetgrent(char **hostp, char **userp, char **domp)
{
	if (nextgrp) {
		*hostp = nextgrp->ng_str[NG_HOST];
		*userp = nextgrp->ng_str[NG_USER];
		*domp = nextgrp->ng_str[NG_DOM];
		nextgrp = nextgrp->ng_next;
		return (1);
	}
	return (0);
}

/*
 * __endnetgrent() - cleanup
 */
void
__endnetgrent(void)
{
	struct linelist *lp, *olp;
	struct netgrp *gp, *ogp;

	lp = linehead;
	while (lp) {
		olp = lp;
		lp = lp->l_next;
		free(olp->l_groupname);
		free(olp->l_line);
		free(olp);
	}
	linehead = NULL;
	free(grouphead.grname);
	grouphead.grname = NULL;
	gp = grouphead.gr;
	while (gp) {
		ogp = gp;
		gp = gp->ng_next;
		free(ogp->ng_str[NG_HOST]);
		free(ogp->ng_str[NG_USER]);
		free(ogp->ng_str[NG_DOM]);
		free(ogp);
	}
	grouphead.gr = NULL;
}

/*
 * Parse the netgroup file setting up the linked lists.
 */
static int
parse_netgrp(char *group)
{
	char *spos, *epos;
	int len, strpos;
#ifdef DEBUG
	int fields;
#endif
	char *pos, *gpos;
	struct netgrp *grp;
	struct linelist *lp = linehead;

	/*
	 * First, see if the line has already been read in.
	 */
	while (lp) {
		if (!strcmp(group, lp->l_groupname))
			break;
		lp = lp->l_next;
	}
	if (lp == NULL && (lp = read_for_group(group)) == NULL)
		return (1);
	if (lp->l_parsed) {
#ifdef DEBUG
		/*
		 * This error message is largely superfluous since the
		 * code handles the error condition successfully, and
		 * spewing it out from inside libc can actually hose
		 * certain programs.
		 */
		fprintf(stderr, "Cycle in netgroup %s\n", lp->l_groupname);
#endif
		return (1);
	} else
		lp->l_parsed = 1;
	pos = lp->l_line;
	/* Watch for null pointer dereferences, dammit! */
	while (pos != NULL && *pos != '\0') {
		if (*pos == '(') {
			grp = malloc(sizeof(struct netgrp));
			bzero(grp, sizeof(struct netgrp));
			grp->ng_next = grouphead.gr;
			grouphead.gr = grp;
			pos++;
			gpos = strsep(&pos, ")");
#ifdef DEBUG
			fields = 0;
#endif
			for (strpos = 0; strpos < 3; strpos++) {
				if ((spos = strsep(&gpos, ","))) {
#ifdef DEBUG
					fields++;
#endif
					while (*spos == ' ' || *spos == '\t')
						spos++;
					if ((epos = strpbrk(spos, " \t"))) {
						*epos = '\0';
						len = epos - spos;
					} else
						len = strlen(spos);
					if (len > 0) {
						grp->ng_str[strpos] = malloc(len + 1);
						bcopy(spos, grp->ng_str[strpos],
						    len + 1);
					}
				} else {
					/*
					 * All other systems I've tested
					 * return NULL for empty netgroup
					 * fields. It's up to user programs
					 * to handle the NULLs appropriately.
					 */
					grp->ng_str[strpos] = NULL;
				}
			}
#ifdef DEBUG
			/*
			 * Note: on other platforms, malformed netgroup
			 * entries are not normally flagged. While we
			 * can catch bad entries and report them, we should
			 * stay silent by default for compatibility's sake.
			 */
			if (fields < 3)
					fprintf(stderr, "Bad entry (%s%s%s%s%s) in netgroup \"%s\"\n",
						grp->ng_str[NG_HOST] == NULL ? "" : grp->ng_str[NG_HOST],
						grp->ng_str[NG_USER] == NULL ? "" : ",",
						grp->ng_str[NG_USER] == NULL ? "" : grp->ng_str[NG_USER],
						grp->ng_str[NG_DOM] == NULL ? "" : ",",
						grp->ng_str[NG_DOM] == NULL ? "" : grp->ng_str[NG_DOM],
						lp->l_groupname);
#endif
		} else {
			spos = strsep(&pos, ", \t");
			if (parse_netgrp(spos))
				continue;
		}
		/* Watch for null pointer dereferences, dammit! */
		if (pos != NULL)
			while (*pos == ' ' || *pos == ',' || *pos == '\t')
				pos++;
	}
	return (0);
}

/*
 * Read the netgroup file and save lines until the line for the netgroup
 * is found. Return 1 if eof is encountered.
 */
static struct linelist *
read_for_group(char *group)
{
	char *pos, *spos, *linep = NULL, *olinep = NULL;
	int len, olen;
	int cont;
	struct linelist *lp;
	char line[LINSIZ + 1];
	char *data = NULL;

	data = lookup (gtable, group);
	snprintf(line, sizeof line, "%s %s", group, data);
	pos = (char *)&line;
#ifdef CANT_HAPPEN
	if (*pos == '#')
		continue;
#endif
	while (*pos == ' ' || *pos == '\t')
		pos++;
	spos = pos;
	while (*pos != ' ' && *pos != '\t' && *pos != '\n' &&
		*pos != '\0')
		pos++;
	len = pos - spos;
	while (*pos == ' ' || *pos == '\t')
		pos++;
	if (*pos != '\n' && *pos != '\0') {
		lp = malloc(sizeof(*lp));
		lp->l_parsed = 0;
		lp->l_groupname = malloc(len + 1);
		bcopy(spos, lp->l_groupname, len);
		*(lp->l_groupname + len) = '\0';
		len = strlen(pos);
		olen = 0;
		/*
		 * Loop around handling line continuations.
		 */
		do {
			if (*(pos + len - 1) == '\n')
				len--;
			if (*(pos + len - 1) == '\\') {
				len--;
				cont = 1;
			} else
				cont = 0;
			if (len > 0) {
				linep = malloc(olen + len + 1);
				if (olen > 0) {
					bcopy(olinep, linep, olen);
					free(olinep);
				}
				bcopy(pos, linep + olen, len);
				olen += len;
				*(linep + olen) = '\0';
				olinep = linep;
			}
#ifdef CANT_HAPPEN
			if (cont) {
				if (fgets(line, sizeof(line), netf)) {
					pos = line;
					len = strlen(pos);
				} else
					cont = 0;
			}
#endif
		} while (cont);
		lp->l_line = linep;
		lp->l_next = linehead;
		linehead = lp;
#ifdef CANT_HAPPEN
		/*
		 * If this is the one we wanted, we are done.
		 */
		if (!strcmp(lp->l_groupname, group))
#endif
			return (lp);
	}
	return (NULL);
}
