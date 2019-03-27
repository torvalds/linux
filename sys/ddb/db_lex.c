/*-
 * SPDX-License-Identifier: MIT-CMU
 *
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */
/*
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */
/*
 * Lexical analyzer.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/libkern.h>

#include <ddb/ddb.h>
#include <ddb/db_lex.h>

static char	db_line[DB_MAXLINE];
static char *	db_lp, *db_endlp;

static int	db_lex(void);
static void 	db_flush_line(void);
static int 	db_read_char(void);
static void 	db_unread_char(int);

int
db_read_line(void)
{
	int	i;

	i = db_readline(db_line, sizeof(db_line));
	if (i == 0)
	    return (0);	/* EOI */
	db_lp = db_line;
	db_endlp = db_lp + i;
	return (i);
}

/*
 * Simulate a line of input into DDB.
 */
void
db_inject_line(const char *command)
{

	strlcpy(db_line, command, sizeof(db_line));
	db_lp = db_line;
	db_endlp = db_lp + strlen(command);
}

/*
 * In rare cases, we may want to pull the remainder of the line input
 * verbatim, rather than lexing it.  For example, when assigning literal
 * values associated with scripts.  In that case, return a static pointer to
 * the current location in the input buffer.  The caller must be aware that
 * the contents are not stable if other lex/input calls are made.
 */
char *
db_get_line(void)
{

	return (db_lp);
}

static void
db_flush_line()
{
	db_lp = db_line;
	db_endlp = db_line;
}

static int	db_look_char = 0;

static int
db_read_char(void)
{
	int	c;

	if (db_look_char != 0) {
	    c = db_look_char;
	    db_look_char = 0;
	}
	else if (db_lp >= db_endlp)
	    c = -1;
	else
	    c = *db_lp++;
	return (c);
}

static void
db_unread_char(c)
	int c;
{
	db_look_char = c;
}

static int	db_look_token = 0;

void
db_unread_token(t)
	int	t;
{
	db_look_token = t;
}

int
db_read_token()
{
	int	t;

	if (db_look_token) {
	    t = db_look_token;
	    db_look_token = 0;
	}
	else
	    t = db_lex();
	return (t);
}

db_expr_t	db_tok_number;
char	db_tok_string[TOK_STRING_SIZE];

db_expr_t	db_radix = 16;

void
db_flush_lex(void)
{
	db_flush_line();
	db_look_char = 0;
	db_look_token = 0;
}

static int
db_lex(void)
{
	int	c;

	c = db_read_char();
	while (c <= ' ' || c > '~') {
	    if (c == '\n' || c == -1)
		return (tEOL);
	    c = db_read_char();
	}

	if (c >= '0' && c <= '9') {
	    /* number */
	    int	r, digit = 0;

	    if (c > '0')
		r = db_radix;
	    else {
		c = db_read_char();
		if (c == 'O' || c == 'o')
		    r = 8;
		else if (c == 'T' || c == 't')
		    r = 10;
		else if (c == 'X' || c == 'x')
		    r = 16;
		else {
		    r = db_radix;
		    db_unread_char(c);
		}
		c = db_read_char();
	    }
	    db_tok_number = 0;
	    for (;;) {
		if (c >= '0' && c <= ((r == 8) ? '7' : '9'))
		    digit = c - '0';
		else if (r == 16 && ((c >= 'A' && c <= 'F') ||
				     (c >= 'a' && c <= 'f'))) {
		    if (c >= 'a')
			digit = c - 'a' + 10;
		    else if (c >= 'A')
			digit = c - 'A' + 10;
		}
		else
		    break;
		db_tok_number = db_tok_number * r + digit;
		c = db_read_char();
	    }
	    if ((c >= '0' && c <= '9') ||
		(c >= 'A' && c <= 'Z') ||
		(c >= 'a' && c <= 'z') ||
		(c == '_'))
	    {
		db_error("Bad character in number\n");
		db_flush_lex();
		return (tEOF);
	    }
	    db_unread_char(c);
	    return (tNUMBER);
	}
	if ((c >= 'A' && c <= 'Z') ||
	    (c >= 'a' && c <= 'z') ||
	    c == '_' || c == '\\')
	{
	    /* string */
	    char *cp;

	    cp = db_tok_string;
	    if (c == '\\') {
		c = db_read_char();
		if (c == '\n' || c == -1)
		    db_error("Bad escape\n");
	    }
	    *cp++ = c;
	    while (1) {
		c = db_read_char();
		if ((c >= 'A' && c <= 'Z') ||
		    (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') ||
		    c == '_' || c == '\\' || c == ':' || c == '.')
		{
		    if (c == '\\') {
			c = db_read_char();
			if (c == '\n' || c == -1)
			    db_error("Bad escape\n");
		    }
		    *cp++ = c;
		    if (cp == db_tok_string+sizeof(db_tok_string)) {
			db_error("String too long\n");
			db_flush_lex();
			return (tEOF);
		    }
		    continue;
		}
		else {
		    *cp = '\0';
		    break;
		}
	    }
	    db_unread_char(c);
	    return (tIDENT);
	}

	switch (c) {
	    case '+':
		return (tPLUS);
	    case '-':
		return (tMINUS);
	    case '.':
		c = db_read_char();
		if (c == '.')
		    return (tDOTDOT);
		db_unread_char(c);
		return (tDOT);
	    case '*':
		return (tSTAR);
	    case '/':
		return (tSLASH);
	    case '=':
		c = db_read_char();
		if (c == '=')
		    return (tLOG_EQ);
		db_unread_char(c);
		return (tEQ);
	    case '%':
		return (tPCT);
	    case '#':
		return (tHASH);
	    case '(':
		return (tLPAREN);
	    case ')':
		return (tRPAREN);
	    case ',':
		return (tCOMMA);
	    case '"':
		return (tDITTO);
	    case '$':
		return (tDOLLAR);
	    case '!':
		c = db_read_char();
		if (c == '='){
			return (tLOG_NOT_EQ);
		}
		db_unread_char(c);
		return (tEXCL);
	    case ';':
		return (tSEMI);
	    case '&':
		c = db_read_char();
		if (c == '&')
		    return (tLOG_AND);
		db_unread_char(c);
		return (tBIT_AND);
	    case '|':
		c = db_read_char();
		if (c == '|')
		    return (tLOG_OR);
		db_unread_char(c);
		return (tBIT_OR);
	    case '<':
		c = db_read_char();
		if (c == '<')
		    return (tSHIFT_L);
		if (c == '=') 
		    return (tLESS_EQ);
		db_unread_char(c);
		return (tLESS);
	    case '>':
		c = db_read_char();
		if (c == '>')
		    return (tSHIFT_R);
		if (c == '=')
		    return (tGREATER_EQ);
		db_unread_char(c);
		return (tGREATER);
	    case '?':
		return (tQUESTION);
	    case '~':
		return (tBIT_NOT);
	    case -1:
		return (tEOF);
	}
	db_printf("Bad character\n");
	db_flush_lex();
	return (tEOF);
}
