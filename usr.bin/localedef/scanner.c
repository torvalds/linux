/*-
 * Copyright 2010 Nexenta Systems, Inc.  All rights reserved.
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
 * This file contains the "scanner", which tokenizes the input files
 * for localedef for processing by the higher level grammar processor.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <wchar.h>
#include <sys/types.h>
#include <assert.h>
#include "localedef.h"
#include "parser.h"

int			com_char = '#';
int			esc_char = '\\';
int			mb_cur_min = 1;
int			mb_cur_max = 1;
int			lineno = 1;
int			warnings = 0;
int			is_stdin = 1;
FILE			*input;
static int		nextline;
//static FILE		*input = stdin;
static const char	*filename = "<stdin>";
static int		instring = 0;
static int		escaped = 0;

/*
 * Token space ... grows on demand.
 */
static char *token = NULL;
static int tokidx;
static int toksz = 0;
static int hadtok = 0;

/*
 * Wide string space ... grows on demand.
 */
static wchar_t *widestr = NULL;
static int wideidx = 0;
static int widesz = 0;

/*
 * The last keyword seen.  This is useful to trigger the special lexer rules
 * for "copy" and also collating symbols and elements.
 */
int	last_kw = 0;
static int	category = T_END;

static struct token {
	int id;
	const char *name;
} keywords[] = {
	{ T_COM_CHAR,		"comment_char" },
	{ T_ESC_CHAR,		"escape_char" },
	{ T_END,		"END" },
	{ T_COPY,		"copy" },
	{ T_MESSAGES,		"LC_MESSAGES" },
	{ T_YESSTR,		"yesstr" },
	{ T_YESEXPR,		"yesexpr" },
	{ T_NOSTR,		"nostr" },
	{ T_NOEXPR,		"noexpr" },
	{ T_MONETARY,		"LC_MONETARY" },
	{ T_INT_CURR_SYMBOL,	"int_curr_symbol" },
	{ T_CURRENCY_SYMBOL,	"currency_symbol" },
	{ T_MON_DECIMAL_POINT,	"mon_decimal_point" },
	{ T_MON_THOUSANDS_SEP,	"mon_thousands_sep" },
	{ T_POSITIVE_SIGN,	"positive_sign" },
	{ T_NEGATIVE_SIGN,	"negative_sign" },
	{ T_MON_GROUPING,	"mon_grouping" },
	{ T_INT_FRAC_DIGITS,	"int_frac_digits" },
	{ T_FRAC_DIGITS,	"frac_digits" },
	{ T_P_CS_PRECEDES,	"p_cs_precedes" },
	{ T_P_SEP_BY_SPACE,	"p_sep_by_space" },
	{ T_N_CS_PRECEDES,	"n_cs_precedes" },
	{ T_N_SEP_BY_SPACE,	"n_sep_by_space" },
	{ T_P_SIGN_POSN,	"p_sign_posn" },
	{ T_N_SIGN_POSN,	"n_sign_posn" },
	{ T_INT_P_CS_PRECEDES,	"int_p_cs_precedes" },
	{ T_INT_N_CS_PRECEDES,	"int_n_cs_precedes" },
	{ T_INT_P_SEP_BY_SPACE,	"int_p_sep_by_space" },
	{ T_INT_N_SEP_BY_SPACE,	"int_n_sep_by_space" },
	{ T_INT_P_SIGN_POSN,	"int_p_sign_posn" },
	{ T_INT_N_SIGN_POSN,	"int_n_sign_posn" },
	{ T_COLLATE,		"LC_COLLATE" },
	{ T_COLLATING_SYMBOL,	"collating-symbol" },
	{ T_COLLATING_ELEMENT,	"collating-element" },
	{ T_FROM,		"from" },
	{ T_ORDER_START,	"order_start" },
	{ T_ORDER_END,		"order_end" },
	{ T_FORWARD,		"forward" },
	{ T_BACKWARD,		"backward" },
	{ T_POSITION,		"position" },
	{ T_IGNORE,		"IGNORE" },
	{ T_UNDEFINED,		"UNDEFINED" },
	{ T_NUMERIC,		"LC_NUMERIC" },
	{ T_DECIMAL_POINT,	"decimal_point" },
	{ T_THOUSANDS_SEP,	"thousands_sep" },
	{ T_GROUPING,		"grouping" },
	{ T_TIME,		"LC_TIME" },
	{ T_ABDAY,		"abday" },
	{ T_DAY,		"day" },
	{ T_ABMON,		"abmon" },
	{ T_MON,		"mon" },
	{ T_D_T_FMT,		"d_t_fmt" },
	{ T_D_FMT,		"d_fmt" },
	{ T_T_FMT,		"t_fmt" },
	{ T_AM_PM,		"am_pm" },
	{ T_T_FMT_AMPM,		"t_fmt_ampm" },
	{ T_ERA,		"era" },
	{ T_ERA_D_FMT,		"era_d_fmt" },
	{ T_ERA_T_FMT,		"era_t_fmt" },
	{ T_ERA_D_T_FMT,	"era_d_t_fmt" },
	{ T_ALT_DIGITS,		"alt_digits" },
	{ T_CTYPE,		"LC_CTYPE" },
	{ T_ISUPPER,		"upper" },
	{ T_ISLOWER,		"lower" },
	{ T_ISALPHA,		"alpha" },
	{ T_ISDIGIT,		"digit" },
	{ T_ISPUNCT,		"punct" },
	{ T_ISXDIGIT,		"xdigit" },
	{ T_ISSPACE,		"space" },
	{ T_ISPRINT,		"print" },
	{ T_ISGRAPH,		"graph" },
	{ T_ISBLANK,		"blank" },
	{ T_ISCNTRL,		"cntrl" },
	/*
	 * These entries are local additions, and not specified by
	 * TOG.  Note that they are not guaranteed to be accurate for
	 * all locales, and so applications should not depend on them.
	 */
	{ T_ISSPECIAL,		"special" },
	{ T_ISENGLISH,		"english" },
	{ T_ISPHONOGRAM,	"phonogram" },
	{ T_ISIDEOGRAM,		"ideogram" },
	{ T_ISNUMBER,		"number" },
	/*
	 * We have to support this in the grammar, but it would be a
	 * syntax error to define a character as one of these without
	 * also defining it as an alpha or digit.  We ignore it in our
	 * parsing.
	 */
	{ T_ISALNUM,		"alnum" },
	{ T_TOUPPER,		"toupper" },
	{ T_TOLOWER,		"tolower" },

	/*
	 * These are keywords used in the charmap file.  Note that
	 * Solaris originally used angle brackets to wrap some of them,
	 * but we removed that to simplify our parser.  The first of these
	 * items are "global items."
	 */
	{ T_CHARMAP,		"CHARMAP" },
	{ T_WIDTH,		"WIDTH" },

	{ -1, NULL },
};

