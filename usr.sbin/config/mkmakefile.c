/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1990, 1993
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
static char sccsid[] = "@(#)mkmakefile.c	8.1 (Berkeley) 6/6/93";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

/*
 * Build the makefile for the system, from
 * the information in the files files and the
 * additional files for the machine being compiled to.
 */

#include <ctype.h>
#include <err.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/cnv.h>
#include <sys/nv.h>
#include <sys/param.h>
#include "y.tab.h"
#include "config.h"
#include "configvers.h"

static char *tail(char *);
static void do_clean(FILE *);
static void do_rules(FILE *);
static void do_xxfiles(char *, FILE *);
static void do_objs(FILE *);
static void do_before_depend(FILE *);
static int opteq(const char *, const char *);
static void read_files(void);
static void sanitize_envline(char *result, const char *src);
static bool preprocess(char *line, char *result);
static void process_into_file(char *line, FILE *ofp);
static void process_into_nvlist(char *line, nvlist_t *nvl);
static void dump_nvlist(nvlist_t *nvl, FILE *ofp);

static void errout(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(1);
}

/*
 * Lookup a file, by name.
 */
static struct file_list *
fl_lookup(char *file)
{
	struct file_list *fp;

	STAILQ_FOREACH(fp, &ftab, f_next) {
		if (eq(fp->f_fn, file))
			return (fp);
	}
	return (0);
}

/*
 * Make a new file list entry
 */
static struct file_list *
new_fent(void)
{
	struct file_list *fp;

	fp = (struct file_list *) calloc(1, sizeof *fp);
	if (fp == NULL)
		err(EXIT_FAILURE, "calloc");
	STAILQ_INSERT_TAIL(&ftab, fp, f_next);
	return (fp);
}

/*
 * Open the correct Makefile and return it, or error out.
 */
FILE *
open_makefile_template(void)
{
	FILE *ifp;
	char line[BUFSIZ];

	snprintf(line, sizeof(line), "../../conf/Makefile.%s", machinename);
	ifp = fopen(line, "r");
	if (ifp == NULL) {
		snprintf(line, sizeof(line), "Makefile.%s", machinename);
		ifp = fopen(line, "r");
	}
	if (ifp == NULL)
		err(1, "%s", line);
	return (ifp);
}

/*
 * Build the makefile from the skeleton
 */
void
makefile(void)
{
	FILE *ifp, *ofp;
	char line[BUFSIZ];
	struct opt *op, *t;

	read_files();
	ifp = open_makefile_template();
	ofp = fopen(path("Makefile.new"), "w");
	if (ofp == NULL)
		err(1, "%s", path("Makefile.new"));
	fprintf(ofp, "KERN_IDENT=%s\n", ident);
	fprintf(ofp, "MACHINE=%s\n", machinename);
	fprintf(ofp, "MACHINE_ARCH=%s\n", machinearch);
	SLIST_FOREACH_SAFE(op, &mkopt, op_next, t) {
		fprintf(ofp, "%s=%s", op->op_name, op->op_value);
		while ((op = SLIST_NEXT(op, op_append)) != NULL)
			fprintf(ofp, " %s", op->op_value);
		fprintf(ofp, "\n");
	}
	if (debugging)
		fprintf(ofp, "DEBUG=-g\n");
	if (profiling)
		fprintf(ofp, "PROFLEVEL=%d\n", profiling);
	if (*srcdir != '\0')
		fprintf(ofp,"S=%s\n", srcdir);
	while (fgets(line, BUFSIZ, ifp) != NULL) {
		if (*line != '%') {
			fprintf(ofp, "%s", line);
			continue;
		}
		if (eq(line, "%BEFORE_DEPEND\n"))
			do_before_depend(ofp);
		else if (eq(line, "%OBJS\n"))
			do_objs(ofp);
		else if (strncmp(line, "%FILES.", 7) == 0)
			do_xxfiles(line, ofp);
		else if (eq(line, "%RULES\n"))
			do_rules(ofp);
		else if (eq(line, "%CLEAN\n"))
			do_clean(ofp);
		else if (strncmp(line, "%VERSREQ=", 9) == 0)
			line[0] = '\0'; /* handled elsewhere */
		else
			fprintf(stderr,
			    "Unknown %% construct in generic makefile: %s",
			    line);
	}
	(void) fclose(ifp);
	(void) fclose(ofp);
	moveifchanged(path("Makefile.new"), path("Makefile"));
}

