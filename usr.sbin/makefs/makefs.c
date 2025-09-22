/*	$OpenBSD: makefs.c,v 1.22 2022/12/04 23:50:51 cheloha Exp $	*/
/*	$NetBSD: makefs.c,v 1.53 2015/11/27 15:10:32 joerg Exp $	*/

/*
 * Copyright (c) 2001-2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Luke Mewburn for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "makefs.h"

/*
 * list of supported file systems and dispatch functions
 */
typedef struct {
	const char	*type;
	void		(*prepare_options)(fsinfo_t *);
	int		(*parse_options)(const char *, fsinfo_t *);
	void		(*cleanup_options)(fsinfo_t *);
	void		(*make_fs)(const char *, const char *, fsnode *,
				fsinfo_t *);
} fstype_t;

static fstype_t fstypes[] = {
#define ENTRY(name) { \
	# name, name ## _prep_opts, name ## _parse_opts, \
	name ## _cleanup_opts, name ## _makefs  \
}
	ENTRY(ffs),
	ENTRY(cd9660),
	ENTRY(msdos),
	{ .type = NULL	},
};

int Tflag;
time_t stampts;
struct timespec	start_time;

static	fstype_t *get_fstype(const char *);
static time_t get_tstamp(const char *);
static long long strsuftoll(const char *, const char *, long long, long long);
static __dead void usage(void);