/*
 * These special words are only used in a charmap file, enclosed in <>.
 */
static struct token symwords[] = {
	{ T_COM_CHAR,		"comment_char" },
	{ T_ESC_CHAR,		"escape_char" },
	{ T_CODE_SET,		"code_set_name" },
	{ T_MB_CUR_MAX,		"mb_cur_max" },
	{ T_MB_CUR_MIN,		"mb_cur_min" },
	{ -1, NULL },
};

static int categories[] = {
	T_CHARMAP,
	T_CTYPE,
	T_COLLATE,
	T_MESSAGES,
	T_MONETARY,
	T_NUMERIC,
	T_TIME,
	T_WIDTH,
	0
};

void
reset_scanner(const char *fname)
{
	if (fname == NULL) {
		filename = "<stdin>";
		is_stdin = 1;
	} else {
		if (!is_stdin)
			(void) fclose(input);
		if ((input = fopen(fname, "r")) == NULL) {
			perror("fopen");
			exit(4);
		} else {
			is_stdin = 0;
		}
		filename = fname;
	}
	com_char = '#';
	esc_char = '\\';
	instring = 0;
	escaped = 0;
	lineno = 1;
	nextline = 1;
	tokidx = 0;
	wideidx = 0;
}

#define	hex(x)	\
	(isdigit(x) ? (x - '0') : ((islower(x) ? (x - 'a') : (x - 'A')) + 10))
#define	isodigit(x)	((x >= '0') && (x <= '7'))