static void
sanitize_envline(char *result, const char *src)
{
	const char *eq;
	char c, *dst;
	bool leading;

	/* If there is no '=' it's not a well-formed name=value line. */
	if ((eq = strchr(src, '=')) == NULL) {
		*result = 0;
		return;
	}
	dst = result;

	/* Copy chars before the '=', skipping any leading spaces/quotes. */
	leading = true;
	while (src < eq) {
		c = *src++;
		if (leading && (isspace(c) || c == '"'))
			continue;
		*dst++ = c;
		leading = false;
	}

	/* If it was all leading space, we don't have a well-formed line. */
	if (leading) {
		*result = 0;
		return;
	}

	/* Trim spaces/quotes immediately before the '=', then copy the '='. */
	while (isspace(dst[-1]) || dst[-1] == '"')
		--dst;
	*dst++ = *src++;

	/* Copy chars after the '=', skipping any leading whitespace. */
	leading = true;
	while ((c = *src++) != 0) {
		if (leading && (isspace(c) || c == '"'))
			continue;
		*dst++ = c;
		leading = false;
	}

	/* If it was all leading space, it's a valid 'var=' (nil value). */
	if (leading) {
		*dst = 0;
		return;
	}

	/* Trim trailing whitespace and quotes. */
	while (isspace(dst[-1]) || dst[-1] == '"')
		--dst;

	*dst = 0;
}

/*
 * Returns true if the caller may use the string.
 */
static bool
preprocess(char *line, char *result)
{
	char *s;

	/* Strip any comments */
	if ((s = strchr(line, '#')) != NULL)
		*s = '\0';
	sanitize_envline(result, line);
	/* Return true if it's non-empty */
	return (*result != '\0');
}

static void
process_into_file(char *line, FILE *ofp)
{
	char result[BUFSIZ];

	if (preprocess(line, result))
		fprintf(ofp, "\"%s\\0\"\n", result);
}

static void
process_into_nvlist(char *line, nvlist_t *nvl)
{
	char result[BUFSIZ], *s;

	if (preprocess(line, result)) {
		s = strchr(result, '=');
		*s = '\0';
		if (nvlist_exists(nvl, result))
			nvlist_free(nvl, result);
		nvlist_add_string(nvl, result, s + 1);
	}
}

static void
dump_nvlist(nvlist_t *nvl, FILE *ofp)
{
	const char *name;
	void *cookie;

	if (nvl == NULL)
		return;

	while (!nvlist_empty(nvl)) {
		cookie = NULL;
		name = nvlist_next(nvl, NULL, &cookie);
		fprintf(ofp, "\"%s=%s\\0\"\n", name,
		     cnvlist_get_string(cookie));

		cnvlist_free_string(cookie);
	}
}

/*
 * Build hints.c from the skeleton
 */
void
makehints(void)
{
	FILE *ifp, *ofp;
	nvlist_t *nvl;
	char line[BUFSIZ];
	struct hint *hint;

	ofp = fopen(path("hints.c.new"), "w");
	if (ofp == NULL)
		err(1, "%s", path("hints.c.new"));
	fprintf(ofp, "#include <sys/types.h>\n");
	fprintf(ofp, "#include <sys/systm.h>\n");
	fprintf(ofp, "\n");
	/*
	 * Write out hintmode for older kernels. Remove when config(8) major
	 * version rolls over.
	 */
	if (versreq <= CONFIGVERS_ENVMODE_REQ)
		fprintf(ofp, "int hintmode = %d;\n",
			!STAILQ_EMPTY(&hints) ? 1 : 0);
	fprintf(ofp, "char static_hints[] = {\n");
	nvl = nvlist_create(0);
	STAILQ_FOREACH(hint, &hints, hint_next) {
		ifp = fopen(hint->hint_name, "r");
		if (ifp == NULL)
			err(1, "%s", hint->hint_name);
		while (fgets(line, BUFSIZ, ifp) != NULL)
			process_into_nvlist(line, nvl);
		dump_nvlist(nvl, ofp);
		fclose(ifp);
	}
	nvlist_destroy(nvl);
	fprintf(ofp, "\"\\0\"\n};\n");
	fclose(ofp);
	moveifchanged(path("hints.c.new"), path("hints.c"));
}

