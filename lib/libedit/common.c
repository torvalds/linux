/*	$NetBSD: common.c,v 1.40 2016/03/02 19:24:20 christos Exp $	*/

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
static char sccsid[] = "@(#)common.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: common.c,v 1.40 2016/03/02 19:24:20 christos Exp $");
#endif
#endif /* not lint && not SCCSID */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * common.c: Common Editor functions
 */
#include <ctype.h>
#include <string.h>

#include "el.h"
#include "common.h"
#include "parse.h"
#include "vi.h"

/* ed_end_of_file():
 *	Indicate end of file
 *	[^D]
 */
protected el_action_t
/*ARGSUSED*/
ed_end_of_file(EditLine *el, wint_t c __attribute__((__unused__)))
{

	re_goto_bottom(el);
	*el->el_line.lastchar = '\0';
	return CC_EOF;
}


/* ed_insert():
 *	Add character to the line
 *	Insert a character [bound to all insert keys]
 */
protected el_action_t
ed_insert(EditLine *el, wint_t c)
{
	int count = el->el_state.argument;

	if (c == '\0')
		return CC_ERROR;

	if (el->el_line.lastchar + el->el_state.argument >=
	    el->el_line.limit) {
		/* end of buffer space, try to allocate more */
		if (!ch_enlargebufs(el, (size_t) count))
			return CC_ERROR;	/* error allocating more */
	}

	if (count == 1) {
		if (el->el_state.inputmode == MODE_INSERT
		    || el->el_line.cursor >= el->el_line.lastchar)
			c_insert(el, 1);

		*el->el_line.cursor++ = (Char)c;
		re_fastaddc(el);		/* fast refresh for one char. */
	} else {
		if (el->el_state.inputmode != MODE_REPLACE_1)
			c_insert(el, el->el_state.argument);

		while (count-- && el->el_line.cursor < el->el_line.lastchar)
			*el->el_line.cursor++ = (Char)c;
		re_refresh(el);
	}

	if (el->el_state.inputmode == MODE_REPLACE_1)
		return vi_command_mode(el, 0);

	return CC_NORM;
}


/* ed_delete_prev_word():
 *	Delete from beginning of current word to cursor
 *	[M-^?] [^W]
 */
protected el_action_t
/*ARGSUSED*/
ed_delete_prev_word(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *cp, *p, *kp;

	if (el->el_line.cursor == el->el_line.buffer)
		return CC_ERROR;

	cp = c__prev_word(el->el_line.cursor, el->el_line.buffer,
	    el->el_state.argument, ce__isword);

	for (p = cp, kp = el->el_chared.c_kill.buf; p < el->el_line.cursor; p++)
		*kp++ = *p;
	el->el_chared.c_kill.last = kp;

	c_delbefore(el, (int)(el->el_line.cursor - cp));/* delete before dot */
	el->el_line.cursor = cp;
	if (el->el_line.cursor < el->el_line.buffer)
		el->el_line.cursor = el->el_line.buffer; /* bounds check */
	return CC_REFRESH;
}


/* ed_delete_next_char():
 *	Delete character under cursor
 *	[^D] [x]
 */
