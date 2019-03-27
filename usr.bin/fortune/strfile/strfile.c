/*-
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ken Arnold.
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
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static const char sccsid[] = "@(#)strfile.c   8.1 (Berkeley) 5/31/93";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>
#include <ctype.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "strfile.h"

/*
 *	This program takes a file composed of strings separated by
 * lines starting with two consecutive delimiting character (default
 * character is '%') and creates another file which consists of a table
 * describing the file (structure from "strfile.h"), a table of seek
 * pointers to the start of the strings, and the strings, each terminated
 * by a null byte.  Usage:
 *
 *	% strfile [-iorsx] [ -cC ] sourcefile [ datafile ]
 *
 *	C - Allow comments marked by a double delimiter at line's beginning
 *	c - Change delimiting character from '%' to 'C'
 *	s - Silent.  Give no summary of data processed at the end of
 *	    the run.
 *	o - order the strings in alphabetic order
 *	i - if ordering, ignore case
 *	r - randomize the order of the strings
 *	x - set rotated bit
 *
 *		Ken Arnold	Sept. 7, 1978 --
 *
 *	Added ordering options.
 */

#define	STORING_PTRS	(Oflag || Rflag)
#define	CHUNKSIZE	512

#define		ALLOC(ptr, sz)	do { \
			if (ptr == NULL) \
				ptr = malloc(CHUNKSIZE * sizeof(*ptr)); \
			else if (((sz) + 1) % CHUNKSIZE == 0) \
				ptr = realloc(ptr, ((sz) + CHUNKSIZE) * sizeof(*ptr)); \
			if (ptr == NULL) { \
				fprintf(stderr, "out of space\n"); \
				exit(1); \
			} \
		} while (0)

typedef struct {
	int	first;
	off_t	pos;
} STR;

static char	*Infile		= NULL,		/* input file name */
		Outfile[MAXPATHLEN] = "",	/* output file name */
		Delimch		= '%';		/* delimiting character */

static int	Cflag		= false;	/* embedded comments */
static int	Sflag		= false;	/* silent run flag */
static int	Oflag		= false;	/* ordering flag */
static int	Iflag		= false;	/* ignore case flag */
static int	Rflag		= false;	/* randomize order flag */
static int	Xflag		= false;	/* set rotated bit */
static uint32_t	Num_pts		= 0;		/* number of pointers/strings */

static off_t	*Seekpts;

static FILE	*Sort_1, *Sort_2;		/* pointers for sorting */

static STRFILE	Tbl;				/* statistics table */

static STR	*Firstch;			/* first chars of each string */

static void add_offset(FILE *, off_t);
static int cmp_str(const void *, const void *);
static int stable_collate_range_cmp(int, int);
static void do_order(void);
static void getargs(int, char **);
static void randomize(void);
static void usage(void);

/*
 * main:
 *	Drive the sucker.  There are two main modes -- either we store
 *	the seek pointers, if the table is to be sorted or randomized,
 *	or we write the pointer directly to the file, if we are to stay
 *	in file order.  If the former, we allocate and re-allocate in
 *	CHUNKSIZE blocks; if the latter, we just write each pointer,
 *	and then seek back to the beginning to write in the table.
 */
