/* $OpenBSD: gnum4.c,v 1.50 2015/04/29 00:13:26 millert Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 1999 Marc Espie
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
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * functions needed to support gnu-m4 extensions, including a fake freezing
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <err.h>
#include <paths.h>
#include <regex.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include "mdef.h"
#include "stdd.h"
#include "extern.h"


int mimic_gnu = 0;

/*
 * Support for include path search
 * First search in the current directory.
 * If not found, and the path is not absolute, include path kicks in.
 * First, -I options, in the order found on the command line.
 * Then M4PATH env variable
 */

static struct path_entry {
	char *name;
	struct path_entry *next;
} *first, *last;

static struct path_entry *new_path_entry(const char *);
static void ensure_m4path(void);
static struct input_file *dopath(struct input_file *, const char *);

static struct path_entry *
new_path_entry(const char *dirname)
{
	struct path_entry *n;

	n = malloc(sizeof(struct path_entry));
	if (!n)
		errx(1, "out of memory");
	n->name = xstrdup(dirname);
	n->next = 0;
	return n;
}

void
addtoincludepath(const char *dirname)
{
	struct path_entry *n;

	n = new_path_entry(dirname);

	if (last) {
		last->next = n;
		last = n;
	}
	else
		last = first = n;
}

static void
ensure_m4path(void)
{
	static int envpathdone = 0;
	char *envpath;
	char *sweep;
	char *path;

	if (envpathdone)
		return;
	envpathdone = TRUE;
	envpath = getenv("M4PATH");
	if (!envpath)
		return;
	/* for portability: getenv result is read-only */
	envpath = xstrdup(envpath);
	for (sweep = envpath;
	    (path = strsep(&sweep, ":")) != NULL;)
	    addtoincludepath(path);
	free(envpath);
}

static
struct input_file *
dopath(struct input_file *i, const char *filename)
{
	char path[PATH_MAX];
	struct path_entry *pe;
	FILE *f;

	for (pe = first; pe; pe = pe->next) {
		snprintf(path, sizeof(path), "%s/%s", pe->name, filename);
		if ((f = fopen(path, "r")) != NULL) {
			set_input(i, f, path);
			return i;
		}
	}
	return NULL;
}

struct input_file *
fopen_trypath(struct input_file *i, const char *filename)
{
	FILE *f;

	f = fopen(filename, "r");
	if (f != NULL) {
		set_input(i, f, filename);
		return i;
	}
	if (filename[0] == '/')
		return NULL;

	ensure_m4path();

	return dopath(i, filename);
}

void
doindir(const char *argv[], int argc)
{
	ndptr n;
	struct macro_definition *p = NULL;

	n = lookup(argv[2]);
	if (n == NULL || (p = macro_getdef(n)) == NULL)
		m4errx(1, "indir: undefined macro %s.", argv[2]);
	argv[1] = p->defn;

	eval(argv+1, argc-1, p->type, is_traced(n));
}

void
dobuiltin(const char *argv[], int argc)
{
	ndptr p;

	argv[1] = NULL;
	p = macro_getbuiltin(argv[2]);
	if (p != NULL)
		eval(argv+1, argc-1, macro_builtin_type(p), is_traced(p));
	else
		m4errx(1, "unknown builtin %s.", argv[2]);
}


/* We need some temporary buffer space, as pb pushes BACK and substitution
 * proceeds forward... */
static char *buffer;
static size_t bufsize = 0;
static size_t current = 0;

static void addchars(const char *, size_t);
static void addchar(int);
static char *twiddle(const char *);
static char *getstring(void);
static void exit_regerror(int, regex_t *, const char *);
static void do_subst(const char *, regex_t *, const char *, const char *,
    regmatch_t *);
static void do_regexpindex(const char *, regex_t *, const char *, regmatch_t *);
static void do_regexp(const char *, regex_t *, const char *, const char *,
    regmatch_t *);
