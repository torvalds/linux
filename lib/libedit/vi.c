/*	$NetBSD: vi.c,v 1.55 2016/03/02 19:24:20 christos Exp $	*/

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
static char sccsid[] = "@(#)vi.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: vi.c,v 1.55 2016/03/02 19:24:20 christos Exp $");
#endif
#endif /* not lint && not SCCSID */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * vi.c: Vi mode commands.
 */
#include <sys/wait.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "el.h"
#include "common.h"
#include "emacs.h"
#include "vi.h"

private el_action_t	cv_action(EditLine *, wint_t);
private el_action_t	cv_paste(EditLine *, wint_t);

/* cv_action():
 *	Handle vi actions.
 */
private el_action_t
cv_action(EditLine *el, wint_t c)
{

	if (el->el_chared.c_vcmd.action != NOP) {
		/* 'cc', 'dd' and (possibly) friends */
		if (c != (wint_t)el->el_chared.c_vcmd.action)
			return CC_ERROR;

		if (!(c & YANK))
			cv_undo(el);
		cv_yank(el, el->el_line.buffer,
		    (int)(el->el_line.lastchar - el->el_line.buffer));
		el->el_chared.c_vcmd.action = NOP;
		el->el_chared.c_vcmd.pos = 0;
		if (!(c & YANK)) {
			el->el_line.lastchar = el->el_line.buffer;
			el->el_line.cursor = el->el_line.buffer;
		}
		if (c & INSERT)
			el->el_map.current = el->el_map.key;

		return CC_REFRESH;
	}
	el->el_chared.c_vcmd.pos = el->el_line.cursor;
	el->el_chared.c_vcmd.action = c;
	return CC_ARGHACK;
}

/* cv_paste():
 *	Paste previous deletion before or after the cursor
 */
private el_action_t
cv_paste(EditLine *el, wint_t c)
{
	c_kill_t *k = &el->el_chared.c_kill;
	size_t len = (size_t)(k->last - k->buf);

	if (k->buf == NULL || len == 0)
		return CC_ERROR;
#ifdef DEBUG_PASTE
	(void) fprintf(el->el_errfile, "Paste: \"" FSTARSTR "\"\n", (int)len,
	    k->buf);
#endif

	cv_undo(el);

	if (!c && el->el_line.cursor < el->el_line.lastchar)
		el->el_line.cursor++;

	c_insert(el, (int)len);
	if (el->el_line.cursor + len > el->el_line.lastchar)
		return CC_ERROR;
	(void) memcpy(el->el_line.cursor, k->buf, len *
	    sizeof(*el->el_line.cursor));

	return CC_REFRESH;
}


/* vi_paste_next():
 *	Vi paste previous deletion to the right of the cursor
 *	[p]
 */
protected el_action_t
/*ARGSUSED*/
vi_paste_next(EditLine *el, wint_t c __attribute__((__unused__)))
{

	return cv_paste(el, 0);
}


/* vi_paste_prev():
 *	Vi paste previous deletion to the left of the cursor
 *	[P]
 */
protected el_action_t
/*ARGSUSED*/
vi_paste_prev(EditLine *el, wint_t c __attribute__((__unused__)))
{

	return cv_paste(el, 1);
}


/* vi_prev_big_word():
 *	Vi move to the previous space delimited word
 *	[B]
 */
protected el_action_t
/*ARGSUSED*/
vi_prev_big_word(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor == el->el_line.buffer)
		return CC_ERROR;

	el->el_line.cursor = cv_prev_word(el->el_line.cursor,
	    el->el_line.buffer,
	    el->el_state.argument,
	    cv__isWord);

	if (el->el_chared.c_vcmd.action != NOP) {
		cv_delfini(el);
		return CC_REFRESH;
	}
	return CC_CURSOR;
}


/* vi_prev_word():
 *	Vi move to the previous word
 *	[b]
 */
protected el_action_t
/*ARGSUSED*/
vi_prev_word(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor == el->el_line.buffer)
		return CC_ERROR;

	el->el_line.cursor = cv_prev_word(el->el_line.cursor,
	    el->el_line.buffer,
	    el->el_state.argument,
	    cv__isword);

	if (el->el_chared.c_vcmd.action != NOP) {
		cv_delfini(el);
		return CC_REFRESH;
	}
	return CC_CURSOR;
}