protected el_action_t
/*ARGSUSED*/
ed_delete_next_char(EditLine *el, wint_t c __attribute__((__unused__)))
{
#ifdef DEBUG_EDIT
#define	EL	el->el_line
	(void) fprintf(el->el_errfile,
	    "\nD(b: %p(" FSTR ")  c: %p(" FSTR ") last: %p(" FSTR
	    ") limit: %p(" FSTR ")\n",
	    EL.buffer, EL.buffer, EL.cursor, EL.cursor, EL.lastchar,
	    EL.lastchar, EL.limit, EL.limit);
#endif
	if (el->el_line.cursor == el->el_line.lastchar) {
			/* if I'm at the end */
		if (el->el_map.type == MAP_VI) {
			if (el->el_line.cursor == el->el_line.buffer) {
				/* if I'm also at the beginning */
#ifdef KSHVI
				return CC_ERROR;
#else
				/* then do an EOF */
				terminal_writec(el, c);
				return CC_EOF;
#endif
			} else {
#ifdef KSHVI
				el->el_line.cursor--;
#else
				return CC_ERROR;
#endif
			}
		} else
				return CC_ERROR;
	}
	c_delafter(el, el->el_state.argument);	/* delete after dot */
	if (el->el_map.type == MAP_VI &&
	    el->el_line.cursor >= el->el_line.lastchar &&
	    el->el_line.cursor > el->el_line.buffer)
			/* bounds check */
		el->el_line.cursor = el->el_line.lastchar - 1;
	return CC_REFRESH;
}


/* ed_kill_line():
 *	Cut to the end of line
 *	[^K] [^K]
 */
protected el_action_t
/*ARGSUSED*/
ed_kill_line(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *kp, *cp;

	cp = el->el_line.cursor;
	kp = el->el_chared.c_kill.buf;
	while (cp < el->el_line.lastchar)
		*kp++ = *cp++;	/* copy it */
	el->el_chared.c_kill.last = kp;
			/* zap! -- delete to end */
	el->el_line.lastchar = el->el_line.cursor;
	return CC_REFRESH;
}


/* ed_move_to_end():
 *	Move cursor to the end of line
 *	[^E] [^E]
 */
protected el_action_t
/*ARGSUSED*/
ed_move_to_end(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_line.cursor = el->el_line.lastchar;
	if (el->el_map.type == MAP_VI) {
		if (el->el_chared.c_vcmd.action != NOP) {
			cv_delfini(el);
			return CC_REFRESH;
		}
#ifdef VI_MOVE
		el->el_line.cursor--;
#endif
	}
	return CC_CURSOR;
}


/* ed_move_to_beg():
 *	Move cursor to the beginning of line
 *	[^A] [^A]
 */
protected el_action_t
/*ARGSUSED*/
ed_move_to_beg(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_line.cursor = el->el_line.buffer;

	if (el->el_map.type == MAP_VI) {
			/* We want FIRST non space character */
		while (Isspace(*el->el_line.cursor))
			el->el_line.cursor++;
		if (el->el_chared.c_vcmd.action != NOP) {
			cv_delfini(el);
			return CC_REFRESH;
		}
	}
	return CC_CURSOR;
}


/* ed_transpose_chars():
 *	Exchange the character to the left of the cursor with the one under it
 *	[^T] [^T]
 */
protected el_action_t
ed_transpose_chars(EditLine *el, wint_t c)
{

	if (el->el_line.cursor < el->el_line.lastchar) {
		if (el->el_line.lastchar <= &el->el_line.buffer[1])
			return CC_ERROR;
		else
			el->el_line.cursor++;
	}
	if (el->el_line.cursor > &el->el_line.buffer[1]) {
		/* must have at least two chars entered */
		c = el->el_line.cursor[-2];
		el->el_line.cursor[-2] = el->el_line.cursor[-1];
		el->el_line.cursor[-1] = (Char)c;
		return CC_REFRESH;
	} else
		return CC_ERROR;
}


/* ed_next_char():
 *	Move to the right one character
 *	[^F] [^F]
 */
protected el_action_t
/*ARGSUSED*/
ed_next_char(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *lim = el->el_line.lastchar;

	if (el->el_line.cursor >= lim ||
	    (el->el_line.cursor == lim - 1 &&
	    el->el_map.type == MAP_VI &&
	    el->el_chared.c_vcmd.action == NOP))
		return CC_ERROR;

	el->el_line.cursor += el->el_state.argument;
	if (el->el_line.cursor > lim)
		el->el_line.cursor = lim;

	if (el->el_map.type == MAP_VI)
		if (el->el_chared.c_vcmd.action != NOP) {
			cv_delfini(el);
			return CC_REFRESH;
		}
	return CC_CURSOR;
}


