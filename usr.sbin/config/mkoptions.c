/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995  Peter Wemm
 * Copyright (c) 1980, 1993
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
static char sccsid[] = "@(#)mkheaders.c	8.1 (Berkeley) 6/6/93";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

/*
 * Make all the .h files for the optional entries
 */

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "config.h"
#include "y.tab.h"

static	struct users {
	int	u_default;
	int	u_min;
	int	u_max;
} users = { 8, 2, 512 };

static char *lower(char *);
static void read_options(void);
static void do_option(char *);
static char *tooption(char *);

void
options(void)
{
	char buf[40];
	struct cputype *cp;
	struct opt_list *ol;
	struct opt *op;

	/* Fake the cpu types as options. */
	SLIST_FOREACH(cp, &cputype, cpu_next) {
		op = (struct opt *)calloc(1, sizeof(*op));
		if (op == NULL)
			err(EXIT_FAILURE, "calloc");
		op->op_name = ns(cp->cpu_name);
		SLIST_INSERT_HEAD(&opt, op, op_next);
	}	

	if (maxusers == 0) {
		/* fprintf(stderr, "maxusers not specified; will auto-size\n"); */
	} else if (maxusers < users.u_min) {
		fprintf(stderr, "minimum of %d maxusers assumed\n",
		    users.u_min);
		maxusers = users.u_min;
	} else if (maxusers > users.u_max)
		fprintf(stderr, "warning: maxusers > %d (%d)\n",
		    users.u_max, maxusers);

	/* Fake MAXUSERS as an option. */
	op = (struct opt *)calloc(1, sizeof(*op));
	if (op == NULL)
		err(EXIT_FAILURE, "calloc");
	op->op_name = ns("MAXUSERS");
	snprintf(buf, sizeof(buf), "%d", maxusers);
	op->op_value = ns(buf);
	SLIST_INSERT_HEAD(&opt, op, op_next);

	read_options();

	/* Fake the value of MACHINE_ARCH as an option if necessary */
	SLIST_FOREACH(ol, &otab, o_next) {
		if (strcasecmp(ol->o_name, machinearch) != 0)
			continue;

		op = (struct opt *)calloc(1, sizeof(*op));
		if (op == NULL)
			err(EXIT_FAILURE, "calloc");
		op->op_name = ns(ol->o_name);
		SLIST_INSERT_HEAD(&opt, op, op_next);
		break;
	}

	SLIST_FOREACH(op, &opt, op_next) {
		SLIST_FOREACH(ol, &otab, o_next) {
			if (eq(op->op_name, ol->o_name) &&
			    (ol->o_flags & OL_ALIAS)) {
				fprintf(stderr, "Mapping option %s to %s.\n",
				    op->op_name, ol->o_file);
				op->op_name = ol->o_file;
				break;
			}
		}
	}
	SLIST_FOREACH(ol, &otab, o_next)
		do_option(ol->o_name);
	SLIST_FOREACH(op, &opt, op_next) {
		if (!op->op_ownfile && strncmp(op->op_name, "DEV_", 4)) {
			fprintf(stderr, "%s: unknown option \"%s\"\n",
			       PREFIX, op->op_name);
			exit(1);
		}
	}
}

/*
 * Generate an <options>.h file
 */

static void
do_option(char *name)
{
	char *file, *inw;
	const char *basefile;
	struct opt_list *ol;
	struct opt *op;
	struct opt_head op_head;
	FILE *inf, *outf;
	char *value;
	char *oldvalue;
	int seen;
	int tidy;

	file = tooption(name);
	/*
	 * Check to see if the option was specified..
	 */
	value = NULL;
	SLIST_FOREACH(op, &opt, op_next) {
		if (eq(name, op->op_name)) {
			oldvalue = value;
			value = op->op_value;
			if (value == NULL)
				value = ns("1");
			if (oldvalue != NULL && !eq(value, oldvalue))
				fprintf(stderr,
			    "%s: option \"%s\" redefined from %s to %s\n",
				   PREFIX, op->op_name, oldvalue,
				   value);
			op->op_ownfile++;
		}
	}

	remember(file);
	inf = fopen(file, "r");
	if (inf == NULL) {
		outf = fopen(file, "w");
		if (outf == NULL)
			err(1, "%s", file);

		/* was the option in the config file? */
		if (value) {
			fprintf(outf, "#define %s %s\n", name, value);
		} /* else empty file */

		(void)fclose(outf);
		return;
	}
	basefile = "";
	SLIST_FOREACH(ol, &otab, o_next)
		if (eq(name, ol->o_name)) {
			basefile = ol->o_file;
			break;
		}
	oldvalue = NULL;
	SLIST_INIT(&op_head);
	seen = 0;
	tidy = 0;
	for (;;) {
		char *cp;
		char *invalue;

		/* get the #define */
		if ((inw = get_word(inf)) == NULL || inw == (char *)EOF)
			break;
		/* get the option name */
		if ((inw = get_word(inf)) == NULL || inw == (char *)EOF)
			break;
		inw = ns(inw);
		/* get the option value */
		if ((cp = get_word(inf)) == NULL || cp == (char *)EOF)
			break;
		/* option value */
		invalue = ns(cp); /* malloced */
		if (eq(inw, name)) {
			oldvalue = invalue;
			invalue = value;
			seen++;
		}
		SLIST_FOREACH(ol, &otab, o_next)
			if (eq(inw, ol->o_name))
				break;
		if (!eq(inw, name) && !ol) {
			fprintf(stderr,
			    "WARNING: unknown option `%s' removed from %s\n",
			    inw, file);
			tidy++;
		} else if (ol != NULL && !eq(basefile, ol->o_file)) {
			fprintf(stderr,
			    "WARNING: option `%s' moved from %s to %s\n",
			    inw, basefile, ol->o_file);
			tidy++;
		} else {
			op = (struct opt *) calloc(1, sizeof *op);
			if (op == NULL)
				err(EXIT_FAILURE, "calloc");
			op->op_name = inw;
			op->op_value = invalue;
			SLIST_INSERT_HEAD(&op_head, op, op_next);
		}

		/* EOL? */
		cp = get_word(inf);
		if (cp == (char *)EOF)
			break;
	}
	(void)fclose(inf);
	if (!tidy && ((value == NULL && oldvalue == NULL) ||
	    (value && oldvalue && eq(value, oldvalue)))) {	
		while (!SLIST_EMPTY(&op_head)) {
			op = SLIST_FIRST(&op_head);
			SLIST_REMOVE_HEAD(&op_head, op_next);
			free(op->op_name);
			free(op->op_value);
			free(op);
		}
		return;
	}

	if (value && !seen) {
		/* New option appears */
		op = (struct opt *) calloc(1, sizeof *op);
		if (op == NULL)
			err(EXIT_FAILURE, "calloc");
		op->op_name = ns(name);
		op->op_value = value ? ns(value) : NULL;
		SLIST_INSERT_HEAD(&op_head, op, op_next);
	}

	outf = fopen(file, "w");
	if (outf == NULL)
		err(1, "%s", file);
	while (!SLIST_EMPTY(&op_head)) {
		op = SLIST_FIRST(&op_head);
		/* was the option in the config file? */
		if (op->op_value) {
			fprintf(outf, "#define %s %s\n",
				op->op_name, op->op_value);
		}
		SLIST_REMOVE_HEAD(&op_head, op_next);
		free(op->op_name);
		free(op->op_value);
		free(op);
	}
	(void)fclose(outf);
}

