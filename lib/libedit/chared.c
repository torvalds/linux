/*	$OpenBSD: chared.c,v 1.28 2017/04/12 18:24:37 tb Exp $	*/
/*	$NetBSD: chared.c,v 1.28 2009/12/30 22:37:40 christos Exp $	*/

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

/*
 * chared.c: Character editor utilities
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "el.h"
#include "common.h"
#include "fcns.h"

/* value to leave unused in line buffer */
#define	EL_LEAVE	2

/* cv_undo():
 *	Handle state for the vi undo command
 */
protected void
cv_undo(EditLine *el)
{
	c_undo_t *vu = &el->el_chared.c_undo;
	c_redo_t *r = &el->el_chared.c_redo;
	size_t size;

	/* Save entire line for undo */
	size = el->el_line.lastchar - el->el_line.buffer;
	vu->len = size;
	vu->cursor = (int)(el->el_line.cursor - el->el_line.buffer);
	(void)memcpy(vu->buf, el->el_line.buffer, size * sizeof(*vu->buf));

	/* save command info for redo */
	r->count = el->el_state.doingarg ? el->el_state.argument : 0;
	r->action = el->el_chared.c_vcmd.action;
	r->pos = r->buf;
	r->cmd = el->el_state.thiscmd;
	r->ch = el->el_state.thisch;
}

/* cv_yank():
 *	Save yank/delete data for paste
 */
protected void
cv_yank(EditLine *el, const wchar_t *ptr, int size)
{
	c_kill_t *k = &el->el_chared.c_kill;

	(void)memcpy(k->buf, ptr, size * sizeof(*k->buf));
	k->last = k->buf + size;
}


/* c_insert():
 *	Insert num characters
 */
protected void
c_insert(EditLine *el, int num)
{
	wchar_t *cp;

	if (el->el_line.lastchar + num >= el->el_line.limit) {
		if (!ch_enlargebufs(el, (size_t)num))
			return;		/* can't go past end of buffer */
	}

	if (el->el_line.cursor < el->el_line.lastchar) {
		/* if I must move chars */
		for (cp = el->el_line.lastchar; cp >= el->el_line.cursor; cp--)
			cp[num] = *cp;
	}
	el->el_line.lastchar += num;
}


/* c_delafter():
 *	Delete num characters after the cursor
 */
protected void
c_delafter(EditLine *el, int num)
{

	if (el->el_line.cursor + num > el->el_line.lastchar)
		num = (int)(el->el_line.lastchar - el->el_line.cursor);

	if (el->el_map.current != el->el_map.emacs) {
		cv_undo(el);
		cv_yank(el, el->el_line.cursor, num);
	}

	if (num > 0) {
		wchar_t *cp;

		for (cp = el->el_line.cursor; cp <= el->el_line.lastchar; cp++)
			*cp = cp[num];

		el->el_line.lastchar -= num;
	}
}


/* c_delafter1():
 *	Delete the character after the cursor, do not yank
 */
protected void
c_delafter1(EditLine *el)
{
	wchar_t *cp;

	for (cp = el->el_line.cursor; cp <= el->el_line.lastchar; cp++)
		*cp = cp[1];

	el->el_line.lastchar--;
}


/* c_delbefore():
 *	Delete num characters before the cursor
 */
protected void
c_delbefore(EditLine *el, int num)
{

	if (el->el_line.cursor - num < el->el_line.buffer)
		num = (int)(el->el_line.cursor - el->el_line.buffer);

	if (el->el_map.current != el->el_map.emacs) {
		cv_undo(el);
		cv_yank(el, el->el_line.cursor - num, num);
	}

	if (num > 0) {
		wchar_t *cp;

		for (cp = el->el_line.cursor - num;
		    cp <= el->el_line.lastchar;
		    cp++)
			*cp = cp[num];

		el->el_line.lastchar -= num;
	}
}


/* c_delbefore1():
 *	Delete the character before the cursor, do not yank
 */
protected void
c_delbefore1(EditLine *el)
{
	wchar_t *cp;

	for (cp = el->el_line.cursor - 1; cp <= el->el_line.lastchar; cp++)
		*cp = cp[1];

	el->el_line.lastchar--;
}


/* ce__isword():
 *	Return if p is part of a word according to emacs
 */
protected int
ce__isword(wint_t p)
{
	return iswalnum(p) || wcschr(L"*?_-.[]~=", p) != NULL;
}


/* cv__isword():
 *	Return if p is part of a word according to vi
 */
protected int
cv__isword(wint_t p)
{
	if (iswalnum(p) || p == L'_')
		return 1;
	if (iswgraph(p))
		return 2;
	return 0;
}


