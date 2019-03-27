/*	$NetBSD: terminal.c,v 1.24 2016/03/22 01:38:17 christos Exp $	*/

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
static char sccsid[] = "@(#)term.c	8.2 (Berkeley) 4/30/95";
#else
__RCSID("$NetBSD: terminal.c,v 1.24 2016/03/22 01:38:17 christos Exp $");
#endif
#endif /* not lint && not SCCSID */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * terminal.c: Editor/termcap-curses interface
 *	       We have to declare a static variable here, since the
 *	       termcap putchar routine does not take an argument!
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_TERMCAP_H
#include <termcap.h>
#endif
#ifdef HAVE_CURSES_H
#include <curses.h>
#elif HAVE_NCURSES_H
#include <ncurses.h>
#endif

/* Solaris's term.h does horrid things. */
#if defined(HAVE_TERM_H) && !defined(__sun) && !defined(HAVE_TERMCAP_H)
#include <term.h>
#endif

#ifdef _REENTRANT
#include <pthread.h>
#endif

#include "el.h"

/*
 * IMPORTANT NOTE: these routines are allowed to look at the current screen
 * and the current position assuming that it is correct.  If this is not
 * true, then the update will be WRONG!  This is (should be) a valid
 * assumption...
 */

#define	TC_BUFSIZE	((size_t)2048)

#define	GoodStr(a)	(el->el_terminal.t_str[a] != NULL && \
			    el->el_terminal.t_str[a][0] != '\0')
#define	Str(a)		el->el_terminal.t_str[a]
#define	Val(a)		el->el_terminal.t_val[a]

private const struct termcapstr {
	const char *name;
	const char *long_name;
} tstr[] = {
#define	T_al	0
	{ "al", "add new blank line" },
#define	T_bl	1
	{ "bl", "audible bell" },
#define	T_cd	2
	{ "cd", "clear to bottom" },
#define	T_ce	3
	{ "ce", "clear to end of line" },
#define	T_ch	4
	{ "ch", "cursor to horiz pos" },
#define	T_cl	5
	{ "cl", "clear screen" },
#define	T_dc	6
	{ "dc", "delete a character" },
#define	T_dl	7
	{ "dl", "delete a line" },
#define	T_dm	8
	{ "dm", "start delete mode" },
#define	T_ed	9
	{ "ed", "end delete mode" },
#define	T_ei	10
	{ "ei", "end insert mode" },
#define	T_fs	11
	{ "fs", "cursor from status line" },
#define	T_ho	12
	{ "ho", "home cursor" },
#define	T_ic	13
	{ "ic", "insert character" },
#define	T_im	14
	{ "im", "start insert mode" },
#define	T_ip	15
	{ "ip", "insert padding" },
#define	T_kd	16
	{ "kd", "sends cursor down" },
#define	T_kl	17
	{ "kl", "sends cursor left" },
#define	T_kr	18
	{ "kr", "sends cursor right" },
#define	T_ku	19
	{ "ku", "sends cursor up" },
#define	T_md	20
	{ "md", "begin bold" },
#define	T_me	21
	{ "me", "end attributes" },
#define	T_nd	22
	{ "nd", "non destructive space" },
#define	T_se	23
	{ "se", "end standout" },
#define	T_so	24
	{ "so", "begin standout" },
#define	T_ts	25
	{ "ts", "cursor to status line" },
#define	T_up	26
	{ "up", "cursor up one" },
#define	T_us	27
	{ "us", "begin underline" },
#define	T_ue	28
	{ "ue", "end underline" },
#define	T_vb	29
	{ "vb", "visible bell" },
#define	T_DC	30
	{ "DC", "delete multiple chars" },
#define	T_DO	31
	{ "DO", "cursor down multiple" },
#define	T_IC	32
	{ "IC", "insert multiple chars" },
#define	T_LE	33
	{ "LE", "cursor left multiple" },
#define	T_RI	34
	{ "RI", "cursor right multiple" },
#define	T_UP	35
	{ "UP", "cursor up multiple" },
#define	T_kh	36
	{ "kh", "send cursor home" },
#define	T_at7	37
	{ "@7", "send cursor end" },
#define	T_kD	38
	{ "kD", "send cursor delete" },
#define	T_str	39
	{ NULL, NULL }
};

private const struct termcapval {
	const char *name;
	const char *long_name;
} tval[] = {
#define	T_am	0
	{ "am", "has automatic margins" },
#define	T_pt	1
	{ "pt", "has physical tabs" },
#define	T_li	2
	{ "li", "Number of lines" },
#define	T_co	3
	{ "co", "Number of columns" },
#define	T_km	4
	{ "km", "Has meta key" },
#define	T_xt	5
	{ "xt", "Tab chars destructive" },
#define	T_xn	6
	{ "xn", "newline ignored at right margin" },
#define	T_MT	7
	{ "MT", "Has meta key" },			/* XXX? */
#define	T_val	8
	{ NULL, NULL, }
};
/* do two or more of the attributes use me */

private void	terminal_setflags(EditLine *);
private int	terminal_rebuffer_display(EditLine *);
private void	terminal_free_display(EditLine *);
private int	terminal_alloc_display(EditLine *);
private void	terminal_alloc(EditLine *, const struct termcapstr *,
    const char *);
private void	terminal_init_arrow(EditLine *);
private void	terminal_reset_arrow(EditLine *);
private int	terminal_putc(int);
private void	terminal_tputs(EditLine *, const char *, int);

#ifdef _REENTRANT
private pthread_mutex_t terminal_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
private FILE *terminal_outfile = NULL;


/* terminal_setflags():
 *	Set the terminal capability flags
 */
private void
terminal_setflags(EditLine *el)
{
	EL_FLAGS = 0;
	if (el->el_tty.t_tabs)
		EL_FLAGS |= (Val(T_pt) && !Val(T_xt)) ? TERM_CAN_TAB : 0;

	EL_FLAGS |= (Val(T_km) || Val(T_MT)) ? TERM_HAS_META : 0;
	EL_FLAGS |= GoodStr(T_ce) ? TERM_CAN_CEOL : 0;
	EL_FLAGS |= (GoodStr(T_dc) || GoodStr(T_DC)) ? TERM_CAN_DELETE : 0;
	EL_FLAGS |= (GoodStr(T_im) || GoodStr(T_ic) || GoodStr(T_IC)) ?
	    TERM_CAN_INSERT : 0;
	EL_FLAGS |= (GoodStr(T_up) || GoodStr(T_UP)) ? TERM_CAN_UP : 0;
	EL_FLAGS |= Val(T_am) ? TERM_HAS_AUTO_MARGINS : 0;
	EL_FLAGS |= Val(T_xn) ? TERM_HAS_MAGIC_MARGINS : 0;

	if (GoodStr(T_me) && GoodStr(T_ue))
		EL_FLAGS |= (strcmp(Str(T_me), Str(T_ue)) == 0) ?
		    TERM_CAN_ME : 0;
	else
		EL_FLAGS &= ~TERM_CAN_ME;
	if (GoodStr(T_me) && GoodStr(T_se))
		EL_FLAGS |= (strcmp(Str(T_me), Str(T_se)) == 0) ?
		    TERM_CAN_ME : 0;


#ifdef DEBUG_SCREEN
	if (!EL_CAN_UP) {
		(void) fprintf(el->el_errfile,
		    "WARNING: Your terminal cannot move up.\n");
		(void) fprintf(el->el_errfile,
		    "Editing may be odd for long lines.\n");
	}
	if (!EL_CAN_CEOL)
		(void) fprintf(el->el_errfile, "no clear EOL capability.\n");
	if (!EL_CAN_DELETE)
		(void) fprintf(el->el_errfile, "no delete char capability.\n");
	if (!EL_CAN_INSERT)
		(void) fprintf(el->el_errfile, "no insert char capability.\n");
#endif /* DEBUG_SCREEN */
}