/* vi_next_big_word():
 *	Vi move to the next space delimited word
 *	[W]
 */
protected el_action_t
/*ARGSUSED*/
vi_next_big_word(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor >= el->el_line.lastchar - 1)
		return CC_ERROR;

	el->el_line.cursor = cv_next_word(el, el->el_line.cursor,
	    el->el_line.lastchar, el->el_state.argument, cv__isWord);

	if (el->el_map.type == MAP_VI)
		if (el->el_chared.c_vcmd.action != NOP) {
			cv_delfini(el);
			return CC_REFRESH;
		}
	return CC_CURSOR;
}


/* vi_next_word():
 *	Vi move to the next word
 *	[w]
 */
protected el_action_t
/*ARGSUSED*/
vi_next_word(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor >= el->el_line.lastchar - 1)
		return CC_ERROR;

	el->el_line.cursor = cv_next_word(el, el->el_line.cursor,
	    el->el_line.lastchar, el->el_state.argument, cv__isword);

	if (el->el_map.type == MAP_VI)
		if (el->el_chared.c_vcmd.action != NOP) {
			cv_delfini(el);
			return CC_REFRESH;
		}
	return CC_CURSOR;
}


/* vi_change_case():
 *	Vi change case of character under the cursor and advance one character
 *	[~]
 */
protected el_action_t
vi_change_case(EditLine *el, wint_t c)
{
	int i;

	if (el->el_line.cursor >= el->el_line.lastchar)
		return CC_ERROR;
	cv_undo(el);
	for (i = 0; i < el->el_state.argument; i++) {

		c = *el->el_line.cursor;
		if (Isupper(c))
			*el->el_line.cursor = Tolower(c);
		else if (Islower(c))
			*el->el_line.cursor = Toupper(c);

		if (++el->el_line.cursor >= el->el_line.lastchar) {
			el->el_line.cursor--;
			re_fastaddc(el);
			break;
		}
		re_fastaddc(el);
	}
	return CC_NORM;
}


/* vi_change_meta():
 *	Vi change prefix command
 *	[c]
 */
protected el_action_t
/*ARGSUSED*/
vi_change_meta(EditLine *el, wint_t c __attribute__((__unused__)))
{

	/*
         * Delete with insert == change: first we delete and then we leave in
         * insert mode.
         */
	return cv_action(el, DELETE | INSERT);
}


/* vi_insert_at_bol():
 *	Vi enter insert mode at the beginning of line
 *	[I]
 */
protected el_action_t
/*ARGSUSED*/
vi_insert_at_bol(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_line.cursor = el->el_line.buffer;
	cv_undo(el);
	el->el_map.current = el->el_map.key;
	return CC_CURSOR;
}


/* vi_replace_char():
 *	Vi replace character under the cursor with the next character typed
 *	[r]
 */
protected el_action_t
/*ARGSUSED*/
vi_replace_char(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor >= el->el_line.lastchar)
		return CC_ERROR;

	el->el_map.current = el->el_map.key;
	el->el_state.inputmode = MODE_REPLACE_1;
	cv_undo(el);
	return CC_ARGHACK;
}


/* vi_replace_mode():
 *	Vi enter replace mode
 *	[R]
 */
protected el_action_t
/*ARGSUSED*/
vi_replace_mode(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_map.current = el->el_map.key;
	el->el_state.inputmode = MODE_REPLACE;
	cv_undo(el);
	return CC_NORM;
}


/* vi_substitute_char():
 *	Vi replace character under the cursor and enter insert mode
 *	[s]
 */
protected el_action_t
/*ARGSUSED*/
vi_substitute_char(EditLine *el, wint_t c __attribute__((__unused__)))
{

	c_delafter(el, el->el_state.argument);
	el->el_map.current = el->el_map.key;
	return CC_REFRESH;
}


/* vi_substitute_line():
 *	Vi substitute entire line
 *	[S]
 */
protected el_action_t
/*ARGSUSED*/
vi_substitute_line(EditLine *el, wint_t c __attribute__((__unused__)))
{

	cv_undo(el);
	cv_yank(el, el->el_line.buffer,
	    (int)(el->el_line.lastchar - el->el_line.buffer));
	(void) em_kill_line(el, 0);
	el->el_map.current = el->el_map.key;
	return CC_REFRESH;
}