int
main(int ac, char *av[])
{
	char *sp, *nsp, dc;
	FILE *inf, *outf;
	off_t last_off, pos, *p;
	size_t length;
	int first;
	uint32_t cnt;
	STR *fp;
	static char string[257];

	setlocale(LC_ALL, "");

	getargs(ac, av);		/* evalute arguments */
	dc = Delimch;
	if ((inf = fopen(Infile, "r")) == NULL) {
		perror(Infile);
		exit(1);
	}

	if ((outf = fopen(Outfile, "w")) == NULL) {
		perror(Outfile);
		exit(1);
	}
	if (!STORING_PTRS)
		fseek(outf, (long)sizeof(Tbl), SEEK_SET);

	/*
	 * Write the strings onto the file
	 */

	Tbl.str_longlen = 0;
	Tbl.str_shortlen = 0xffffffff;
	Tbl.str_delim = dc;
	Tbl.str_version = VERSION;
	first = Oflag;
	add_offset(outf, ftello(inf));
	last_off = 0;
	do {
		sp = fgets(string, 256, inf);
		if (sp == NULL || (sp[0] == dc && sp[1] == '\n')) {
			pos = ftello(inf);
			length = (size_t)(pos - last_off) -
			    (sp != NULL ? strlen(sp) : 0);
			last_off = pos;
			if (length == 0)
				continue;
			add_offset(outf, pos);
			if ((size_t)Tbl.str_longlen < length)
				Tbl.str_longlen = length;
			if ((size_t)Tbl.str_shortlen > length)
				Tbl.str_shortlen = length;
			first = Oflag;
		}
		else if (first) {
			for (nsp = sp; !isalnum((unsigned char)*nsp); nsp++)
				continue;
			ALLOC(Firstch, Num_pts);
			fp = &Firstch[Num_pts - 1];
			if (Iflag && isupper((unsigned char)*nsp))
				fp->first = tolower((unsigned char)*nsp);
			else
				fp->first = *nsp;
			fp->pos = Seekpts[Num_pts - 1];
			first = false;
		}
	} while (sp != NULL);

	/*
	 * write the tables in
	 */

	fclose(inf);
	Tbl.str_numstr = Num_pts - 1;

	if (Cflag)
		Tbl.str_flags |= STR_COMMENTS;

	if (Oflag)
		do_order();
	else if (Rflag)
		randomize();

	if (Xflag)
		Tbl.str_flags |= STR_ROTATED;

	if (!Sflag) {
		printf("\"%s\" created\n", Outfile);
		if (Num_pts == 2)
			puts("There was 1 string");
		else
			printf("There were %u strings\n", Num_pts - 1);
		printf("Longest string: %u byte%s\n", Tbl.str_longlen,
		       Tbl.str_longlen == 1 ? "" : "s");
		printf("Shortest string: %u byte%s\n", Tbl.str_shortlen,
		       Tbl.str_shortlen == 1 ? "" : "s");
	}

	rewind(outf);
	Tbl.str_version = htobe32(Tbl.str_version);
	Tbl.str_numstr = htobe32(Tbl.str_numstr);
	Tbl.str_longlen = htobe32(Tbl.str_longlen);
	Tbl.str_shortlen = htobe32(Tbl.str_shortlen);
	Tbl.str_flags = htobe32(Tbl.str_flags);
	fwrite((char *)&Tbl, sizeof(Tbl), 1, outf);
	if (STORING_PTRS) {
		for (p = Seekpts, cnt = Num_pts; cnt--; ++p)
			*p = htobe64(*p);
		fwrite(Seekpts, sizeof(*Seekpts), (size_t)Num_pts, outf);
	}
	fclose(outf);
	exit(0);
}

/*
 *	This routine evaluates arguments from the command line
 */
void
getargs(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "Cc:iorsx")) != -1)
		switch(ch) {
		case 'C':			/* embedded comments */
			Cflag++;
			break;
		case 'c':			/* new delimiting char */
			Delimch = *optarg;
			if (!isascii(Delimch)) {
				printf("bad delimiting character: '\\%o\n'",
				       (unsigned char)Delimch);
			}
			break;
		case 'i':			/* ignore case in ordering */
			Iflag++;
			break;
		case 'o':			/* order strings */
			Oflag++;
			break;
		case 'r':			/* randomize pointers */
			Rflag++;
			break;
		case 's':			/* silent */
			Sflag++;
			break;
		case 'x':			/* set the rotated bit */
			Xflag++;
			break;
		case '?':
		default:
			usage();
		}
	argv += optind;

	if (*argv) {
		Infile = *argv;
		if (*++argv)
			strcpy(Outfile, *argv);
	}
	if (!Infile) {
		puts("No input file name");
		usage();
	}
	if (*Outfile == '\0') {
		strlcpy(Outfile, Infile, sizeof(Outfile));
		strlcat(Outfile, ".dat", sizeof(Outfile));
	}
}

