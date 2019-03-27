/*	$NetBSD: emacs.c,v 1.32 2016/02/16 22:53:14 christos Exp $	*/

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
static char sccsid[] = "@(#)emacs.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: emacs.c,v 1.32 2016/02/16 22:53:14 christos Exp $");
#endif
#endif /* not lint && not SCCSID */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * emacs.c: Emacs functions
 */
#include <ctype.h>

#include "el.h"
#include "emacs.h"

/* em_delete_or_list():
 *	Delete character under cursor or list completions if at end of line
 *	[^D]
 */
protected el_action_t
/*ARGSUSED*/
em_delete_or_list(EditLine *el, wint_t c)
{

	if (el->el_line.cursor == el->el_line.lastchar) {
					/* if I'm at the end */
		if (el->el_line.cursor == el->el_line.buffer) {
					/* and the beginning */
			terminal_writec(el, c);	/* then do an EOF */
			return CC_EOF;
		} else {
			/*
			 * Here we could list completions, but it is an
			 * error right now
			 */
			terminal_beep(el);
			return CC_ERROR;
		}
	} else {
		if (el->el_state.doingarg)
			c_delafter(el, el->el_state.argument);
		else
			c_delafter1(el);
		if (el->el_line.cursor > el->el_line.lastchar)
			el->el_line.cursor = el->el_line.lastchar;
				/* bounds check */
		return CC_REFRESH;
	}
}


/* em_delete_next_word():
 *	Cut from cursor to end of current word
 *	[M-d]
 */
protected el_action_t
/*ARGSUSED*/
em_delete_next_word(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *cp, *p, *kp;

	if (el->el_line.cursor == el->el_line.lastchar)
		return CC_ERROR;

	cp = c__next_word(el->el_line.cursor, el->el_line.lastchar,
	    el->el_state.argument, ce__isword);

	for (p = el->el_line.cursor, kp = el->el_chared.c_kill.buf; p < cp; p++)
				/* save the text */
		*kp++ = *p;
	el->el_chared.c_kill.last = kp;

	c_delafter(el, (int)(cp - el->el_line.cursor));	/* delete after dot */
	if (el->el_line.cursor > el->el_line.lastchar)
		el->el_line.cursor = el->el_line.lastchar;
				/* bounds check */
	return CC_REFRESH;
}


/* em_yank():
 *	Paste cut buffer at cursor position
 *	[^Y]
 */
protected el_action_t
/*ARGSUSED*/
em_yank(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *kp, *cp;

	if (el->el_chared.c_kill.last == el->el_chared.c_kill.buf)
		return CC_NORM;

	if (el->el_line.lastchar +
	    (el->el_chared.c_kill.last - el->el_chared.c_kill.buf) >=
	    el->el_line.limit)
		return CC_ERROR;

	el->el_chared.c_kill.mark = el->el_line.cursor;
	cp = el->el_line.cursor;

	/* open the space, */
	c_insert(el,
	    (int)(el->el_chared.c_kill.last - el->el_chared.c_kill.buf));
	/* copy the chars */
	for (kp = el->el_chared.c_kill.buf; kp < el->el_chared.c_kill.last; kp++)
		*cp++ = *kp;

	/* if an arg, cursor at beginning else cursor at end */
	if (el->el_state.argument == 1)
		el->el_line.cursor = cp;

	return CC_REFRESH;
}


/* em_kill_line():
 *	Cut the entire line and save in cut buffer
 *	[^U]
 */
protected el_action_t
/*ARGSUSED*/
em_kill_line(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *kp, *cp;

	cp = el->el_line.buffer;
	kp = el->el_chared.c_kill.buf;
	while (cp < el->el_line.lastchar)
		*kp++ = *cp++;	/* copy it */
	el->el_chared.c_kill.last = kp;
				/* zap! -- delete all of it */
	el->el_line.lastchar = el->el_line.buffer;
	el->el_line.cursor = el->el_line.buffer;
	return CC_REFRESH;
}


/* em_kill_region():
 *	Cut area between mark and cursor and save in cut buffer
 *	[^W]
 */
