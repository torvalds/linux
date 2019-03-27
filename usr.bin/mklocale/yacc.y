%{
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Borman at Krystal Technologies.
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
static char sccsid[] = "@(#)yacc.y	8.1 (Berkeley) 6/6/93";
#endif /* 0 */
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ldef.h"
#include "extern.h"
#include "runefile.h"

static void *xmalloc(unsigned int sz);
static uint32_t *xlalloc(unsigned int sz);
void yyerror(const char *s);
static uint32_t *xrelalloc(uint32_t *old, unsigned int sz);
static void dump_tables(void);
static void cleanout(void);

const char	*locale_file = "<stdout>";

rune_map	maplower = { { 0 }, NULL };
rune_map	mapupper = { { 0 }, NULL };
rune_map	types = { { 0 }, NULL };

_FileRuneLocale	new_locale = { "", "", {}, {}, {}, 0, 0, 0, 0 };
char		*variable = NULL;

void set_map(rune_map *, rune_list *, uint32_t);
void set_digitmap(rune_map *, rune_list *);
void add_map(rune_map *, rune_list *, uint32_t);
static void usage(void);
%}

%union	{
    int32_t	rune;
    int		i;
    char	*str;

    rune_list	*list;
}

%token	<rune>	RUNE
%token		LBRK
%token		RBRK
%token		THRU
%token		MAPLOWER
%token		MAPUPPER
%token		DIGITMAP
%token	<i>	LIST
%token	<str>	VARIABLE
%token		ENCODING
%token		INVALID
%token	<str>	STRING

%type	<list>	list
%type	<list>	map


%%

locale	:	/* empty */
	|	table
	    	{ dump_tables(); }
	;

table	:	entry
	|	table entry
	;

entry	:	ENCODING STRING
		{ if (strcmp($2, "NONE") &&
		      strcmp($2, "ASCII") &&
		      strcmp($2, "UTF-8") &&
		      strcmp($2, "EUC") &&
		      strcmp($2, "GBK") &&
		      strcmp($2, "GB18030") &&
		      strcmp($2, "GB2312") &&
		      strcmp($2, "BIG5") &&
		      strcmp($2, "MSKanji"))
			warnx("ENCODING %s is not supported by libc", $2);
		strlcpy(new_locale.encoding, $2,
		    sizeof(new_locale.encoding)); }
	|	VARIABLE
		{ new_locale.variable_len = strlen($1) + 1;
		  variable = xmalloc(new_locale.variable_len);
		  strcpy(variable, $1);
		}
	|	INVALID RUNE
		{ warnx("the INVALID keyword is deprecated"); }
	|	LIST list
		{ set_map(&types, $2, $1); }
	|	MAPLOWER map
		{ set_map(&maplower, $2, 0); }
	|	MAPUPPER map
		{ set_map(&mapupper, $2, 0); }
	|	DIGITMAP map
		{ set_digitmap(&types, $2); }
	;

list	:	RUNE
		{
		    $$ = (rune_list *)xmalloc(sizeof(rune_list));
		    $$->min = $1;
		    $$->max = $1;
		    $$->next = 0;
		}
	|	RUNE THRU RUNE
		{
		    $$ = (rune_list *)xmalloc(sizeof(rune_list));
		    $$->min = $1;
		    $$->max = $3;
		    $$->next = 0;
		}
	|	list RUNE
		{
		    $$ = (rune_list *)xmalloc(sizeof(rune_list));
		    $$->min = $2;
		    $$->max = $2;
		    $$->next = $1;
		}
	|	list RUNE THRU RUNE
		{
		    $$ = (rune_list *)xmalloc(sizeof(rune_list));
		    $$->min = $2;
		    $$->max = $4;
		    $$->next = $1;
		}
	;

map	:	LBRK RUNE RUNE RBRK
		{
		    $$ = (rune_list *)xmalloc(sizeof(rune_list));
		    $$->min = $2;
		    $$->max = $2;
		    $$->map = $3;
		    $$->next = 0;
		}
	|	map LBRK RUNE RUNE RBRK
		{
		    $$ = (rune_list *)xmalloc(sizeof(rune_list));
		    $$->min = $3;
		    $$->max = $3;
		    $$->map = $4;
		    $$->next = $1;
		}
	|	LBRK RUNE THRU RUNE ':' RUNE RBRK
		{
		    $$ = (rune_list *)xmalloc(sizeof(rune_list));
		    $$->min = $2;
		    $$->max = $4;
		    $$->map = $6;
		    $$->next = 0;
		}
	|	map LBRK RUNE THRU RUNE ':' RUNE RBRK
		{
		    $$ = (rune_list *)xmalloc(sizeof(rune_list));
		    $$->min = $3;
		    $$->max = $5;
		    $$->map = $7;
		    $$->next = $1;
		}
	;
