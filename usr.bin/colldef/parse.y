%{
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1995 Alex Tatmanjants <alex@elvisti.kiev.ua>
 *		at Electronni Visti IA, Kiev, Ukraine.
 *			All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE
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

#include <sys/types.h>
#include <arpa/inet.h>
#include <err.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>
#include "common.h"

extern FILE *yyin;
void yyerror(const char *fmt, ...) __printflike(1, 2);
int yyparse(void);
int yylex(void);
static void usage(void);
static void collate_print_tables(void);

#undef STR_LEN
#define STR_LEN 10
#undef TABLE_SIZE
#define TABLE_SIZE 100
#undef COLLATE_VERSION
#define COLLATE_VERSION    "1.0\n"
#undef COLLATE_VERSION_2
#define COLLATE_VERSION1_2 "1.2\n"

struct __collate_st_char_pri {
	int prim, sec;
};

struct __collate_st_chain_pri {
	u_char str[STR_LEN];
	int prim, sec;
};

char map_name[FILENAME_MAX] = ".";
char curr_chain[STR_LEN];

char __collate_version[STR_LEN];
u_char charmap_table[UCHAR_MAX + 1][CHARMAP_SYMBOL_LEN];

#undef __collate_substitute_table
u_char __collate_substitute_table[UCHAR_MAX + 1][STR_LEN];
#undef __collate_char_pri_table
struct __collate_st_char_pri __collate_char_pri_table[UCHAR_MAX + 1];
struct __collate_st_chain_pri *__collate_chain_pri_table;

int chain_index = 0;
int prim_pri = 1, sec_pri = 1;
#ifdef COLLATE_DEBUG
int debug;
#endif

const char *out_file = "LC_COLLATE";
%}
%union {
	u_char ch;
	u_char str[BUFSIZE];
}
%token SUBSTITUTE WITH ORDER RANGE
%token <str> STRING
%token <str> DEFN
%token <ch> CHAR
%%
collate : statment_list
;
statment_list : statment
	| statment_list '\n' statment
;
statment :
	| charmap
	| substitute
	| order
;
charmap : DEFN CHAR {
	if (strlen($1) + 1 > CHARMAP_SYMBOL_LEN)
		yyerror("Charmap symbol name '%s' is too long", $1);
	strcpy(charmap_table[$2], $1);
}
;
substitute : SUBSTITUTE CHAR WITH STRING {
	if ($2 == '\0')
		yyerror("NUL character can't be substituted");
	if (strchr($4, $2) != NULL)
		yyerror("Char 0x%02x substitution is recursive", $2);
	if (strlen($4) + 1 > STR_LEN)
		yyerror("Char 0x%02x substitution is too long", $2);
	strcpy(__collate_substitute_table[$2], $4);
}
;
order : ORDER order_list {
	FILE *fp;
	int ch, substed, ordered;
	uint32_t u32;

	for (ch = 0; ch < UCHAR_MAX + 1; ch++) {
		substed = (__collate_substitute_table[ch][0] != ch);
		ordered = !!__collate_char_pri_table[ch].prim;
		if (!ordered && !substed)
			yyerror("Char 0x%02x not found", ch);
		if (substed && ordered)
			yyerror("Char 0x%02x can't be ordered since substituted", ch);
	}

	if ((__collate_chain_pri_table = realloc(__collate_chain_pri_table,
	     sizeof(*__collate_chain_pri_table) * (chain_index + 1))) == NULL)
		yyerror("can't grow chain table");
	(void)memset(&__collate_chain_pri_table[chain_index], 0,
		     sizeof(__collate_chain_pri_table[0]));
	chain_index++;

#ifdef COLLATE_DEBUG
	if (debug)
		collate_print_tables();
#endif
	if ((fp = fopen(out_file, "w")) == NULL)
		err(EX_UNAVAILABLE, "can't open destination file %s",
		    out_file);

	strcpy(__collate_version, COLLATE_VERSION1_2);
	if (fwrite(__collate_version, sizeof(__collate_version), 1, fp) != 1)
		err(EX_IOERR,
		"I/O error writing collate version to destination file %s",
		    out_file);
	u32 = htonl(chain_index);
	if (fwrite(&u32, sizeof(u32), 1, fp) != 1)
		err(EX_IOERR,
		"I/O error writing chains number to destination file %s",
		    out_file);
	if (fwrite(__collate_substitute_table,
		   sizeof(__collate_substitute_table), 1, fp) != 1)
		err(EX_IOERR,
		"I/O error writing substitution table to destination file %s",
		    out_file);
	for (ch = 0; ch < UCHAR_MAX + 1; ch++) {
		__collate_char_pri_table[ch].prim =
		    htonl(__collate_char_pri_table[ch].prim);
		__collate_char_pri_table[ch].sec =
		    htonl(__collate_char_pri_table[ch].sec);
	}
	if (fwrite(__collate_char_pri_table,
		   sizeof(__collate_char_pri_table), 1, fp) != 1)
		err(EX_IOERR,
		"I/O error writing char table to destination file %s",
		    out_file);
	for (ch = 0; ch < chain_index; ch++) {
		__collate_chain_pri_table[ch].prim =
		    htonl(__collate_chain_pri_table[ch].prim);
		__collate_chain_pri_table[ch].sec =
		    htonl(__collate_chain_pri_table[ch].sec);
	}
	if (fwrite(__collate_chain_pri_table,
		   sizeof(*__collate_chain_pri_table), chain_index, fp) !=
		   (size_t)chain_index)
		err(EX_IOERR,
		"I/O error writing chain table to destination file %s",
		    out_file);
	if (fclose(fp) != 0)
		err(EX_IOERR, "I/O error closing destination file %s",
		    out_file);
	exit(EX_OK);
}
;
order_list : item
	| order_list ';' item
