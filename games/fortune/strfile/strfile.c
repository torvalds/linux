/*	$OpenBSD: strfile.c,v 1.30 2020/02/14 19:17:33 schwarze Exp $	*/
/*	$NetBSD: strfile.c,v 1.4 1995/04/24 12:23:09 cgd Exp $	*/

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

#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

# define	ALLOC(ptr,sz)	do { \
			if (ptr == NULL) \
				ptr = calloc(CHUNKSIZE, sizeof *ptr); \
			else if (((sz) + 1) % CHUNKSIZE == 0) \
				ptr = reallocarray(ptr, \
						   (sz) + CHUNKSIZE, \
						   sizeof(*ptr)); \
			if (ptr == NULL) \
				err(1, NULL); \
		} while (0)

typedef struct {
	char	first;
	int32_t	pos;
} STR;

char	*Infile		= NULL,		/* input file name */
	Outfile[PATH_MAX] = "",		/* output file name */
	Delimch		= '%';		/* delimiting character */

bool	Sflag		= false;	/* silent run flag */
bool	Oflag		= false;	/* ordering flag */
bool	Iflag		= false;	/* ignore case flag */
bool	Rflag		= false;	/* randomize order flag */
bool	Xflag		= false;	/* set rotated bit */
long	Num_pts		= 0;		/* number of pointers/strings */

int32_t	*Seekpts;

FILE	*Sort_1, *Sort_2;		/* pointers for sorting */

STRFILE	Tbl;				/* statistics table */

STR	*Firstch;			/* first chars of each string */