/* vi_change_to_eol():
 *	Vi change to end of line
 *	[C]
 */
protected el_action_t
/*ARGSUSED*/
vi_change_to_eol(EditLine *el, wint_t c __attribute__((__unused__)))
{

	cv_undo(el);
	cv_yank(el, el->el_line.cursor,
	    (int)(el->el_line.lastchar - el->el_line.cursor));
	(void) ed_kill_line(el, 0);
	el->el_map.current = el->el_map.key;
	return CC_REFRESH;
}


/* vi_insert():
 *	Vi enter insert mode
 *	[i]
 */
protected el_action_t
/*ARGSUSED*/
vi_insert(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_map.current = el->el_map.key;
	cv_undo(el);
	return CC_NORM;
}


/* vi_add():
 *	Vi enter insert mode after the cursor
 *	[a]
 */
protected el_action_t
/*ARGSUSED*/
vi_add(EditLine *el, wint_t c __attribute__((__unused__)))
{
	int ret;

	el->el_map.current = el->el_map.key;
	if (el->el_line.cursor < el->el_line.lastchar) {
		el->el_line.cursor++;
		if (el->el_line.cursor > el->el_line.lastchar)
			el->el_line.cursor = el->el_line.lastchar;
		ret = CC_CURSOR;
	} else
		ret = CC_NORM;

	cv_undo(el);

	return (el_action_t)ret;
}


/* vi_add_at_eol():
 *	Vi enter insert mode at end of line
 *	[A]
 */
protected el_action_t
/*ARGSUSED*/
vi_add_at_eol(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_map.current = el->el_map.key;
	el->el_line.cursor = el->el_line.lastchar;
	cv_undo(el);
	return CC_CURSOR;
}


/* vi_delete_meta():
 *	Vi delete prefix command
 *	[d]
 */
protected el_action_t
/*ARGSUSED*/
vi_delete_meta(EditLine *el, wint_t c __attribute__((__unused__)))
{

	return cv_action(el, DELETE);
}


/* vi_end_big_word():
 *	Vi move to the end of the current space delimited word
 *	[E]
 */
protected el_action_t
/*ARGSUSED*/
vi_end_big_word(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor == el->el_line.lastchar)
		return CC_ERROR;

	el->el_line.cursor = cv__endword(el->el_line.cursor,
	    el->el_line.lastchar, el->el_state.argument, cv__isWord);

	if (el->el_chared.c_vcmd.action != NOP) {
		el->el_line.cursor++;
		cv_delfini(el);
		return CC_REFRESH;
	}
	return CC_CURSOR;
}


/* vi_end_word():
 *	Vi move to the end of the current word
 *	[e]
 */
protected el_action_t
/*ARGSUSED*/
vi_end_word(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor == el->el_line.lastchar)
		return CC_ERROR;

	el->el_line.cursor = cv__endword(el->el_line.cursor,
	    el->el_line.lastchar, el->el_state.argument, cv__isword);

	if (el->el_chared.c_vcmd.action != NOP) {
		el->el_line.cursor++;
		cv_delfini(el);
		return CC_REFRESH;
	}
	return CC_CURSOR;
}


/* vi_undo():
 *	Vi undo last change
 *	[u]
 */
protected el_action_t
/*ARGSUSED*/
vi_undo(EditLine *el, wint_t c __attribute__((__unused__)))
{
	c_undo_t un = el->el_chared.c_undo;

	if (un.len == -1)
		return CC_ERROR;

	/* switch line buffer and undo buffer */
	el->el_chared.c_undo.buf = el->el_line.buffer;
	el->el_chared.c_undo.len = el->el_line.lastchar - el->el_line.buffer;
	el->el_chared.c_undo.cursor =
	    (int)(el->el_line.cursor - el->el_line.buffer);
	el->el_line.limit = un.buf + (el->el_line.limit - el->el_line.buffer);
	el->el_line.buffer = un.buf;
	el->el_line.cursor = un.buf + un.cursor;
	el->el_line.lastchar = un.buf + un.len;

	return CC_REFRESH;
}


/* vi_command_mode():
 *	Vi enter command mode (use alternative key bindings)
 *	[<ESC>]
 */