%%

int debug;
FILE *fp;

static void
cleanout(void)
{
    if (fp != NULL)
	unlink(locale_file);
}

int
main(int ac, char *av[])
{
    int x;

    fp = stdout;

    while ((x = getopt(ac, av, "do:")) != -1) {
	switch(x) {
	case 'd':
	    debug = 1;
	    break;
	case 'o':
	    locale_file = optarg;
	    if ((fp = fopen(locale_file, "w")) == NULL)
		err(1, "%s", locale_file);
	    atexit(cleanout);
	    break;
	default:
	    usage();
	}
    }

    switch (ac - optind) {
    case 0:
	break;
    case 1:
	if (freopen(av[optind], "r", stdin) == 0)
	    err(1, "%s", av[optind]);
	break;
    default:
	usage();
    }
    for (x = 0; x < _CACHED_RUNES; ++x) {
	mapupper.map[x] = x;
	maplower.map[x] = x;
    }
    memcpy(new_locale.magic, _FILE_RUNE_MAGIC_1, sizeof(new_locale.magic));

    yyparse();

    return(0);
}

static void
usage(void)
{
    fprintf(stderr, "usage: mklocale [-d] [-o output] [source]\n");
    exit(1);
}

void
yyerror(const char *s)
{
    fprintf(stderr, "%s\n", s);
}

static void *
xmalloc(unsigned int sz)
{
    void *r = malloc(sz);
    if (!r)
	errx(1, "xmalloc");
    return(r);
}

static uint32_t *
xlalloc(unsigned int sz)
{
    uint32_t *r = (uint32_t *)malloc(sz * sizeof(uint32_t));
    if (!r)
	errx(1, "xlalloc");
    return(r);
}

static uint32_t *
xrelalloc(uint32_t *old, unsigned int sz)
{
    uint32_t *r = (uint32_t *)realloc((char *)old,
						sz * sizeof(uint32_t));
    if (!r)
	errx(1, "xrelalloc");
    return(r);
}

void
set_map(rune_map *map, rune_list *list, uint32_t flag)
{
    while (list) {
	rune_list *nlist = list->next;
	add_map(map, list, flag);
	list = nlist;
    }
}

void
set_digitmap(rune_map *map, rune_list *list)
{
    int32_t i;

    while (list) {
	rune_list *nlist = list->next;
	for (i = list->min; i <= list->max; ++i) {
	    if (list->map + (i - list->min)) {
		rune_list *tmp = (rune_list *)xmalloc(sizeof(rune_list));
		tmp->min = i;
		tmp->max = i;
		add_map(map, tmp, list->map + (i - list->min));
	    }
	}
	free(list);
	list = nlist;
    }
}