/* terminal_init():
 *	Initialize the terminal stuff
 */
protected int
terminal_init(EditLine *el)
{

	el->el_terminal.t_buf = el_malloc(TC_BUFSIZE *
	    sizeof(*el->el_terminal.t_buf));
	if (el->el_terminal.t_buf == NULL)
		goto fail1;
	el->el_terminal.t_cap = el_malloc(TC_BUFSIZE *
	    sizeof(*el->el_terminal.t_cap));
	if (el->el_terminal.t_cap == NULL)
		goto fail2;
	el->el_terminal.t_fkey = el_malloc(A_K_NKEYS *
	    sizeof(*el->el_terminal.t_fkey));
	if (el->el_terminal.t_fkey == NULL)
		goto fail3;
	el->el_terminal.t_loc = 0;
	el->el_terminal.t_str = el_malloc(T_str *
	    sizeof(*el->el_terminal.t_str));
	if (el->el_terminal.t_str == NULL)
		goto fail4;
	(void) memset(el->el_terminal.t_str, 0, T_str *
	    sizeof(*el->el_terminal.t_str));
	el->el_terminal.t_val = el_malloc(T_val *
	    sizeof(*el->el_terminal.t_val));
	if (el->el_terminal.t_val == NULL)
		goto fail5;
	(void) memset(el->el_terminal.t_val, 0, T_val *
	    sizeof(*el->el_terminal.t_val));
	(void) terminal_set(el, NULL);
	terminal_init_arrow(el);
	return 0;
fail5:
	free(el->el_terminal.t_str);
	el->el_terminal.t_str = NULL;
fail4:
	free(el->el_terminal.t_fkey);
	el->el_terminal.t_fkey = NULL;
fail3:
	free(el->el_terminal.t_cap);
	el->el_terminal.t_cap = NULL;
fail2:
	free(el->el_terminal.t_buf);
	el->el_terminal.t_buf = NULL;
fail1:
	return -1;
}

/* terminal_end():
 *	Clean up the terminal stuff
 */
protected void
terminal_end(EditLine *el)
{

	el_free(el->el_terminal.t_buf);
	el->el_terminal.t_buf = NULL;
	el_free(el->el_terminal.t_cap);
	el->el_terminal.t_cap = NULL;
	el->el_terminal.t_loc = 0;
	el_free(el->el_terminal.t_str);
	el->el_terminal.t_str = NULL;
	el_free(el->el_terminal.t_val);
	el->el_terminal.t_val = NULL;
	el_free(el->el_terminal.t_fkey);
	el->el_terminal.t_fkey = NULL;
	terminal_free_display(el);
}


/* terminal_alloc():
 *	Maintain a string pool for termcap strings
 */
private void
terminal_alloc(EditLine *el, const struct termcapstr *t, const char *cap)
{
	char termbuf[TC_BUFSIZE];
	size_t tlen, clen;
	char **tlist = el->el_terminal.t_str;
	char **tmp, **str = &tlist[t - tstr];

	(void) memset(termbuf, 0, sizeof(termbuf));
	if (cap == NULL || *cap == '\0') {
		*str = NULL;
		return;
	} else
		clen = strlen(cap);

	tlen = *str == NULL ? 0 : strlen(*str);

	/*
         * New string is shorter; no need to allocate space
         */
	if (clen <= tlen) {
		if (*str)
			(void) strcpy(*str, cap);	/* XXX strcpy is safe */
		return;
	}
	/*
         * New string is longer; see if we have enough space to append
         */
	if (el->el_terminal.t_loc + 3 < TC_BUFSIZE) {
						/* XXX strcpy is safe */
		(void) strcpy(*str = &el->el_terminal.t_buf[
		    el->el_terminal.t_loc], cap);
		el->el_terminal.t_loc += clen + 1;	/* one for \0 */
		return;
	}
	/*
         * Compact our buffer; no need to check compaction, cause we know it
         * fits...
         */
	tlen = 0;
	for (tmp = tlist; tmp < &tlist[T_str]; tmp++)
		if (*tmp != NULL && **tmp != '\0' && *tmp != *str) {
			char *ptr;

			for (ptr = *tmp; *ptr != '\0'; termbuf[tlen++] = *ptr++)
				continue;
			termbuf[tlen++] = '\0';
		}
	memcpy(el->el_terminal.t_buf, termbuf, TC_BUFSIZE);
	el->el_terminal.t_loc = tlen;
	if (el->el_terminal.t_loc + 3 >= TC_BUFSIZE) {
		(void) fprintf(el->el_errfile,
		    "Out of termcap string space.\n");
		return;
	}
					/* XXX strcpy is safe */
	(void) strcpy(*str = &el->el_terminal.t_buf[el->el_terminal.t_loc],
	    cap);
	el->el_terminal.t_loc += (size_t)clen + 1;	/* one for \0 */
	return;
}


/* terminal_rebuffer_display():
 *	Rebuffer the display after the screen changed size
 */
private int
terminal_rebuffer_display(EditLine *el)
{
	coord_t *c = &el->el_terminal.t_size;

	terminal_free_display(el);

	c->h = Val(T_co);
	c->v = Val(T_li);

	if (terminal_alloc_display(el) == -1)
		return -1;
	return 0;
}


/* terminal_alloc_display():
 *	Allocate a new display.
 */