static void add_sub(int, const char *, regex_t *, regmatch_t *);
static void add_replace(const char *, regex_t *, const char *, regmatch_t *);
#define addconstantstring(s) addchars((s), sizeof(s)-1)

static void
addchars(const char *c, size_t n)
{
	if (n == 0)
		return;
	while (current + n > bufsize) {
		if (bufsize == 0)
			bufsize = 1024;
		else if (bufsize <= SIZE_MAX/2) {
			bufsize *= 2;
		} else {
			errx(1, "size overflow");
		}
		buffer = xrealloc(buffer, bufsize, NULL);
	}
	memcpy(buffer+current, c, n);
	current += n;
}

static void
addchar(int c)
{
	if (current +1 > bufsize) {
		if (bufsize == 0)
			bufsize = 1024;
		else
			bufsize *= 2;
		buffer = xrealloc(buffer, bufsize, NULL);
	}
	buffer[current++] = c;
}

static char *
getstring(void)
{
	addchar('\0');
	current = 0;
	return buffer;
}


static void
exit_regerror(int er, regex_t *re, const char *source)
{
	size_t	errlen;
	char	*errbuf;

	errlen = regerror(er, re, NULL, 0);
	errbuf = xalloc(errlen,
	    "malloc in regerror: %lu", (unsigned long)errlen);
	regerror(er, re, errbuf, errlen);
	m4errx(1, "regular expression error in %s: %s.", source, errbuf);
}

static void
add_sub(int n, const char *string, regex_t *re, regmatch_t *pm)
{
	if (n > (int)re->re_nsub)
		warnx("No subexpression %d", n);
	/* Subexpressions that did not match are
	 * not an error.  */
	else if (pm[n].rm_so != -1 &&
	    pm[n].rm_eo != -1) {
		addchars(string + pm[n].rm_so,
			pm[n].rm_eo - pm[n].rm_so);
	}
}

/* Add replacement string to the output buffer, recognizing special
 * constructs and replacing them with substrings of the original string.
 */
static void
add_replace(const char *string, regex_t *re, const char *replace, regmatch_t *pm)
{
	const char *p;

	for (p = replace; *p != '\0'; p++) {
		if (*p == '&' && !mimic_gnu) {
			add_sub(0, string, re, pm);
			continue;
		}
		if (*p == '\\') {
			if (p[1] == '\\') {
				addchar(p[1]);
				p++;
				continue;
			}
			if (p[1] == '&') {
				if (mimic_gnu)
					add_sub(0, string, re, pm);
				else
					addchar(p[1]);
				p++;
				continue;
			}
			if (isdigit((unsigned char)p[1])) {
				add_sub(*(++p) - '0', string, re, pm);
				continue;
			}
		}
		addchar(*p);
	}
}

static void
do_subst(const char *string, regex_t *re, const char *source,
    const char *replace, regmatch_t *pm)
{
	int error;
	int flags = 0;
	const char *last_match = NULL;

	while ((error = regexec(re, string, re->re_nsub+1, pm, flags)) == 0) {
		if (pm[0].rm_eo != 0) {
			if (string[pm[0].rm_eo-1] == '\n')
				flags = 0;
			else
				flags = REG_NOTBOL;
		}

		/* NULL length matches are special... We use the `vi-mode'
		 * rule: don't allow a NULL-match at the last match
		 * position.
		 */
		if (pm[0].rm_so == pm[0].rm_eo &&
		    string + pm[0].rm_so == last_match) {
			if (*string == '\0')
				return;
			addchar(*string);
			if (*string++ == '\n')
				flags = 0;
			else
				flags = REG_NOTBOL;
			continue;
		}
		last_match = string + pm[0].rm_so;
		addchars(string, pm[0].rm_so);
		add_replace(string, re, replace, pm);
		string += pm[0].rm_eo;
	}
	if (error != REG_NOMATCH)
		exit_regerror(error, re, source);
	pbstr(string);
}