/*
 * Build env.c from the skeleton
 */
void
makeenv(void)
{
	FILE *ifp, *ofp;
	nvlist_t *nvl;
	char line[BUFSIZ];
	struct envvar *envvar;

	ofp = fopen(path("env.c.new"), "w");
	if (ofp == NULL)
		err(1, "%s", path("env.c.new"));
	fprintf(ofp, "#include <sys/types.h>\n");
	fprintf(ofp, "#include <sys/systm.h>\n");
	fprintf(ofp, "\n");
	/*
	 * Write out envmode for older kernels. Remove when config(8) major
	 * version rolls over.
	 */
	if (versreq <= CONFIGVERS_ENVMODE_REQ)
		fprintf(ofp, "int envmode = %d;\n",
			!STAILQ_EMPTY(&envvars) ? 1 : 0);
	fprintf(ofp, "char static_env[] = {\n");
	nvl = nvlist_create(0);
	STAILQ_FOREACH(envvar, &envvars, envvar_next) {
		if (envvar->env_is_file) {
			ifp = fopen(envvar->env_str, "r");
			if (ifp == NULL)
				err(1, "%s", envvar->env_str);
			while (fgets(line, BUFSIZ, ifp) != NULL)
				process_into_nvlist(line, nvl);
			dump_nvlist(nvl, ofp);
			fclose(ifp);
		} else
			process_into_file(envvar->env_str, ofp);
	}
	nvlist_destroy(nvl);
	fprintf(ofp, "\"\\0\"\n};\n");
	fclose(ofp);
	moveifchanged(path("env.c.new"), path("env.c"));
}