private int
terminal_alloc_display(EditLine *el)
{
	int i;
	Char **b;
	coord_t *c = &el->el_terminal.t_size;

	b =  el_malloc(sizeof(*b) * (size_t)(c->v + 1));
	if (b == NULL)
		goto done;
	for (i = 0; i < c->v; i++) {
		b[i] = el_malloc(sizeof(**b) * (size_t)(c->h + 1));
		if (b[i] == NULL) {
			while (--i >= 0)
				el_free(b[i]);
			el_free(b);
			goto done;
		}
	}
	b[c->v] = NULL;
	el->el_display = b;

	b = el_malloc(sizeof(*b) * (size_t)(c->v + 1));
	if (b == NULL)
		goto done;
	for (i = 0; i < c->v; i++) {
		b[i] = el_malloc(sizeof(**b) * (size_t)(c->h + 1));
		if (b[i] == NULL) {
			while (--i >= 0)
				el_free(b[i]);
			el_free(b);
			goto done;
		}
	}
	b[c->v] = NULL;
	el->el_vdisplay = b;
	return 0;
done:
	terminal_free_display(el);
	return -1;
}


/* terminal_free_display():
 *	Free the display buffers
 */
private void
terminal_free_display(EditLine *el)
{
	Char **b;
	Char **bufp;

	b = el->el_display;
	el->el_display = NULL;
	if (b != NULL) {
		for (bufp = b; *bufp != NULL; bufp++)
			el_free(*bufp);
		el_free(b);
	}
	b = el->el_vdisplay;
	el->el_vdisplay = NULL;
	if (b != NULL) {
		for (bufp = b; *bufp != NULL; bufp++)
			el_free(*bufp);
		el_free(b);
	}
}


/* terminal_move_to_line():
 *	move to line <where> (first line == 0)
 *	as efficiently as possible
 */
protected void
terminal_move_to_line(EditLine *el, int where)
{
	int del;

	if (where == el->el_cursor.v)
		return;

	if (where > el->el_terminal.t_size.v) {
#ifdef DEBUG_SCREEN
		(void) fprintf(el->el_errfile,
		    "%s: where is ridiculous: %d\r\n", __func__, where);
#endif /* DEBUG_SCREEN */
		return;
	}
	if ((del = where - el->el_cursor.v) > 0) {
		while (del > 0) {
			if (EL_HAS_AUTO_MARGINS &&
			    el->el_display[el->el_cursor.v][0] != '\0') {
                                size_t h = (size_t)
				    (el->el_terminal.t_size.h - 1);
#ifdef WIDECHAR
                                for (; h > 0 &&
                                         el->el_display[el->el_cursor.v][h] ==
                                                 MB_FILL_CHAR;
                                         h--)
                                                continue;
#endif
				/* move without newline */
				terminal_move_to_char(el, (int)h);
				terminal_overwrite(el, &el->el_display
				    [el->el_cursor.v][el->el_cursor.h],
				    (size_t)(el->el_terminal.t_size.h -
				    el->el_cursor.h));
				/* updates Cursor */
				del--;
			} else {
				if ((del > 1) && GoodStr(T_DO)) {
					terminal_tputs(el, tgoto(Str(T_DO), del,
					    del), del);
					del = 0;
				} else {
					for (; del > 0; del--)
						terminal__putc(el, '\n');
					/* because the \n will become \r\n */
					el->el_cursor.h = 0;
				}
			}
		}
	} else {		/* del < 0 */
		if (GoodStr(T_UP) && (-del > 1 || !GoodStr(T_up)))
			terminal_tputs(el, tgoto(Str(T_UP), -del, -del), -del);
		else {
			if (GoodStr(T_up))
				for (; del < 0; del++)
					terminal_tputs(el, Str(T_up), 1);
		}
	}
	el->el_cursor.v = where;/* now where is here */
}


/* terminal_move_to_char():
 *	Move to the character position specified
 */
protected void
terminal_move_to_char(EditLine *el, int where)
{
	int del, i;

mc_again:
	if (where == el->el_cursor.h)
		return;

	if (where > el->el_terminal.t_size.h) {
#ifdef DEBUG_SCREEN
		(void) fprintf(el->el_errfile,
		    "%s: where is ridiculous: %d\r\n", __func__, where);
#endif /* DEBUG_SCREEN */
		return;
	}
	if (!where) {		/* if where is first column */
		terminal__putc(el, '\r');	/* do a CR */
		el->el_cursor.h = 0;
		return;
	}
	del = where - el->el_cursor.h;

	if ((del < -4 || del > 4) && GoodStr(T_ch))
		/* go there directly */
		terminal_tputs(el, tgoto(Str(T_ch), where, where), where);
	else {
		if (del > 0) {	/* moving forward */
			if ((del > 4) && GoodStr(T_RI))
				terminal_tputs(el, tgoto(Str(T_RI), del, del),
				    del);
			else {
					/* if I can do tabs, use them */
				if (EL_CAN_TAB) {
					if ((el->el_cursor.h & 0370) !=
					    (where & ~0x7)
#ifdef WIDECHAR
					    && (el->el_display[
					    el->el_cursor.v][where & 0370] !=
					    MB_FILL_CHAR)
#endif
					    ) {
						/* if not within tab stop */
						for (i =
						    (el->el_cursor.h & 0370);
						    i < (where & ~0x7);
						    i += 8)
							terminal__putc(el,
							    '\t');
							/* then tab over */
						el->el_cursor.h = where & ~0x7;
					}
				}
				/*
				 * it's usually cheaper to just write the
				 * chars, so we do.
				 */
				/*
				 * NOTE THAT terminal_overwrite() WILL CHANGE
				 * el->el_cursor.h!!!
				 */
				terminal_overwrite(el, &el->el_display[
				    el->el_cursor.v][el->el_cursor.h],
				    (size_t)(where - el->el_cursor.h));

			}
		} else {	/* del < 0 := moving backward */
			if ((-del > 4) && GoodStr(T_LE))
				terminal_tputs(el, tgoto(Str(T_LE), -del, -del),
				    -del);
			else {	/* can't go directly there */
				/*
				 * if the "cost" is greater than the "cost"
				 * from col 0
				 */
				if (EL_CAN_TAB ?
				    ((unsigned int)-del >
				    (((unsigned int) where >> 3) +
				     (where & 07)))
				    : (-del > where)) {
					terminal__putc(el, '\r');/* do a CR */
					el->el_cursor.h = 0;
					goto mc_again;	/* and try again */
				}
				for (i = 0; i < -del; i++)
					terminal__putc(el, '\b');
			}
		}
	}
	el->el_cursor.h = where;		/* now where is here */
}


/* terminal_overwrite():
 *	Overstrike num characters
 *	Assumes MB_FILL_CHARs are present to keep the column count correct
 */
