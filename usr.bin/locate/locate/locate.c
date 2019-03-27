/*
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995 Wolfram Schneider <wosch@FreeBSD.org>. Berlin.
 * Copyright (c) 1989, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * James A. Woods.
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
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
static const char copyright[] =
"@(#) Copyright (c) 1995-1996 Wolfram Schneider, Berlin.\n\
@(#) Copyright (c) 1989, 1993\n\
        The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)locate.c    8.1 (Berkeley) 6/6/93";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

/*
 * Ref: Usenix ;login:, Vol 8, No 1, February/March, 1983, p. 8.
 *
 * Locate scans a file list for the full pathname of a file given only part
 * of the name.  The list has been processed with with "front-compression"
 * and bigram coding.  Front compression reduces space by a factor of 4-5,
 * bigram coding by a further 20-25%.
 *
 * The codes are:
 *
 *      0-28    likeliest differential counts + offset to make nonnegative
 *      30      switch code for out-of-range count to follow in next word
 *      31      an 8 bit char followed
 *      128-255 bigram codes (128 most common, as determined by 'updatedb')
 *      32-127  single character (printable) ascii residue (ie, literal)
 *
 * A novel two-tiered string search technique is employed:
 *
 * First, a metacharacter-free subpattern and partial pathname is matched
 * BACKWARDS to avoid full expansion of the pathname list.  The time savings
 * is 40-50% over forward matching, which cannot efficiently handle
 * overlapped search patterns and compressed path residue.
 *
 * Then, the actual shell glob-style regular expression (if in this form) is
 * matched against the candidate pathnames using the slower routines provided
 * in the standard 'find'.
 */

#include <sys/param.h>
#include <ctype.h>
#include <err.h>
#include <fnmatch.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef MMAP
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <sys/mman.h>
#  include <fcntl.h>
#endif


#include "locate.h"
#include "pathnames.h"

#ifdef DEBUG
#  include <sys/time.h>
#  include <sys/types.h>
#  include <sys/resource.h>
#endif

int f_mmap;             /* use mmap */
int f_icase;            /* ignore case */
int f_stdin;            /* read database from stdin */
int f_statistic;        /* print statistic */
int f_silent;           /* suppress output, show only count of matches */
int f_limit;            /* limit number of output lines, 0 == infinite */
u_int counter;          /* counter for matches [-c] */
char separator='\n';	/* line separator */


void    usage(void);
void    statistic(FILE *, char *);
void    fastfind(FILE *, char *, char *);
void    fastfind_icase(FILE *, char *, char *);
void    fastfind_mmap(char *, caddr_t, int, char *);
void    fastfind_mmap_icase(char *, caddr_t, int, char *);
void	search_mmap(char *, char **);
void	search_fopen(char *, char **);
unsigned long cputime(void);

extern char     **colon(char **, char*, char*);
extern void     print_matches(u_int);
extern int      getwm(caddr_t);
extern int      getwf(FILE *);
extern u_char   *tolower_word(u_char *);
extern int	check_bigram_char(int);
extern char 	*patprep(char *);

int
main(int argc, char **argv)
{
        register int ch;
        char **dbv = NULL;
	char *path_fcodes;      /* locate database */
#ifdef MMAP
        f_mmap = 1;		/* mmap is default */
#endif
	(void) setlocale(LC_ALL, "");

        while ((ch = getopt(argc, argv, "0Scd:il:ms")) != -1)
                switch(ch) {
                case '0':	/* 'find -print0' style */
			separator = '\0';
			break;
                case 'S':	/* statistic lines */   
                        f_statistic = 1;
                        break;
                case 'l': /* limit number of output lines, 0 == infinite */
                        f_limit = atoi(optarg);
                        break;
                case 'd':	/* database */
                        dbv = colon(dbv, optarg, _PATH_FCODES);
                        break;
                case 'i':	/* ignore case */
                        f_icase = 1;
                        break;
                case 'm':	/* mmap */
#ifdef MMAP
                        f_mmap = 1;
#else
						warnx("mmap(2) not implemented");
#endif
                        break;
                case 's':	/* stdio lib */
                        f_mmap = 0;
                        break;
                case 'c': /* suppress output, show only count of matches */
                        f_silent = 1;
                        break;
                default:
                        usage();
                }
        argv += optind;
        argc -= optind;

        /* to few arguments */
        if (argc < 1 && !(f_statistic))
                usage();

        /* no (valid) database as argument */
        if (dbv == NULL || *dbv == NULL) {
                /* try to read database from environment */
                if ((path_fcodes = getenv("LOCATE_PATH")) == NULL ||
		     *path_fcodes == '\0')
                        /* use default database */
                        dbv = colon(dbv, _PATH_FCODES, _PATH_FCODES);
                else		/* $LOCATE_PATH */
                        dbv = colon(dbv, path_fcodes, _PATH_FCODES);
        }

        if (f_icase && UCHAR_MAX < 4096) /* init tolower lookup table */
                for (ch = 0; ch < UCHAR_MAX + 1; ch++)
                        myctype[ch] = tolower(ch);

        /* foreach database ... */
        while((path_fcodes = *dbv) != NULL) {
                dbv++;

                if (!strcmp(path_fcodes, "-"))
                        f_stdin = 1;
		else
			f_stdin = 0;

#ifndef MMAP
		f_mmap = 0;	/* be paranoid */
#endif
                if (!f_mmap || f_stdin || f_statistic) 
			search_fopen(path_fcodes, argv);
                else 
			search_mmap(path_fcodes, argv);
        }

        if (f_silent)
                print_matches(counter);
        exit(0);
}