protected el_action_t
/*ARGSUSED*/
em_kill_region(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *kp, *cp;

	if (!el->el_chared.c_kill.mark)
		return CC_ERROR;

	if (el->el_chared.c_kill.mark > el->el_line.cursor) {
		cp = el->el_line.cursor;
		kp = el->el_chared.c_kill.buf;
		while (cp < el->el_chared.c_kill.mark)
			*kp++ = *cp++;	/* copy it */
		el->el_chared.c_kill.last = kp;
		c_delafter(el, (int)(cp - el->el_line.cursor));
	} else {		/* mark is before cursor */
		cp = el->el_chared.c_kill.mark;
		kp = el->el_chared.c_kill.buf;
		while (cp < el->el_line.cursor)
			*kp++ = *cp++;	/* copy it */
		el->el_chared.c_kill.last = kp;
		c_delbefore(el, (int)(cp - el->el_chared.c_kill.mark));
		el->el_line.cursor = el->el_chared.c_kill.mark;
	}
	return CC_REFRESH;
}


/* em_copy_region():
 *	Copy area between mark and cursor to cut buffer
 *	[M-W]
 */
protected el_action_t
/*ARGSUSED*/
em_copy_region(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *kp, *cp;

	if (!el->el_chared.c_kill.mark)
		return CC_ERROR;

	if (el->el_chared.c_kill.mark > el->el_line.cursor) {
		cp = el->el_line.cursor;
		kp = el->el_chared.c_kill.buf;
		while (cp < el->el_chared.c_kill.mark)
			*kp++ = *cp++;	/* copy it */
		el->el_chared.c_kill.last = kp;
	} else {
		cp = el->el_chared.c_kill.mark;
		kp = el->el_chared.c_kill.buf;
		while (cp < el->el_line.cursor)
			*kp++ = *cp++;	/* copy it */
		el->el_chared.c_kill.last = kp;
	}
	return CC_NORM;
}


/* em_gosmacs_transpose():
 *	Exchange the two characters before the cursor
 *	Gosling emacs transpose chars [^T]
 */
protected el_action_t
em_gosmacs_transpose(EditLine *el, wint_t c)
{

	if (el->el_line.cursor > &el->el_line.buffer[1]) {
		/* must have at least two chars entered */
		c = el->el_line.cursor[-2];
		el->el_line.cursor[-2] = el->el_line.cursor[-1];
		el->el_line.cursor[-1] = (Char)c;
		return CC_REFRESH;
	} else
		return CC_ERROR;
}


/* em_next_word():
 *	Move next to end of current word
 *	[M-f]
 */
protected el_action_t
/*ARGSUSED*/
em_next_word(EditLine *el, wint_t c __attribute__((__unused__)))
{
	if (el->el_line.cursor == el->el_line.lastchar)
		return CC_ERROR;

	el->el_line.cursor = c__next_word(el->el_line.cursor,
	    el->el_line.lastchar,
	    el->el_state.argument,
	    ce__isword);

	if (el->el_map.type == MAP_VI)
		if (el->el_chared.c_vcmd.action != NOP) {
			cv_delfini(el);
			return CC_REFRESH;
		}
	return CC_CURSOR;
}


/* em_upper_case():
 *	Uppercase the characters from cursor to end of current word
 *	[M-u]
 */
protected el_action_t
/*ARGSUSED*/
em_upper_case(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *cp, *ep;

	ep = c__next_word(el->el_line.cursor, el->el_line.lastchar,
	    el->el_state.argument, ce__isword);

	for (cp = el->el_line.cursor; cp < ep; cp++)
		if (Islower(*cp))
			*cp = Toupper(*cp);

	el->el_line.cursor = ep;
	if (el->el_line.cursor > el->el_line.lastchar)
		el->el_line.cursor = el->el_line.lastchar;
	return CC_REFRESH;
}


/* em_capitol_case():
 *	Capitalize the characters from cursor to end of current word
 *	[M-c]
 */
protected el_action_t
/*ARGSUSED*/
em_capitol_case(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *cp, *ep;

	ep = c__next_word(el->el_line.cursor, el->el_line.lastchar,
	    el->el_state.argument, ce__isword);

	for (cp = el->el_line.cursor; cp < ep; cp++) {
		if (Isalpha(*cp)) {
			if (Islower(*cp))
				*cp = Toupper(*cp);
			cp++;
			break;
		}
	}
	for (; cp < ep; cp++)
		if (Isupper(*cp))
			*cp = Tolower(*cp);

	el->el_line.cursor = ep;
	if (el->el_line.cursor > el->el_line.lastchar)
		el->el_line.cursor = el->el_line.lastchar;
	return CC_REFRESH;
}