static void
do_regexp(const char *string, regex_t *re, const char *source,
    const char *replace, regmatch_t *pm)
{
	int error;

	switch(error = regexec(re, string, re->re_nsub+1, pm, 0)) {
	case 0:
		add_replace(string, re, replace, pm);
		pbstr(getstring());
		break;
	case REG_NOMATCH:
		break;
	default:
		exit_regerror(error, re, source);
	}
}

static void
do_regexpindex(const char *string, regex_t *re, const char *source,
    regmatch_t *pm)
{
	int error;

	switch(error = regexec(re, string, re->re_nsub+1, pm, 0)) {
	case 0:
		pbunsigned(pm[0].rm_so);
		break;
	case REG_NOMATCH:
		pbnum(-1);
		break;
	default:
		exit_regerror(error, re, source);
	}
}

/* In Gnu m4 mode, parentheses for backmatch don't work like POSIX 1003.2
 * says. So we twiddle with the regexp before passing it to regcomp.
 */
static char *
twiddle(const char *p)
{
	/* + at start of regexp is a normal character for Gnu m4 */
	if (*p == '^') {
		addchar(*p);
		p++;
	}
	if (*p == '+') {
		addchar('\\');
	}
	/* This could use strcspn for speed... */
	while (*p != '\0') {
		if (*p == '\\') {
			switch(p[1]) {
			case '(':
			case ')':
			case '|':
				addchar(p[1]);
				break;
			case 'w':
				addconstantstring("[_a-zA-Z0-9]");
				break;
			case 'W':
				addconstantstring("[^_a-zA-Z0-9]");
				break;
			case '<':
				addconstantstring("[[:<:]]");
				break;
			case '>':
				addconstantstring("[[:>:]]");
				break;
			default:
				addchars(p, 2);
				break;
			}
			p+=2;
			continue;
		}
		if (*p == '(' || *p == ')' || *p == '|')
			addchar('\\');

		addchar(*p);
		p++;
	}
	return getstring();
}

/* patsubst(string, regexp, opt replacement) */
/* argv[2]: string
 * argv[3]: regexp
 * argv[4]: opt rep
 */
void
dopatsubst(const char *argv[], int argc)
{
	if (argc <= 3) {
		warnx("Too few arguments to patsubst");
		return;
	}
	/* special case: empty regexp */
	if (argv[3][0] == '\0') {
		const char *s;
		size_t len;
		if (argc > 4 && argv[4])
			len = strlen(argv[4]);
		else
			len = 0;
		for (s = argv[2]; *s != '\0'; s++) {
			addchars(argv[4], len);
			addchar(*s);
		}
	} else {
		int error;
		regex_t re;
		regmatch_t *pmatch;
		int mode = REG_EXTENDED;
		const char *source;
		size_t l = strlen(argv[3]);

		if (!mimic_gnu ||
		    (argv[3][0] == '^') ||
		    (l > 0 && argv[3][l-1] == '$'))
			mode |= REG_NEWLINE;

		source = mimic_gnu ? twiddle(argv[3]) : argv[3];
		error = regcomp(&re, source, mode);
		if (error != 0)
			exit_regerror(error, &re, source);

		pmatch = xreallocarray(NULL, re.re_nsub+1, sizeof(regmatch_t),
		    NULL);
		do_subst(argv[2], &re, source,
		    argc > 4 && argv[4] != NULL ? argv[4] : "", pmatch);
		free(pmatch);
		regfree(&re);
	}
	pbstr(getstring());
}

void
doregexp(const char *argv[], int argc)
{
	int error;
	regex_t re;
	regmatch_t *pmatch;
	const char *source;

	if (argc <= 3) {
		warnx("Too few arguments to regexp");
		return;
	}
	/* special gnu case */
	if (argv[3][0] == '\0' && mimic_gnu) {
		if (argc == 4 || argv[4] == NULL)
			return;
		else
			pbstr(argv[4]);
	}
	source = mimic_gnu ? twiddle(argv[3]) : argv[3];
	error = regcomp(&re, source, REG_EXTENDED|REG_NEWLINE);
	if (error != 0)
		exit_regerror(error, &re, source);

	pmatch = xreallocarray(NULL, re.re_nsub+1, sizeof(regmatch_t), NULL);
	if (argc == 4 || argv[4] == NULL)
		do_regexpindex(argv[2], &re, source, pmatch);
	else
		do_regexp(argv[2], &re, source, argv[4], pmatch);
	free(pmatch);
	regfree(&re);
}