protected void
terminal_overwrite(EditLine *el, const Char *cp, size_t n)
{
	if (n == 0)
		return;

	if (n > (size_t)el->el_terminal.t_size.h) {
#ifdef DEBUG_SCREEN
		(void) fprintf(el->el_errfile,
		    "%s: n is ridiculous: %zu\r\n", __func__, n);
#endif /* DEBUG_SCREEN */
		return;
	}

        do {
                /* terminal__putc() ignores any MB_FILL_CHARs */
                terminal__putc(el, *cp++);
                el->el_cursor.h++;
        } while (--n);

	if (el->el_cursor.h >= el->el_terminal.t_size.h) {	/* wrap? */
		if (EL_HAS_AUTO_MARGINS) {	/* yes */
			el->el_cursor.h = 0;
			el->el_cursor.v++;
			if (EL_HAS_MAGIC_MARGINS) {
				/* force the wrap to avoid the "magic"
				 * situation */
				Char c;
				if ((c = el->el_display[el->el_cursor.v]
				    [el->el_cursor.h]) != '\0') {
					terminal_overwrite(el, &c, (size_t)1);
#ifdef WIDECHAR
					while (el->el_display[el->el_cursor.v]
					    [el->el_cursor.h] == MB_FILL_CHAR)
						el->el_cursor.h++;
#endif
				} else {
					terminal__putc(el, ' ');
					el->el_cursor.h = 1;
				}
			}
		} else		/* no wrap, but cursor stays on screen */
			el->el_cursor.h = el->el_terminal.t_size.h - 1;
	}
}


/* terminal_deletechars():
 *	Delete num characters
 */
protected void
terminal_deletechars(EditLine *el, int num)
{
	if (num <= 0)
		return;

	if (!EL_CAN_DELETE) {
#ifdef DEBUG_EDIT
		(void) fprintf(el->el_errfile, "   ERROR: cannot delete   \n");
#endif /* DEBUG_EDIT */
		return;
	}
	if (num > el->el_terminal.t_size.h) {
#ifdef DEBUG_SCREEN
		(void) fprintf(el->el_errfile,
		    "%s: num is ridiculous: %d\r\n", __func__, num);
#endif /* DEBUG_SCREEN */
		return;
	}
	if (GoodStr(T_DC))	/* if I have multiple delete */
		if ((num > 1) || !GoodStr(T_dc)) {	/* if dc would be more
							 * expen. */
			terminal_tputs(el, tgoto(Str(T_DC), num, num), num);
			return;
		}
	if (GoodStr(T_dm))	/* if I have delete mode */
		terminal_tputs(el, Str(T_dm), 1);

	if (GoodStr(T_dc))	/* else do one at a time */
		while (num--)
			terminal_tputs(el, Str(T_dc), 1);

	if (GoodStr(T_ed))	/* if I have delete mode */
		terminal_tputs(el, Str(T_ed), 1);
}


/* terminal_insertwrite():
 *	Puts terminal in insert character mode or inserts num
 *	characters in the line
 *      Assumes MB_FILL_CHARs are present to keep column count correct
 */
protected void
terminal_insertwrite(EditLine *el, Char *cp, int num)
{
	if (num <= 0)
		return;
	if (!EL_CAN_INSERT) {
#ifdef DEBUG_EDIT
		(void) fprintf(el->el_errfile, "   ERROR: cannot insert   \n");
#endif /* DEBUG_EDIT */
		return;
	}
	if (num > el->el_terminal.t_size.h) {
#ifdef DEBUG_SCREEN
		(void) fprintf(el->el_errfile,
		    "%s: num is ridiculous: %d\r\n", __func__, num);
#endif /* DEBUG_SCREEN */
		return;
	}
	if (GoodStr(T_IC))	/* if I have multiple insert */
		if ((num > 1) || !GoodStr(T_ic)) {
				/* if ic would be more expensive */
			terminal_tputs(el, tgoto(Str(T_IC), num, num), num);
			terminal_overwrite(el, cp, (size_t)num);
				/* this updates el_cursor.h */
			return;
		}
	if (GoodStr(T_im) && GoodStr(T_ei)) {	/* if I have insert mode */
		terminal_tputs(el, Str(T_im), 1);

		el->el_cursor.h += num;
		do
			terminal__putc(el, *cp++);
		while (--num);

		if (GoodStr(T_ip))	/* have to make num chars insert */
			terminal_tputs(el, Str(T_ip), 1);

		terminal_tputs(el, Str(T_ei), 1);
		return;
	}
	do {
		if (GoodStr(T_ic))	/* have to make num chars insert */
			terminal_tputs(el, Str(T_ic), 1);

		terminal__putc(el, *cp++);

		el->el_cursor.h++;

		if (GoodStr(T_ip))	/* have to make num chars insert */
			terminal_tputs(el, Str(T_ip), 1);
					/* pad the inserted char */

	} while (--num);
}


/* terminal_clear_EOL():
 *	clear to end of line.  There are num characters to clear
 */
protected void
terminal_clear_EOL(EditLine *el, int num)
{
	int i;

	if (EL_CAN_CEOL && GoodStr(T_ce))
		terminal_tputs(el, Str(T_ce), 1);
	else {
		for (i = 0; i < num; i++)
			terminal__putc(el, ' ');
		el->el_cursor.h += num;	/* have written num spaces */
	}
}


/* terminal_clear_screen():
 *	Clear the screen
 */
protected void
terminal_clear_screen(EditLine *el)
{				/* clear the whole screen and home */

	if (GoodStr(T_cl))
		/* send the clear screen code */
		terminal_tputs(el, Str(T_cl), Val(T_li));
	else if (GoodStr(T_ho) && GoodStr(T_cd)) {
		terminal_tputs(el, Str(T_ho), Val(T_li));	/* home */
		/* clear to bottom of screen */
		terminal_tputs(el, Str(T_cd), Val(T_li));
	} else {
		terminal__putc(el, '\r');
		terminal__putc(el, '\n');
	}
}


/* terminal_beep():
 *	Beep the way the terminal wants us
 */
protected void
terminal_beep(EditLine *el)
{
	if (GoodStr(T_bl))
		/* what termcap says we should use */
		terminal_tputs(el, Str(T_bl), 1);
	else
		terminal__putc(el, '\007');	/* an ASCII bell; ^G */
}


protected void
terminal_get(EditLine *el, const char **term)
{
	*term = el->el_terminal.t_name;
}


/* terminal_set():
 *	Read in the terminal capabilities from the requested terminal
 */