void add_offset(FILE *, int32_t);
int cmp_str(const void *, const void *);
void do_order(void);
void getargs(int, char **);
void randomize(void);
char *unctrl(char);
__dead void usage(void);

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
	bool		first;
	char		*sp, dc;
	FILE		*inf, *outf;
	int32_t		last_off, length, pos;
	int32_t		*p;
	int		cnt;
	char		*nsp;
	STR		*fp;
	static char	string[257];

	if (pledge("stdio rpath wpath cpath", NULL) == -1)
		err(1, "pledge");

	getargs(ac, av);		/* evalute arguments */
	dc = Delimch;
	if ((inf = fopen(Infile, "r")) == NULL)
		err(1, "%s", Infile);

	if ((outf = fopen(Outfile, "w")) == NULL)
		err(1, "%s", Outfile);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (!STORING_PTRS)
		(void) fseek(outf, sizeof Tbl, SEEK_SET);

	/*
	 * Write the strings onto the file
	 */

	Tbl.str_longlen = 0;
	Tbl.str_shortlen = (unsigned int) 0xffffffff;
	Tbl.str_delim = dc;
	Tbl.str_version = VERSION;
	first = Oflag;
	add_offset(outf, ftell(inf));
	last_off = 0;
	do {
		sp = fgets(string, sizeof(string), inf);
		if (sp == NULL || (sp[0] == dc && sp[1] == '\n')) {
			pos = ftell(inf);
			length = pos - last_off - (sp ? strlen(sp) : 0);
			last_off = pos;
			if (!length)
				continue;
			add_offset(outf, pos);
			if (Tbl.str_longlen < (u_int32_t)length)
				Tbl.str_longlen = length;
			if (Tbl.str_shortlen > (u_int32_t)length)
				Tbl.str_shortlen = length;
			first = Oflag;
		} else if (first) {
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

	(void) fclose(inf);
	Tbl.str_numstr = Num_pts - 1;
	if (Tbl.str_numstr == 0)
		Tbl.str_shortlen = 0;

	if (Oflag)
		do_order();
	else if (Rflag)
		randomize();

	if (Xflag)
		Tbl.str_flags |= STR_ROTATED;

	if (!Sflag) {
		printf("\"%s\" created\n", Outfile);
		if (Tbl.str_numstr == 1)
			puts("There was 1 string");
		else
			printf("There were %u strings\n", Tbl.str_numstr);
		printf("Longest string: %lu byte%s\n",
			  (unsigned long) Tbl.str_longlen,
		       Tbl.str_longlen == 1 ? "" : "s");
		printf("Shortest string: %lu byte%s\n",
			  (unsigned long) Tbl.str_shortlen,
		       Tbl.str_shortlen == 1 ? "" : "s");
	}

	(void) fseek(outf, 0, SEEK_SET);
	Tbl.str_version = htonl(Tbl.str_version);
	Tbl.str_numstr = htonl(Tbl.str_numstr);
	Tbl.str_longlen = htonl(Tbl.str_longlen);
	Tbl.str_shortlen = htonl(Tbl.str_shortlen);
	Tbl.str_flags = htonl(Tbl.str_flags);
	(void) fwrite(&Tbl.str_version,  sizeof(Tbl.str_version),  1, outf);
	(void) fwrite(&Tbl.str_numstr,   sizeof(Tbl.str_numstr),   1, outf);
	(void) fwrite(&Tbl.str_longlen,  sizeof(Tbl.str_longlen),  1, outf);
	(void) fwrite(&Tbl.str_shortlen, sizeof(Tbl.str_shortlen), 1, outf);
	(void) fwrite(&Tbl.str_flags,    sizeof(Tbl.str_flags),    1, outf);
	(void) fwrite( Tbl.stuff,	 sizeof(Tbl.stuff),	   1, outf);
	if (STORING_PTRS) {
		for (p = Seekpts, cnt = Num_pts; cnt--; ++p) {
			*p = htonl(*p);
			(void) fwrite(p, sizeof(*p), 1, outf);
		}
	}
	if (fclose(outf))
		err(1, "fclose `%s'", Outfile);
	return 0;
}

/*
 *	This routine evaluates arguments from the command line
 */
void
getargs(int argc, char *argv[])
{
	int	ch;

	while ((ch = getopt(argc, argv, "c:hiorsx")) != -1) {
		switch(ch) {
		case 'c':			/* new delimiting char */
			Delimch = *optarg;
			if (!isascii((unsigned char)Delimch)) {
				printf("bad delimiting character: '\\%o\n'",
				       Delimch);
			}
			break;
		case 'i':			/* ignore case in ordering */
			Iflag = true;
			break;
		case 'o':			/* order strings */
			Oflag = true;
			break;
		case 'r':			/* randomize pointers */
			Rflag = true;
			break;
		case 's':			/* silent */
			Sflag = true;
			break;
		case 'x':			/* set the rotated bit */
			Xflag = true;
			break;
		case 'h':
		default:
			usage();
		}
	}
	argv += optind;

	if (*argv) {
		Infile = *argv;
		if (*++argv)
			(void) strlcpy(Outfile, *argv, sizeof Outfile);
	}
	if (!Infile) {
		puts("No input file name");
		usage();
	}
	if (*Outfile == '\0') {
		(void) strlcpy(Outfile, Infile, sizeof(Outfile));
		if (strlcat(Outfile, ".dat", sizeof(Outfile)) >= sizeof(Outfile))
			errx(1, "`%s': name too long", Infile);
	}
}

void
usage(void)
{
	(void) fprintf(stderr,
	    "%s [-iorsx] [-c char] sourcefile [datafile]\n", getprogname());
	exit(1);
}

/*
 * add_offset:
 *	Add an offset to the list, or write it out, as appropriate.
 */
void
add_offset(FILE *fp, int32_t off)
{
	int32_t net;

	if (!STORING_PTRS) {
		net = htonl(off);
		fwrite(&net, 1, sizeof net, fp);
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
	int	i;
	int32_t	*lp;
	STR	*fp;

	Sort_1 = fopen(Infile, "r");
	Sort_2 = fopen(Infile, "r");
	qsort((char *) Firstch, (int) Tbl.str_numstr, sizeof *Firstch, cmp_str);
	i = Tbl.str_numstr;
	lp = Seekpts;
	fp = Firstch;
	while (i--)
		*lp++ = fp++->pos;
	(void) fclose(Sort_1);
	(void) fclose(Sort_2);
	Tbl.str_flags |= STR_ORDERED;
}

/*
 * cmp_str:
 *	Compare two strings in the file
 */
char *
unctrl(char c)
{
	static char	buf[3];

	if (isprint((unsigned char)c)) {
		buf[0] = c;
		buf[1] = '\0';
	} else if (c == 0177) {
		buf[0] = '^';
		buf[1] = '?';
	} else {
		buf[0] = '^';
		buf[1] = c + 'A' - 1;
	}
	return buf;
}

int
cmp_str(const void *p1, const void *p2)
{
	bool	n1, n2;
	int	c1, c2;

# define	SET_N(nf,ch)	(nf = (ch == '\n'))
# define	IS_END(ch,nf)	(ch == Delimch && nf)

	c1 = ((STR *)p1)->first;
	c2 = ((STR *)p2)->first;
	if (c1 != c2)
		return c1 - c2;

	(void) fseek(Sort_1, ((STR *)p1)->pos, SEEK_SET);
	(void) fseek(Sort_2, ((STR *)p2)->pos, SEEK_SET);

	n1 = false;
	n2 = false;
	while (!isalnum(c1 = getc(Sort_1)) && c1 != '\0')
		SET_N(n1, c1);
	while (!isalnum(c2 = getc(Sort_2)) && c2 != '\0')
		SET_N(n2, c2);

	while (!IS_END(c1, n1) && !IS_END(c2, n2)) {
		if (Iflag) {
			if (isupper(c1))
				c1 = tolower(c1);
			if (isupper(c2))
				c2 = tolower(c2);
		}
		if (c1 != c2)
			return c1 - c2;
		SET_N(n1, c1);
		SET_N(n2, c2);
		c1 = getc(Sort_1);
		c2 = getc(Sort_2);
	}
	if (IS_END(c1, n1))
		c1 = 0;
	if (IS_END(c2, n2))
		c2 = 0;
	return c1 - c2;
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
	int	cnt, i;
	int32_t	tmp;
	int32_t	*sp;

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
