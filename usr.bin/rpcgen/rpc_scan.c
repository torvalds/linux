/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

#if 0
#ifndef lint
#ident	"@(#)rpc_scan.c	1.13	93/07/05 SMI"
static char sccsid[] = "@(#)rpc_scan.c 1.11 89/02/22 (C) 1987 SMI";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * rpc_scan.c, Scanner for the RPC protocol compiler
 * Copyright (C) 1987, Sun Microsystems, Inc.
 */

#include <sys/types.h>

#include <sys/wait.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "rpc_parse.h"
#include "rpc_scan.h"
#include "rpc_util.h"

#define startcomment(where) (where[0] == '/' && where[1] == '*')
#define endcomment(where) (where[-1] == '*' && where[0] == '/')

static int pushed = 0;	/* is a token pushed */
static token lasttok;	/* last token, if pushed */

static void unget_token( token * );
static void findstrconst(char **, const char **);
static void findchrconst(char **, const char **);
static void findconst(char **, const char **);
static void findkind( char **, token * );
static int cppline( char * );
static int directive( char * );
static void printdirective( char * );
static void docppline(char *, int *, const char **);

/*
 * scan expecting 1 given token
 */
void
scan(tok_kind expect, token *tokp)
{
	get_token(tokp);
	if (tokp->kind != expect) {
		expected1(expect);
	}
}

/*
 * scan expecting any of the 2 given tokens
 */
void
scan2(tok_kind expect1, tok_kind expect2, token *tokp)
{
	get_token(tokp);
	if (tokp->kind != expect1 && tokp->kind != expect2) {
		expected2(expect1, expect2);
	}
}

/*
 * scan expecting any of the 3 given token
 */
void
scan3(tok_kind expect1, tok_kind expect2, tok_kind expect3, token *tokp)
{
	get_token(tokp);
	if (tokp->kind != expect1 && tokp->kind != expect2
	    && tokp->kind != expect3) {
		expected3(expect1, expect2, expect3);
	}
}

/*
 * scan expecting a constant, possibly symbolic
 */
void
scan_num(token *tokp)
{
	get_token(tokp);
	switch (tokp->kind) {
	case TOK_IDENT:
		break;
	default:
		error("constant or identifier expected");
	}
}

/*
 * Peek at the next token
 */
void
peek(token *tokp)
{
	get_token(tokp);
	unget_token(tokp);
}

/*
 * Peek at the next token and scan it if it matches what you expect
 */
int
peekscan(tok_kind expect, token *tokp)
{
	peek(tokp);
	if (tokp->kind == expect) {
		get_token(tokp);
		return (1);
	}
	return (0);
}

/*
 * Get the next token, printing out any directive that are encountered.
 */
void
get_token(token *tokp)
{
	int commenting;
	int stat = 0;
	
	
	if (pushed) {
		pushed = 0;
		*tokp = lasttok;
		return;
	}
	commenting = 0;
	for (;;) {
		if (*where == 0) {
			for (;;) {
				if (!fgets(curline, MAXLINESIZE, fin)) {
					tokp->kind = TOK_EOF;
					/* now check if cpp returned non NULL value */
					waitpid(childpid, &stat, WUNTRACED);
					if (stat > 0) {
					/* Set return value from rpcgen */
						nonfatalerrors = stat >> 8;
					}
					*where = 0;
					return;
				}
				linenum++;
				if (commenting) {
					break;
				} else if (cppline(curline)) {
					docppline(curline, &linenum,
						  &infilename);
				} else if (directive(curline)) {
					printdirective(curline);
				} else {
					break;
				}
			}
			where = curline;
		} else if (isspace(*where)) {
			while (isspace(*where)) {
				where++;	/* eat */
			}
		} else if (commenting) {
			for (where++; *where; where++) {
				if (endcomment(where)) {
					where++;
					commenting--;
					break;
				}
			}
		} else if (startcomment(where)) {
			where += 2;
			commenting++;
		} else {
			break;
		}
	}

	/*
	 * 'where' is not whitespace, comment or directive Must be a token!
	 */
	switch (*where) {
	case ':':
		tokp->kind = TOK_COLON;
		where++;
		break;
	case ';':
		tokp->kind = TOK_SEMICOLON;
		where++;
		break;
	case ',':
		tokp->kind = TOK_COMMA;
		where++;
		break;
	case '=':
		tokp->kind = TOK_EQUAL;
		where++;
		break;
	case '*':
		tokp->kind = TOK_STAR;
		where++;
		break;
	case '[':
		tokp->kind = TOK_LBRACKET;
		where++;
		break;
	case ']':
		tokp->kind = TOK_RBRACKET;
		where++;
		break;
	case '{':
		tokp->kind = TOK_LBRACE;
		where++;
		break;
	case '}':
		tokp->kind = TOK_RBRACE;
		where++;
		break;
	case '(':
		tokp->kind = TOK_LPAREN;
		where++;
		break;
	case ')':
		tokp->kind = TOK_RPAREN;
		where++;
		break;
	case '<':
		tokp->kind = TOK_LANGLE;
		where++;
		break;
	case '>':
		tokp->kind = TOK_RANGLE;
		where++;
		break;

	case '"':
		tokp->kind = TOK_STRCONST;
		findstrconst(&where, &tokp->str);
		break;
	case '\'':
		tokp->kind = TOK_CHARCONST;
		findchrconst(&where, &tokp->str);
		break;

	case '-':
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		tokp->kind = TOK_IDENT;
		findconst(&where, &tokp->str);
		break;

	default:
		if (!(isalpha(*where) || *where == '_')) {
			char buf[100];
			char *p;

			s_print(buf, "illegal character in file: ");
			p = buf + strlen(buf);
			if (isprint(*where)) {
				s_print(p, "%c", *where);
			} else {
				s_print(p, "%d", *where);
			}
			error(buf);
		}
		findkind(&where, tokp);
		break;
	}
}