;
chain : CHAR CHAR {
	curr_chain[0] = $1;
	curr_chain[1] = $2;
	if (curr_chain[0] == '\0' || curr_chain[1] == '\0')
		yyerror("\\0 can't be chained");
	curr_chain[2] = '\0';
}
	| chain CHAR {
	static char tb[2];

	tb[0] = $2;
	if (tb[0] == '\0')
		yyerror("\\0 can't be chained");
	if (strlen(curr_chain) + 2 > STR_LEN)
		yyerror("Chain '%s' grows too long", curr_chain);
	(void)strcat(curr_chain, tb);
}
;
item :  CHAR {
	if (__collate_char_pri_table[$1].prim)
		yyerror("Char 0x%02x duplicated", $1);
	__collate_char_pri_table[$1].prim = prim_pri++;
}
	| chain {
	if ((__collate_chain_pri_table = realloc(__collate_chain_pri_table,
	     sizeof(*__collate_chain_pri_table) * (chain_index + 1))) == NULL)
		yyerror("can't grow chain table");
	(void)memset(&__collate_chain_pri_table[chain_index], 0,
		     sizeof(__collate_chain_pri_table[0]));
	(void)strcpy(__collate_chain_pri_table[chain_index].str, curr_chain);
	__collate_chain_pri_table[chain_index].prim = prim_pri++;
	chain_index++;
}
	| CHAR RANGE CHAR {
	u_int i;

	if ($3 <= $1)
		yyerror("Illegal range 0x%02x -- 0x%02x", $1, $3);

	for (i = $1; i <= $3; i++) {
		if (__collate_char_pri_table[(u_char)i].prim)
			yyerror("Char 0x%02x duplicated", (u_char)i);
		__collate_char_pri_table[(u_char)i].prim = prim_pri++;
	}
}
	| '{' prim_order_list '}' {
	prim_pri++;
}
	| '(' sec_order_list ')' {
	prim_pri++;
	sec_pri = 1;
}
;
prim_order_list : prim_sub_item
	| prim_order_list ',' prim_sub_item 
;
sec_order_list : sec_sub_item
	| sec_order_list ',' sec_sub_item 