void
add_map(rune_map *map, rune_list *list, uint32_t flag)
{
    int32_t i;
    rune_list *lr = 0;
    rune_list *r;
    int32_t run;

    while (list->min < _CACHED_RUNES && list->min <= list->max) {
	if (flag)
	    map->map[list->min++] |= flag;
	else
	    map->map[list->min++] = list->map++;
    }

    if (list->min > list->max) {
	free(list);
	return;
    }

    run = list->max - list->min + 1;

    if (!(r = map->root) || (list->max < r->min - 1)
			 || (!flag && list->max == r->min - 1)) {
	if (flag) {
	    list->types = xlalloc(run);
	    for (i = 0; i < run; ++i)
		list->types[i] = flag;
	}
	list->next = map->root;
	map->root = list;
	return;
    }

    for (r = map->root; r && r->max + 1 < list->min; r = r->next)
	lr = r;

    if (!r) {
	/*
	 * We are off the end.
	 */
	if (flag) {
	    list->types = xlalloc(run);
	    for (i = 0; i < run; ++i)
		list->types[i] = flag;
	}
	list->next = 0;
	lr->next = list;
	return;
    }

    if (list->max < r->min - 1) {
	/*
	 * We come before this range and we do not intersect it.
	 * We are not before the root node, it was checked before the loop
	 */
	if (flag) {
	    list->types = xlalloc(run);
	    for (i = 0; i < run; ++i)
		list->types[i] = flag;
	}
	list->next = lr->next;
	lr->next = list;
	return;
    }

    /*
     * At this point we have found that we at least intersect with
     * the range pointed to by `r', we might intersect with one or
     * more ranges beyond `r' as well.
     */

    if (!flag && list->map - list->min != r->map - r->min) {
	/*
	 * There are only two cases when we are doing case maps and
	 * our maps needn't have the same offset.  When we are adjoining
	 * but not intersecting.
	 */
	if (list->max + 1 == r->min) {
	    lr->next = list;
	    list->next = r;
	    return;
	}
	if (list->min - 1 == r->max) {
	    list->next = r->next;
	    r->next = list;
	    return;
	}
	errx(1, "error: conflicting map entries");
    }

    if (list->min >= r->min && list->max <= r->max) {
	/*
	 * Subset case.
	 */

	if (flag) {
	    for (i = list->min; i <= list->max; ++i)
		r->types[i - r->min] |= flag;
	}
	free(list);
	return;
    }
    if (list->min <= r->min && list->max >= r->max) {
	/*
	 * Superset case.  Make him big enough to hold us.
	 * We might need to merge with the guy after him.
	 */
	if (flag) {
	    list->types = xlalloc(list->max - list->min + 1);

	    for (i = list->min; i <= list->max; ++i)
		list->types[i - list->min] = flag;

	    for (i = r->min; i <= r->max; ++i)
		list->types[i - list->min] |= r->types[i - r->min];

	    free(r->types);
	    r->types = list->types;
	} else {
	    r->map = list->map;
	}
	r->min = list->min;
	r->max = list->max;
	free(list);
    } else if (list->min < r->min) {
	/*
	 * Our tail intersects his head.
	 */
	if (flag) {
	    list->types = xlalloc(r->max - list->min + 1);

	    for (i = r->min; i <= r->max; ++i)
		list->types[i - list->min] = r->types[i - r->min];

	    for (i = list->min; i < r->min; ++i)
		list->types[i - list->min] = flag;

	    for (i = r->min; i <= list->max; ++i)
		list->types[i - list->min] |= flag;

	    free(r->types);
	    r->types = list->types;
	} else {
	    r->map = list->map;
	}
	r->min = list->min;
	free(list);
	return;
    } else {
	/*
	 * Our head intersects his tail.
	 * We might need to merge with the guy after him.
	 */
	if (flag) {
	    r->types = xrelalloc(r->types, list->max - r->min + 1);

	    for (i = list->min; i <= r->max; ++i)
		r->types[i - r->min] |= flag;

	    for (i = r->max+1; i <= list->max; ++i)
		r->types[i - r->min] = flag;
	}
	r->max = list->max;
	free(list);
    }

    /*
     * Okay, check to see if we grew into the next guy(s)
     */
    while ((lr = r->next) && r->max >= lr->min) {
	if (flag) {
	    if (r->max >= lr->max) {
		/*
		 * Good, we consumed all of him.
		 */
		for (i = lr->min; i <= lr->max; ++i)
		    r->types[i - r->min] |= lr->types[i - lr->min];
	    } else {
		/*
		 * "append" him on to the end of us.
		 */
		r->types = xrelalloc(r->types, lr->max - r->min + 1);

		for (i = lr->min; i <= r->max; ++i)
		    r->types[i - r->min] |= lr->types[i - lr->min];

		for (i = r->max+1; i <= lr->max; ++i)
		    r->types[i - r->min] = lr->types[i - lr->min];

		r->max = lr->max;
	    }
	} else {
	    if (lr->max > r->max)
		r->max = lr->max;
	}

	r->next = lr->next;

	if (flag)
	    free(lr->types);
	free(lr);
    }
}