/*
 * Arguments:
 * db	database
 * s	search strings
 */
void
search_fopen(char *db, char **s)
{
	FILE *fp;
#ifdef DEBUG
        long t0;
#endif
	       
	/* can only read stdin once */
	if (f_stdin) { 
		fp = stdin;
		if (*(s+1) != NULL) {
			warnx("read database from stdin, use only `%s' as pattern", *s);
			*(s+1) = NULL;
		}
	} 
	else if ((fp = fopen(db, "r")) == NULL)
		err(1,  "`%s'", db);

	/* count only chars or lines */
	if (f_statistic) {
		statistic(fp, db);
		(void)fclose(fp);
		return;
	}

	/* foreach search string ... */
	while(*s != NULL) {
#ifdef DEBUG
		t0 = cputime();
#endif
		if (!f_stdin &&
		    fseek(fp, (long)0, SEEK_SET) == -1)
			err(1, "fseek to begin of ``%s''\n", db);

		if (f_icase)
			fastfind_icase(fp, *s, db);
		else
			fastfind(fp, *s, db);
#ifdef DEBUG
		warnx("fastfind %ld ms", cputime () - t0);
#endif
		s++;
	} 
	(void)fclose(fp);
} 

#ifdef MMAP
/*
 * Arguments:
 * db	database
 * s	search strings
 */
void
search_mmap(char *db, char **s)
{
        struct stat sb;
        int fd;
        caddr_t p;
        off_t len;
#ifdef DEBUG
        long t0;
#endif
	if ((fd = open(db, O_RDONLY)) == -1 ||
	    fstat(fd, &sb) == -1)
		err(1, "`%s'", db);
	len = sb.st_size;
	if (len < (2*NBG))
		errx(1,
		    "database too small: %s\nRun /usr/libexec/locate.updatedb",
		    db);

	if ((p = mmap((caddr_t)0, (size_t)len,
		      PROT_READ, MAP_SHARED,
		      fd, (off_t)0)) == MAP_FAILED)
		err(1, "mmap ``%s''", db);

	/* foreach search string ... */
	while (*s != NULL) {
#ifdef DEBUG
		t0 = cputime();
#endif
		if (f_icase)
			fastfind_mmap_icase(*s, p, (int)len, db);
		else
			fastfind_mmap(*s, p, (int)len, db);
#ifdef DEBUG
		warnx("fastfind %ld ms", cputime () - t0);
#endif
		s++;
	}

	if (munmap(p, (size_t)len) == -1)
		warn("munmap %s\n", db);
	
	(void)close(fd);
}
#endif /* MMAP */

#ifdef DEBUG
unsigned long
cputime ()
{
	struct rusage rus;

	getrusage(RUSAGE_SELF, &rus);
	return(rus.ru_utime.tv_sec * 1000 + rus.ru_utime.tv_usec / 1000);
}
#endif /* DEBUG */

void
usage ()
{
        (void)fprintf(stderr,
	"usage: locate [-0Scims] [-l limit] [-d database] pattern ...\n\n");
        (void)fprintf(stderr,
	"default database: `%s' or $LOCATE_PATH\n", _PATH_FCODES);
        exit(1);
}


/* load fastfind functions */

/* statistic */
/* fastfind_mmap, fastfind_mmap_icase */
#ifdef MMAP
#undef FF_MMAP
#undef FF_ICASE

#define FF_MMAP
#include "fastfind.c"
#define FF_ICASE
#include "fastfind.c"
#endif /* MMAP */

/* fopen */
/* fastfind, fastfind_icase */
#undef FF_MMAP
#undef FF_ICASE
#include "fastfind.c"
#define FF_ICASE
#include "fastfind.c"
