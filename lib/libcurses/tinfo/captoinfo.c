/* $OpenBSD: captoinfo.c,v 1.17 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2020,2021 Thomas E. Dickey                                *
 * Copyright 1998-2016,2017 Free Software Foundation, Inc.                  *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

/*
 *	captoinfo.c
 *
 *	Provide conversion in both directions between termcap and terminfo.
 *
 * cap-to-info --- conversion between termcap and terminfo formats
 *
 *	The captoinfo() code was swiped from Ross Ridge's mytinfo package,
 *	adapted to fit ncurses by Eric S. Raymond <esr@snark.thyrsus.com>.
 *
 *	It has just one entry point:
 *
 *	char *_nc_captoinfo(n, s, parameterized)
 *
 *	Convert value s for termcap string capability named n into terminfo
 *	format.
 *
 *	This code recognizes all the standard 4.4BSD %-escapes:
 *
 *	%%       output `%'
 *	%d       output value as in printf %d
 *	%2       output value as in printf %2d
 *	%3       output value as in printf %3d
 *	%.       output value as in printf %c
 *	%+x      add x to value, then do %.
 *	%>xy     if value > x then add y, no output
 *	%r       reverse order of two parameters, no output
 *	%i       increment by one, no output
 *	%n       exclusive-or all parameters with 0140 (Datamedia 2500)
 *	%B       BCD (16*(value/10)) + (value%10), no output
 *	%D       Reverse coding (value - 2*(value%16)), no output (Delta Data).
 *
 *	Also, %02 and %03 are accepted as synonyms for %2 and %3.
 *
 *	Besides all the standard termcap escapes, this translator understands
 *	the following extended escapes:
 *
 *	used by GNU Emacs termcap libraries
 *		%a[+*-/=][cp]x	GNU arithmetic.
 *		%m		xor the first two parameters by 0177
 *		%b		backup to previous parameter
 *		%f		skip this parameter
 *
 *	used by the University of Waterloo (MFCF) termcap libraries
 *		%-x	 subtract parameter FROM char x and output it as a char
 *		%ax	 add the character x to parameter
 *
 *	If #define WATERLOO is on, also enable these translations:
 *
 *		%sx	 subtract parameter FROM the character x
 *
 *	By default, this Waterloo translations are not compiled in, because
 *	the Waterloo %s conflicts with the way terminfo uses %s in strings for
 *	function programming.
 *
 *	Note the two definitions of %a: the GNU definition is translated if the
 *	characters after the 'a' are valid for it, otherwise the UW definition
 *	is translated.
 */

#include <curses.priv.h>

#include <ctype.h>
#include <tic.h>

MODULE_ID("$Id: captoinfo.c,v 1.17 2023/10/17 09:52:09 nicm Exp $")

#if 0
#define DEBUG_THIS(p) DEBUG(9, p)
#else
#define DEBUG_THIS(p)		/* nothing */
#endif

#define MAX_PUSHED	16	/* max # args we can push onto the stack */

static int stack[MAX_PUSHED];	/* the stack */
static int stackptr;		/* the next empty place on the stack */
static int onstack;		/* the top of stack */
static int seenm;		/* seen a %m */
static int seenn;		/* seen a %n */
static int seenr;		/* seen a %r */
static int param;		/* current parameter */
static char *dp;		/* pointer to end of the converted string */

static char *my_string;
static size_t my_length;

static char *
init_string(void)
/* initialize 'my_string', 'my_length' */
{
    if (my_string == 0)
	TYPE_MALLOC(char, my_length = 256, my_string);

    *my_string = '\0';
    return my_string;
}

static char *
save_string(char *d, const char *const s)
{
    size_t have = (size_t) (d - my_string);
    size_t need = have + strlen(s) + 2;
    if (need > my_length) {
	my_string = (char *) _nc_doalloc(my_string, my_length = (need + need));
	if (my_string == 0)
	    _nc_err_abort(MSG_NO_MEMORY);
	d = my_string + have;
    }
    _nc_STRCPY(d, s, my_length - have);
    return d + strlen(d);
}

static NCURSES_INLINE char *
save_char(char *s, int c)
{
    static char temp[2];
    temp[0] = (char) c;
    return save_string(s, temp);
}

static void
push(void)
/* push onstack on to the stack */
{
    if (stackptr >= MAX_PUSHED)
	_nc_warning("string too complex to convert");
    else
	stack[stackptr++] = onstack;
}