protected el_action_t
/*ARGSUSED*/
vi_command_mode(EditLine *el, wint_t c __attribute__((__unused__)))
{

	/* [Esc] cancels pending action */
	el->el_chared.c_vcmd.action = NOP;
	el->el_chared.c_vcmd.pos = 0;

	el->el_state.doingarg = 0;

	el->el_state.inputmode = MODE_INSERT;
	el->el_map.current = el->el_map.alt;
#ifdef VI_MOVE
	if (el->el_line.cursor > el->el_line.buffer)
		el->el_line.cursor--;
#endif
	return CC_CURSOR;
}


/* vi_zero():
 *	Vi move to the beginning of line
 *	[0]
 */
protected el_action_t
vi_zero(EditLine *el, wint_t c)
{

	if (el->el_state.doingarg)
		return ed_argument_digit(el, c);

	el->el_line.cursor = el->el_line.buffer;
	if (el->el_chared.c_vcmd.action != NOP) {
		cv_delfini(el);
		return CC_REFRESH;
	}
	return CC_CURSOR;
}


/* vi_delete_prev_char():
 *	Vi move to previous character (backspace)
 *	[^H] in insert mode only
 */
protected el_action_t
/*ARGSUSED*/
vi_delete_prev_char(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_line.cursor <= el->el_line.buffer)
		return CC_ERROR;

	c_delbefore1(el);
	el->el_line.cursor--;
	return CC_REFRESH;
}


/* vi_list_or_eof():
 *	Vi list choices for completion or indicate end of file if empty line
 *	[^D]
 */
protected el_action_t
/*ARGSUSED*/
vi_list_or_eof(EditLine *el, wint_t c)
{

	if (el->el_line.cursor == el->el_line.lastchar) {
		if (el->el_line.cursor == el->el_line.buffer) {
			terminal_writec(el, c);	/* then do a EOF */
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
#ifdef notyet
		re_goto_bottom(el);
		*el->el_line.lastchar = '\0';	/* just in case */
		return CC_LIST_CHOICES;
#else
		/*
		 * Just complain for now.
		 */
		terminal_beep(el);
		return CC_ERROR;
#endif
	}
}


/* vi_kill_line_prev():
 *	Vi cut from beginning of line to cursor
 *	[^U]
 */
protected el_action_t
/*ARGSUSED*/
vi_kill_line_prev(EditLine *el, wint_t c __attribute__((__unused__)))
{
	Char *kp, *cp;

	cp = el->el_line.buffer;
	kp = el->el_chared.c_kill.buf;
	while (cp < el->el_line.cursor)
		*kp++ = *cp++;	/* copy it */
	el->el_chared.c_kill.last = kp;
	c_delbefore(el, (int)(el->el_line.cursor - el->el_line.buffer));
	el->el_line.cursor = el->el_line.buffer;	/* zap! */
	return CC_REFRESH;
}


/* vi_search_prev():
 *	Vi search history previous
 *	[?]
 */
protected el_action_t
/*ARGSUSED*/
vi_search_prev(EditLine *el, wint_t c __attribute__((__unused__)))
{

	return cv_search(el, ED_SEARCH_PREV_HISTORY);
}


/* vi_search_next():
 *	Vi search history next
 *	[/]
 */
protected el_action_t
/*ARGSUSED*/
vi_search_next(EditLine *el, wint_t c __attribute__((__unused__)))
{

	return cv_search(el, ED_SEARCH_NEXT_HISTORY);
}


/* vi_repeat_search_next():
 *	Vi repeat current search in the same search direction
 *	[n]
 */
protected el_action_t
/*ARGSUSED*/
vi_repeat_search_next(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_search.patlen == 0)
		return CC_ERROR;
	else
		return cv_repeat_srch(el, el->el_search.patdir);
}


/* vi_repeat_search_prev():
 *	Vi repeat current search in the opposite search direction
 *	[N]
 */
/*ARGSUSED*/
protected el_action_t
vi_repeat_search_prev(EditLine *el, wint_t c __attribute__((__unused__)))
{

	if (el->el_search.patlen == 0)
		return CC_ERROR;
	else
		return (cv_repeat_srch(el,
		    el->el_search.patdir == ED_SEARCH_PREV_HISTORY ?
		    ED_SEARCH_NEXT_HISTORY : ED_SEARCH_PREV_HISTORY));
}