static int
scanc(void)
{
	int	c;

	if (is_stdin)
		c = getc(stdin);
	else
		c = getc(input);
	lineno = nextline;
	if (c == '\n') {
		nextline++;
	}
	return (c);
}

static void
unscanc(int c)
{
	if (c == '\n') {
		nextline--;
	}
	if (ungetc(c, is_stdin ? stdin : input) < 0) {
		yyerror("ungetc failed");
	}
}

static int
scan_hex_byte(void)
{
	int	c1, c2;
	int	v;

	c1 = scanc();
	if (!isxdigit(c1)) {
		yyerror("malformed hex digit");
		return (0);
	}
	c2 = scanc();
	if (!isxdigit(c2)) {
		yyerror("malformed hex digit");
		return (0);
	}
	v = ((hex(c1) << 4) | hex(c2));
	return (v);
}

static int
scan_dec_byte(void)
{
	int	c1, c2, c3;
	int	b;

	c1 = scanc();
	if (!isdigit(c1)) {
		yyerror("malformed decimal digit");
		return (0);
	}
	b = c1 - '0';
	c2 = scanc();
	if (!isdigit(c2)) {
		yyerror("malformed decimal digit");
		return (0);
	}
	b *= 10;
	b += (c2 - '0');
	c3 = scanc();
	if (!isdigit(c3)) {
		unscanc(c3);
	} else {
		b *= 10;
		b += (c3 - '0');
	}
	return (b);
}

static int
scan_oct_byte(void)
{
	int c1, c2, c3;
	int	b;

	b = 0;

	c1 = scanc();
	if (!isodigit(c1)) {
		yyerror("malformed octal digit");
		return (0);
	}
	b = c1 - '0';
	c2 = scanc();
	if (!isodigit(c2)) {
		yyerror("malformed octal digit");
		return (0);
	}
	b *= 8;
	b += (c2 - '0');
	c3 = scanc();
	if (!isodigit(c3)) {
		unscanc(c3);
	} else {
		b *= 8;
		b += (c3 - '0');
	}
	return (b);
}

void
add_tok(int c)
{
	if ((tokidx + 1) >= toksz) {
		toksz += 64;
		if ((token = realloc(token, toksz)) == NULL) {
			yyerror("out of memory");
			tokidx = 0;
			toksz = 0;
			return;
		}
	}

	token[tokidx++] = (char)c;
	token[tokidx] = 0;
}
void
add_wcs(wchar_t c)
{
	if ((wideidx + 1) >= widesz) {
		widesz += 64;
		widestr = realloc(widestr, (widesz * sizeof (wchar_t)));
		if (widestr == NULL) {
			yyerror("out of memory");
			wideidx = 0;
			widesz = 0;
			return;
		}
	}

	widestr[wideidx++] = c;
	widestr[wideidx] = 0;
}

wchar_t *
get_wcs(void)
{
	wchar_t *ws = widestr;
	wideidx = 0;
	widestr = NULL;
	widesz = 0;
	if (ws == NULL) {
		if ((ws = wcsdup(L"")) == NULL) {
			yyerror("out of memory");
		}
	}
	return (ws);
}

static int
get_byte(void)
{
	int	c;

	if ((c = scanc()) != esc_char) {
		unscanc(c);
		return (EOF);
	}
	c = scanc();

	switch (c) {
	case 'd':
	case 'D':
		return (scan_dec_byte());
	case 'x':
	case 'X':
		return (scan_hex_byte());
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
		/* put the character back so we can get it */
		unscanc(c);
		return (scan_oct_byte());
	default:
		unscanc(c);
		unscanc(esc_char);
		return (EOF);
	}
}

int
get_escaped(int c)
{
	switch (c) {
	case 'n':
		return ('\n');
	case 'r':
		return ('\r');
	case 't':
		return ('\t');
	case 'f':
		return ('\f');
	case 'v':
		return ('\v');
	case 'b':
		return ('\b');
	case 'a':
		return ('\a');
	default:
		return (c);
	}
}

