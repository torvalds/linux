/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2003 Poul-Henning Kamp
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * NetBSD: ccdconfig.c,v 1.6 1996/05/16 07:11:18 thorpej Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgeom.h>

#define CCDF_UNIFORM    0x02    /* use LCCD of sizes for uniform interleave */
#define CCDF_MIRROR     0x04    /* use mirroring */
#define CCDF_NO_OFFSET  0x08    /* do not leave space in front */
#define CCDF_LINUX      0x10    /* use Linux compatibility mode */

#include "pathnames.h"

static	int lineno = 0;
static	int verbose = 0;
static	const char *ccdconf = _PATH_CCDCONF;

static struct flagval {
	const char	*fv_flag;
	int		fv_val;
} flagvaltab[] = {
	{ "CCDF_UNIFORM",	CCDF_UNIFORM },
	{ "uniform",		CCDF_UNIFORM },
	{ "CCDF_MIRROR",	CCDF_MIRROR },
	{ "mirror",		CCDF_MIRROR },
	{ "CCDF_NO_OFFSET",	CCDF_NO_OFFSET },
	{ "no_offset",		CCDF_NO_OFFSET },
	{ "CCDF_LINUX",		CCDF_LINUX },
	{ "linux",		CCDF_LINUX },
	{ "none",		0 },
	{ NULL,			0 },
};

#define CCD_CONFIG		0	/* configure a device */
#define CCD_CONFIGALL		1	/* configure all devices */
#define CCD_UNCONFIG		2	/* unconfigure a device */
#define CCD_UNCONFIGALL		3	/* unconfigure all devices */
#define CCD_DUMP		4	/* dump a ccd's configuration */

static	int do_single(int, char **, int);
static	int do_all(int);
static	int dump_ccd(int, char **);
static	int flags_to_val(char *);
static	int resolve_ccdname(char *);
static	void usage(void);