/* ed_prev_word():
 *	Move to the beginning of the current word
 *	[M-b] [b]
 */
protected el_action_t
/*ARGSUSED*/
ed_prev_word(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor == el->el_line.buffer)
		return CC_ERROR;

	el->el_line.cursor = c__prev_word(el->el_line.cursor,
	    el->el_line.buffer,
	    el->el_state.argument,
	    ce__isword);

	if (el->el_map.type == MAP_VI)
		if (el->el_chared.c_vcmd.action != NOP) {
			cv_delfini(el);
			return CC_REFRESH;
		}
	return CC_CURSOR;
}


/* ed_prev_char():
 *	Move to the left one character
 *	[^B] [^B]
 */
protected el_action_t
/*ARGSUSED*/
ed_prev_char(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor > el->el_line.buffer) {
		el->el_line.cursor -= el->el_state.argument;
		if (el->el_line.cursor < el->el_line.buffer)
			el->el_line.cursor = el->el_line.buffer;

		if (el->el_map.type == MAP_VI)
			if (el->el_chared.c_vcmd.action != NOP) {
				cv_delfini(el);
				return CC_REFRESH;
			}
		return CC_CURSOR;
	} else
		return CC_ERROR;
}


/* ed_quoted_insert():
 *	Add the next character typed verbatim
 *	[^V] [^V]
 */
protected el_action_t
ed_quoted_insert(EditLine *el, wint_t c)
{
	int num;

	tty_quotemode(el);
	num = el_wgetc(el, &c);
	tty_noquotemode(el);
	if (num == 1)
		return ed_insert(el, c);
	else
		return ed_end_of_file(el, 0);
}


/* ed_digit():
 *	Adds to argument or enters a digit
 */
protected el_action_t
ed_digit(EditLine *el, wint_t c)
{

	if (!Isdigit(c))
		return CC_ERROR;

	if (el->el_state.doingarg) {
			/* if doing an arg, add this in... */
		if (el->el_state.lastcmd == EM_UNIVERSAL_ARGUMENT)
			el->el_state.argument = c - '0';
		else {
			if (el->el_state.argument > 1000000)
				return CC_ERROR;
			el->el_state.argument =
			    (el->el_state.argument * 10) + (c - '0');
		}
		return CC_ARGHACK;
	}

	return ed_insert(el, c);
}


/* ed_argument_digit():
 *	Digit that starts argument
 *	For ESC-n
 */
protected el_action_t
ed_argument_digit(EditLine *el, wint_t c)
{

	if (!Isdigit(c))
		return CC_ERROR;

	if (el->el_state.doingarg) {
		if (el->el_state.argument > 1000000)
			return CC_ERROR;
		el->el_state.argument = (el->el_state.argument * 10) +
		    (c - '0');
	} else {		/* else starting an argument */
		el->el_state.argument = c - '0';
		el->el_state.doingarg = 1;
	}
	return CC_ARGHACK;
}


/* ed_unassigned():
 *	Indicates unbound character
 *	Bound to keys that are not assigned
 */
protected el_action_t
/*ARGSUSED*/
ed_unassigned(EditLine *el __attribute__((__unused__)),
    wint_t c __attribute__((__unused__)))
{

	return CC_ERROR;
}


/**
 ** TTY key handling.
 **/

/* ed_tty_sigint():
 *	Tty interrupt character
 *	[^C]
 */
protected el_action_t
/*ARGSUSED*/
ed_tty_sigint(EditLine *el __attribute__((__unused__)),
	      wint_t c __attribute__((__unused__)))
{

	return CC_NORM;
}


/* ed_tty_dsusp():
 *	Tty delayed suspend character
 *	[^Y]
 */
protected el_action_t
/*ARGSUSED*/
ed_tty_dsusp(EditLine *el __attribute__((__unused__)),
	     wint_t c __attribute__((__unused__)))
{

	return CC_NORM;
}


