/*	$NetBSD: chared.h,v 1.27 2016/02/16 22:53:14 christos Exp $	*/

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
 *
 *	@(#)chared.h	8.1 (Berkeley) 6/4/93
 * $FreeBSD$
 */

/*
 * el.chared.h: Character editor interface
 */
#ifndef _h_el_chared
#define	_h_el_chared

#define	EL_MAXMACRO	10

/*
 * This is an issue of basic "vi" look-and-feel. Defining VI_MOVE works
 * like real vi: i.e. the transition from command<->insert modes moves
 * the cursor.
 *
 * On the other hand we really don't want to move the cursor, because
 * all the editing commands don't include the character under the cursor.
 * Probably the best fix is to make all the editing commands aware of
 * this fact.
 */
#define	VI_MOVE


typedef struct c_macro_t {
	int	  level;
	int	  offset;
	Char	**macro;
} c_macro_t;

/*
 * Undo information for vi - no undo in emacs (yet)
 */
typedef struct c_undo_t {
	ssize_t	 len;			/* length of saved line */
	int	 cursor;		/* position of saved cursor */
	Char	*buf;			/* full saved text */
} c_undo_t;

/* redo for vi */
typedef struct c_redo_t {
	Char	*buf;			/* redo insert key sequence */
	Char	*pos;
	Char	*lim;
	el_action_t	cmd;		/* command to redo */
	Char	ch;			/* char that invoked it */
	int	count;
	int	action;			/* from cv_action() */
} c_redo_t;

/*
 * Current action information for vi
 */
typedef struct c_vcmd_t {
	int	 action;
	Char	*pos;
} c_vcmd_t;

/*
 * Kill buffer for emacs
 */
typedef struct c_kill_t {
	Char	*buf;
	Char	*last;
	Char	*mark;
} c_kill_t;

typedef void (*el_zfunc_t)(EditLine *, void *);
typedef const char *(*el_afunc_t)(void *, const char *);

/*
 * Note that we use both data structures because the user can bind
 * commands from both editors!
 */
typedef struct el_chared_t {
	c_undo_t	c_undo;
	c_kill_t	c_kill;
	c_redo_t	c_redo;
	c_vcmd_t	c_vcmd;
	c_macro_t	c_macro;
	el_zfunc_t	c_resizefun;
	el_afunc_t	c_aliasfun;
	void *		c_resizearg;
	void *		c_aliasarg;
} el_chared_t;


#define	STRQQ		"\"\""

#define	isglob(a)	(strchr("*[]?", (a)) != NULL)

#define	NOP		0x00
#define	DELETE		0x01
#define	INSERT		0x02
#define	YANK		0x04

#define	CHAR_FWD	(+1)
#define	CHAR_BACK	(-1)

#define	MODE_INSERT	0
#define	MODE_REPLACE	1
#define	MODE_REPLACE_1	2


protected int	 cv__isword(wint_t);
protected int	 cv__isWord(wint_t);
protected void	 cv_delfini(EditLine *);
protected Char	*cv__endword(Char *, Char *, int, int (*)(wint_t));
protected int	 ce__isword(wint_t);
protected void	 cv_undo(EditLine *);
protected void	 cv_yank(EditLine *, const Char *, int);
protected Char	*cv_next_word(EditLine*, Char *, Char *, int, int (*)(wint_t));
protected Char	*cv_prev_word(Char *, Char *, int, int (*)(wint_t));
protected Char	*c__next_word(Char *, Char *, int, int (*)(wint_t));
protected Char	*c__prev_word(Char *, Char *, int, int (*)(wint_t));
protected void	 c_insert(EditLine *, int);
protected void	 c_delbefore(EditLine *, int);
protected void	 c_delbefore1(EditLine *);
protected void	 c_delafter(EditLine *, int);
protected void	 c_delafter1(EditLine *);
protected int	 c_gets(EditLine *, Char *, const Char *);
protected int	 c_hpos(EditLine *);

protected int	 ch_init(EditLine *);
protected void	 ch_reset(EditLine *, int);
protected int	 ch_resizefun(EditLine *, el_zfunc_t, void *);
protected int	 ch_aliasfun(EditLine *, el_afunc_t, void *);
protected int	 ch_enlargebufs(EditLine *, size_t);
protected void	 ch_end(EditLine *);

#endif /* _h_el_chared */