/* cv__isWord():
 *	Return if p is part of a big word according to vi
 */
protected int
cv__isWord(wint_t p)
{
	return !iswspace(p);
}


/* c__prev_word():
 *	Find the previous word
 */
protected wchar_t *
c__prev_word(wchar_t *p, wchar_t *low, int n, int (*wtest)(wint_t))
{
	p--;

	while (n--) {
		while ((p >= low) && !(*wtest)(*p))
			p--;
		while ((p >= low) && (*wtest)(*p))
			p--;
	}

	/* cp now points to one character before the word */
	p++;
	if (p < low)
		p = low;
	/* cp now points where we want it */
	return p;
}


/* c__next_word():
 *	Find the next word
 */
protected wchar_t *
c__next_word(wchar_t *p, wchar_t *high, int n, int (*wtest)(wint_t))
{
	while (n--) {
		while ((p < high) && !(*wtest)(*p))
			p++;
		while ((p < high) && (*wtest)(*p))
			p++;
	}
	if (p > high)
		p = high;
	/* p now points where we want it */
	return p;
}

/* cv_next_word():
 *	Find the next word vi style
 */
protected wchar_t *
cv_next_word(EditLine *el, wchar_t *p, wchar_t *high, int n,
    int (*wtest)(wint_t))
{
	int test;

	while (n--) {
		test = (*wtest)(*p);
		while ((p < high) && (*wtest)(*p) == test)
			p++;
		/*
		 * vi historically deletes with cw only the word preserving the
		 * trailing whitespace! This is not what 'w' does..
		 */
		if (n || el->el_chared.c_vcmd.action != (DELETE|INSERT))
			while ((p < high) && iswspace(*p))
				p++;
	}

	/* p now points where we want it */
	if (p > high)
		return high;
	else
		return p;
}


/* cv_prev_word():
 *	Find the previous word vi style
 */
protected wchar_t *
cv_prev_word(wchar_t *p, wchar_t *low, int n, int (*wtest)(wint_t))
{
	int test;

	p--;
	while (n--) {
		while ((p > low) && iswspace(*p))
			p--;
		test = (*wtest)(*p);
		while ((p >= low) && (*wtest)(*p) == test)
			p--;
	}
	p++;

	/* p now points where we want it */
	if (p < low)
		return low;
	else
		return p;
}


/* cv_delfini():
 *	Finish vi delete action
 */
protected void
cv_delfini(EditLine *el)
{
	int size;
	int action = el->el_chared.c_vcmd.action;

	if (action & INSERT)
		el->el_map.current = el->el_map.key;

	if (el->el_chared.c_vcmd.pos == 0)
		/* sanity */
		return;

	size = (int)(el->el_line.cursor - el->el_chared.c_vcmd.pos);
	if (size == 0)
		size = 1;
	el->el_line.cursor = el->el_chared.c_vcmd.pos;
	if (action & YANK) {
		if (size > 0)
			cv_yank(el, el->el_line.cursor, size);
		else
			cv_yank(el, el->el_line.cursor + size, -size);
	} else {
		if (size > 0) {
			c_delafter(el, size);
			re_refresh_cursor(el);
		} else  {
			c_delbefore(el, -size);
			el->el_line.cursor += size;
		}
	}
	el->el_chared.c_vcmd.action = NOP;
}


/* cv__endword():
 *	Go to the end of this word according to vi
 */
protected wchar_t *
cv__endword(wchar_t *p, wchar_t *high, int n, int (*wtest)(wint_t))
{
	int test;

	p++;

	while (n--) {
		while ((p < high) && iswspace(*p))
			p++;

		test = (*wtest)(*p);
		while ((p < high) && (*wtest)(*p) == test)
			p++;
	}
	p--;
	return p;
}

/* ch_init():
 *	Initialize the character editor
 */