static void
pop(void)
/* pop the top of the stack into onstack */
{
    if (stackptr == 0) {
	if (onstack == 0)
	    _nc_warning("I'm confused");
	else
	    onstack = 0;
    } else
	onstack = stack[--stackptr];
    param++;
}

static int
cvtchar(register const char *sp)
/* convert a character to a terminfo push */
{
    unsigned char c = 0;
    int len;

    switch (*sp) {
    case '\\':
	switch (*++sp) {
	case '\'':
	case '$':
	case '\\':
	case '%':
	    c = UChar(*sp);
	    len = 2;
	    break;
	case '\0':
	    c = '\\';
	    len = 1;
	    break;
	case '0':
	case '1':
	case '2':
	case '3':
	    len = 1;
	    while (isdigit(UChar(*sp))) {
		c = UChar(8 * c + (*sp++ - '0'));
		len++;
	    }
	    break;
	default:
	    c = UChar(*sp);
	    len = (c != '\0') ? 2 : 1;
	    break;
	}
	break;
    case '^':
	len = 2;
	c = UChar(*++sp);
	if (c == '?') {
	    c = 127;
	} else if (c == '\0') {
	    len = 1;
	} else {
	    c &= 0x1f;
	}
	break;
    default:
	c = UChar(*sp);
	len = (c != '\0') ? 1 : 0;
    }
    if (isgraph(c) && c != ',' && c != '\'' && c != '\\' && c != ':') {
	dp = save_string(dp, "%\'");
	dp = save_char(dp, c);
	dp = save_char(dp, '\'');
    } else if (c != '\0') {
	dp = save_string(dp, "%{");
	if (c > 99)
	    dp = save_char(dp, c / 100 + '0');
	if (c > 9)
	    dp = save_char(dp, ((int) (c / 10)) % 10 + '0');
	dp = save_char(dp, c % 10 + '0');
	dp = save_char(dp, '}');
    }
    return len;
}

static void
getparm(int parm, int n)
/* push n copies of param on the terminfo stack if not already there */
{
    int nn;

    if (seenr) {
	if (parm == 1)
	    parm = 2;
	else if (parm == 2)
	    parm = 1;
    }

    for (nn = 0; nn < n; ++nn) {
	dp = save_string(dp, "%p");
	dp = save_char(dp, '0' + parm);
    }

    if (onstack == parm) {
	if (n > 1) {
	    _nc_warning("string may not be optimal");
	    dp = save_string(dp, "%Pa");
	    while (n-- > 0) {
		dp = save_string(dp, "%ga");
	    }
	}
	return;
    }
    if (onstack != 0)
	push();

    onstack = parm;

    if (seenn && parm < 3) {
	dp = save_string(dp, "%{96}%^");
    }

    if (seenm && parm < 3) {
	dp = save_string(dp, "%{127}%^");
    }
}

/*
 * Convert a termcap string to terminfo format.
 * 'cap' is the relevant terminfo capability index.
 * 's' is the string value of the capability.
 * 'parameterized' tells what type of translations to do:
 *	% translations if 1
 *	pad translations if >=0
 */