;
prim_sub_item : CHAR {
	if (__collate_char_pri_table[$1].prim)
		yyerror("Char 0x%02x duplicated", $1);
	__collate_char_pri_table[$1].prim = prim_pri;
}
	| CHAR RANGE CHAR {
	u_int i;

	if ($3 <= $1)
		yyerror("Illegal range 0x%02x -- 0x%02x",
			$1, $3);

	for (i = $1; i <= $3; i++) {
		if (__collate_char_pri_table[(u_char)i].prim)
			yyerror("Char 0x%02x duplicated", (u_char)i);
		__collate_char_pri_table[(u_char)i].prim = prim_pri;
	}
}
	| chain {
	if ((__collate_chain_pri_table = realloc(__collate_chain_pri_table,
	     sizeof(*__collate_chain_pri_table) * (chain_index + 1))) == NULL)
		yyerror("can't grow chain table");
	(void)memset(&__collate_chain_pri_table[chain_index], 0,
		     sizeof(__collate_chain_pri_table[0]));
	(void)strcpy(__collate_chain_pri_table[chain_index].str, curr_chain);
	__collate_chain_pri_table[chain_index].prim = prim_pri;
	chain_index++;
}
;
sec_sub_item : CHAR {
	if (__collate_char_pri_table[$1].prim)
		yyerror("Char 0x%02x duplicated", $1);
	__collate_char_pri_table[$1].prim = prim_pri;
	__collate_char_pri_table[$1].sec = sec_pri++;
}
	| CHAR RANGE CHAR {
	u_int i;

	if ($3 <= $1)
		yyerror("Illegal range 0x%02x -- 0x%02x",
			$1, $3);

	for (i = $1; i <= $3; i++) {
		if (__collate_char_pri_table[(u_char)i].prim)
			yyerror("Char 0x%02x duplicated", (u_char)i);
		__collate_char_pri_table[(u_char)i].prim = prim_pri;
		__collate_char_pri_table[(u_char)i].sec = sec_pri++;
	}
}
	| chain {
	if ((__collate_chain_pri_table = realloc(__collate_chain_pri_table,
	     sizeof(*__collate_chain_pri_table) * (chain_index + 1))) == NULL)
		yyerror("can't grow chain table");
	(void)memset(&__collate_chain_pri_table[chain_index], 0,
		     sizeof(__collate_chain_pri_table[0]));
	(void)strcpy(__collate_chain_pri_table[chain_index].str, curr_chain);
	__collate_chain_pri_table[chain_index].prim = prim_pri;
	__collate_chain_pri_table[chain_index].sec = sec_pri++;
	chain_index++;
}
;
%%
int
main(int ac, char **av)
{
	int ch;

#ifdef COLLATE_DEBUG
	while((ch = getopt(ac, av, ":do:I:")) != -1) {
#else
	while((ch = getopt(ac, av, ":o:I:")) != -1) {
#endif
		switch (ch)
		{
#ifdef COLLATE_DEBUG
		  case 'd':
			debug++;
			break;
#endif
		  case 'o':
			out_file = optarg;
			break;

		  case 'I':
			strlcpy(map_name, optarg, sizeof(map_name));
			break;

		  default:
			usage();
		}
	}
	ac -= optind;
	av += optind;
	if (ac > 0) {
		if ((yyin = fopen(*av, "r")) == NULL)
			err(EX_UNAVAILABLE, "can't open source file %s", *av);
	}
	for (ch = 0; ch <= UCHAR_MAX; ch++)
		__collate_substitute_table[ch][0] = ch;
	yyparse();
	return 0;
}

static void
usage(void)
{
	fprintf(stderr, "usage: colldef [-I map_dir] [-o out_file] [filename]\n");
	exit(EX_USAGE);
}

void
yyerror(const char *fmt, ...)
{
	va_list ap;
	char msg[128];

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	errx(EX_UNAVAILABLE, "%s near line %d", msg, line_no);
}

#ifdef COLLATE_DEBUG
static void
collate_print_tables(void)
{
	int i;

	printf("Substitute table:\n");
	for (i = 0; i < UCHAR_MAX + 1; i++)
	    if (i != *__collate_substitute_table[i])
		printf("\t'%c' --> \"%s\"\n", i,
		       __collate_substitute_table[i]);
	printf("Chain priority table:\n");
	for (i = 0; i < chain_index - 1; i++)
		printf("\t\"%s\" : %d %d\n",
		    __collate_chain_pri_table[i].str,
		    __collate_chain_pri_table[i].prim,
		    __collate_chain_pri_table[i].sec);
	printf("Char priority table:\n");
	for (i = 0; i < UCHAR_MAX + 1; i++)
		printf("\t'%c' : %d %d\n", i, __collate_char_pri_table[i].prim,
		       __collate_char_pri_table[i].sec);
}
#endif