static void
read_file(char *fname)
{
	char ifname[MAXPATHLEN];
	FILE *fp;
	struct file_list *tp;
	struct device *dp;
	struct opt *op;
	char *wd, *this, *compilewith, *depends, *clean, *warning;
	const char *objprefix;
	int compile, match, nreqs, std, filetype, not,
	    imp_rule, no_obj, before_depend, nowerror;

	fp = fopen(fname, "r");
	if (fp == NULL)
		err(1, "%s", fname);
next:
	/*
	 * include "filename"
	 * filename    [ standard | optional ]
	 *	[ dev* [ | dev* ... ] | profiling-routine ] [ no-obj ]
	 *	[ compile-with "compile rule" [no-implicit-rule] ]
	 *      [ dependency "dependency-list"] [ before-depend ]
	 *	[ clean "file-list"] [ warning "text warning" ]
	 *	[ obj-prefix "file prefix"]
	 */
	wd = get_word(fp);
	if (wd == (char *)EOF) {
		(void) fclose(fp);
		return;
	} 
	if (wd == NULL)
		goto next;
	if (wd[0] == '#')
	{
		while (((wd = get_word(fp)) != (char *)EOF) && wd)
			;
		goto next;
	}
	if (eq(wd, "include")) {
		wd = get_quoted_word(fp);
		if (wd == (char *)EOF || wd == NULL)
			errout("%s: missing include filename.\n", fname);
		(void) snprintf(ifname, sizeof(ifname), "../../%s", wd);
		read_file(ifname);
		while (((wd = get_word(fp)) != (char *)EOF) && wd)
			;
		goto next;
	}
	this = ns(wd);
	wd = get_word(fp);
	if (wd == (char *)EOF)
		return;
	if (wd == NULL)
		errout("%s: No type for %s.\n", fname, this);
	tp = fl_lookup(this);
	compile = 0;
	match = 1;
	nreqs = 0;
	compilewith = 0;
	depends = 0;
	clean = 0;
	warning = 0;
	std = 0;
	imp_rule = 0;
	no_obj = 0;
	before_depend = 0;
	nowerror = 0;
	not = 0;
	filetype = NORMAL;
	objprefix = "";
	if (eq(wd, "standard"))
		std = 1;
	else if (!eq(wd, "optional"))
		errout("%s: \"%s\" %s must be optional or standard\n",
		    fname, wd, this);
	for (wd = get_word(fp); wd; wd = get_word(fp)) {
		if (wd == (char *)EOF)
			return;
		if (eq(wd, "!")) {
			not = 1;
			continue;
		}
		if (eq(wd, "|")) {
			if (nreqs == 0)
				errout("%s: syntax error describing %s\n",
				       fname, this);
			compile += match;
			match = 1;
			nreqs = 0;
			continue;
		}
		if (eq(wd, "no-obj")) {
			no_obj++;
			continue;
		}
		if (eq(wd, "no-implicit-rule")) {
			if (compilewith == NULL)
				errout("%s: alternate rule required when "
				       "\"no-implicit-rule\" is specified for"
				       " %s.\n",
				       fname, this);
			imp_rule++;
			continue;
		}
		if (eq(wd, "before-depend")) {
			before_depend++;
			continue;
		}
		if (eq(wd, "dependency")) {
			wd = get_quoted_word(fp);
			if (wd == (char *)EOF || wd == NULL)
				errout("%s: %s missing dependency string.\n",
				       fname, this);
			depends = ns(wd);
			continue;
		}
		if (eq(wd, "clean")) {
			wd = get_quoted_word(fp);
			if (wd == (char *)EOF || wd == NULL)
				errout("%s: %s missing clean file list.\n",
				       fname, this);
			clean = ns(wd);
			continue;
		}
		if (eq(wd, "compile-with")) {
			wd = get_quoted_word(fp);
			if (wd == (char *)EOF || wd == NULL)
				errout("%s: %s missing compile command string.\n",
				       fname, this);
			compilewith = ns(wd);
			continue;
		}
		if (eq(wd, "warning")) {
			wd = get_quoted_word(fp);
			if (wd == (char *)EOF || wd == NULL)
				errout("%s: %s missing warning text string.\n",
				       fname, this);
			warning = ns(wd);
			continue;
		}
		if (eq(wd, "obj-prefix")) {
			wd = get_quoted_word(fp);
			if (wd == (char *)EOF || wd == NULL)
				errout("%s: %s missing object prefix string.\n",
				       fname, this);
			objprefix = ns(wd);
			continue;
		}
		if (eq(wd, "nowerror")) {
			nowerror = 1;
			continue;
		}
		if (eq(wd, "local")) {
			filetype = LOCAL;
			continue;
		}
		if (eq(wd, "no-depend")) {
			filetype = NODEPEND;
			continue;
		}
		nreqs++;
		if (eq(wd, "profiling-routine")) {
			filetype = PROFILING;
			continue;
		}
		if (std)
			errout("standard entry %s has optional inclusion specifier %s!\n",
			       this, wd);
		STAILQ_FOREACH(dp, &dtab, d_next)
			if (eq(dp->d_name, wd)) {
				if (not)
					match = 0;
				else
					dp->d_done |= DEVDONE;
				goto nextparam;
			}
		SLIST_FOREACH(op, &opt, op_next)
			if (op->op_value == 0 && opteq(op->op_name, wd)) {
				if (not)
					match = 0;
				goto nextparam;
			}
		match &= not;
nextparam:;
		not = 0;
	}
	compile += match;
	if (compile && tp == NULL) {
		if (std == 0 && nreqs == 0)
			errout("%s: what is %s optional on?\n",
			       fname, this);
		if (filetype == PROFILING && profiling == 0)
			goto next;
		tp = new_fent();
		tp->f_fn = this;
		tp->f_type = filetype;
		if (filetype == LOCAL)
			tp->f_srcprefix = "";
		else
			tp->f_srcprefix = "$S/";
		if (imp_rule)
			tp->f_flags |= NO_IMPLCT_RULE;
		if (no_obj)
			tp->f_flags |= NO_OBJ;
		if (before_depend)
			tp->f_flags |= BEFORE_DEPEND;
		if (nowerror)
			tp->f_flags |= NOWERROR;
		tp->f_compilewith = compilewith;
		tp->f_depends = depends;
		tp->f_clean = clean;
		tp->f_warn = warning;
		tp->f_objprefix = objprefix;
	}
	goto next;
}