/* ed_tty_flush_output():
 *	Tty flush output characters
 *	[^O]
 */
protected el_action_t
/*ARGSUSED*/
ed_tty_flush_output(EditLine *el __attribute__((__unused__)),
		    wint_t c __attribute__((__unused__)))
{

	return CC_NORM;
}


/* ed_tty_sigquit():
 *	Tty quit character
 *	[^\]
 */
protected el_action_t
/*ARGSUSED*/
ed_tty_sigquit(EditLine *el __attribute__((__unused__)),
	       wint_t c __attribute__((__unused__)))
{

	return CC_NORM;
}


/* ed_tty_sigtstp():
 *	Tty suspend character
 *	[^Z]
 */
protected el_action_t
/*ARGSUSED*/
ed_tty_sigtstp(EditLine *el __attribute__((__unused__)),
	       wint_t c __attribute__((__unused__)))
{

	return CC_NORM;
}


/* ed_tty_stop_output():
 *	Tty disallow output characters
 *	[^S]
 */
protected el_action_t
/*ARGSUSED*/
ed_tty_stop_output(EditLine *el __attribute__((__unused__)),
		   wint_t c __attribute__((__unused__)))
{

	return CC_NORM;
}


/* ed_tty_start_output():
 *	Tty allow output characters
 *	[^Q]
 */
protected el_action_t
/*ARGSUSED*/
ed_tty_start_output(EditLine *el __attribute__((__unused__)),
		    wint_t c __attribute__((__unused__)))
{

	return CC_NORM;
}


/* ed_newline():
 *	Execute command
 *	[^J]
 */
protected el_action_t
/*ARGSUSED*/
ed_newline(EditLine *el, wint_t c __attribute__((__unused__)))
{

	re_goto_bottom(el);
	*el->el_line.lastchar++ = '\n';
	*el->el_line.lastchar = '\0';
	return CC_NEWLINE;
}


/* ed_delete_prev_char():
 *	Delete the character to the left of the cursor
 *	[^?]
 */
protected el_action_t
/*ARGSUSED*/
ed_delete_prev_char(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor <= el->el_line.buffer)
		return CC_ERROR;

	c_delbefore(el, el->el_state.argument);
	el->el_line.cursor -= el->el_state.argument;
	if (el->el_line.cursor < el->el_line.buffer)
		el->el_line.cursor = el->el_line.buffer;
	return CC_REFRESH;
}


/* ed_clear_screen():
 *	Clear screen leaving current line at the top
 *	[^L]
 */
protected el_action_t
/*ARGSUSED*/
ed_clear_screen(EditLine *el, wint_t c __attribute__((__unused__)))
{

	terminal_clear_screen(el);	/* clear the whole real screen */
	re_clear_display(el);	/* reset everything */
	return CC_REFRESH;
}


/* ed_redisplay():
 *	Redisplay everything
 *	^R
 */
protected el_action_t
/*ARGSUSED*/
ed_redisplay(EditLine *el __attribute__((__unused__)),
	     wint_t c __attribute__((__unused__)))
{

	return CC_REDISPLAY;
}


/* ed_start_over():
 *	Erase current line and start from scratch
 *	[^G]
 */
protected el_action_t
/*ARGSUSED*/
ed_start_over(EditLine *el, wint_t c __attribute__((__unused__)))
{

	ch_reset(el, 0);
	return CC_REFRESH;
}


/* ed_sequence_lead_in():
 *	First character in a bound sequence
 *	Placeholder for external keys
 */
protected el_action_t
/*ARGSUSED*/
ed_sequence_lead_in(EditLine *el __attribute__((__unused__)),
		    wint_t c __attribute__((__unused__)))
{

	return CC_NORM;
}


/* ed_prev_history():
 *	Move to the previous history line
 *	[^P] [k]
 */