static void
dump_tables(void)
{
    int x, first_d, curr_d;
    rune_list *list;

    /*
     * See if we can compress some of the istype arrays
     */
    for(list = types.root; list; list = list->next) {
	list->map = list->types[0];
	for (x = 1; x < list->max - list->min + 1; ++x) {
	    if ((int32_t)list->types[x] != list->map) {
		list->map = 0;
		break;
	    }
	}
    }

    first_d = curr_d = -1;
    for (x = 0; x < _CACHED_RUNES; ++x) {
	uint32_t r = types.map[x];

	if (r & _CTYPE_D) {
		if (first_d < 0)
			first_d = curr_d = x;
		else if (x != curr_d + 1)
			errx(1, "error: DIGIT range is not contiguous");
		else if (x - first_d > 9)
			errx(1, "error: DIGIT range is too big");
		else
			curr_d++;
		if (!(r & _CTYPE_X))
			errx(1,
			"error: DIGIT range is not a subset of XDIGIT range");
	}
    }
    if (first_d < 0)
	errx(1, "error: no DIGIT range defined in the single byte area");
    else if (curr_d - first_d < 9)
	errx(1, "error: DIGIT range is too small in the single byte area");

    /*
     * Fill in our tables.  Do this in network order so that
     * diverse machines have a chance of sharing data.
     * (Machines like Crays cannot share with little machines due to
     *  word size.  Sigh.  We tried.)
     */
    for (x = 0; x < _CACHED_RUNES; ++x) {
	new_locale.runetype[x] = htonl(types.map[x]);
	new_locale.maplower[x] = htonl(maplower.map[x]);
	new_locale.mapupper[x] = htonl(mapupper.map[x]);
    }

    /*
     * Count up how many ranges we will need for each of the extents.
     */
    list = types.root;

    while (list) {
	new_locale.runetype_ext_nranges++;
	list = list->next;
    }
    new_locale.runetype_ext_nranges =
         htonl(new_locale.runetype_ext_nranges);

    list = maplower.root;

    while (list) {
	new_locale.maplower_ext_nranges++;
	list = list->next;
    }
    new_locale.maplower_ext_nranges =
        htonl(new_locale.maplower_ext_nranges);

    list = mapupper.root;

    while (list) {
	new_locale.mapupper_ext_nranges++;
	list = list->next;
    }
    new_locale.mapupper_ext_nranges =
        htonl(new_locale.mapupper_ext_nranges);

    new_locale.variable_len = htonl(new_locale.variable_len);

    /*
     * Okay, we are now ready to write the new locale file.
     */

    /*
     * PART 1: The _FileRuneLocale structure
     */
    if (fwrite((char *)&new_locale, sizeof(new_locale), 1, fp) != 1) {
	perror(locale_file);
	exit(1);
    }
    /*
     * PART 2: The runetype_ext structures (not the actual tables)
     */
    list = types.root;

    while (list) {
	_FileRuneEntry re;

	re.min = htonl(list->min);
	re.max = htonl(list->max);
	re.map = htonl(list->map);

	if (fwrite((char *)&re, sizeof(re), 1, fp) != 1) {
	    perror(locale_file);
	    exit(1);
	}

        list = list->next;
    }
    /*
     * PART 3: The maplower_ext structures
     */
    list = maplower.root;

    while (list) {
	_FileRuneEntry re;

	re.min = htonl(list->min);
	re.max = htonl(list->max);
	re.map = htonl(list->map);

	if (fwrite((char *)&re, sizeof(re), 1, fp) != 1) {
	    perror(locale_file);
	    exit(1);
	}

        list = list->next;
    }
    /*
     * PART 4: The mapupper_ext structures
     */
    list = mapupper.root;

    while (list) {
	_FileRuneEntry re;

	re.min = htonl(list->min);
	re.max = htonl(list->max);
	re.map = htonl(list->map);

	if (fwrite((char *)&re, sizeof(re), 1, fp) != 1) {
	    perror(locale_file);
	    exit(1);
	}

        list = list->next;
    }
    /*
     * PART 5: The runetype_ext tables
     */
    list = types.root;

    while (list) {
	for (x = 0; x < list->max - list->min + 1; ++x)
	    list->types[x] = htonl(list->types[x]);

	if (!list->map) {
	    if (fwrite((char *)list->types,
		       (list->max - list->min + 1) * sizeof(uint32_t),
		       1, fp) != 1) {
		perror(locale_file);
		exit(1);
	    }
	}
        list = list->next;
    }
    /*
     * PART 6: And finally the variable data
     */
    if (new_locale.variable_len != 0 &&
	fwrite(variable, ntohl(new_locale.variable_len), 1, fp) != 1) {
	perror(locale_file);
	exit(1);
    }
    if (fclose(fp) != 0) {
	perror(locale_file);
	exit(1);
    }
    fp = NULL;

    if (!debug)
	return;

    if (new_locale.encoding[0])
	fprintf(stderr, "ENCODING	%s\n", new_locale.encoding);
    if (variable)
	fprintf(stderr, "VARIABLE	%s\n", variable);

    fprintf(stderr, "\nMAPLOWER:\n\n");

    for (x = 0; x < _CACHED_RUNES; ++x) {
	if (isprint(maplower.map[x]))
	    fprintf(stderr, " '%c'", (int)maplower.map[x]);
	else if (maplower.map[x])
	    fprintf(stderr, "%04x", maplower.map[x]);
	else
	    fprintf(stderr, "%4x", 0);
	if ((x & 0xf) == 0xf)
	    fprintf(stderr, "\n");
	else
	    fprintf(stderr, " ");
    }
    fprintf(stderr, "\n");

    for (list = maplower.root; list; list = list->next)
	fprintf(stderr, "\t%04x - %04x : %04x\n", list->min, list->max, list->map);

    fprintf(stderr, "\nMAPUPPER:\n\n");

    for (x = 0; x < _CACHED_RUNES; ++x) {
	if (isprint(mapupper.map[x]))
	    fprintf(stderr, " '%c'", (int)mapupper.map[x]);
	else if (mapupper.map[x])
	    fprintf(stderr, "%04x", mapupper.map[x]);
	else
	    fprintf(stderr, "%4x", 0);
	if ((x & 0xf) == 0xf)
	    fprintf(stderr, "\n");
	else
	    fprintf(stderr, " ");
    }
    fprintf(stderr, "\n");

    for (list = mapupper.root; list; list = list->next)
	fprintf(stderr, "\t%04x - %04x : %04x\n", list->min, list->max, list->map);


    fprintf(stderr, "\nTYPES:\n\n");

    for (x = 0; x < _CACHED_RUNES; ++x) {
	uint32_t r = types.map[x];

	if (r) {
	    if (isprint(x))
		fprintf(stderr, " '%c': %2d", x, (int)(r & 0xff));
	    else
		fprintf(stderr, "%04x: %2d", x, (int)(r & 0xff));

	    fprintf(stderr, " %4s", (r & _CTYPE_A) ? "alph" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_C) ? "ctrl" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_D) ? "dig" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_G) ? "graf" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_L) ? "low" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_P) ? "punc" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_S) ? "spac" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_U) ? "upp" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_X) ? "xdig" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_B) ? "blnk" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_R) ? "prnt" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_I) ? "ideo" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_T) ? "spec" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_Q) ? "phon" : "");
	    fprintf(stderr, "\n");
	}
    }

    for (list = types.root; list; list = list->next) {
	if (list->map && list->min + 3 < list->max) {
	    uint32_t r = list->map;

	    fprintf(stderr, "%04x: %2d",
		(uint32_t)list->min, (int)(r & 0xff));

	    fprintf(stderr, " %4s", (r & _CTYPE_A) ? "alph" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_C) ? "ctrl" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_D) ? "dig" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_G) ? "graf" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_L) ? "low" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_P) ? "punc" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_S) ? "spac" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_U) ? "upp" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_X) ? "xdig" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_B) ? "blnk" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_R) ? "prnt" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_I) ? "ideo" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_T) ? "spec" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_Q) ? "phon" : "");
	    fprintf(stderr, "\n...\n");

	    fprintf(stderr, "%04x: %2d",
		(uint32_t)list->max, (int)(r & 0xff));

	    fprintf(stderr, " %4s", (r & _CTYPE_A) ? "alph" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_C) ? "ctrl" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_D) ? "dig" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_G) ? "graf" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_L) ? "low" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_P) ? "punc" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_S) ? "spac" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_U) ? "upp" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_X) ? "xdig" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_B) ? "blnk" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_R) ? "prnt" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_I) ? "ideo" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_T) ? "spec" : "");
	    fprintf(stderr, " %4s", (r & _CTYPE_Q) ? "phon" : "");
	    fprintf(stderr, "\n");
	} else 
	for (x = list->min; x <= list->max; ++x) {
	    uint32_t r = ntohl(list->types[x - list->min]);

	    if (r) {
		fprintf(stderr, "%04x: %2d", x, (int)(r & 0xff));

		fprintf(stderr, " %4s", (r & _CTYPE_A) ? "alph" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_C) ? "ctrl" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_D) ? "dig" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_G) ? "graf" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_L) ? "low" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_P) ? "punc" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_S) ? "spac" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_U) ? "upp" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_X) ? "xdig" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_B) ? "blnk" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_R) ? "prnt" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_I) ? "ideo" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_T) ? "spec" : "");
		fprintf(stderr, " %4s", (r & _CTYPE_Q) ? "phon" : "");
		fprintf(stderr, "\n");
	    }
	}
    }
}