/* em_lower_case():
 *	Lowercase the characters from cursor to end of current word
 *	[M-l]
 */
protected el_action_t
/*ARGSUSED*/
em_lower_case(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *cp, *ep;

	ep = c__next_word(el->el_line.cursor, el->el_line.lastchar,
	    el->el_state.argument, ce__isword);

	for (cp = el->el_line.cursor; cp < ep; cp++)
		if (Isupper(*cp))
			*cp = Tolower(*cp);

	el->el_line.cursor = ep;
	if (el->el_line.cursor > el->el_line.lastchar)
		el->el_line.cursor = el->el_line.lastchar;
	return CC_REFRESH;
}


/* em_set_mark():
 *	Set the mark at cursor
 *	[^@]
 */
protected el_action_t
/*ARGSUSED*/
em_set_mark(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_chared.c_kill.mark = el->el_line.cursor;
	return CC_NORM;
}


/* em_exchange_mark():
 *	Exchange the cursor and mark
 *	[^X^X]
 */
protected el_action_t
/*ARGSUSED*/
em_exchange_mark(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *cp;

	cp = el->el_line.cursor;
	el->el_line.cursor = el->el_chared.c_kill.mark;
	el->el_chared.c_kill.mark = cp;
	return CC_CURSOR;
}


/* em_universal_argument():
 *	Universal argument (argument times 4)
 *	[^U]
 */
protected el_action_t
/*ARGSUSED*/
em_universal_argument(EditLine *el, wint_t c __attribute__((__unused__)))
{				/* multiply current argument by 4 */

	if (el->el_state.argument > 1000000)
		return CC_ERROR;
	el->el_state.doingarg = 1;
	el->el_state.argument *= 4;
	return CC_ARGHACK;
}


/* em_meta_next():
 *	Add 8th bit to next character typed
 *	[<ESC>]
 */
protected el_action_t
/*ARGSUSED*/
em_meta_next(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_state.metanext = 1;
	return CC_ARGHACK;
}


/* em_toggle_overwrite():
 *	Switch from insert to overwrite mode or vice versa
 */
protected el_action_t
/*ARGSUSED*/
em_toggle_overwrite(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_state.inputmode = (el->el_state.inputmode == MODE_INSERT) ?
	    MODE_REPLACE : MODE_INSERT;
	return CC_NORM;
}


/* em_copy_prev_word():
 *	Copy current word to cursor
 */
protected el_action_t
/*ARGSUSED*/
em_copy_prev_word(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *cp, *oldc, *dp;

	if (el->el_line.cursor == el->el_line.buffer)
		return CC_ERROR;

	oldc = el->el_line.cursor;
	/* does a bounds check */
	cp = c__prev_word(el->el_line.cursor, el->el_line.buffer,
	    el->el_state.argument, ce__isword);

	c_insert(el, (int)(oldc - cp));
	for (dp = oldc; cp < oldc && dp < el->el_line.lastchar; cp++)
		*dp++ = *cp;

	el->el_line.cursor = dp;/* put cursor at end */

	return CC_REFRESH;
}


/* em_inc_search_next():
 *	Emacs incremental next search
 */
protected el_action_t
/*ARGSUSED*/
em_inc_search_next(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_search.patlen = 0;
	return ce_inc_search(el, ED_SEARCH_NEXT_HISTORY);
}


/* em_inc_search_prev():
 *	Emacs incremental reverse search
 */
protected el_action_t
/*ARGSUSED*/
em_inc_search_prev(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_search.patlen = 0;
	return ce_inc_search(el, ED_SEARCH_PREV_HISTORY);
}


/* em_delete_prev_char():
 *	Delete the character to the left of the cursor
 *	[^?]
 */
protected el_action_t
/*ARGSUSED*/
em_delete_prev_char(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor <= el->el_line.buffer)
		return CC_ERROR;

	if (el->el_state.doingarg)
		c_delbefore(el, el->el_state.argument);
	else
		c_delbefore1(el);
	el->el_line.cursor -= el->el_state.argument;
	if (el->el_line.cursor < el->el_line.buffer)
		el->el_line.cursor = el->el_line.buffer;
	return CC_REFRESH;
}