protected int
ch_init(EditLine *el)
{
	el->el_line.buffer = calloc(EL_BUFSIZ, sizeof(*el->el_line.buffer));
	if (el->el_line.buffer == NULL)
		return -1;
	el->el_line.cursor = el->el_line.buffer;
	el->el_line.lastchar = el->el_line.buffer;
	el->el_line.limit = &el->el_line.buffer[EL_BUFSIZ - EL_LEAVE];

	el->el_chared.c_undo.buf = calloc(EL_BUFSIZ,
	    sizeof(*el->el_chared.c_undo.buf));
	if (el->el_chared.c_undo.buf == NULL)
		return -1;
	el->el_chared.c_undo.len = -1;
	el->el_chared.c_undo.cursor = 0;

	el->el_chared.c_redo.buf = reallocarray(NULL, EL_BUFSIZ,
	    sizeof(*el->el_chared.c_redo.buf));
	if (el->el_chared.c_redo.buf == NULL)
		return -1;
	el->el_chared.c_redo.pos = el->el_chared.c_redo.buf;
	el->el_chared.c_redo.lim = el->el_chared.c_redo.buf + EL_BUFSIZ;
	el->el_chared.c_redo.cmd = ED_UNASSIGNED;

	el->el_chared.c_vcmd.action = NOP;
	el->el_chared.c_vcmd.pos = el->el_line.buffer;

	el->el_chared.c_kill.buf = calloc(EL_BUFSIZ,
	    sizeof(*el->el_chared.c_kill.buf));
	if (el->el_chared.c_kill.buf == NULL)
		return -1;
	el->el_chared.c_kill.mark = el->el_line.buffer;
	el->el_chared.c_kill.last = el->el_chared.c_kill.buf;
	el->el_chared.c_resizefun = NULL;
	el->el_chared.c_resizearg = NULL;

	el->el_map.current = el->el_map.key;

	el->el_state.inputmode = MODE_INSERT; /* XXX: save a default */
	el->el_state.doingarg = 0;
	el->el_state.metanext = 0;
	el->el_state.argument = 1;
	el->el_state.lastcmd = ED_UNASSIGNED;

	return 0;
}

/* ch_reset():
 *	Reset the character editor
 */
protected void
ch_reset(EditLine *el)
{
	el->el_line.cursor		= el->el_line.buffer;
	el->el_line.lastchar		= el->el_line.buffer;

	el->el_chared.c_undo.len	= -1;
	el->el_chared.c_undo.cursor	= 0;

	el->el_chared.c_vcmd.action	= NOP;
	el->el_chared.c_vcmd.pos	= el->el_line.buffer;

	el->el_chared.c_kill.mark	= el->el_line.buffer;

	el->el_map.current		= el->el_map.key;

	el->el_state.inputmode		= MODE_INSERT; /* XXX: save a default */
	el->el_state.doingarg		= 0;
	el->el_state.metanext		= 0;
	el->el_state.argument		= 1;
	el->el_state.lastcmd		= ED_UNASSIGNED;

	el->el_history.eventno		= 0;
}

/* ch_enlargebufs():
 *	Enlarge line buffer to be able to hold twice as much characters.
 *	Returns 1 if successful, 0 if not.
 */
protected int
ch_enlargebufs(EditLine *el, size_t addlen)
{
	size_t sz, newsz;
	wchar_t *newbuffer, *oldbuf, *oldkbuf;

	sz = el->el_line.limit - el->el_line.buffer + EL_LEAVE;
	newsz = sz * 2;
	/*
	 * If newly required length is longer than current buffer, we need
	 * to make the buffer big enough to hold both old and new stuff.
	 */
	if (addlen > sz) {
		while(newsz - sz < addlen)
			newsz *= 2;
	}

	/*
	 * Reallocate line buffer.
	 */
	newbuffer = recallocarray(el->el_line.buffer, sz, newsz,
	    sizeof(*newbuffer));
	if (!newbuffer)
		return 0;

	oldbuf = el->el_line.buffer;

	el->el_line.buffer = newbuffer;
	el->el_line.cursor = newbuffer + (el->el_line.cursor - oldbuf);
	el->el_line.lastchar = newbuffer + (el->el_line.lastchar - oldbuf);
	/* don't set new size until all buffers are enlarged */
	el->el_line.limit  = &newbuffer[sz - EL_LEAVE];

	/*
	 * Reallocate kill buffer.
	 */
	newbuffer = recallocarray(el->el_chared.c_kill.buf, sz, newsz,
	    sizeof(*newbuffer));
	if (!newbuffer)
		return 0;

	oldkbuf = el->el_chared.c_kill.buf;

	el->el_chared.c_kill.buf = newbuffer;
	el->el_chared.c_kill.last = newbuffer +
					(el->el_chared.c_kill.last - oldkbuf);
	el->el_chared.c_kill.mark = el->el_line.buffer +
					(el->el_chared.c_kill.mark - oldbuf);

	/*
	 * Reallocate undo buffer.
	 */
	newbuffer = recallocarray(el->el_chared.c_undo.buf, sz, newsz,
	    sizeof(*newbuffer));
	if (!newbuffer)
		return 0;
	el->el_chared.c_undo.buf = newbuffer;

	newbuffer = reallocarray(el->el_chared.c_redo.buf,
	    newsz, sizeof(*newbuffer));
	if (!newbuffer)
		return 0;
	el->el_chared.c_redo.pos = newbuffer +
			(el->el_chared.c_redo.pos - el->el_chared.c_redo.buf);
	el->el_chared.c_redo.lim = newbuffer +
			(el->el_chared.c_redo.lim - el->el_chared.c_redo.buf);
	el->el_chared.c_redo.buf = newbuffer;

	if (!hist_enlargebuf(el, sz, newsz))
		return 0;

	/* Safe to set enlarged buffer size */
	el->el_line.limit  = &el->el_line.buffer[newsz - EL_LEAVE];
	if (el->el_chared.c_resizefun)
		(*el->el_chared.c_resizefun)(el, el->el_chared.c_resizearg);
	return 1;
}