protected int
terminal_set(EditLine *el, const char *term)
{
	int i;
	char buf[TC_BUFSIZE];
	char *area;
	const struct termcapstr *t;
	sigset_t oset, nset;
	int lins, cols;

	(void) sigemptyset(&nset);
	(void) sigaddset(&nset, SIGWINCH);
	(void) sigprocmask(SIG_BLOCK, &nset, &oset);

	area = buf;


	if (term == NULL)
		term = getenv("TERM");

	if (!term || !term[0])
		term = "dumb";

	if (strcmp(term, "emacs") == 0)
		el->el_flags |= EDIT_DISABLED;

	(void) memset(el->el_terminal.t_cap, 0, TC_BUFSIZE);

	i = tgetent(el->el_terminal.t_cap, term);

	if (i <= 0) {
		if (i == -1)
			(void) fprintf(el->el_errfile,
			    "Cannot read termcap database;\n");
		else if (i == 0)
			(void) fprintf(el->el_errfile,
			    "No entry for terminal type \"%s\";\n", term);
		(void) fprintf(el->el_errfile,
		    "using dumb terminal settings.\n");
		Val(T_co) = 80;	/* do a dumb terminal */
		Val(T_pt) = Val(T_km) = Val(T_li) = 0;
		Val(T_xt) = Val(T_MT);
		for (t = tstr; t->name != NULL; t++)
			terminal_alloc(el, t, NULL);
	} else {
		/* auto/magic margins */
		Val(T_am) = tgetflag("am");
		Val(T_xn) = tgetflag("xn");
		/* Can we tab */
		Val(T_pt) = tgetflag("pt");
		Val(T_xt) = tgetflag("xt");
		/* do we have a meta? */
		Val(T_km) = tgetflag("km");
		Val(T_MT) = tgetflag("MT");
		/* Get the size */
		Val(T_co) = tgetnum("co");
		Val(T_li) = tgetnum("li");
		for (t = tstr; t->name != NULL; t++) {
			/* XXX: some systems' tgetstr needs non const */
			terminal_alloc(el, t, tgetstr(strchr(t->name, *t->name),
			    &area));
		}
	}

	if (Val(T_co) < 2)
		Val(T_co) = 80;	/* just in case */
	if (Val(T_li) < 1)
		Val(T_li) = 24;

	el->el_terminal.t_size.v = Val(T_co);
	el->el_terminal.t_size.h = Val(T_li);

	terminal_setflags(el);

				/* get the correct window size */
	(void) terminal_get_size(el, &lins, &cols);
	if (terminal_change_size(el, lins, cols) == -1)
		return -1;
	(void) sigprocmask(SIG_SETMASK, &oset, NULL);
	terminal_bind_arrow(el);
	el->el_terminal.t_name = term;
	return i <= 0 ? -1 : 0;
}


/* terminal_get_size():
 *	Return the new window size in lines and cols, and
 *	true if the size was changed.
 */
protected int
terminal_get_size(EditLine *el, int *lins, int *cols)
{

	*cols = Val(T_co);
	*lins = Val(T_li);

#ifdef TIOCGWINSZ
	{
		struct winsize ws;
		if (ioctl(el->el_infd, TIOCGWINSZ, &ws) != -1) {
			if (ws.ws_col)
				*cols = ws.ws_col;
			if (ws.ws_row)
				*lins = ws.ws_row;
		}
	}
#endif
#ifdef TIOCGSIZE
	{
		struct ttysize ts;
		if (ioctl(el->el_infd, TIOCGSIZE, &ts) != -1) {
			if (ts.ts_cols)
				*cols = ts.ts_cols;
			if (ts.ts_lines)
				*lins = ts.ts_lines;
		}
	}
#endif
	return Val(T_co) != *cols || Val(T_li) != *lins;
}


/* terminal_change_size():
 *	Change the size of the terminal
 */
protected int
terminal_change_size(EditLine *el, int lins, int cols)
{
	/*
         * Just in case
         */
	Val(T_co) = (cols < 2) ? 80 : cols;
	Val(T_li) = (lins < 1) ? 24 : lins;

	/* re-make display buffers */
	if (terminal_rebuffer_display(el) == -1)
		return -1;
	re_clear_display(el);
	return 0;
}


/* terminal_init_arrow():
 *	Initialize the arrow key bindings from termcap
 */
private void
terminal_init_arrow(EditLine *el)
{
	funckey_t *arrow = el->el_terminal.t_fkey;

	arrow[A_K_DN].name = STR("down");
	arrow[A_K_DN].key = T_kd;
	arrow[A_K_DN].fun.cmd = ED_NEXT_HISTORY;
	arrow[A_K_DN].type = XK_CMD;

	arrow[A_K_UP].name = STR("up");
	arrow[A_K_UP].key = T_ku;
	arrow[A_K_UP].fun.cmd = ED_PREV_HISTORY;
	arrow[A_K_UP].type = XK_CMD;

	arrow[A_K_LT].name = STR("left");
	arrow[A_K_LT].key = T_kl;
	arrow[A_K_LT].fun.cmd = ED_PREV_CHAR;
	arrow[A_K_LT].type = XK_CMD;

	arrow[A_K_RT].name = STR("right");
	arrow[A_K_RT].key = T_kr;
	arrow[A_K_RT].fun.cmd = ED_NEXT_CHAR;
	arrow[A_K_RT].type = XK_CMD;

	arrow[A_K_HO].name = STR("home");
	arrow[A_K_HO].key = T_kh;
	arrow[A_K_HO].fun.cmd = ED_MOVE_TO_BEG;
	arrow[A_K_HO].type = XK_CMD;

	arrow[A_K_EN].name = STR("end");
	arrow[A_K_EN].key = T_at7;
	arrow[A_K_EN].fun.cmd = ED_MOVE_TO_END;
	arrow[A_K_EN].type = XK_CMD;

	arrow[A_K_DE].name = STR("delete");
	arrow[A_K_DE].key = T_kD;
	arrow[A_K_DE].fun.cmd = ED_DELETE_NEXT_CHAR;
	arrow[A_K_DE].type = XK_CMD;
}


/* terminal_reset_arrow():
 *	Reset arrow key bindings
 */