NCURSES_EXPORT(char *)
_nc_captoinfo(const char *cap, const char *s, int const parameterized)
{
    const char *capstart;

    stackptr = 0;
    onstack = 0;
    seenm = 0;
    seenn = 0;
    seenr = 0;
    param = 1;

    DEBUG_THIS(("_nc_captoinfo params %d, %s", parameterized, s));

    dp = init_string();

    /* skip the initial padding (if we haven't been told not to) */
    capstart = 0;
    if (s == 0)
	s = "";
    if (parameterized >= 0 && isdigit(UChar(*s)))
	for (capstart = s; *s != '\0'; s++)
	    if (!(isdigit(UChar(*s)) || *s == '*' || *s == '.'))
		break;

    while (*s != '\0') {
	switch (*s) {
	case '%':
	    s++;
	    if (parameterized < 1) {
		dp = save_char(dp, '%');
		break;
	    }
	    switch (*s++) {
	    case '%':
		dp = save_string(dp, "%%");
		break;
	    case 'r':
		if (seenr++ == 1) {
		    _nc_warning("saw %%r twice in %s", cap);
		}
		break;
	    case 'm':
		if (seenm++ == 1) {
		    _nc_warning("saw %%m twice in %s", cap);
		}
		break;
	    case 'n':
		if (seenn++ == 1) {
		    _nc_warning("saw %%n twice in %s", cap);
		}
		break;
	    case 'i':
		dp = save_string(dp, "%i");
		break;
	    case '6':
	    case 'B':
		getparm(param, 1);
		dp = save_string(dp, "%{10}%/%{16}%*");
		getparm(param, 1);
		dp = save_string(dp, "%{10}%m%+");
		break;
	    case '8':
	    case 'D':
		getparm(param, 2);
		dp = save_string(dp, "%{2}%*%-");
		break;
	    case '>':
		/* %?%{x}%>%t%{y}%+%; */
		if (s[0] && s[1]) {
		    getparm(param, 2);
		    dp = save_string(dp, "%?");
		    s += cvtchar(s);
		    dp = save_string(dp, "%>%t");
		    s += cvtchar(s);
		    dp = save_string(dp, "%+%;");
		} else {
		    _nc_warning("expected two characters after %%>");
		    dp = save_string(dp, "%>");
		}
		break;
	    case 'a':
		if ((*s == '=' || *s == '+' || *s == '-'
		     || *s == '*' || *s == '/')
		    && (s[1] == 'p' || s[1] == 'c')
		    && s[2] != '\0') {
		    int l;
		    l = 2;
		    if (*s != '=')
			getparm(param, 1);
		    if (s[1] == 'p') {
			getparm(param + s[2] - '@', 1);
			if (param != onstack) {
			    pop();
			    param--;
			}
			l++;
		    } else
			l += cvtchar(s + 2);
		    switch (*s) {
		    case '+':
			dp = save_string(dp, "%+");
			break;
		    case '-':
			dp = save_string(dp, "%-");
			break;
		    case '*':
			dp = save_string(dp, "%*");
			break;
		    case '/':
			dp = save_string(dp, "%/");
			break;
		    case '=':
			if (seenr) {
			    if (param == 1)
				onstack = 2;
			    else if (param == 2)
				onstack = 1;
			    else
				onstack = param;
			} else
			    onstack = param;
			break;
		    }
		    s += l;
		    break;
		}
		getparm(param, 1);
		s += cvtchar(s);
		dp = save_string(dp, "%+");
		break;
	    case '+':
		getparm(param, 1);
		s += cvtchar(s);
		dp = save_string(dp, "%+%c");
		pop();
		break;
	    case 's':
#ifdef WATERLOO
		s += cvtchar(s);
		getparm(param, 1);
		dp = save_string(dp, "%-");
#else
		getparm(param, 1);
		dp = save_string(dp, "%s");
		pop();
#endif /* WATERLOO */
		break;
	    case '-':
		s += cvtchar(s);
		getparm(param, 1);
		dp = save_string(dp, "%-%c");
		pop();
		break;
	    case '.':
		getparm(param, 1);
		dp = save_string(dp, "%c");
		pop();
		break;
	    case '0':		/* not clear any of the historical termcaps did this */
		if (*s == '3') {
		    ++s;
		    goto see03;
		}
		if (*s == '2') {
		    ++s;
		    goto see02;
		}
		goto invalid;
	    case '2':
	      see02:
		getparm(param, 1);
		dp = save_string(dp, "%2d");
		pop();
		break;
	    case '3':
	      see03:
		getparm(param, 1);
		dp = save_string(dp, "%3d");
		pop();
		break;
	    case 'd':
		getparm(param, 1);
		dp = save_string(dp, "%d");
		pop();
		break;
	    case 'f':
		param++;
		break;
	    case 'b':
		param--;
		break;
	    case '\\':
		dp = save_string(dp, "%\\");
		break;
	    default:
	      invalid:
		dp = save_char(dp, '%');
		s--;
		_nc_warning("unknown %% code %s (%#x) in %s",
			    unctrl((chtype) *s), UChar(*s), cap);
		break;
	    }
	    break;
	default:
	    if (*s != '\0')
		dp = save_char(dp, *s++);
	    break;
	}
    }

    /*
     * Now, if we stripped off some leading padding, add it at the end
     * of the string as mandatory padding.
     */
    if (capstart) {
	dp = save_string(dp, "$<");
	for (s = capstart; *s != '\0'; s++)
	    if (isdigit(UChar(*s)) || *s == '*' || *s == '.')
		dp = save_char(dp, *s);
	    else
		break;
	dp = save_string(dp, "/>");
    }

    (void) save_char(dp, '\0');

    DEBUG_THIS(("... _nc_captoinfo %s", NonNull(my_string)));

    return (my_string);
}