/* vi_next_char():
 *	Vi move to the character specified next
 *	[f]
 */
protected el_action_t
/*ARGSUSED*/
vi_next_char(EditLine *el, wint_t c __attribute__((__unused__)))
{
	return cv_csearch(el, CHAR_FWD, -1, el->el_state.argument, 0);
}


/* vi_prev_char():
 *	Vi move to the character specified previous
 *	[F]
 */
protected el_action_t
/*ARGSUSED*/
vi_prev_char(EditLine *el, wint_t c __attribute__((__unused__)))
{
	return cv_csearch(el, CHAR_BACK, -1, el->el_state.argument, 0);
}


/* vi_to_next_char():
 *	Vi move up to the character specified next
 *	[t]
 */
protected el_action_t
/*ARGSUSED*/
vi_to_next_char(EditLine *el, wint_t c __attribute__((__unused__)))
{
	return cv_csearch(el, CHAR_FWD, -1, el->el_state.argument, 1);
}


/* vi_to_prev_char():
 *	Vi move up to the character specified previous
 *	[T]
 */
protected el_action_t
/*ARGSUSED*/
vi_to_prev_char(EditLine *el, wint_t c __attribute__((__unused__)))
{
	return cv_csearch(el, CHAR_BACK, -1, el->el_state.argument, 1);
}


/* vi_repeat_next_char():
 *	Vi repeat current character search in the same search direction
 *	[;]
 */
protected el_action_t
/*ARGSUSED*/
vi_repeat_next_char(EditLine *el, wint_t c __attribute__((__unused__)))
{

	return cv_csearch(el, el->el_search.chadir, el->el_search.chacha,
		el->el_state.argument, el->el_search.chatflg);
}


/* vi_repeat_prev_char():
 *	Vi repeat current character search in the opposite search direction
 *	[,]
 */
protected el_action_t
/*ARGSUSED*/
vi_repeat_prev_char(EditLine *el, wint_t c __attribute__((__unused__)))
{
	el_action_t r;
	int dir = el->el_search.chadir;

	r = cv_csearch(el, -dir, el->el_search.chacha,
		el->el_state.argument, el->el_search.chatflg);
	el->el_search.chadir = dir;
	return r;
}


/* vi_match():
 *	Vi go to matching () {} or []
 *	[%]
 */
protected el_action_t
/*ARGSUSED*/
vi_match(EditLine *el, wint_t c __attribute__((__unused__)))
{
	const Char match_chars[] = STR("()[]{}");
	Char *cp;
	size_t delta, i, count;
	Char o_ch, c_ch;

	*el->el_line.lastchar = '\0';		/* just in case */

	i = Strcspn(el->el_line.cursor, match_chars);
	o_ch = el->el_line.cursor[i];
	if (o_ch == 0)
		return CC_ERROR;
	delta = (size_t)(Strchr(match_chars, o_ch) - match_chars);
	c_ch = match_chars[delta ^ 1];
	count = 1;
	delta = 1 - (delta & 1) * 2;

	for (cp = &el->el_line.cursor[i]; count; ) {
		cp += delta;
		if (cp < el->el_line.buffer || cp >= el->el_line.lastchar)
			return CC_ERROR;
		if (*cp == o_ch)
			count++;
		else if (*cp == c_ch)
			count--;
	}

	el->el_line.cursor = cp;

	if (el->el_chared.c_vcmd.action != NOP) {
		/* NB posix says char under cursor should NOT be deleted
		   for -ve delta - this is different to netbsd vi. */
		if (delta > 0)
			el->el_line.cursor++;
		cv_delfini(el);
		return CC_REFRESH;
	}
	return CC_CURSOR;
}

/* vi_undo_line():
 *	Vi undo all changes to line
 *	[U]
 */
protected el_action_t
/*ARGSUSED*/
vi_undo_line(EditLine *el, wint_t c __attribute__((__unused__)))
{

	cv_undo(el);
	return hist_get(el);
}

/* vi_to_column():
 *	Vi go to specified column
 *	[|]
 * NB netbsd vi goes to screen column 'n', posix says nth character
 */
