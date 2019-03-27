/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001, 2002 Dima Dorfman.
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

/*
 * DEVFS control.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

int mpfd;

static ctbl_t ctbl_main = {
	{ "rule",		rule_main },
	{ "ruleset",		ruleset_main },
	{ NULL,			NULL }
};

int
main(int ac, char **av)
{
	const char *mountpt;
	struct cmd *c;
	int ch;

	mountpt = NULL;
	while ((ch = getopt(ac, av, "m:")) != -1)
		switch (ch) {
		case 'm':
			mountpt = optarg;
			break;
		default:
			usage();
		}
	ac -= optind;
	av += optind;
	if (ac < 1)
		usage();

	if (mountpt == NULL)
		mountpt = _PATH_DEV;
	mpfd = open(mountpt, O_RDONLY);
	if (mpfd == -1)
		err(1, "open: %s", mountpt);

	for (c = ctbl_main; c->name != NULL; ++c)
		if (strcmp(c->name, av[0]) == 0)
			exit((*c->handler)(ac, av));
	errx(1, "unknown command: %s", av[0]);
}

/*
 * Convert an integer to a "number" (ruleset numbers and rule numbers
 * are 16-bit).  If the conversion is successful, num contains the
 * integer representation of s and 1 is returned; otherwise, 0 is
 * returned and num is unchanged.
 */
int
atonum(const char *s, uint16_t *num)
{
	unsigned long ul;
	char *cp;

	ul = strtoul(s, &cp, 10);
	if (ul > UINT16_MAX || *cp != '\0')
		return (0);
	*num = (uint16_t)ul;
	return (1);
}

/*
 * Convert user input in ASCII to an integer.
 */
int
eatoi(const char *s)
{
	char *cp;
	long l;

	l = strtol(s, &cp, 10);
	if (l > INT_MAX || *cp != '\0')
		errx(1, "error converting to integer: %s", s);
	return ((int)l);
}

/*
 * As atonum(), but the result of failure is death.
 */
uint16_t
eatonum(const char *s)
{
	uint16_t num;

	if (!atonum(s, &num))
		errx(1, "error converting to number: %s", s); /* XXX clarify */
	return (num);
}

/*
 * Read a line from a /FILE/.  If the return value isn't 0, it is the
 * length of the line, a pointer to which exists in /line/.  It is the
 * caller's responsibility to free(3) it.  If the return value is 0,
 * there was an error or we reached EOF, and /line/ is undefined (so,
 * obviously, the caller shouldn't try to free(3) it).
 */
size_t
efgetln(FILE *fp, char **line)
{
	size_t rv;
	char *cp;

	cp = fgetln(fp, &rv);
	if (cp == NULL) {
		*line = NULL;
		return (rv);
	}
	if (cp[rv - 1] == '\n') {
		cp[rv - 1] = '\0';
		*line = strdup(cp);
		if (*line == NULL)
			errx(1, "cannot allocate memory");
		--rv;
	} else {
		*line = malloc(rv + 1);
		if (*line == NULL)
			errx(1, "cannot allocate memory");
		memcpy(*line, cp, rv);
		(*line)[rv] = '\0';
	}
	assert(rv == strlen(*line));
	return (rv);
}

struct ptrstq {
	STAILQ_ENTRY(ptrstq)	 tq;
	void			*ptr;
};

/*
 * Create an argument vector from /line/.  The caller must free(3)
 * /avp/, and /avp[0]/ when the argument vector is no longer
 * needed unless /acp/ is 0, in which case /avp/ is undefined.
 * /avp/ is NULL-terminated, so it is actually one longer than /acp/.
 */
void
tokenize(const char *line, int *acp, char ***avp)
{
	static const char *delims = " \t\n";
	struct ptrstq *pt;
	STAILQ_HEAD(, ptrstq) plist;
	char **ap, *cp, *wline, *xcp;

	line += strspn(line, delims);
	wline = strdup(line);
	if (wline == NULL)
		errx(1, "cannot allocate memory");

	STAILQ_INIT(&plist);
	for (xcp = wline, *acp = 0;
	     (cp = strsep(&xcp, delims)) != NULL;)
		if (*cp != '\0') {
			pt = calloc(1, sizeof(*pt));
			if (pt == NULL)
				errx(1, "cannot allocate memory");
			pt->ptr = cp;
			STAILQ_INSERT_TAIL(&plist, pt, tq);
			++*acp;
		}
	if (*acp == 0)
		return;
	assert(STAILQ_FIRST(&plist)->ptr == wline);
	*avp = malloc(sizeof(**avp) * (*acp + 1));
	if (*avp == NULL)
		errx(1, "cannot allocate memory");
	for (ap = *avp; !STAILQ_EMPTY(&plist);) {
		pt = STAILQ_FIRST(&plist);
		*ap = pt->ptr;
		++ap;
		assert(ap <= *avp + (*acp));
		STAILQ_REMOVE_HEAD(&plist, tq);
		free(pt);
	}
	*ap = NULL;
}

void
usage(void)
{

	fprintf(stderr, "usage: %s\n%s\n",
	    "\tdevfs [-m mount-point] [-s ruleset] rule ...",
	    "\tdevfs [-m mount-point] ruleset ...");
	exit(1);
}