/*
 * Check for an expression that corresponds to "%B" (BCD):
 *	(parameter / 10) * 16 + (parameter % 10)
 */
static int
bcd_expression(const char *str)
{
    /* leave this non-const for HPUX */
    static char fmt[] = "%%p%c%%{10}%%/%%{16}%%*%%p%c%%{10}%%m%%+";
    int len = 0;
    char ch1, ch2;

    if (sscanf(str, fmt, &ch1, &ch2) == 2
	&& isdigit(UChar(ch1))
	&& isdigit(UChar(ch2))
	&& (ch1 == ch2)) {
	len = 28;
#ifndef NDEBUG
	{
	    char buffer[80];
	    int tst;
	    _nc_SPRINTF(buffer, _nc_SLIMIT(sizeof(buffer)) fmt, ch1, ch2);
	    tst = strlen(buffer) - 1;
	    assert(len == tst);
	}
#endif
    }
    return len;
}

static char *
save_tc_char(char *bufptr, int c1)
{
    if (is7bits(c1) && isprint(c1)) {
	if (c1 == ':' || c1 == '\\')
	    bufptr = save_char(bufptr, '\\');
	bufptr = save_char(bufptr, c1);
    } else {
	char temp[80];

	if (c1 == (c1 & 0x1f)) {	/* iscntrl() returns T on 255 */
	    _nc_SPRINTF(temp, _nc_SLIMIT(sizeof(temp))
			"%.20s", unctrl((chtype) c1));
	} else {
	    _nc_SPRINTF(temp, _nc_SLIMIT(sizeof(temp))
			"\\%03o", c1);
	}
	bufptr = save_string(bufptr, temp);
    }
    return bufptr;
}

static char *
save_tc_inequality(char *bufptr, int c1, int c2)
{
    bufptr = save_string(bufptr, "%>");
    bufptr = save_tc_char(bufptr, c1);
    bufptr = save_tc_char(bufptr, c2);
    return bufptr;
}

/*
 * info-to-cap --- conversion between terminfo and termcap formats
 *
 * Here are the capabilities infotocap assumes it can translate to:
 *
 *     %%       output `%'
 *     %d       output value as in printf %d
 *     %2       output value as in printf %2d
 *     %3       output value as in printf %3d
 *     %.       output value as in printf %c
 *     %+c      add character c to value, then do %.
 *     %>xy     if value > x then add y, no output
 *     %r       reverse order of two parameters, no output
 *     %i       increment by one, no output
 *     %n       exclusive-or all parameters with 0140 (Datamedia 2500)
 *     %B       BCD (16*(value/10)) + (value%10), no output
 *     %D       Reverse coding (value - 2*(value%16)), no output (Delta Data).
 *     %m       exclusive-or all parameters with 0177 (not in 4.4BSD)
 */

#define octal_fixup(n, c) fixups[n].ch = ((fixups[n].ch << 3) | ((c) - '0'))

/*
 * Convert a terminfo string to termcap format.  Parameters are as in
 * _nc_captoinfo().
 */