int
get_wide(void)
{
	static char mbs[MB_LEN_MAX + 1] = "";
	static int mbi = 0;
	int c;
	wchar_t	wc;

	if (mb_cur_max >= (int)sizeof (mbs)) {
		yyerror("max multibyte character size too big");
		mbi = 0;
		return (T_NULL);
	}
	for (;;) {
		if ((mbi == mb_cur_max) || ((c = get_byte()) == EOF)) {
			/*
			 * end of the byte sequence reached, but no
			 * valid wide decoding.  fatal error.
			 */
			mbi = 0;
			yyerror("not a valid character encoding");
			return (T_NULL);
		}
		mbs[mbi++] = c;
		mbs[mbi] = 0;

		/* does it decode? */
		if (to_wide(&wc, mbs) >= 0) {
			break;
		}
	}

	mbi = 0;
	if ((category != T_CHARMAP) && (category != T_WIDTH)) {
		if (check_charmap(wc) < 0) {
			yyerror("no symbolic name for character");
			return (T_NULL);
		}
	}

	yylval.wc = wc;
	return (T_CHAR);
}

int
get_symbol(void)
{
	int	c;

	while ((c = scanc()) != EOF) {
		if (escaped) {
			escaped = 0;
			if (c == '\n')
				continue;
			add_tok(get_escaped(c));
			continue;
		}
		if (c == esc_char) {
			escaped = 1;
			continue;
		}
		if (c == '\n') {	/* well that's strange! */
			yyerror("unterminated symbolic name");
			continue;
		}
		if (c == '>') {		/* end of symbol */

			/*
			 * This restarts the token from the beginning
			 * the next time we scan a character.  (This
			 * token is complete.)
			 */

			if (token == NULL) {
				yyerror("missing symbolic name");
				return (T_NULL);
			}
			tokidx = 0;

			/*
			 * A few symbols are handled as keywords outside
			 * of the normal categories.
			 */
			if (category == T_END) {
				int i;
				for (i = 0; symwords[i].name != 0; i++) {
					if (strcmp(token, symwords[i].name) ==
					    0) {
						last_kw = symwords[i].id;
						return (last_kw);
					}
				}
			}
			/*
			 * Contextual rule: Only literal characters are
			 * permitted in CHARMAP.  Anywhere else the symbolic
			 * forms are fine.
			 */
			if ((category != T_CHARMAP) &&
			    (lookup_charmap(token, &yylval.wc)) != -1) {
				return (T_CHAR);
			}
			if ((yylval.collsym = lookup_collsym(token)) != NULL) {
				return (T_COLLSYM);
			}
			if ((yylval.collelem = lookup_collelem(token)) !=
			    NULL) {
				return (T_COLLELEM);
			}
			/* its an undefined symbol */
			yylval.token = strdup(token);
			token = NULL;
			toksz = 0;
			tokidx = 0;
			return (T_SYMBOL);
		}
		add_tok(c);
	}

	yyerror("unterminated symbolic name");
	return (EOF);
}

int
get_category(void)
{
	return (category);
}

static int
consume_token(void)
{
	int	len = tokidx;
	int	i;

	tokidx = 0;
	if (token == NULL)
		return (T_NULL);

	/*
	 * this one is special, because we don't want it to alter the
	 * last_kw field.
	 */
	if (strcmp(token, "...") == 0) {
		return (T_ELLIPSIS);
	}

	/* search for reserved words first */
	for (i = 0; keywords[i].name; i++) {
		int j;
		if (strcmp(keywords[i].name, token) != 0) {
			continue;
		}

		last_kw = keywords[i].id;

		/* clear the top level category if we're done with it */
		if (last_kw == T_END) {
			category = T_END;
		}

		/* set the top level category if we're changing */
		for (j = 0; categories[j]; j++) {
			if (categories[j] != last_kw)
				continue;
			category = last_kw;
		}

		return (keywords[i].id);
	}

	/* maybe its a numeric constant? */
	if (isdigit(*token) || (*token == '-' && isdigit(token[1]))) {
		char *eptr;
		yylval.num = strtol(token, &eptr, 10);
		if (*eptr != 0)
			yyerror("malformed number");
		return (T_NUMBER);
	}

	/*
	 * A single lone character is treated as a character literal.
	 * To avoid duplication of effort, we stick in the charmap.
	 */
	if (len == 1) {
		yylval.wc = token[0];
		return (T_CHAR);
	}

	/* anything else is treated as a symbolic name */
	yylval.token = strdup(token);
	token = NULL;
	toksz = 0;
	tokidx = 0;
	return (T_NAME);
}