void
usage(void)
{
	fprintf(stderr,
	    "strfile [-Ciorsx] [-c char] source_file [output_file]\n");
	exit(1);
}

/*
 * add_offset:
 *	Add an offset to the list, or write it out, as appropriate.
 */
void
add_offset(FILE *fp, off_t off)
{
	off_t beoff;

	if (!STORING_PTRS) {
		beoff = htobe64(off);
		fwrite(&beoff, 1, sizeof(beoff), fp);
	} else {
		ALLOC(Seekpts, Num_pts + 1);
		Seekpts[Num_pts] = off;
	}
	Num_pts++;
}

/*
 * do_order:
 *	Order the strings alphabetically (possibly ignoring case).
 */
void
do_order(void)
{
	uint32_t i;
	off_t *lp;
	STR *fp;

	Sort_1 = fopen(Infile, "r");
	Sort_2 = fopen(Infile, "r");
	qsort(Firstch, (size_t)Tbl.str_numstr, sizeof(*Firstch), cmp_str);
	i = Tbl.str_numstr;
	lp = Seekpts;
	fp = Firstch;
	while (i--)
		*lp++ = fp++->pos;
	fclose(Sort_1);
	fclose(Sort_2);
	Tbl.str_flags |= STR_ORDERED;
}

static int
stable_collate_range_cmp(int c1, int c2)
{
	static char s1[2], s2[2];
	int ret;

	s1[0] = c1;
	s2[0] = c2;
	if ((ret = strcoll(s1, s2)) != 0)
		return (ret);
	return (c1 - c2);
}

/*
 * cmp_str:
 *	Compare two strings in the file
 */
int
cmp_str(const void *s1, const void *s2)
{
	const STR *p1, *p2;
	int c1, c2, n1, n2, r;

#define	SET_N(nf,ch)	(nf = (ch == '\n'))
#define	IS_END(ch,nf)	(ch == EOF || (ch == (unsigned char)Delimch && nf))

	p1 = (const STR *)s1;
	p2 = (const STR *)s2;

	c1 = (unsigned char)p1->first;
	c2 = (unsigned char)p2->first;
	if ((r = stable_collate_range_cmp(c1, c2)) != 0)
		return (r);

	fseeko(Sort_1, p1->pos, SEEK_SET);
	fseeko(Sort_2, p2->pos, SEEK_SET);

	n1 = false;
	n2 = false;
	while (!isalnum(c1 = getc(Sort_1)) && c1 != '\0' && c1 != EOF)
		SET_N(n1, c1);
	while (!isalnum(c2 = getc(Sort_2)) && c2 != '\0' && c2 != EOF)
		SET_N(n2, c2);

	while (!IS_END(c1, n1) && !IS_END(c2, n2)) {
		if (Iflag) {
			if (isupper(c1))
				c1 = tolower(c1);
			if (isupper(c2))
				c2 = tolower(c2);
		}
		if ((r = stable_collate_range_cmp(c1, c2)) != 0)
			return (r);
		SET_N(n1, c1);
		SET_N(n2, c2);
		c1 = getc(Sort_1);
		c2 = getc(Sort_2);
	}
	if (IS_END(c1, n1))
		c1 = 0;
	if (IS_END(c2, n2))
		c2 = 0;

	return (stable_collate_range_cmp(c1, c2));
}

/*
 * randomize:
 *	Randomize the order of the string table.  We must be careful
 *	not to randomize across delimiter boundaries.  All
 *	randomization is done within each block.
 */
void
randomize(void)
{
	uint32_t cnt, i;
	off_t tmp;
	off_t *sp;

	Tbl.str_flags |= STR_RANDOM;
	cnt = Tbl.str_numstr;

	/*
	 * move things around randomly
	 */

	for (sp = Seekpts; cnt > 0; cnt--, sp++) {
		i = arc4random_uniform(cnt);
		tmp = sp[0];
		sp[0] = sp[i];
		sp[i] = tmp;
	}
}