protected el_action_t
/*ARGSUSED*/
ed_prev_history(EditLine *el, wint_t c __attribute__((__unused__)))
{
	char beep = 0;
	int sv_event = el->el_history.eventno;

	el->el_chared.c_undo.len = -1;
	*el->el_line.lastchar = '\0';		/* just in case */

	if (el->el_history.eventno == 0) {	/* save the current buffer
						 * away */
		(void) Strncpy(el->el_history.buf, el->el_line.buffer,
		    EL_BUFSIZ);
		el->el_history.last = el->el_history.buf +
		    (el->el_line.lastchar - el->el_line.buffer);
	}
	el->el_history.eventno += el->el_state.argument;

	if (hist_get(el) == CC_ERROR) {
		if (el->el_map.type == MAP_VI) {
			el->el_history.eventno = sv_event;
		}
		beep = 1;
		/* el->el_history.eventno was fixed by first call */
		(void) hist_get(el);
	}
	if (beep)
		return CC_REFRESH_BEEP;
	return CC_REFRESH;
}


/* ed_next_history():
 *	Move to the next history line
 *	[^N] [j]
 */
protected el_action_t
/*ARGSUSED*/
ed_next_history(EditLine *el, wint_t c __attribute__((__unused__)))
{
	el_action_t beep = CC_REFRESH, rval;

	el->el_chared.c_undo.len = -1;
	*el->el_line.lastchar = '\0';	/* just in case */

	el->el_history.eventno -= el->el_state.argument;

	if (el->el_history.eventno < 0) {
		el->el_history.eventno = 0;
		beep = CC_REFRESH_BEEP;
	}
	rval = hist_get(el);
	if (rval == CC_REFRESH)
		return beep;
	return rval;

}


/* ed_search_prev_history():
 *	Search previous in history for a line matching the current
 *	next search history [M-P] [K]
 */
protected el_action_t
/*ARGSUSED*/
ed_search_prev_history(EditLine *el, wint_t c __attribute__((__unused__)))
{
	const Char *hp;
	int h;
	int found = 0;

	el->el_chared.c_vcmd.action = NOP;
	el->el_chared.c_undo.len = -1;
	*el->el_line.lastchar = '\0';	/* just in case */
	if (el->el_history.eventno < 0) {
#ifdef DEBUG_EDIT
		(void) fprintf(el->el_errfile,
		    "e_prev_search_hist(): eventno < 0;\n");
#endif
		el->el_history.eventno = 0;
		return CC_ERROR;
	}
	if (el->el_history.eventno == 0) {
		(void) Strncpy(el->el_history.buf, el->el_line.buffer,
		    EL_BUFSIZ);
		el->el_history.last = el->el_history.buf +
		    (el->el_line.lastchar - el->el_line.buffer);
	}
	if (el->el_history.ref == NULL)
		return CC_ERROR;

	hp = HIST_FIRST(el);
	if (hp == NULL)
		return CC_ERROR;

	c_setpat(el);		/* Set search pattern !! */

	for (h = 1; h <= el->el_history.eventno; h++)
		hp = HIST_NEXT(el);

	while (hp != NULL) {
#ifdef SDEBUG
		(void) fprintf(el->el_errfile, "Comparing with \"%s\"\n", hp);
#endif
		if ((Strncmp(hp, el->el_line.buffer, (size_t)
			    (el->el_line.lastchar - el->el_line.buffer)) ||
			hp[el->el_line.lastchar - el->el_line.buffer]) &&
		    c_hmatch(el, hp)) {
			found = 1;
			break;
		}
		h++;
		hp = HIST_NEXT(el);
	}

	if (!found) {
#ifdef SDEBUG
		(void) fprintf(el->el_errfile, "not found\n");
#endif
		return CC_ERROR;
	}
	el->el_history.eventno = h;

	return hist_get(el);
}