void
doformat(const char *argv[], int argc)
{
	const char *format = argv[2];
	int pos = 3;
	int left_padded;
	long width;
	size_t l;
	const char *thisarg = NULL;
	char temp[2];
	long extra;

	while (*format != 0) {
		if (*format != '%') {
			addchar(*format++);
			continue;
		}

		format++;
		if (*format == '%') {
			addchar(*format++);
			continue;
		}
		if (*format == 0) {
			addchar('%');
			break;
		}

		if (*format == '*') {
			format++;
			if (pos >= argc)
				m4errx(1,
				    "Format with too many format specifiers.");
			width = strtol(argv[pos++], NULL, 10);
		} else {
			width = strtol(format, __DECONST(char **,&format), 10);
		}
		if (width < 0) {
			left_padded = 1;
			width = -width;
		} else {
			left_padded = 0;
		}
		if (*format == '.') {
			format++;
			if (*format == '*') {
				format++;
				if (pos >= argc)
					m4errx(1,
					    "Format with too many format specifiers.");
				extra = strtol(argv[pos++], NULL, 10);
			} else {
				extra = strtol(format, __DECONST(char **, &format), 10);
			}
		} else {
			extra = LONG_MAX;
		}
		if (pos >= argc)
			m4errx(1, "Format with too many format specifiers.");
		switch(*format) {
		case 's':
			thisarg = argv[pos++];
			break;
		case 'c':
			temp[0] = strtoul(argv[pos++], NULL, 10);
			temp[1] = 0;
			thisarg = temp;
			break;
		default:
			m4errx(1, "Unsupported format specification: %s.",
			    argv[2]);
		}
		format++;
		l = strlen(thisarg);
		if ((long)l > extra)
			l = extra;
		if (!left_padded) {
			while ((long)l < width--)
				addchar(' ');
		}
		addchars(thisarg, l);
		if (left_padded) {
			while ((long)l < width--)
				addchar(' ');
		}
	}
	pbstr(getstring());
}

void
doesyscmd(const char *cmd)
{
	int p[2];
	pid_t pid, cpid;
	char *argv[4];
	int cc;
	int status;

	/* Follow gnu m4 documentation: first flush buffers. */
	fflush(NULL);

	argv[0] = __DECONST(char *, "sh");
	argv[1] = __DECONST(char *, "-c");
	argv[2] = __DECONST(char *, cmd);
	argv[3] = NULL;

	/* Just set up standard output, share stderr and stdin with m4 */
	if (pipe(p) == -1)
		err(1, "bad pipe");
	switch(cpid = fork()) {
	case -1:
		err(1, "bad fork");
		/* NOTREACHED */
	case 0:
		(void) close(p[0]);
		(void) dup2(p[1], 1);
		(void) close(p[1]);
		execv(_PATH_BSHELL, argv);
		exit(1);
	default:
		/* Read result in two stages, since m4's buffer is
		 * pushback-only. */
		(void) close(p[1]);
		do {
			char result[BUFSIZE];
			cc = read(p[0], result, sizeof result);
			if (cc > 0)
				addchars(result, cc);
		} while (cc > 0 || (cc == -1 && errno == EINTR));

		(void) close(p[0]);
		while ((pid = wait(&status)) != cpid && pid >= 0)
			continue;
		pbstr(getstring());
	}
}

void
getdivfile(const char *name)
{
	FILE *f;
	int c;

	f = fopen(name, "r");
	if (!f)
		return;

	while ((c = getc(f))!= EOF)
		putc(c, active);
	(void) fclose(f);
}