static void
unget_token(token *tokp)
{
	lasttok = *tokp;
	pushed = 1;
}

static void
findstrconst(char **str, const char **val)
{
	char *p;
	char *tmp;
	int size;

	p = *str;
	do {
		p++;
	} while (*p && *p != '"');
	if (*p == 0) {
		error("unterminated string constant");
	}
	p++;
	size = p - *str + 1;
	tmp = xmalloc(size);
	(void) strlcpy(tmp, *str, size);
	*val = tmp;
	*str = p;
}

static void
findchrconst(char **str, const char **val)
{
	char *p;
	char *tmp;
	int size;

	p = *str;
	do {
		p++;
	} while (*p && *p != '\'');
	if (*p == 0) {
		error("unterminated string constant");
	}
	p++;
	size = p - *str + 1;
	if (size != 4) {
		error("empty char string");
	}
	tmp = xmalloc(size);
	(void) strlcpy(tmp, *str, size);
	*val = tmp;
	*str = p;
}

static void
findconst(char **str, const char **val)
{
	char *p;
	char *tmp;
	int size;

	p = *str;
	if (*p == '0' && *(p + 1) == 'x') {
		p++;
		do {
			p++;
		} while (isxdigit(*p));
	} else {
		do {
			p++;
		} while (isdigit(*p));
	}
	size = p - *str + 1;
	tmp = xmalloc(size);
	(void) strlcpy(tmp, *str, size);
	*val = tmp;
	*str = p;
}

static token symbols[] = {
			  {TOK_CONST, "const"},
			  {TOK_UNION, "union"},
			  {TOK_SWITCH, "switch"},
			  {TOK_CASE, "case"},
			  {TOK_DEFAULT, "default"},
			  {TOK_STRUCT, "struct"},
			  {TOK_TYPEDEF, "typedef"},
			  {TOK_ENUM, "enum"},
			  {TOK_OPAQUE, "opaque"},
			  {TOK_BOOL, "bool"},
			  {TOK_VOID, "void"},
			  {TOK_CHAR, "char"},
			  {TOK_INT, "int"},
			  {TOK_UNSIGNED, "unsigned"},
			  {TOK_SHORT, "short"},
			  {TOK_LONG, "long"},
			  {TOK_HYPER, "hyper"},
			  {TOK_FLOAT, "float"},
			  {TOK_DOUBLE, "double"},
			  {TOK_QUAD, "quadruple"},
			  {TOK_STRING, "string"},
			  {TOK_PROGRAM, "program"},
			  {TOK_VERSION, "version"},
			  {TOK_EOF, "??????"},
};

static void
findkind(char **mark, token *tokp)
{
	int len;
	token *s;
	char *str, *tmp;

	str = *mark;
	for (s = symbols; s->kind != TOK_EOF; s++) {
		len = strlen(s->str);
		if (strncmp(str, s->str, len) == 0) {
			if (!isalnum(str[len]) && str[len] != '_') {
				tokp->kind = s->kind;
				tokp->str = s->str;
				*mark = str + len;
				return;
			}
		}
	}
	tokp->kind = TOK_IDENT;
	for (len = 0; isalnum(str[len]) || str[len] == '_'; len++);
	tmp = xmalloc(len + 1);
	(void) strlcpy(tmp, str, len + 1);
	tokp->str = tmp;
	*mark = str + len;
}

static int
cppline(char *line)
{
	return (line == curline && *line == '#');
}

static int
directive(char *line)
{
	return (line == curline && *line == '%');
}

static void
printdirective(char *line)
{
	f_print(fout, "%s", line + 1);
}

static void
docppline(char *line, int *lineno, const char **fname)
{
	char *file;
	int num;
	char *p;

	line++;
	while (isspace(*line)) {
		line++;
	}
	num = atoi(line);
	while (isdigit(*line)) {
		line++;
	}
	while (isspace(*line)) {
		line++;
	}
	if (*line != '"') {
		error("preprocessor error");
	}
	line++;
	p = file = xmalloc(strlen(line) + 1);
	while (*line && *line != '"') {
		*p++ = *line++;
	}
	if (*line == 0) {
		error("preprocessor error");
	}
	*p = 0;
	if (*file == 0) {
		*fname = NULL;
		free(file);
	} else {
		*fname = file;
	}
	*lineno = num - 1;
}