/* ed_search_next_history():
 *	Search next in history for a line matching the current
 *	[M-N] [J]
 */
protected el_action_t
/*ARGSUSED*/
ed_search_next_history(EditLine *el, wint_t c __attribute__((__unused__)))
{
	const Char *hp;
	int h;
	int found = 0;

	el->el_chared.c_vcmd.action = NOP;
	el->el_chared.c_undo.len = -1;
	*el->el_line.lastchar = '\0';	/* just in case */

	if (el->el_history.eventno == 0)
		return CC_ERROR;

	if (el->el_history.ref == NULL)
		return CC_ERROR;

	hp = HIST_FIRST(el);
	if (hp == NULL)
		return CC_ERROR;

	c_setpat(el);		/* Set search pattern !! */

	for (h = 1; h < el->el_history.eventno && hp; h++) {
#ifdef SDEBUG
		(void) fprintf(el->el_errfile, "Comparing with \"%s\"\n", hp);
#endif
		if ((Strncmp(hp, el->el_line.buffer, (size_t)
			    (el->el_line.lastchar - el->el_line.buffer)) ||
			hp[el->el_line.lastchar - el->el_line.buffer]) &&
		    c_hmatch(el, hp))
			found = h;
		hp = HIST_NEXT(el);
	}

	if (!found) {		/* is it the current history number? */
		if (!c_hmatch(el, el->el_history.buf)) {
#ifdef SDEBUG
			(void) fprintf(el->el_errfile, "not found\n");
#endif
			return CC_ERROR;
		}
	}
	el->el_history.eventno = found;

	return hist_get(el);
}


/* ed_prev_line():
 *	Move up one line
 *	Could be [k] [^p]
 */
protected el_action_t
/*ARGSUSED*/
ed_prev_line(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *ptr;
	int nchars = c_hpos(el);

	/*
         * Move to the line requested
         */
	if (*(ptr = el->el_line.cursor) == '\n')
		ptr--;

	for (; ptr >= el->el_line.buffer; ptr--)
		if (*ptr == '\n' && --el->el_state.argument <= 0)
			break;

	if (el->el_state.argument > 0)
		return CC_ERROR;

	/*
         * Move to the beginning of the line
         */
	for (ptr--; ptr >= el->el_line.buffer && *ptr != '\n'; ptr--)
		continue;

	/*
         * Move to the character requested
         */
	for (ptr++;
	    nchars-- > 0 && ptr < el->el_line.lastchar && *ptr != '\n';
	    ptr++)
		continue;

	el->el_line.cursor = ptr;
	return CC_CURSOR;
}


/* ed_next_line():
 *	Move down one line
 *	Could be [j] [^n]
 */
protected el_action_t
/*ARGSUSED*/
ed_next_line(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *ptr;
	int nchars = c_hpos(el);

	/*
         * Move to the line requested
         */
	for (ptr = el->el_line.cursor; ptr < el->el_line.lastchar; ptr++)
		if (*ptr == '\n' && --el->el_state.argument <= 0)
			break;

	if (el->el_state.argument > 0)
		return CC_ERROR;

	/*
         * Move to the character requested
         */
	for (ptr++;
	    nchars-- > 0 && ptr < el->el_line.lastchar && *ptr != '\n';
	    ptr++)
		continue;

	el->el_line.cursor = ptr;
	return CC_CURSOR;
}


/* ed_command():
 *	Editline extended command
 *	[M-X] [:]
 */
protected el_action_t
/*ARGSUSED*/
ed_command(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char tmpbuf[EL_BUFSIZ];
	int tmplen;

	tmplen = c_gets(el, tmpbuf, STR("\n: "));
	terminal__putc(el, '\n');

	if (tmplen < 0 || (tmpbuf[tmplen] = 0, parse_line(el, tmpbuf)) == -1)
		terminal_beep(el);

	el->el_map.current = el->el_map.key;
	re_clear_display(el);
	return CC_REFRESH;
}
