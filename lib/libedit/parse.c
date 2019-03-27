/*	$NetBSD: parse.c,v 1.35 2016/02/17 19:47:49 christos Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
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

#include "config.h"
#if !defined(lint) && !defined(SCCSID)
#if 0
static char sccsid[] = "@(#)parse.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: parse.c,v 1.35 2016/02/17 19:47:49 christos Exp $");
#endif
#endif /* not lint && not SCCSID */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * parse.c: parse an editline extended command
 *
 * commands are:
 *
 *	bind
 *	echotc
 *	edit
 *	gettc
 *	history
 *	settc
 *	setty
 */
#include <stdlib.h>
#include <string.h>

#include "el.h"
#include "parse.h"

private const struct {
	const Char *name;
	int (*func)(EditLine *, int, const Char **);
} cmds[] = {
	{ STR("bind"),		map_bind	},
	{ STR("echotc"),	terminal_echotc	},
	{ STR("edit"),		el_editmode	},
	{ STR("history"),	hist_command	},
	{ STR("telltc"),	terminal_telltc	},
	{ STR("settc"),	        terminal_settc	},
	{ STR("setty"),	        tty_stty	},
	{ NULL,		        NULL		}
};


/* parse_line():
 *	Parse a line and dispatch it
 */
protected int
parse_line(EditLine *el, const Char *line)
{
	const Char **argv;
	int argc;
	TYPE(Tokenizer) *tok;

	tok = FUN(tok,init)(NULL);
	FUN(tok,str)(tok, line, &argc, &argv);
	argc = FUN(el,parse)(el, argc, argv);
	FUN(tok,end)(tok);
	return argc;
}


/* el_parse():
 *	Command dispatcher
 */
public int
FUN(el,parse)(EditLine *el, int argc, const Char *argv[])
{
	const Char *ptr;
	int i;

	if (argc < 1)
		return -1;
	ptr = Strchr(argv[0], ':');
	if (ptr != NULL) {
		Char *tprog;
		size_t l;

		if (ptr == argv[0])
			return 0;
		l = (size_t)(ptr - argv[0] - 1);
		tprog = el_malloc((l + 1) * sizeof(*tprog));
		if (tprog == NULL)
			return 0;
		(void) Strncpy(tprog, argv[0], l);
		tprog[l] = '\0';
		ptr++;
		l = (size_t)el_match(el->el_prog, tprog);
		el_free(tprog);
		if (!l)
			return 0;
	} else
		ptr = argv[0];

	for (i = 0; cmds[i].name != NULL; i++)
		if (Strcmp(cmds[i].name, ptr) == 0) {
			i = (*cmds[i].func) (el, argc, argv);
			return -i;
		}
	return -1;
}


/* parse__escape():
 *	Parse a string of the form ^<char> \<odigit> \<char> \U+xxxx and return
 *	the appropriate character or -1 if the escape is not valid
 */
protected int
parse__escape(const Char **ptr)
{
	const Char *p;
	wint_t c;

	p = *ptr;

	if (p[1] == 0)
		return -1;

	if (*p == '\\') {
		p++;
		switch (*p) {
		case 'a':
			c = '\007';	/* Bell */
			break;
		case 'b':
			c = '\010';	/* Backspace */
			break;
		case 't':
			c = '\011';	/* Horizontal Tab */
			break;
		case 'n':
			c = '\012';	/* New Line */
			break;
		case 'v':
			c = '\013';	/* Vertical Tab */
			break;
		case 'f':
			c = '\014';	/* Form Feed */
			break;
		case 'r':
			c = '\015';	/* Carriage Return */
			break;
		case 'e':
			c = '\033';	/* Escape */
			break;
                case 'U':               /* Unicode \U+xxxx or \U+xxxxx format */
                {
                        int i;
                        const Char hex[] = STR("0123456789ABCDEF");
                        const Char *h;
                        ++p;
                        if (*p++ != '+')
                                return -1;
			c = 0;
                        for (i = 0; i < 5; ++i) {
                                h = Strchr(hex, *p++);
                                if (!h && i < 4)
                                        return -1;
                                else if (h)
                                        c = (c << 4) | ((int)(h - hex));
                                else
                                        --p;
                        }
                        if (c > 0x10FFFF) /* outside valid character range */
                                return -1;
                        break;
                }
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		{
			int cnt, ch;

			for (cnt = 0, c = 0; cnt < 3; cnt++) {
				ch = *p++;
				if (ch < '0' || ch > '7') {
					p--;
					break;
				}
				c = (c << 3) | (ch - '0');
			}
			if ((c & (wint_t)0xffffff00) != (wint_t)0)
				return -1;
			--p;
			break;
		}
		default:
			c = *p;
			break;
		}
	} else if (*p == '^') {
		p++;
		c = (*p == '?') ? '\177' : (*p & 0237);
	} else
		c = *p;
	*ptr = ++p;
	return c;
}

/* parse__string():
 *	Parse the escapes from in and put the raw string out
 */
protected Char *
parse__string(Char *out, const Char *in)
{
	Char *rv = out;
	int n;

	for (;;)
		switch (*in) {
		case '\0':
			*out = '\0';
			return rv;

		case '\\':
		case '^':
			if ((n = parse__escape(&in)) == -1)
				return NULL;
			*out++ = (Char)n;
			break;

		case 'M':
			if (in[1] == '-' && in[2] != '\0') {
				*out++ = '\033';
				in += 2;
				break;
			}
			/*FALLTHROUGH*/

		default:
			*out++ = *in++;
			break;
		}
}


/* parse_cmd():
 *	Return the command number for the command string given
 *	or -1 if one is not found
 */
protected int
parse_cmd(EditLine *el, const Char *cmd)
{
	el_bindings_t *b = el->el_map.help;
	size_t i;

	for (i = 0; i < el->el_map.nfunc; i++)
		if (Strcmp(b[i].name, cmd) == 0)
			return b[i].func;
	return -1;
}