/* ch_end():
 *	Free the data structures used by the editor
 */
protected void
ch_end(EditLine *el)
{
	free(el->el_line.buffer);
	el->el_line.buffer = NULL;
	el->el_line.limit = NULL;
	free(el->el_chared.c_undo.buf);
	el->el_chared.c_undo.buf = NULL;
	free(el->el_chared.c_redo.buf);
	el->el_chared.c_redo.buf = NULL;
	el->el_chared.c_redo.pos = NULL;
	el->el_chared.c_redo.lim = NULL;
	el->el_chared.c_redo.cmd = ED_UNASSIGNED;
	free(el->el_chared.c_kill.buf);
	el->el_chared.c_kill.buf = NULL;
	ch_reset(el);
}


/* el_insertstr():
 *	Insert string at cursorI
 */
int
el_winsertstr(EditLine *el, const wchar_t *s)
{
	size_t len;

	if ((len = wcslen(s)) == 0)
		return -1;
	if (el->el_line.lastchar + len >= el->el_line.limit) {
		if (!ch_enlargebufs(el, len))
			return -1;
	}

	c_insert(el, (int)len);
	while (*s)
		*el->el_line.cursor++ = *s++;
	return 0;
}


/* el_deletestr():
 *	Delete num characters before the cursor
 */
void
el_deletestr(EditLine *el, int n)
{
	if (n <= 0)
		return;

	if (el->el_line.cursor < &el->el_line.buffer[n])
		return;

	c_delbefore(el, n);		/* delete before dot */
	el->el_line.cursor -= n;
	if (el->el_line.cursor < el->el_line.buffer)
		el->el_line.cursor = el->el_line.buffer;
}

/* c_gets():
 *	Get a string
 */
protected int
c_gets(EditLine *el, wchar_t *buf, const wchar_t *prompt)
{
	ssize_t len;
	wchar_t *cp = el->el_line.buffer, ch;

	if (prompt) {
		len = wcslen(prompt);
		(void)memcpy(cp, prompt, len * sizeof(*cp));
		cp += len;
	}
	len = 0;

	for (;;) {
		el->el_line.cursor = cp;
		*cp = ' ';
		el->el_line.lastchar = cp + 1;
		re_refresh(el);

		if (el_wgetc(el, &ch) != 1) {
			ed_end_of_file(el, 0);
			len = -1;
			break;
		}

		switch (ch) {

		case L'\b':	/* Delete and backspace */
		case 0177:
			if (len == 0) {
				len = -1;
				break;
			}
			len--;
			cp--;
			continue;

		case 0033:	/* ESC */
		case L'\r':	/* Newline */
		case L'\n':
			buf[len] = ch;
			break;

		default:
			if (len >= EL_BUFSIZ - 16)
				terminal_beep(el);
			else {
				buf[len++] = ch;
				*cp++ = ch;
			}
			continue;
		}
		break;
	}

	el->el_line.buffer[0] = '\0';
	el->el_line.lastchar = el->el_line.buffer;
	el->el_line.cursor = el->el_line.buffer;
	return (int)len;
}


/* c_hpos():
 *	Return the current horizontal position of the cursor
 */
protected int
c_hpos(EditLine *el)
{
	wchar_t *ptr;

	/*
	 * Find how many characters till the beginning of this line.
	 */
	if (el->el_line.cursor == el->el_line.buffer)
		return 0;
	else {
		for (ptr = el->el_line.cursor - 1;
		     ptr >= el->el_line.buffer && *ptr != '\n';
		     ptr--)
			continue;
		return (int)(el->el_line.cursor - ptr - 1);
	}
}

protected int
ch_resizefun(EditLine *el, el_zfunc_t f, void *a)
{
	el->el_chared.c_resizefun = f;
	el->el_chared.c_resizearg = a;
	return 0;
}