/*
 * Read in the information about files used in making the system.
 * Store it in the ftab linked list.
 */
static void
read_files(void)
{
	char fname[MAXPATHLEN];
	struct files_name *nl, *tnl;
	
	(void) snprintf(fname, sizeof(fname), "../../conf/files");
	read_file(fname);
	(void) snprintf(fname, sizeof(fname),
		       	"../../conf/files.%s", machinename);
	read_file(fname);
	for (nl = STAILQ_FIRST(&fntab); nl != NULL; nl = tnl) {
		read_file(nl->f_name);
		tnl = STAILQ_NEXT(nl, f_next);
		free(nl->f_name);
		free(nl);
	}
}

static int
opteq(const char *cp, const char *dp)
{
	char c, d;

	for (; ; cp++, dp++) {
		if (*cp != *dp) {
			c = isupper(*cp) ? tolower(*cp) : *cp;
			d = isupper(*dp) ? tolower(*dp) : *dp;
			if (c != d)
				return (0);
		}
		if (*cp == 0)
			return (1);
	}
}

static void
do_before_depend(FILE *fp)
{
	struct file_list *tp;
	int lpos, len;

	fputs("BEFORE_DEPEND=", fp);
	lpos = 15;
	STAILQ_FOREACH(tp, &ftab, f_next)
		if (tp->f_flags & BEFORE_DEPEND) {
			len = strlen(tp->f_fn);
			if ((len = 3 + len) + lpos > 72) {
				lpos = 8;
				fputs("\\\n\t", fp);
			}
			if (tp->f_flags & NO_IMPLCT_RULE)
				fprintf(fp, "%s ", tp->f_fn);
			else
				fprintf(fp, "%s%s ", tp->f_srcprefix,
				    tp->f_fn);
			lpos += len + 1;
		}
	if (lpos != 8)
		putc('\n', fp);
}

static void
do_objs(FILE *fp)
{
	struct file_list *tp;
	int lpos, len;
	char *cp, och, *sp;

	fprintf(fp, "OBJS=");
	lpos = 6;
	STAILQ_FOREACH(tp, &ftab, f_next) {
		if (tp->f_flags & NO_OBJ)
			continue;
		sp = tail(tp->f_fn);
		cp = sp + (len = strlen(sp)) - 1;
		och = *cp;
		*cp = 'o';
		len += strlen(tp->f_objprefix);
		if (len + lpos > 72) {
			lpos = 8;
			fprintf(fp, "\\\n\t");
		}
		fprintf(fp, "%s%s ", tp->f_objprefix, sp);
		lpos += len + 1;
		*cp = och;
	}
	if (lpos != 8)
		putc('\n', fp);
}

static void
do_xxfiles(char *tag, FILE *fp)
{
	struct file_list *tp;
	int lpos, len, slen;
	char *suff, *SUFF;

	if (tag[strlen(tag) - 1] == '\n')
		tag[strlen(tag) - 1] = '\0';

	suff = ns(tag + 7);
	SUFF = ns(suff);
	raisestr(SUFF);
	slen = strlen(suff);

	fprintf(fp, "%sFILES=", SUFF);
	free(SUFF);
	lpos = 8;
	STAILQ_FOREACH(tp, &ftab, f_next)
		if (tp->f_type != NODEPEND) {
			len = strlen(tp->f_fn);
			if (tp->f_fn[len - slen - 1] != '.')
				continue;
			if (strcasecmp(&tp->f_fn[len - slen], suff) != 0)
				continue;
			if ((len = 3 + len) + lpos > 72) {
				lpos = 8;
				fputs("\\\n\t", fp);
			}
			fprintf(fp, "%s%s ", tp->f_srcprefix, tp->f_fn);
			lpos += len + 1;
		}
	free(suff);
	if (lpos != 8)
		putc('\n', fp);
}