int
main(int argc, char *argv[])
{
	fstype_t	*fstype;
	fsinfo_t	 fsoptions;
	fsnode		*root;
	int		 ch, len;

	if ((fstype = get_fstype(DEFAULT_FSTYPE)) == NULL)
		errx(1, "Unknown default fs type `%s'.", DEFAULT_FSTYPE);

		/* set default fsoptions */
	(void)memset(&fsoptions, 0, sizeof(fsoptions));
	fsoptions.fd = -1;
	fsoptions.sectorsize = -1;

	if (fstype->prepare_options)
		fstype->prepare_options(&fsoptions);

	ch = clock_gettime(CLOCK_REALTIME, &start_time);
	if (ch == -1)
		err(1, "Unable to get system time");


	while ((ch = getopt(argc, argv, "b:f:M:m:O:o:s:S:t:T:")) != -1) {
		switch (ch) {
		case 'b':
			len = strlen(optarg) - 1;
			if (optarg[len] == '%') {
				optarg[len] = '\0';
				fsoptions.freeblockpc =
				    strsuftoll("free block percentage",
					optarg, 0, 99);
			} else {
				fsoptions.freeblocks =
				    strsuftoll("free blocks",
					optarg, 0, LLONG_MAX);
			}
			break;

		case 'f':
			len = strlen(optarg) - 1;
			if (optarg[len] == '%') {
				optarg[len] = '\0';
				fsoptions.freefilepc =
				    strsuftoll("free file percentage",
					optarg, 0, 99);
			} else {
				fsoptions.freefiles =
				    strsuftoll("free files",
					optarg, 0, LLONG_MAX);
			}
			break;

		case 'M':
			fsoptions.minsize =
			    strsuftoll("minimum size", optarg, 1LL, LLONG_MAX);
			break;

		case 'm':
			fsoptions.maxsize =
			    strsuftoll("maximum size", optarg, 1LL, LLONG_MAX);
			break;

		case 'O':
			fsoptions.offset =
			    strsuftoll("offset", optarg, 0LL, LLONG_MAX);
			break;

		case 'o':
		{
			char *p;

			while ((p = strsep(&optarg, ",")) != NULL) {
				if (*p == '\0')
					errx(1, "Empty option");
				if (! fstype->parse_options(p, &fsoptions))
					usage();
			}
			break;
		}

		case 's':
			fsoptions.minsize = fsoptions.maxsize =
			    strsuftoll("size", optarg, 1LL, LLONG_MAX);
			break;

		case 'S':
			fsoptions.sectorsize =
			    (int)strsuftoll("sector size", optarg,
				1LL, INT_MAX);
			break;

		case 't':
			/* Check current one and cleanup if necessary. */
			if (fstype->cleanup_options)
				fstype->cleanup_options(&fsoptions);
			fsoptions.fs_specific = NULL;
			if ((fstype = get_fstype(optarg)) == NULL)
				errx(1, "Unknown fs type `%s'.", optarg);
			fstype->prepare_options(&fsoptions);
			break;

		case 'T':
			Tflag = 1;
			stampts = get_tstamp(optarg);
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	if (unveil(argv[0], "rwc") == -1)
		err(1, "unveil %s", argv[0]);
	if (unveil(argv[1], "rw") == -1)
		err(1, "unveil %s", argv[1]);
	if (pledge("stdio rpath wpath cpath", NULL) == -1)
		err(1, "pledge");

				/* walk the tree */
	root = walk_dir(argv[1], ".", NULL, NULL);

				/* build the file system */
	fstype->make_fs(argv[0], argv[1], root, &fsoptions);

	free_fsnodes(root);

	exit(0);
}

int
set_option(const option_t *options, const char *option, char *buf, size_t len)
{
	char *var, *val;
	int retval;

	assert(option != NULL);

	var = estrdup(option);
	for (val = var; *val; val++)
		if (*val == '=') {
			*val++ = '\0';
			break;
		}
	retval = set_option_var(options, var, val, buf, len);
	free(var);
	return retval;
}

int
set_option_var(const option_t *options, const char *var, const char *val,
    char *buf, size_t len)
{
	char *s;
	size_t i;

#define NUM(type) \
	if (!*val) { \
		*(type *)options[i].value = 1; \
		break; \
	} \
	*(type *)options[i].value = (type)strsuftoll(options[i].name, val, \
	    options[i].minimum, options[i].maximum); break

	for (i = 0; options[i].name != NULL; i++) {
		if (strcmp(options[i].name, var) != 0)
			continue;
		switch (options[i].type) {
		case OPT_BOOL:
			*(int *)options[i].value = 1;
			break;
		case OPT_STRARRAY:
			strlcpy((void *)options[i].value, val, (size_t)
			    options[i].maximum);
			break;
		case OPT_STRPTR:
			s = estrdup(val);
			*(char **)options[i].value = s;
			break;
		case OPT_STRBUF:
			if (buf == NULL)
				abort();
			strlcpy(buf, val, len);
			break;
		case OPT_INT64:
			NUM(uint64_t);
		case OPT_INT32:
			NUM(uint32_t);
		case OPT_INT16:
			NUM(uint16_t);
		case OPT_INT8:
			NUM(uint8_t);
		default:
			warnx("Unknown type %d in option %s", options[i].type,
			    val);
			return 0;
		}
		return i;
	}
	warnx("Unknown option `%s'", var);
	return -1;
}


static fstype_t *
get_fstype(const char *type)
{
	int i;

	for (i = 0; fstypes[i].type != NULL; i++)
		if (strcmp(fstypes[i].type, type) == 0)
			return (&fstypes[i]);
	return (NULL);
}

option_t *
copy_opts(const option_t *o)
{
	size_t i;
	for (i = 0; o[i].name; i++)
		continue;
	i++;
	return memcpy(ecalloc(i, sizeof(*o)), o, i * sizeof(*o));
}

static time_t
get_tstamp(const char *b)
{
	time_t when;
	char *eb;

	errno = 0;
	when = strtoll(b, &eb, 0);
	if (b == eb || *eb || errno) {
		errx(1, "Cannot get timestamp from `%s'",
		    optarg);
	}
	return when;
}

/* XXX */
static long long
strsuftoll(const char *desc, const char *val, long long min, long long max)
{
	long long res;

	if (scan_scaled((char *)val, &res) == -1)
		err(1, "%s", desc);
	if (res < min || res > max)
		errc(1, ERANGE, "%s", desc);
	return res;
}

static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
"usage: %s [-b free-blocks] [-f free-files] [-M minimum-size]\n"
"\t[-m maximum-size] [-O offset] [-o fs-options] [-S sector-size]\n"
"\t[-s image-size] [-T timestamp] [-t fs-type] image-file directory\n",
	    __progname);

	exit(1);
}
