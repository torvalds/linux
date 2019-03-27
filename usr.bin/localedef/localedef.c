/*-
 * Copyright 2018 Nexenta Systems, Inc.
 * Copyright 2015 John Marino <draco@marino.st>
 *
 * This source code is derived from the illumos localedef command, and
 * provided under BSD-style license terms by Nexenta Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * POSIX localedef.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>
#include <stddef.h>
#include <unistd.h>
#include <limits.h>
#include <locale.h>
#include <dirent.h>
#include "localedef.h"
#include "parser.h"

#ifndef	TEXT_DOMAIN
#define	TEXT_DOMAIN	"SYS_TEST"
#endif

static int bsd = 0;
static int byteorder = 0;
int verbose = 0;
int undefok = 0;
int warnok = 0;
static char *locname = NULL;
static char locpath[PATH_MAX];

const char *
category_name(void)
{
	switch (get_category()) {
	case T_CHARMAP:
		return ("CHARMAP");
	case T_WIDTH:
		return ("WIDTH");
	case T_COLLATE:
		return ("LC_COLLATE");
	case T_CTYPE:
		return ("LC_CTYPE");
	case T_MESSAGES:
		return ("LC_MESSAGES");
	case T_MONETARY:
		return ("LC_MONETARY");
	case T_NUMERIC:
		return ("LC_NUMERIC");
	case T_TIME:
		return ("LC_TIME");
	default:
		INTERR;
		return (NULL);
	}
}

static char *
category_file(void)
{
	if (bsd)
		(void) snprintf(locpath, sizeof (locpath), "%s.%s",
		    locname, category_name());
	else
		(void) snprintf(locpath, sizeof (locpath), "%s/%s",
		    locname, category_name());
	return (locpath);
}

FILE *
open_category(void)
{
	FILE *file;

	if (verbose) {
		(void) printf("Writing category %s: ", category_name());
		(void) fflush(stdout);
	}

	/* make the parent directory */
	if (!bsd)
		(void) mkdir(dirname(category_file()), 0755);

	/*
	 * note that we have to regenerate the file name, as dirname
	 * clobbered it.
	 */
	file = fopen(category_file(), "w");
	if (file == NULL) {
		errf("%s", strerror(errno));
		return (NULL);
	}
	return (file);
}

void
close_category(FILE *f)
{
	if (fchmod(fileno(f), 0644) < 0) {
		(void) fclose(f);
		(void) unlink(category_file());
		errf("%s", strerror(errno));
	}
	if (fclose(f) < 0) {
		(void) unlink(category_file());
		errf("%s", strerror(errno));
	}
	if (verbose) {
		(void) fprintf(stdout, "done.\n");
		(void) fflush(stdout);
	}
}

/*
 * This function is used when copying the category from another
 * locale.  Note that the copy is actually performed using a hard
 * link for efficiency.
 */
void
copy_category(char *src)
{
	char	srcpath[PATH_MAX];
	int	rv;

	(void) snprintf(srcpath, sizeof (srcpath), "%s/%s",
	    src, category_name());
	rv = access(srcpath, R_OK);
	if ((rv != 0) && (strchr(srcpath, '/') == NULL)) {
		/* Maybe we should try the system locale */
		(void) snprintf(srcpath, sizeof (srcpath),
		    "/usr/lib/locale/%s/%s", src, category_name());
		rv = access(srcpath, R_OK);
	}

	if (rv != 0) {
		fprintf(stderr,"source locale data unavailable: %s", src);
		return;
	}

	if (verbose > 1) {
		(void) printf("Copying category %s from %s: ",
		    category_name(), src);
		(void) fflush(stdout);
	}

	/* make the parent directory */
	if (!bsd)
		(void) mkdir(dirname(category_file()), 0755);

	if (link(srcpath, category_file()) != 0) {
		fprintf(stderr,"unable to copy locale data: %s",
			strerror(errno));
		return;
	}
	if (verbose > 1) {
		(void) printf("done.\n");
	}
}