static char *
tail(char *fn)
{
	char *cp;

	cp = strrchr(fn, '/');
	if (cp == NULL)
		return (fn);
	return (cp+1);
}

/*
 * Create the makerules for each file
 * which is part of the system.
 */
static void
do_rules(FILE *f)
{
	char *cp, *np, och;
	struct file_list *ftp;
	char *compilewith;
	char cmd[128];

	STAILQ_FOREACH(ftp, &ftab, f_next) {
		if (ftp->f_warn)
			fprintf(stderr, "WARNING: %s\n", ftp->f_warn);
		cp = (np = ftp->f_fn) + strlen(ftp->f_fn) - 1;
		och = *cp;
		if (ftp->f_flags & NO_IMPLCT_RULE) {
			if (ftp->f_depends)
				fprintf(f, "%s%s: %s\n",
					ftp->f_objprefix, np, ftp->f_depends);
			else
				fprintf(f, "%s%s: \n", ftp->f_objprefix, np);
		}
		else {
			*cp = '\0';
			if (och == 'o') {
				fprintf(f, "%s%so:\n\t-cp %s%so .\n\n",
					ftp->f_objprefix, tail(np),
					ftp->f_srcprefix, np);
				continue;
			}
			if (ftp->f_depends) {
				fprintf(f, "%s%so: %s%s%c %s\n",
					ftp->f_objprefix, tail(np),
					ftp->f_srcprefix, np, och,
					ftp->f_depends);
			}
			else {
				fprintf(f, "%s%so: %s%s%c\n",
					ftp->f_objprefix, tail(np),
					ftp->f_srcprefix, np, och);
			}
		}
		compilewith = ftp->f_compilewith;
		if (compilewith == NULL) {
			const char *ftype = NULL;

			switch (ftp->f_type) {
			case NORMAL:
				ftype = "NORMAL";
				break;
			case PROFILING:
				if (!profiling)
					continue;
				ftype = "PROFILE";
				break;
			default:
				fprintf(stderr,
				    "config: don't know rules for %s\n", np);
				break;
			}
			snprintf(cmd, sizeof(cmd),
			    "${%s_%c%s}", ftype,
			    toupper(och),
			    ftp->f_flags & NOWERROR ? "_NOWERROR" : "");
			compilewith = cmd;
		}
		*cp = och;
		if (strlen(ftp->f_objprefix))
			fprintf(f, "\t%s %s%s\n", compilewith,
			    ftp->f_srcprefix, np);
		else
			fprintf(f, "\t%s\n", compilewith);

		if (!(ftp->f_flags & NO_OBJ))
			fprintf(f, "\t${NORMAL_CTFCONVERT}\n\n");
		else
			fprintf(f, "\n");
	}
}

static void
do_clean(FILE *fp)
{
	struct file_list *tp;
	int lpos, len;

	fputs("CLEAN=", fp);
	lpos = 7;
	STAILQ_FOREACH(tp, &ftab, f_next)
		if (tp->f_clean) {
			len = strlen(tp->f_clean);
			if (len + lpos > 72) {
				lpos = 8;
				fputs("\\\n\t", fp);
			}
			fprintf(fp, "%s ", tp->f_clean);
			lpos += len + 1;
		}
	if (lpos != 8)
		putc('\n', fp);
}

char *
raisestr(char *str)
{
	char *cp = str;

	while (*str) {
		if (islower(*str))
			*str = toupper(*str);
		str++;
	}
	return (cp);
}