NCURSES_EXPORT(char *)
_nc_infotocap(const char *cap GCC_UNUSED, const char *str, int const parameterized)
{
    int seenone = 0, seentwo = 0, saw_m = 0, saw_n = 0;
    const char *padding;
    const char *trimmed = 0;
    int in0, in1, in2;
    char ch1 = 0, ch2 = 0;
    char *bufptr = init_string();
    char octal[4];
    int len;
    int digits;
    bool syntax_error = FALSE;
    int myfix = 0;
    struct {
	int ch;
	int offset;
    } fixups[MAX_TC_FIXUPS];

    DEBUG_THIS(("_nc_infotocap %s params %d, %s",
		_nc_strict_bsd ? "strict" : "loose",
		parameterized,
		_nc_visbuf(str)));

    /* we may have to move some trailing mandatory padding up front */
    padding = str + strlen(str) - 1;
    if (padding > str && *padding == '>') {
	if (padding > (str + 1) && *--padding == '/')
	    --padding;
	while (isdigit(UChar(*padding)) || *padding == '.' || *padding == '*')
	    padding--;
	if (padding > str && *padding == '<' && *--padding == '$')
	    trimmed = padding;
	padding += 2;

	while (isdigit(UChar(*padding)) || *padding == '.' || *padding == '*')
	    bufptr = save_char(bufptr, *padding++);
    }

    for (; !syntax_error &&
	 *str &&
	 ((trimmed == 0) || (str < trimmed)); str++) {
	int c1, c2;
	char *cp = 0;

	if (str[0] == '^') {
	    if (str[1] == '\0' || (str + 1) == trimmed) {
		bufptr = save_string(bufptr, "\\136");
		++str;
	    } else if (str[1] == '?') {
		/*
		 * Although the 4.3BSD termcap file has an instance of "kb=^?",
		 * that appears to be just cut/paste since neither 4.3BSD nor
		 * 4.4BSD termcap interprets "^?" as DEL.
		 */
		bufptr = save_string(bufptr, "\\177");
		++str;
	    } else {
		bufptr = save_char(bufptr, *str++);
		bufptr = save_char(bufptr, *str);
	    }
	} else if (str[0] == ':') {
	    bufptr = save_char(bufptr, '\\');
	    bufptr = save_char(bufptr, '0');
	    bufptr = save_char(bufptr, '7');
	    bufptr = save_char(bufptr, '2');
	} else if (str[0] == '\\') {
	    if (str[1] == '\0' || (str + 1) == trimmed) {
		bufptr = save_string(bufptr, "\\134");
		++str;
	    } else if (str[1] == '^') {
		bufptr = save_string(bufptr, "\\136");
		++str;
	    } else if (str[1] == ',') {
		bufptr = save_char(bufptr, *++str);
	    } else {
		int xx1;

		bufptr = save_char(bufptr, *str++);
		xx1 = *str;
		if (_nc_strict_bsd) {

		    if (isoctal(UChar(xx1))) {
			int pad = 0;
			int xx2;
			int fix = 0;

			if (!isoctal(UChar(str[1])))
			    pad = 2;
			else if (str[1] && !isoctal(UChar(str[2])))
			    pad = 1;

			/*
			 * Test for "\0", "\00" or "\000" and transform those
			 * into "\200".
			 */
			if (xx1 == '0'
			    && ((pad == 2) || (str[1] == '0'))
			    && ((pad >= 1) || (str[2] == '0'))) {
			    xx2 = '2';
			} else {
			    xx2 = '0';
			    pad = 0;	/* FIXME - optionally pad to 3 digits */
			}
			if (myfix < MAX_TC_FIXUPS) {
			    fix = 3 - pad;
			    fixups[myfix].ch = 0;
			    fixups[myfix].offset = (int) (bufptr
							  - my_string
							  - 1);
			}
			while (pad-- > 0) {
			    bufptr = save_char(bufptr, xx2);
			    if (myfix < MAX_TC_FIXUPS) {
				fixups[myfix].ch <<= 3;
				fixups[myfix].ch |= (xx2 - '0');
			    }
			    xx2 = '0';
			}
			if (myfix < MAX_TC_FIXUPS) {
			    int n;
			    for (n = 0; n < fix; ++n) {
				fixups[myfix].ch <<= 3;
				fixups[myfix].ch |= (str[n] - '0');
			    }
			    if (fixups[myfix].ch < 32) {
				++myfix;
			    }
			}
		    } else if (strchr("E\\nrtbf", xx1) == 0) {
			switch (xx1) {
			case 'e':
			    xx1 = 'E';
			    break;
			case 'l':
			    xx1 = 'n';
			    break;
			case 's':
			    bufptr = save_char(bufptr, '0');
			    bufptr = save_char(bufptr, '4');
			    xx1 = '0';
			    break;
			case ':':
			    /*
			     * Note: termcap documentation claims that ":"
			     * must be escaped as "\072", however the
			     * documentation is incorrect - read the code.
			     * The replacement does not work reliably,
			     * so the advice is not helpful.
			     */
			    bufptr = save_char(bufptr, '0');
			    bufptr = save_char(bufptr, '7');
			    xx1 = '2';
			    break;
			default:
			    /* should not happen, but handle this anyway */
			    _nc_SPRINTF(octal, _nc_SLIMIT(sizeof(octal))
					"%03o", UChar(xx1));
			    bufptr = save_char(bufptr, octal[0]);
			    bufptr = save_char(bufptr, octal[1]);
			    xx1 = octal[2];
			    break;
			}
		    }
		} else {
		    if (myfix < MAX_TC_FIXUPS && isoctal(UChar(xx1))) {
			bool will_fix = TRUE;
			int n;

			fixups[myfix].ch = 0;
			fixups[myfix].offset = (int) (bufptr - my_string - 1);
			for (n = 0; n < 3; ++n) {
			    if (isoctal(str[n])) {
				octal_fixup(myfix, str[n]);
			    } else {
				will_fix = FALSE;
				break;
			    }
			}
			if (will_fix && (fixups[myfix].ch < 32))
			    ++myfix;
		    }
		}
		bufptr = save_char(bufptr, xx1);
	    }
	} else if (str[0] == '$' && str[1] == '<') {	/* discard padding */
	    str += 2;
	    while (isdigit(UChar(*str))
		   || *str == '.'
		   || *str == '*'
		   || *str == '/'
		   || *str == '>')
		str++;
	    --str;
	} else if (sscanf(str,
			  "[%%?%%p1%%{8}%%<%%t%d%%p1%%d%%e%%p1%%{16}%%<%%t%d%%p1%%{8}%%-%%d%%e%d;5;%%p1%%d%%;m",
			  &in0, &in1, &in2) == 3
		   && ((in0 == 4 && in1 == 10 && in2 == 48)
		       || (in0 == 3 && in1 == 9 && in2 == 38))) {
	    /* dumb-down an optimized case from xterm-256color for termcap */
	    if ((str = strstr(str, ";m")) == 0)
		break;		/* cannot happen */
	    ++str;
	    if (in2 == 48) {
		bufptr = save_string(bufptr, "[48;5;%dm");
	    } else {
		bufptr = save_string(bufptr, "[38;5;%dm");
	    }
	} else if (str[0] == '%' && str[1] == '%') {	/* escaped '%' */
	    bufptr = save_string(bufptr, "%%");
	    ++str;
	} else if (*str != '%' || (parameterized < 1)) {
	    bufptr = save_char(bufptr, *str);
	} else if (sscanf(str, "%%?%%{%d}%%>%%t%%{%d}%%+%%;", &c1, &c2) == 2) {
	    str = strchr(str, ';');
	    bufptr = save_tc_inequality(bufptr, c1, c2);
	} else if (sscanf(str, "%%?%%{%d}%%>%%t%%'%c'%%+%%;", &c1, &ch2) == 2) {
	    str = strchr(str, ';');
	    bufptr = save_tc_inequality(bufptr, c1, ch2);
	} else if (sscanf(str, "%%?%%'%c'%%>%%t%%{%d}%%+%%;", &ch1, &c2) == 2) {
	    str = strchr(str, ';');
	    bufptr = save_tc_inequality(bufptr, ch1, c2);
	} else if (sscanf(str, "%%?%%'%c'%%>%%t%%'%c'%%+%%;", &ch1, &ch2) == 2) {
	    str = strchr(str, ';');
	    bufptr = save_tc_inequality(bufptr, ch1, ch2);
	} else if ((len = bcd_expression(str)) != 0) {
	    str += len;
	    bufptr = save_string(bufptr, "%B");
	} else if ((sscanf(str, "%%{%d}%%+%%%c", &c1, &ch2) == 2
		    || sscanf(str, "%%'%c'%%+%%%c", &ch1, &ch2) == 2)
		   && ch2 == 'c'
		   && (cp = strchr(str, '+'))) {
	    str = cp + 2;
	    bufptr = save_string(bufptr, "%+");

	    if (ch1)
		c1 = ch1;
	    bufptr = save_tc_char(bufptr, c1);
	}
	/* FIXME: this "works" for 'delta' */
	else if (strncmp(str, "%{2}%*%-", (size_t) 8) == 0) {
	    str += 7;
	    bufptr = save_string(bufptr, "%D");
	} else if (strncmp(str, "%{96}%^", (size_t) 7) == 0) {
	    str += 6;
	    if (saw_m++ == 0) {
		bufptr = save_string(bufptr, "%n");
	    }
	} else if (strncmp(str, "%{127}%^", (size_t) 8) == 0) {
	    str += 7;
	    if (saw_n++ == 0) {
		bufptr = save_string(bufptr, "%m");
	    }
	} else {		/* cm-style format element */
	    str++;
	    switch (*str) {
	    case '%':
		bufptr = save_char(bufptr, '%');
		break;

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
		bufptr = save_char(bufptr, '%');
		ch1 = 0;
		ch2 = 0;
		digits = 0;
		while (isdigit(UChar(*str))) {
		    if (++digits > 2) {
			syntax_error = TRUE;
			break;
		    }
		    ch2 = ch1;
		    ch1 = *str++;
		    if (digits == 2 && ch2 != '0') {
			syntax_error = TRUE;
			break;
		    } else if (_nc_strict_bsd) {
			if (ch1 > '3') {
			    syntax_error = TRUE;
			    break;
			}
		    } else {
			bufptr = save_char(bufptr, ch1);
		    }
		}
		if (syntax_error)
		    break;
		/*
		 * Convert %02 to %2 and %03 to %3
		 */
		if (ch2 == '0' && !_nc_strict_bsd) {
		    ch2 = 0;
		    bufptr[-2] = bufptr[-1];
		    *--bufptr = 0;
		}
		if (_nc_strict_bsd) {
		    if (ch2 != 0 && ch2 != '0') {
			syntax_error = TRUE;
		    } else if (ch1 < '2') {
			ch1 = 'd';
		    }
		    bufptr = save_char(bufptr, ch1);
		}
		if (strchr("oxX.", *str)) {
		    syntax_error = TRUE;	/* termcap doesn't have octal, hex */
		}
		break;

	    case 'd':
		bufptr = save_string(bufptr, "%d");
		break;

	    case 'c':
		bufptr = save_string(bufptr, "%.");
		break;

		/*
		 * %s isn't in termcap, but it is convenient to pass it through
		 * so we can represent things like terminfo pfkey strings in
		 * termcap notation.
		 */
	    case 's':
		if (_nc_strict_bsd) {
		    syntax_error = TRUE;
		} else {
		    bufptr = save_string(bufptr, "%s");
		}
		break;

	    case 'p':
		str++;
		if (*str == '1')
		    seenone = 1;
		else if (*str == '2') {
		    if (!seenone && !seentwo) {
			bufptr = save_string(bufptr, "%r");
			seentwo++;
		    }
		} else if (*str >= '3') {
		    syntax_error = TRUE;
		}
		break;

	    case 'i':
		bufptr = save_string(bufptr, "%i");
		break;

	    default:
		bufptr = save_char(bufptr, *str);
		syntax_error = TRUE;
		break;
	    }			/* endswitch (*str) */
	}			/* endelse (*str == '%') */

	/*
	 * 'str' always points to the end of what was scanned in this step,
	 * but that may not be the end of the string.
	 */
	assert(str != 0);
	if (str == 0 || *str == '\0')
	    break;

    }				/* endwhile (*str) */

    if (!syntax_error &&
	myfix > 0 &&
	((int) strlen(my_string) - (4 * myfix)) < MIN_TC_FIXUPS) {
	while (--myfix >= 0) {
	    char *p = fixups[myfix].offset + my_string;
	    *p++ = '^';
	    *p++ = (char) (fixups[myfix].ch | '@');
	    while ((p[0] = p[2]) != '\0') {
		++p;
	    }
	}
    }

    DEBUG_THIS(("... _nc_infotocap %s",
		syntax_error
		? "<ERR>"
		: _nc_visbuf(my_string)));

    return (syntax_error ? NULL : my_string);
}

#ifdef MAIN

int curr_line;

int
main(int argc, char *argv[])
{
    int c, tc = FALSE;

    while ((c = getopt(argc, argv, "c")) != EOF)
	switch (c) {
	case 'c':
	    tc = TRUE;
	    break;
	}

    curr_line = 0;
    for (;;) {
	char buf[BUFSIZ];

	++curr_line;
	if (fgets(buf, sizeof(buf), stdin) == 0)
	    break;
	buf[strlen(buf) - 1] = '\0';
	_nc_set_source(buf);

	if (tc) {
	    char *cp = _nc_infotocap("to termcap", buf, 1);

	    if (cp)
		(void) fputs(cp, stdout);
	} else
	    (void) fputs(_nc_captoinfo("to terminfo", buf, 1), stdout);
	(void) putchar('\n');
    }
    return (0);
}
#endif /* MAIN */

#if NO_LEAKS
NCURSES_EXPORT(void)
_nc_captoinfo_leaks(void)
{
    if (my_string != 0) {
	FreeAndNull(my_string);
    }
    my_length = 0;
}
#endif