void
scan_to_eol(void)
{
	int	c;
	while ((c = scanc()) != '\n') {
		if (c == EOF) {
			/* end of file without newline! */
			errf("missing newline");
			return;
		}
	}
	assert(c == '\n');
}

int
yylex(void)
{
	int		c;

	while ((c = scanc()) != EOF) {

		/* special handling for quoted string */
		if (instring) {
			if (escaped) {
				escaped = 0;

				/* if newline, just eat and forget it */
				if (c == '\n')
					continue;

				if (strchr("xXd01234567", c)) {
					unscanc(c);
					unscanc(esc_char);
					return (get_wide());
				}
				yylval.wc = get_escaped(c);
				return (T_CHAR);
			}
			if (c == esc_char) {
				escaped = 1;
				continue;
			}
			switch (c) {
			case '<':
				return (get_symbol());
			case '>':
				/* oops! should generate syntax error  */
				return (T_GT);
			case '"':
				instring = 0;
				return (T_QUOTE);
			default:
				yylval.wc = c;
				return (T_CHAR);
			}
		}

		/* escaped characters first */
		if (escaped) {
			escaped = 0;
			if (c == '\n') {
				/* eat the newline */
				continue;
			}
			hadtok = 1;
			if (tokidx) {
				/* an escape mid-token is nonsense */
				return (T_NULL);
			}

			/* numeric escapes are treated as wide characters */
			if (strchr("xXd01234567", c)) {
				unscanc(c);
				unscanc(esc_char);
				return (get_wide());
			}

			add_tok(get_escaped(c));
			continue;
		}

		/* if it is the escape charter itself note it */
		if (c == esc_char) {
			escaped = 1;
			continue;
		}

		/* remove from the comment char to end of line */
		if (c == com_char) {
			while (c != '\n') {
				if ((c = scanc()) == EOF) {
					/* end of file without newline! */
					return (EOF);
				}
			}
			assert(c == '\n');
			if (!hadtok) {
				/*
				 * If there were no tokens on this line,
				 * then just pretend it didn't exist at all.
				 */
				continue;
			}
			hadtok = 0;
			return (T_NL);
		}

		if (strchr(" \t\n;()<>,\"", c) && (tokidx != 0)) {
			/*
			 * These are all token delimiters.  If there
			 * is a token already in progress, we need to
			 * process it.
			 */
			unscanc(c);
			return (consume_token());
		}

		switch (c) {
		case '\n':
			if (!hadtok) {
				/*
				 * If the line was completely devoid of tokens,
				 * then just ignore it.
				 */
				continue;
			}
			/* we're starting a new line, reset the token state */
			hadtok = 0;
			return (T_NL);
		case ',':
			hadtok = 1;
			return (T_COMMA);
		case ';':
			hadtok = 1;
			return (T_SEMI);
		case '(':
			hadtok = 1;
			return (T_LPAREN);
		case ')':
			hadtok = 1;
			return (T_RPAREN);
		case '>':
			hadtok = 1;
			return (T_GT);
		case '<':
			/* symbol start! */
			hadtok = 1;
			return (get_symbol());
		case ' ':
		case '\t':
			/* whitespace, just ignore it */
			continue;
		case '"':
			hadtok = 1;
			instring = 1;
			return (T_QUOTE);
		default:
			hadtok = 1;
			add_tok(c);
			continue;
		}
	}
	return (EOF);
}

void
yyerror(const char *msg)
{
	(void) fprintf(stderr, "%s: %d: error: %s\n",
	    filename, lineno, msg);
	exit(4);
}

void
errf(const char *fmt, ...)
{
	char	*msg;

	va_list	va;
	va_start(va, fmt);
	(void) vasprintf(&msg, fmt, va);
	va_end(va);

	(void) fprintf(stderr, "%s: %d: error: %s\n",
	    filename, lineno, msg);
	free(msg);
	exit(4);
}

void
warn(const char *fmt, ...)
{
	char	*msg;

	va_list	va;
	va_start(va, fmt);
	(void) vasprintf(&msg, fmt, va);
	va_end(va);

	(void) fprintf(stderr, "%s: %d: warning: %s\n",
	    filename, lineno, msg);
	free(msg);
	warnings++;
	if (!warnok)
		exit(4);
}