private void
terminal_reset_arrow(EditLine *el)
{
	funckey_t *arrow = el->el_terminal.t_fkey;
	static const Char strA[] = {033, '[', 'A', '\0'};
	static const Char strB[] = {033, '[', 'B', '\0'};
	static const Char strC[] = {033, '[', 'C', '\0'};
	static const Char strD[] = {033, '[', 'D', '\0'};
	static const Char strH[] = {033, '[', 'H', '\0'};
	static const Char strF[] = {033, '[', 'F', '\0'};
	static const Char stOA[] = {033, 'O', 'A', '\0'};
	static const Char stOB[] = {033, 'O', 'B', '\0'};
	static const Char stOC[] = {033, 'O', 'C', '\0'};
	static const Char stOD[] = {033, 'O', 'D', '\0'};
	static const Char stOH[] = {033, 'O', 'H', '\0'};
	static const Char stOF[] = {033, 'O', 'F', '\0'};

	keymacro_add(el, strA, &arrow[A_K_UP].fun, arrow[A_K_UP].type);
	keymacro_add(el, strB, &arrow[A_K_DN].fun, arrow[A_K_DN].type);
	keymacro_add(el, strC, &arrow[A_K_RT].fun, arrow[A_K_RT].type);
	keymacro_add(el, strD, &arrow[A_K_LT].fun, arrow[A_K_LT].type);
	keymacro_add(el, strH, &arrow[A_K_HO].fun, arrow[A_K_HO].type);
	keymacro_add(el, strF, &arrow[A_K_EN].fun, arrow[A_K_EN].type);
	keymacro_add(el, stOA, &arrow[A_K_UP].fun, arrow[A_K_UP].type);
	keymacro_add(el, stOB, &arrow[A_K_DN].fun, arrow[A_K_DN].type);
	keymacro_add(el, stOC, &arrow[A_K_RT].fun, arrow[A_K_RT].type);
	keymacro_add(el, stOD, &arrow[A_K_LT].fun, arrow[A_K_LT].type);
	keymacro_add(el, stOH, &arrow[A_K_HO].fun, arrow[A_K_HO].type);
	keymacro_add(el, stOF, &arrow[A_K_EN].fun, arrow[A_K_EN].type);

	if (el->el_map.type != MAP_VI)
		return;
	keymacro_add(el, &strA[1], &arrow[A_K_UP].fun, arrow[A_K_UP].type);
	keymacro_add(el, &strB[1], &arrow[A_K_DN].fun, arrow[A_K_DN].type);
	keymacro_add(el, &strC[1], &arrow[A_K_RT].fun, arrow[A_K_RT].type);
	keymacro_add(el, &strD[1], &arrow[A_K_LT].fun, arrow[A_K_LT].type);
	keymacro_add(el, &strH[1], &arrow[A_K_HO].fun, arrow[A_K_HO].type);
	keymacro_add(el, &strF[1], &arrow[A_K_EN].fun, arrow[A_K_EN].type);
	keymacro_add(el, &stOA[1], &arrow[A_K_UP].fun, arrow[A_K_UP].type);
	keymacro_add(el, &stOB[1], &arrow[A_K_DN].fun, arrow[A_K_DN].type);
	keymacro_add(el, &stOC[1], &arrow[A_K_RT].fun, arrow[A_K_RT].type);
	keymacro_add(el, &stOD[1], &arrow[A_K_LT].fun, arrow[A_K_LT].type);
	keymacro_add(el, &stOH[1], &arrow[A_K_HO].fun, arrow[A_K_HO].type);
	keymacro_add(el, &stOF[1], &arrow[A_K_EN].fun, arrow[A_K_EN].type);
}


/* terminal_set_arrow():
 *	Set an arrow key binding
 */
protected int
terminal_set_arrow(EditLine *el, const Char *name, keymacro_value_t *fun,
    int type)
{
	funckey_t *arrow = el->el_terminal.t_fkey;
	int i;

	for (i = 0; i < A_K_NKEYS; i++)
		if (Strcmp(name, arrow[i].name) == 0) {
			arrow[i].fun = *fun;
			arrow[i].type = type;
			return 0;
		}
	return -1;
}


/* terminal_clear_arrow():
 *	Clear an arrow key binding
 */
protected int
terminal_clear_arrow(EditLine *el, const Char *name)
{
	funckey_t *arrow = el->el_terminal.t_fkey;
	int i;

	for (i = 0; i < A_K_NKEYS; i++)
		if (Strcmp(name, arrow[i].name) == 0) {
			arrow[i].type = XK_NOD;
			return 0;
		}
	return -1;
}


/* terminal_print_arrow():
 *	Print the arrow key bindings
 */
protected void
terminal_print_arrow(EditLine *el, const Char *name)
{
	int i;
	funckey_t *arrow = el->el_terminal.t_fkey;

	for (i = 0; i < A_K_NKEYS; i++)
		if (*name == '\0' || Strcmp(name, arrow[i].name) == 0)
			if (arrow[i].type != XK_NOD)
				keymacro_kprint(el, arrow[i].name,
				    &arrow[i].fun, arrow[i].type);
}


/* terminal_bind_arrow():
 *	Bind the arrow keys
 */
protected void
terminal_bind_arrow(EditLine *el)
{
	el_action_t *map;
	const el_action_t *dmap;
	int i, j;
	char *p;
	funckey_t *arrow = el->el_terminal.t_fkey;

	/* Check if the components needed are initialized */
	if (el->el_terminal.t_buf == NULL || el->el_map.key == NULL)
		return;

	map = el->el_map.type == MAP_VI ? el->el_map.alt : el->el_map.key;
	dmap = el->el_map.type == MAP_VI ? el->el_map.vic : el->el_map.emacs;

	terminal_reset_arrow(el);

	for (i = 0; i < A_K_NKEYS; i++) {
		Char wt_str[VISUAL_WIDTH_MAX];
		Char *px;
		size_t n;

		p = el->el_terminal.t_str[arrow[i].key];
		if (!p || !*p)
			continue;
		for (n = 0; n < VISUAL_WIDTH_MAX && p[n]; ++n)
			wt_str[n] = p[n];
		while (n < VISUAL_WIDTH_MAX)
			wt_str[n++] = '\0';
		px = wt_str;
		j = (unsigned char) *p;
		/*
		 * Assign the arrow keys only if:
		 *
		 * 1. They are multi-character arrow keys and the user
		 *    has not re-assigned the leading character, or
		 *    has re-assigned the leading character to be
		 *	  ED_SEQUENCE_LEAD_IN
		 * 2. They are single arrow keys pointing to an
		 *    unassigned key.
		 */
		if (arrow[i].type == XK_NOD)
			keymacro_clear(el, map, px);
		else {
			if (p[1] && (dmap[j] == map[j] ||
				map[j] == ED_SEQUENCE_LEAD_IN)) {
				keymacro_add(el, px, &arrow[i].fun,
				    arrow[i].type);
				map[j] = ED_SEQUENCE_LEAD_IN;
			} else if (map[j] == ED_UNASSIGNED) {
				keymacro_clear(el, map, px);
				if (arrow[i].type == XK_CMD)
					map[j] = arrow[i].fun.cmd;
				else
					keymacro_add(el, px, &arrow[i].fun,
					    arrow[i].type);
			}
		}
	}
}

/* terminal_putc():
 *	Add a character
 */
private int
terminal_putc(int c)
{
	if (terminal_outfile == NULL)
		return -1;
	return fputc(c, terminal_outfile);
}

private void
terminal_tputs(EditLine *el, const char *cap, int affcnt)
{
#ifdef _REENTRANT
	pthread_mutex_lock(&terminal_mutex);
#endif
	terminal_outfile = el->el_outfile;
	(void)tputs(cap, affcnt, terminal_putc);
#ifdef _REENTRANT
	pthread_mutex_unlock(&terminal_mutex);
#endif
}

/* terminal__putc():
 *	Add a character
 */