protected el_action_t
/*ARGSUSED*/
vi_to_column(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_line.cursor = el->el_line.buffer;
	el->el_state.argument--;
	return ed_next_char(el, 0);
}

/* vi_yank_end():
 *	Vi yank to end of line
 *	[Y]
 */
protected el_action_t
/*ARGSUSED*/
vi_yank_end(EditLine *el, wint_t c __attribute__((__unused__)))
{

	cv_yank(el, el->el_line.cursor,
	    (int)(el->el_line.lastchar - el->el_line.cursor));
	return CC_REFRESH;
}

/* vi_yank():
 *	Vi yank
 *	[y]
 */
protected el_action_t
/*ARGSUSED*/
vi_yank(EditLine *el, wint_t c __attribute__((__unused__)))
{

	return cv_action(el, YANK);
}

/* vi_comment_out():
 *	Vi comment out current command
 *	[#]
 */
protected el_action_t
/*ARGSUSED*/
vi_comment_out(EditLine *el, wint_t c __attribute__((__unused__)))
{

	el->el_line.cursor = el->el_line.buffer;
	c_insert(el, 1);
	*el->el_line.cursor = '#';
	re_refresh(el);
	return ed_newline(el, 0);
}

/* vi_alias():
 *	Vi include shell alias
 *	[@]
 * NB: posix implies that we should enter insert mode, however
 * this is against historical precedent...
 */
protected el_action_t
/*ARGSUSED*/
vi_alias(EditLine *el, wint_t c __attribute__((__unused__)))
{
	char alias_name[3];
	const char *alias_text;

	if (el->el_chared.c_aliasfun == NULL)
		return CC_ERROR;

	alias_name[0] = '_';
	alias_name[2] = 0;
	if (el_getc(el, &alias_name[1]) != 1)
		return CC_ERROR;

	alias_text = (*el->el_chared.c_aliasfun)(el->el_chared.c_aliasarg,
	    alias_name);
	if (alias_text != NULL)
		FUN(el,push)(el, ct_decode_string(alias_text, &el->el_scratch));
	return CC_NORM;
}

/* vi_to_history_line():
 *	Vi go to specified history file line.
 *	[G]
 */
protected el_action_t
/*ARGSUSED*/
vi_to_history_line(EditLine *el, wint_t c __attribute__((__unused__)))
{
	int sv_event_no = el->el_history.eventno;
	el_action_t rval;


	if (el->el_history.eventno == 0) {
		 (void) Strncpy(el->el_history.buf, el->el_line.buffer,
		     EL_BUFSIZ);
		 el->el_history.last = el->el_history.buf +
			 (el->el_line.lastchar - el->el_line.buffer);
	}

	/* Lack of a 'count' means oldest, not 1 */
	if (!el->el_state.doingarg) {
		el->el_history.eventno = 0x7fffffff;
		hist_get(el);
	} else {
		/* This is brain dead, all the rest of this code counts
		 * upwards going into the past.  Here we need count in the
		 * other direction (to match the output of fc -l).
		 * I could change the world, but this seems to suffice.
		 */
		el->el_history.eventno = 1;
		if (hist_get(el) == CC_ERROR)
			return CC_ERROR;
		el->el_history.eventno = 1 + el->el_history.ev.num
					- el->el_state.argument;
		if (el->el_history.eventno < 0) {
			el->el_history.eventno = sv_event_no;
			return CC_ERROR;
		}
	}
	rval = hist_get(el);
	if (rval == CC_ERROR)
		el->el_history.eventno = sv_event_no;
	return rval;
}

/* vi_histedit():
 *	Vi edit history line with vi
 *	[v]
 */