int
putl_category(const char *s, FILE *f)
{
	if (s && fputs(s, f) == EOF) {
		(void) fclose(f);
		(void) unlink(category_file());
		errf("%s", strerror(errno));
		return (EOF);
	}
	if (fputc('\n', f) == EOF) {
		(void) fclose(f);
		(void) unlink(category_file());
		errf("%s", strerror(errno));
		return (EOF);
	}
	return (0);
}

int
wr_category(void *buf, size_t sz, FILE *f)
{
	if (!sz) {
		return (0);
	}
	if (fwrite(buf, sz, 1, f) < 1) {
		(void) fclose(f);
		(void) unlink(category_file());
		errf("%s", strerror(errno));
		return (EOF);
	}
	return (0);
}

uint32_t
htote(uint32_t arg)
{

	if (byteorder == 4321)
		return (htobe32(arg));
	else if (byteorder == 1234)
		return (htole32(arg));
	else
		return (arg);
}

int yyparse(void);

static void
usage(void)
{
	(void) fprintf(stderr, "Usage: localedef [options] localename\n");
	(void) fprintf(stderr, "[options] are:\n");
	(void) fprintf(stderr, "  -D          : BSD-style output\n");
	(void) fprintf(stderr, "  -b          : big-endian output\n");
	(void) fprintf(stderr, "  -c          : ignore warnings\n");
	(void) fprintf(stderr, "  -l          : little-endian output\n");
	(void) fprintf(stderr, "  -v          : verbose output\n");
	(void) fprintf(stderr, "  -U          : ignore undefined symbols\n");
	(void) fprintf(stderr, "  -f charmap  : use given charmap file\n");
	(void) fprintf(stderr, "  -u encoding : assume encoding\n");
	(void) fprintf(stderr, "  -w widths   : use screen widths file\n");
	(void) fprintf(stderr, "  -i locsrc   : source file for locale\n");
	exit(4);
}

int
main(int argc, char **argv)
{
	int c;
	char *lfname = NULL;
	char *cfname = NULL;
	char *wfname = NULL;
	DIR *dir;

	init_charmap();
	init_collate();
	init_ctype();
	init_messages();
	init_monetary();
	init_numeric();
	init_time();

	yydebug = 0;

	(void) setlocale(LC_ALL, "");

	while ((c = getopt(argc, argv, "blw:i:cf:u:vUD")) != -1) {
		switch (c) {
		case 'D':
			bsd = 1;
			break;
		case 'b':
		case 'l':
			if (byteorder != 0)
				usage();
			byteorder = c == 'b' ? 4321 : 1234;
			break;
		case 'v':
			verbose++;
			break;
		case 'i':
			lfname = optarg;
			break;
		case 'u':
			set_wide_encoding(optarg);
			break;
		case 'f':
			cfname = optarg;
			break;
		case 'U':
			undefok++;
			break;
		case 'c':
			warnok++;
			break;
		case 'w':
			wfname = optarg;
			break;
		case '?':
			usage();
			break;
		}
	}

	if ((argc - 1) != (optind)) {
		usage();
	}
	locname = argv[argc - 1];
	if (verbose) {
		(void) printf("Processing locale %s.\n", locname);
	}

	if (cfname) {
		if (verbose)
			(void) printf("Loading charmap %s.\n", cfname);
		reset_scanner(cfname);
		(void) yyparse();
	}

	if (wfname) {
		if (verbose)
			(void) printf("Loading widths %s.\n", wfname);
		reset_scanner(wfname);
		(void) yyparse();
	}

	if (verbose) {
		(void) printf("Loading POSIX portable characters.\n");
	}
	add_charmap_posix();

	if (lfname) {
		reset_scanner(lfname);
	} else {
		reset_scanner(NULL);
	}

	/* make the directory for the locale if not already present */
	if (!bsd) {
		while ((dir = opendir(locname)) == NULL) {
			if ((errno != ENOENT) ||
			    (mkdir(locname, 0755) <  0)) {
				errf("%s", strerror(errno));
			}
		}
		(void) closedir(dir);
		(void) mkdir(dirname(category_file()), 0755);
	}

	(void) yyparse();
	if (verbose) {
		(void) printf("All done.\n");
	}
	return (warnings ? 1 : 0);
}