protected int
terminal__putc(EditLine *el, wint_t c)
{
	char buf[MB_LEN_MAX +1];
	ssize_t i;
	if (c == (wint_t)MB_FILL_CHAR)
		return 0;
	i = ct_encode_char(buf, (size_t)MB_LEN_MAX, (Char)c);
	if (i <= 0)
		return (int)i;
	buf[i] = '\0';
	return fputs(buf, el->el_outfile);
}

/* terminal__flush():
 *	Flush output
 */
protected void
terminal__flush(EditLine *el)
{

	(void) fflush(el->el_outfile);
}

/* terminal_writec():
 *	Write the given character out, in a human readable form
 */
protected void
terminal_writec(EditLine *el, wint_t c)
{
	Char visbuf[VISUAL_WIDTH_MAX +1];
	ssize_t vcnt = ct_visual_char(visbuf, VISUAL_WIDTH_MAX, (Char)c);
	if (vcnt < 0)
		vcnt = 0;
	visbuf[vcnt] = '\0';
	terminal_overwrite(el, visbuf, (size_t)vcnt);
	terminal__flush(el);
}


/* terminal_telltc():
 *	Print the current termcap characteristics
 */
protected int
/*ARGSUSED*/
terminal_telltc(EditLine *el, int argc __attribute__((__unused__)),
    const Char **argv __attribute__((__unused__)))
{
	const struct termcapstr *t;
	char **ts;

	(void) fprintf(el->el_outfile, "\n\tYour terminal has the\n");
	(void) fprintf(el->el_outfile, "\tfollowing characteristics:\n\n");
	(void) fprintf(el->el_outfile, "\tIt has %d columns and %d lines\n",
	    Val(T_co), Val(T_li));
	(void) fprintf(el->el_outfile,
	    "\tIt has %s meta key\n", EL_HAS_META ? "a" : "no");
	(void) fprintf(el->el_outfile,
	    "\tIt can%suse tabs\n", EL_CAN_TAB ? " " : "not ");
	(void) fprintf(el->el_outfile, "\tIt %s automatic margins\n",
	    EL_HAS_AUTO_MARGINS ? "has" : "does not have");
	if (EL_HAS_AUTO_MARGINS)
		(void) fprintf(el->el_outfile, "\tIt %s magic margins\n",
		    EL_HAS_MAGIC_MARGINS ? "has" : "does not have");

	for (t = tstr, ts = el->el_terminal.t_str; t->name != NULL; t++, ts++) {
		const char *ub;
		if (*ts && **ts) {
			ub = ct_encode_string(ct_visual_string(
			    ct_decode_string(*ts, &el->el_scratch)),
			    &el->el_scratch);
		} else {
			ub = "(empty)";
		}
		(void) fprintf(el->el_outfile, "\t%25s (%s) == %s\n",
		    t->long_name, t->name, ub);
	}
	(void) fputc('\n', el->el_outfile);
	return 0;
}


/* terminal_settc():
 *	Change the current terminal characteristics
 */
protected int
/*ARGSUSED*/
terminal_settc(EditLine *el, int argc __attribute__((__unused__)),
    const Char **argv)
{
	const struct termcapstr *ts;
	const struct termcapval *tv;
	char what[8], how[8];

	if (argv == NULL || argv[1] == NULL || argv[2] == NULL)
		return -1;

	strncpy(what, ct_encode_string(argv[1], &el->el_scratch), sizeof(what));
	what[sizeof(what) - 1] = '\0';
	strncpy(how,  ct_encode_string(argv[2], &el->el_scratch), sizeof(how));
	how[sizeof(how) - 1] = '\0';

	/*
         * Do the strings first
         */
	for (ts = tstr; ts->name != NULL; ts++)
		if (strcmp(ts->name, what) == 0)
			break;

	if (ts->name != NULL) {
		terminal_alloc(el, ts, how);
		terminal_setflags(el);
		return 0;
	}
	/*
         * Do the numeric ones second
         */
	for (tv = tval; tv->name != NULL; tv++)
		if (strcmp(tv->name, what) == 0)
			break;

	if (tv->name != NULL)
		return -1;

	if (tv == &tval[T_pt] || tv == &tval[T_km] ||
	    tv == &tval[T_am] || tv == &tval[T_xn]) {
		if (strcmp(how, "yes") == 0)
			el->el_terminal.t_val[tv - tval] = 1;
		else if (strcmp(how, "no") == 0)
			el->el_terminal.t_val[tv - tval] = 0;
		else {
			(void) fprintf(el->el_errfile,
			    "" FSTR ": Bad value `%s'.\n", argv[0], how);
			return -1;
		}
		terminal_setflags(el);
		if (terminal_change_size(el, Val(T_li), Val(T_co)) == -1)
			return -1;
		return 0;
	} else {
		long i;
		char *ep;

		i = strtol(how, &ep, 10);
		if (*ep != '\0') {
			(void) fprintf(el->el_errfile,
			    "" FSTR ": Bad value `%s'.\n", argv[0], how);
			return -1;
		}
		el->el_terminal.t_val[tv - tval] = (int) i;
		el->el_terminal.t_size.v = Val(T_co);
		el->el_terminal.t_size.h = Val(T_li);
		if (tv == &tval[T_co] || tv == &tval[T_li])
			if (terminal_change_size(el, Val(T_li), Val(T_co))
			    == -1)
				return -1;
		return 0;
	}
}


/* terminal_gettc():
 *	Get the current terminal characteristics
 */
protected int
/*ARGSUSED*/
terminal_gettc(EditLine *el, int argc __attribute__((__unused__)), char **argv)
{
	const struct termcapstr *ts;
	const struct termcapval *tv;
	char *what;
	void *how;

	if (argv == NULL || argv[1] == NULL || argv[2] == NULL)
		return -1;

	what = argv[1];
	how = argv[2];

	/*
         * Do the strings first
         */
	for (ts = tstr; ts->name != NULL; ts++)
		if (strcmp(ts->name, what) == 0)
			break;

	if (ts->name != NULL) {
		*(char **)how = el->el_terminal.t_str[ts - tstr];
		return 0;
	}
	/*
         * Do the numeric ones second
         */
	for (tv = tval; tv->name != NULL; tv++)
		if (strcmp(tv->name, what) == 0)
			break;

	if (tv->name == NULL)
		return -1;

	if (tv == &tval[T_pt] || tv == &tval[T_km] ||
	    tv == &tval[T_am] || tv == &tval[T_xn]) {
		static char yes[] = "yes";
		static char no[] = "no";
		if (el->el_terminal.t_val[tv - tval])
			*(char **)how = yes;
		else
			*(char **)how = no;
		return 0;
	} else {
		*(int *)how = el->el_terminal.t_val[tv - tval];
		return 0;
	}
}