protected el_action_t
/*ARGSUSED*/
vi_histedit(EditLine *el, wint_t c __attribute__((__unused__)))
{
	int fd;
	pid_t pid;
	ssize_t st;
	int status;
	char tempfile[] = "/tmp/histedit.XXXXXXXXXX";
	char *cp = NULL;
	size_t len;
	Char *line = NULL;

	if (el->el_state.doingarg) {
		if (vi_to_history_line(el, 0) == CC_ERROR)
			return CC_ERROR;
	}

	fd = mkstemp(tempfile);
	if (fd < 0)
		return CC_ERROR;
	len = (size_t)(el->el_line.lastchar - el->el_line.buffer);
#define TMP_BUFSIZ (EL_BUFSIZ * MB_LEN_MAX)
	cp = el_malloc(TMP_BUFSIZ * sizeof(*cp));
	if (cp == NULL)
		goto error;
	line = el_malloc(len * sizeof(*line) + 1);
	if (line == NULL)
		goto error;
	Strncpy(line, el->el_line.buffer, len);
	line[len] = '\0';
	ct_wcstombs(cp, line, TMP_BUFSIZ - 1);
	cp[TMP_BUFSIZ - 1] = '\0';
	len = strlen(cp);
	write(fd, cp, len);
	write(fd, "\n", (size_t)1);
	pid = fork();
	switch (pid) {
	case -1:
		goto error;
	case 0:
		close(fd);
		execlp("vi", "vi", tempfile, (char *)NULL);
		exit(0);
		/*NOTREACHED*/
	default:
		while (waitpid(pid, &status, 0) != pid)
			continue;
		lseek(fd, (off_t)0, SEEK_SET);
		st = read(fd, cp, TMP_BUFSIZ - 1);
		if (st > 0) {
			cp[st] = '\0';
			len = (size_t)(el->el_line.limit - el->el_line.buffer);
			len = ct_mbstowcs(el->el_line.buffer, cp, len);
			if (len > 0 && el->el_line.buffer[len - 1] == '\n')
				--len;
		}
		else
			len = 0;
                el->el_line.cursor = el->el_line.buffer;
                el->el_line.lastchar = el->el_line.buffer + len;
		el_free(cp);
                el_free(line);
		break;
	}

	close(fd);
	unlink(tempfile);
	/* return CC_REFRESH; */
	return ed_newline(el, 0);
error:
	el_free(line);
	el_free(cp);
	close(fd);
	unlink(tempfile);
	return CC_ERROR;
}

/* vi_history_word():
 *	Vi append word from previous input line
 *	[_]
 * Who knows where this one came from!
 * '_' in vi means 'entire current line', so 'cc' is a synonym for 'c_'
 */
protected el_action_t
/*ARGSUSED*/
vi_history_word(EditLine *el, wint_t c __attribute__((__unused__)))
{
	const Char *wp = HIST_FIRST(el);
	const Char *wep, *wsp;
	int len;
	Char *cp;
	const Char *lim;

	if (wp == NULL)
		return CC_ERROR;

	wep = wsp = NULL;
	do {
		while (Isspace(*wp))
			wp++;
		if (*wp == 0)
			break;
		wsp = wp;
		while (*wp && !Isspace(*wp))
			wp++;
		wep = wp;
	} while ((!el->el_state.doingarg || --el->el_state.argument > 0)
	    && *wp != 0);

	if (wsp == NULL || (el->el_state.doingarg && el->el_state.argument != 0))
		return CC_ERROR;

	cv_undo(el);
	len = (int)(wep - wsp);
	if (el->el_line.cursor < el->el_line.lastchar)
		el->el_line.cursor++;
	c_insert(el, len + 1);
	cp = el->el_line.cursor;
	lim = el->el_line.limit;
	if (cp < lim)
		*cp++ = ' ';
	while (wsp < wep && cp < lim)
		*cp++ = *wsp++;
	el->el_line.cursor = cp;

	el->el_map.current = el->el_map.key;
	return CC_REFRESH;
}

/* vi_redo():
 *	Vi redo last non-motion command
 *	[.]
 */
protected el_action_t
/*ARGSUSED*/
vi_redo(EditLine *el, wint_t c __attribute__((__unused__)))
{
	c_redo_t *r = &el->el_chared.c_redo;

	if (!el->el_state.doingarg && r->count) {
		el->el_state.doingarg = 1;
		el->el_state.argument = r->count;
	}

	el->el_chared.c_vcmd.pos = el->el_line.cursor;
	el->el_chared.c_vcmd.action = r->action;
	if (r->pos != r->buf) {
		if (r->pos + 1 > r->lim)
			/* sanity */
			r->pos = r->lim - 1;
		r->pos[0] = 0;
		FUN(el,push)(el, r->buf);
	}

	el->el_state.thiscmd = r->cmd;
	el->el_state.thisch = r->ch;
	return (*el->el_map.func[r->cmd])(el, r->ch);
}