int
main(int argc, char *argv[])
{
	int ch, options = 0, action = CCD_CONFIG;

	while ((ch = getopt(argc, argv, "cCf:guUv")) != -1) {
		switch (ch) {
		case 'c':
			action = CCD_CONFIG;
			++options;
			break;

		case 'C':
			action = CCD_CONFIGALL;
			++options;
			break;

		case 'f':
			ccdconf = optarg;
			break;

		case 'g':
			action = CCD_DUMP;
			break;

		case 'u':
			action = CCD_UNCONFIG;
			++options;
			break;

		case 'U':
			action = CCD_UNCONFIGALL;
			++options;
			break;

		case 'v':
			verbose = 1;
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (options > 1)
		usage();

	if (modfind("g_ccd") < 0) {
		/* Not present in kernel, try loading it */
		if (kldload("geom_ccd") < 0 || modfind("g_ccd") < 0)
			warn("geom_ccd module not available!");
	}

	switch (action) {
		case CCD_CONFIG:
		case CCD_UNCONFIG:
			exit(do_single(argc, argv, action));
			/* NOTREACHED */

		case CCD_CONFIGALL:
		case CCD_UNCONFIGALL:
			exit(do_all(action));
			/* NOTREACHED */

		case CCD_DUMP:
			exit(dump_ccd(argc, argv));
			/* NOTREACHED */
	}
	/* NOTREACHED */
	return (0);
}

static int
do_single(int argc, char **argv, int action)
{
	char *cp, *cp2;
	int ccd, noflags = 0, i, ileave, flags = 0;
	struct gctl_req *grq;
	char const *errstr;
	char buf1[BUFSIZ];
	int ex;

	/*
	 * If unconfiguring, all arguments are treated as ccds.
	 */
	if (action == CCD_UNCONFIG || action == CCD_UNCONFIGALL) {
		ex = 0;
		for (; argc != 0;) {
			cp = *argv++; --argc;
			if ((ccd = resolve_ccdname(cp)) < 0) {
				warnx("invalid ccd name: %s", cp);
				continue;
			}
			grq = gctl_get_handle();
			gctl_ro_param(grq, "verb", -1, "destroy geom");
			gctl_ro_param(grq, "class", -1, "CCD");
			sprintf(buf1, "ccd%d", ccd);
			gctl_ro_param(grq, "geom", -1, buf1);
			errstr = gctl_issue(grq);
			if (errstr == NULL) {		
				if (verbose)
					printf("%s unconfigured\n", cp);
				gctl_free(grq);
				continue;
			}
			warnx(
			    "%s\nor possibly kernel and ccdconfig out of sync",
			    errstr);
			ex = 1;
		}
		return (ex);
	}

	/* Make sure there are enough arguments. */
	if (argc < 4) {
		if (argc == 3) {
			/* Assume that no flags are specified. */
			noflags = 1;
		} else {
			if (action == CCD_CONFIGALL) {
				warnx("%s: bad line: %d", ccdconf, lineno);
				return (1);
			} else
				usage();
		}
	}

	/* First argument is the ccd to configure. */
	cp = *argv++; --argc;
	if ((ccd = resolve_ccdname(cp)) < 0) {
		warnx("invalid ccd name: %s", cp);
		return (1);
	}

	/* Next argument is the interleave factor. */
	cp = *argv++; --argc;
	errno = 0;	/* to check for ERANGE */
	ileave = (int)strtol(cp, &cp2, 10);
	if ((errno == ERANGE) || (ileave < 0) || (*cp2 != '\0')) {
		warnx("invalid interleave factor: %s", cp);
		return (1);
	}

	if (noflags == 0) {
		/* Next argument is the ccd configuration flags. */
		cp = *argv++; --argc;
		if ((flags = flags_to_val(cp)) < 0) {
			warnx("invalid flags argument: %s", cp);
			return (1);
		}
	}
	grq = gctl_get_handle();
	gctl_ro_param(grq, "verb", -1, "create geom");
	gctl_ro_param(grq, "class", -1, "CCD");
	gctl_ro_param(grq, "unit", sizeof(ccd), &ccd);
	gctl_ro_param(grq, "ileave", sizeof(ileave), &ileave);
	if (flags & CCDF_UNIFORM)
		gctl_ro_param(grq, "uniform", -1, "");
	if (flags & CCDF_MIRROR)
		gctl_ro_param(grq, "mirror", -1, "");
	if (flags & CCDF_NO_OFFSET)
		gctl_ro_param(grq, "no_offset", -1, "");
	if (flags & CCDF_LINUX)
		gctl_ro_param(grq, "linux", -1, "");
	gctl_ro_param(grq, "nprovider", sizeof(argc), &argc);
	for (i = 0; i < argc; i++) {
		sprintf(buf1, "provider%d", i);
		cp = argv[i];
		if (!strncmp(cp, _PATH_DEV, strlen(_PATH_DEV)))
			cp += strlen(_PATH_DEV);
		gctl_ro_param(grq, buf1, -1, cp);
	}
	gctl_rw_param(grq, "output", sizeof(buf1), buf1);
	errstr = gctl_issue(grq);
	if (errstr == NULL) {		
		if (verbose) {
			printf("%s", buf1);
		}
		gctl_free(grq);
		return (0);
	}
	warnx(
	    "%s\nor possibly kernel and ccdconfig out of sync",
	    errstr);
	return (1);
}

static int
do_all(int action)
{
	FILE *f;
	char line[_POSIX2_LINE_MAX];
	char *cp, **argv;
	int argc, rval;
	gid_t egid;

	rval = 0;
	egid = getegid();
	if (setegid(getgid()) != 0)
		err(1, "setegid failed");
	if ((f = fopen(ccdconf, "r")) == NULL) {
		if (setegid(egid) != 0)
			err(1, "setegid failed");
		warn("fopen: %s", ccdconf);
		return (1);
	}
	if (setegid(egid) != 0)
		err(1, "setegid failed");

	while (fgets(line, sizeof(line), f) != NULL) {
		argc = 0;
		argv = NULL;
		++lineno;
		if ((cp = strrchr(line, '\n')) != NULL)
			*cp = '\0';

		/* Break up the line and pass it's contents to do_single(). */
		if (line[0] == '\0')
			goto end_of_line;
		for (cp = line; (cp = strtok(cp, " \t")) != NULL; cp = NULL) {
			if (*cp == '#')
				break;
			if ((argv = realloc(argv,
			    sizeof(char *) * ++argc)) == NULL) {
				warnx("no memory to configure ccds");
				return (1);
			}
			argv[argc - 1] = cp;
			/*
			 * If our action is to unconfigure all, then pass
			 * just the first token to do_single() and ignore
			 * the rest.  Since this will be encountered on
			 * our first pass through the line, the Right
			 * Thing will happen.
			 */
			if (action == CCD_UNCONFIGALL) {
				if (do_single(argc, argv, action))
					rval = 1;
				goto end_of_line;
			}
		}
		if (argc != 0)
			if (do_single(argc, argv, action))
				rval = 1;

 end_of_line:
		if (argv != NULL)
			free(argv);
	}

	(void)fclose(f);
	return (rval);
}

static int
resolve_ccdname(char *name)
{

	if (!strncmp(name, _PATH_DEV, strlen(_PATH_DEV)))
		name += strlen(_PATH_DEV);
	if (strncmp(name, "ccd", 3))
		return -1;
	name += 3;
	if (!isdigit(*name))
		return -1;
	return (strtoul(name, NULL, 10));
}

static int
dumpout(int unit)
{
	static int v;
	struct gctl_req *grq;
	int ncp;
	char *cp;
	char const *errstr;

	grq = gctl_get_handle();
	ncp = 65536;
	cp = malloc(ncp);
	gctl_ro_param(grq, "verb", -1, "list");
	gctl_ro_param(grq, "class", -1, "CCD");
	gctl_ro_param(grq, "unit", sizeof(unit), &unit);
	gctl_rw_param(grq, "output", ncp, cp);
	errstr = gctl_issue(grq);
	if (errstr != NULL)
		errx(1, "%s\nor possibly kernel and ccdconfig out of sync",
			errstr);
	if (strlen(cp) == 0)
		errx(1, "ccd%d not configured", unit);
	if (verbose && !v) {
		printf("# ccd\t\tileave\tflags\tcomponent devices\n");
		v = 1;
	}
	printf("%s", cp);
	free(cp);
	return (0);
}

static int
dump_ccd(int argc, char **argv)
{
	int i, error;

	if (argc == 0) {
		error = dumpout(-1);
	} else {
		error = 0;
		for (i = 0; error == 0 && i < argc; i++)
			error = dumpout(resolve_ccdname(argv[i]));
	}
	return (error);
}

static int
flags_to_val(char *flags)
{
	char *cp, *tok;
	int i, tmp, val;

	errno = 0;	/* to check for ERANGE */
	val = (int)strtol(flags, &cp, 0);
	if ((errno != ERANGE) && (*cp == '\0')) {
		if (val & ~(CCDF_UNIFORM|CCDF_MIRROR))
			return (-1);
		return (val);
	}

	/* Check for values represented by strings. */
	if ((cp = strdup(flags)) == NULL)
		err(1, "no memory to parse flags");
	tmp = 0;
	for (tok = cp; (tok = strtok(tok, ",")) != NULL; tok = NULL) {
		for (i = 0; flagvaltab[i].fv_flag != NULL; ++i)
			if (strcmp(tok, flagvaltab[i].fv_flag) == 0)
				break;
		if (flagvaltab[i].fv_flag == NULL) {
			free(cp);
			return (-1);
		}
		tmp |= flagvaltab[i].fv_val;
	}

	/* If we get here, the string was ok. */
	free(cp);
	return (tmp);
}

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n",
		"usage: ccdconfig [-cv] ccd ileave [flags] dev ...",
		"       ccdconfig -C [-v] [-f config_file]",
		"       ccdconfig -u [-v] ccd ...",
		"       ccdconfig -U [-v] [-f config_file]",
		"       ccdconfig -g [ccd ...]");
	exit(1);
}