/* terminal_echotc():
 *	Print the termcap string out with variable substitution
 */
protected int
/*ARGSUSED*/
terminal_echotc(EditLine *el, int argc __attribute__((__unused__)),
    const Char **argv)
{
	char *cap, *scap;
	Char *ep;
	int arg_need, arg_cols, arg_rows;
	int verbose = 0, silent = 0;
	char *area;
	static const char fmts[] = "%s\n", fmtd[] = "%d\n";
	const struct termcapstr *t;
	char buf[TC_BUFSIZE];
	long i;

	area = buf;

	if (argv == NULL || argv[1] == NULL)
		return -1;
	argv++;

	if (argv[0][0] == '-') {
		switch (argv[0][1]) {
		case 'v':
			verbose = 1;
			break;
		case 's':
			silent = 1;
			break;
		default:
			/* stderror(ERR_NAME | ERR_TCUSAGE); */
			break;
		}
		argv++;
	}
	if (!*argv || *argv[0] == '\0')
		return 0;
	if (Strcmp(*argv, STR("tabs")) == 0) {
		(void) fprintf(el->el_outfile, fmts, EL_CAN_TAB ? "yes" : "no");
		return 0;
	} else if (Strcmp(*argv, STR("meta")) == 0) {
		(void) fprintf(el->el_outfile, fmts, Val(T_km) ? "yes" : "no");
		return 0;
	} else if (Strcmp(*argv, STR("xn")) == 0) {
		(void) fprintf(el->el_outfile, fmts, EL_HAS_MAGIC_MARGINS ?
		    "yes" : "no");
		return 0;
	} else if (Strcmp(*argv, STR("am")) == 0) {
		(void) fprintf(el->el_outfile, fmts, EL_HAS_AUTO_MARGINS ?
		    "yes" : "no");
		return 0;
	} else if (Strcmp(*argv, STR("baud")) == 0) {
		(void) fprintf(el->el_outfile, fmtd, (int)el->el_tty.t_speed);
		return 0;
	} else if (Strcmp(*argv, STR("rows")) == 0 ||
                   Strcmp(*argv, STR("lines")) == 0) {
		(void) fprintf(el->el_outfile, fmtd, Val(T_li));
		return 0;
	} else if (Strcmp(*argv, STR("cols")) == 0) {
		(void) fprintf(el->el_outfile, fmtd, Val(T_co));
		return 0;
	}
	/*
         * Try to use our local definition first
         */
	scap = NULL;
	for (t = tstr; t->name != NULL; t++)
		if (strcmp(t->name,
		    ct_encode_string(*argv, &el->el_scratch)) == 0) {
			scap = el->el_terminal.t_str[t - tstr];
			break;
		}
	if (t->name == NULL) {
		/* XXX: some systems' tgetstr needs non const */
                scap = tgetstr(ct_encode_string(*argv, &el->el_scratch), &area);
	}
	if (!scap || scap[0] == '\0') {
		if (!silent)
			(void) fprintf(el->el_errfile,
			    "echotc: Termcap parameter `" FSTR "' not found.\n",
			    *argv);
		return -1;
	}
	/*
         * Count home many values we need for this capability.
         */
	for (cap = scap, arg_need = 0; *cap; cap++)
		if (*cap == '%')
			switch (*++cap) {
			case 'd':
			case '2':
			case '3':
			case '.':
			case '+':
				arg_need++;
				break;
			case '%':
			case '>':
			case 'i':
			case 'r':
			case 'n':
			case 'B':
			case 'D':
				break;
			default:
				/*
				 * hpux has lot's of them...
				 */
				if (verbose)
					(void) fprintf(el->el_errfile,
				"echotc: Warning: unknown termcap %% `%c'.\n",
					    *cap);
				/* This is bad, but I won't complain */
				break;
			}

	switch (arg_need) {
	case 0:
		argv++;
		if (*argv && *argv[0]) {
			if (!silent)
				(void) fprintf(el->el_errfile,
				    "echotc: Warning: Extra argument `" FSTR "'.\n",
				    *argv);
			return -1;
		}
		terminal_tputs(el, scap, 1);
		break;
	case 1:
		argv++;
		if (!*argv || *argv[0] == '\0') {
			if (!silent)
				(void) fprintf(el->el_errfile,
				    "echotc: Warning: Missing argument.\n");
			return -1;
		}
		arg_cols = 0;
		i = Strtol(*argv, &ep, 10);
		if (*ep != '\0' || i < 0) {
			if (!silent)
				(void) fprintf(el->el_errfile,
				    "echotc: Bad value `" FSTR "' for rows.\n",
				    *argv);
			return -1;
		}
		arg_rows = (int) i;
		argv++;
		if (*argv && *argv[0]) {
			if (!silent)
				(void) fprintf(el->el_errfile,
				    "echotc: Warning: Extra argument `" FSTR
				    "'.\n", *argv);
			return -1;
		}
		terminal_tputs(el, tgoto(scap, arg_cols, arg_rows), 1);
		break;
	default:
		/* This is wrong, but I will ignore it... */
		if (verbose)
			(void) fprintf(el->el_errfile,
			 "echotc: Warning: Too many required arguments (%d).\n",
			    arg_need);
		/* FALLTHROUGH */
	case 2:
		argv++;
		if (!*argv || *argv[0] == '\0') {
			if (!silent)
				(void) fprintf(el->el_errfile,
				    "echotc: Warning: Missing argument.\n");
			return -1;
		}
		i = Strtol(*argv, &ep, 10);
		if (*ep != '\0' || i < 0) {
			if (!silent)
				(void) fprintf(el->el_errfile,
				    "echotc: Bad value `" FSTR "' for cols.\n",
				    *argv);
			return -1;
		}
		arg_cols = (int) i;
		argv++;
		if (!*argv || *argv[0] == '\0') {
			if (!silent)
				(void) fprintf(el->el_errfile,
				    "echotc: Warning: Missing argument.\n");
			return -1;
		}
		i = Strtol(*argv, &ep, 10);
		if (*ep != '\0' || i < 0) {
			if (!silent)
				(void) fprintf(el->el_errfile,
				    "echotc: Bad value `" FSTR "' for rows.\n",
				    *argv);
			return -1;
		}
		arg_rows = (int) i;
		if (*ep != '\0') {
			if (!silent)
				(void) fprintf(el->el_errfile,
				    "echotc: Bad value `" FSTR "'.\n", *argv);
			return -1;
		}
		argv++;
		if (*argv && *argv[0]) {
			if (!silent)
				(void) fprintf(el->el_errfile,
				    "echotc: Warning: Extra argument `" FSTR
				    "'.\n", *argv);
			return -1;
		}
		terminal_tputs(el, tgoto(scap, arg_cols, arg_rows), arg_rows);
		break;
	}
	return 0;
}