/*
 * Find the filename to store the option spec into.
 */
static char *
tooption(char *name)
{
	static char hbuf[MAXPATHLEN];
	char nbuf[MAXPATHLEN];
	struct opt_list *po;

	/* "cannot happen"?  the otab list should be complete.. */
	(void)strlcpy(nbuf, "options.h", sizeof(nbuf));

	SLIST_FOREACH(po, &otab, o_next) {
		if (eq(po->o_name, name)) {
			strlcpy(nbuf, po->o_file, sizeof(nbuf));
			break;
		}
	}

	(void)strlcpy(hbuf, path(nbuf), sizeof(hbuf));
	return (hbuf);
}

	
static void
check_duplicate(const char *fname, const char *this)
{
	struct opt_list *po;

	SLIST_FOREACH(po, &otab, o_next) {
		if (eq(po->o_name, this)) {
			fprintf(stderr, "%s: Duplicate option %s.\n",
			    fname, this);
			exit(1);
		}
	}
}

static void
insert_option(const char *fname, char *this, char *val)
{
	struct opt_list *po;

	check_duplicate(fname, this);
	po = (struct opt_list *) calloc(1, sizeof *po);
	if (po == NULL)
		err(EXIT_FAILURE, "calloc");
	po->o_name = this;
	po->o_file = val;
	po->o_flags = 0;
	SLIST_INSERT_HEAD(&otab, po, o_next);
}

static void
update_option(const char *this, char *val, int flags)
{
	struct opt_list *po;

	SLIST_FOREACH(po, &otab, o_next) {
		if (eq(po->o_name, this)) {
			free(po->o_file);
			po->o_file = val;
			po->o_flags = flags;
			return;
		}
	}
	/*
	 * Option not found, but that's OK, we just ignore it since it
	 * may be for another arch.
	 */
	return;
}

static int
read_option_file(const char *fname, int flags)
{
	FILE *fp;
	char *wd, *this, *val;
	char genopt[MAXPATHLEN];

	fp = fopen(fname, "r");
	if (fp == NULL)
		return (0);
	while ((wd = get_word(fp)) != (char *)EOF) {
		if (wd == NULL)
			continue;
		if (wd[0] == '#') {
			while (((wd = get_word(fp)) != (char *)EOF) && wd)
				continue;
			continue;
		}
		this = ns(wd);
		val = get_word(fp);
		if (val == (char *)EOF)
			return (1);
		if (val == NULL) {
			if (flags) {
				fprintf(stderr, "%s: compat file requires two"
				    " words per line at %s\n", fname, this);
				exit(1);
			}
			char *s = ns(this);
			(void)snprintf(genopt, sizeof(genopt), "opt_%s.h",
			    lower(s));
			val = genopt;
			free(s);
		}
		val = ns(val);
		if (flags == 0)
			insert_option(fname, this, val);
		else
			update_option(this, val, flags);
	}
	(void)fclose(fp);
	return (1);
}

/*
 * read the options and options.<machine> files
 */
static void
read_options(void)
{
	char fname[MAXPATHLEN];

	SLIST_INIT(&otab);
	read_option_file("../../conf/options", 0);
	(void)snprintf(fname, sizeof fname, "../../conf/options.%s",
	    machinename);
	if (!read_option_file(fname, 0)) {
		(void)snprintf(fname, sizeof fname, "options.%s", machinename);
		read_option_file(fname, 0);
	}
	read_option_file("../../conf/options-compat", OL_ALIAS);
}

static char *
lower(char *str)
{
	char *cp = str;

	while (*str) {
		if (isupper(*str))
			*str = tolower(*str);
		str++;
	}
	return (cp);
}
