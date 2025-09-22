/*	$OpenBSD: terminal.h,v 1.6 2016/04/11 20:43:33 schwarze Exp $	*/
/*	$NetBSD: term.h,v 1.21 2009/12/30 22:37:40 christos Exp $	*/

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
 *	@(#)term.h	8.1 (Berkeley) 6/4/93
 */

/*
 * el.term.h: Termcap header
 */
#ifndef _h_el_terminal
#define	_h_el_terminal

typedef struct {		/* Symbolic function key bindings	*/
	const wchar_t	*name;	/* name of the key			*/
	int		 key;	/* Index in termcap table		*/
	keymacro_value_t	 fun;	/* Function bound to it			*/
	int		 type;	/* Type of function			*/
} funckey_t;

typedef struct {
	const char *t_name;		/* the terminal name	*/
	coord_t	  t_size;		/* # lines and cols	*/
	int	  t_flags;
#define	TERM_CAN_INSERT		0x001	/* Has insert cap	*/
#define	TERM_CAN_DELETE		0x002	/* Has delete cap	*/
#define	TERM_CAN_CEOL		0x004	/* Has CEOL cap		*/
#define	TERM_CAN_TAB		0x008	/* Can use tabs		*/
#define	TERM_CAN_ME		0x010	/* Can turn all attrs.	*/
#define	TERM_CAN_UP		0x020	/* Can move up		*/
#define	TERM_HAS_META		0x040	/* Has a meta key	*/
#define	TERM_HAS_AUTO_MARGINS	0x080	/* Has auto margins	*/
#define	TERM_HAS_MAGIC_MARGINS	0x100	/* Has magic margins	*/
	char	 *t_buf;		/* Termcap buffer	*/
	int	  t_loc;		/* location used	*/
	char	**t_str;		/* termcap strings	*/
	int	 *t_val;		/* termcap values	*/
	char	 *t_cap;		/* Termcap buffer	*/
	funckey_t	 *t_fkey;		/* Array of keys	*/
} el_terminal_t;

/*
 * fKey indexes
 */
#define	A_K_DN		0
#define	A_K_UP		1
#define	A_K_LT		2
#define	A_K_RT		3
#define	A_K_HO		4
#define	A_K_EN		5
#define	A_K_NKEYS	6

protected void	terminal_move_to_line(EditLine *, int);
protected void	terminal_move_to_char(EditLine *, int);
protected void	terminal_clear_EOL(EditLine *, int);
protected void	terminal_overwrite(EditLine *, const wchar_t *, size_t);
protected void	terminal_insertwrite(EditLine *, wchar_t *, int);
protected void	terminal_deletechars(EditLine *, int);
protected void	terminal_clear_screen(EditLine *);
protected void	terminal_beep(EditLine *);
protected int	terminal_change_size(EditLine *, int, int);
protected int	terminal_get_size(EditLine *, int *, int *);
protected int	terminal_init(EditLine *);
protected void	terminal_bind_arrow(EditLine *);
protected void	terminal_print_arrow(EditLine *, const wchar_t *);
protected int	terminal_clear_arrow(EditLine *, const wchar_t *);
protected int	terminal_set_arrow(EditLine *, const wchar_t *,
    keymacro_value_t *, int);
protected void	terminal_end(EditLine *);
protected void	terminal_get(EditLine *, const char **);
protected int	terminal_set(EditLine *, const char *);
protected int	terminal_settc(EditLine *, int, const wchar_t **);
protected int	terminal_gettc(EditLine *, int, char **);
protected int	terminal_telltc(EditLine *, int, const wchar_t **);
protected int	terminal_echotc(EditLine *, int, const wchar_t **);
protected void	terminal_writec(EditLine *, wint_t);
protected int	terminal__putc(EditLine *, wint_t);
protected void	terminal__flush(EditLine *);

/*
 * Easy access macros
 */
#define	EL_FLAGS	(el)->el_terminal.t_flags

#define	EL_CAN_INSERT		(EL_FLAGS & TERM_CAN_INSERT)
#define	EL_CAN_DELETE		(EL_FLAGS & TERM_CAN_DELETE)
#define	EL_CAN_CEOL		(EL_FLAGS & TERM_CAN_CEOL)
#define	EL_CAN_TAB		(EL_FLAGS & TERM_CAN_TAB)
#define	EL_CAN_ME		(EL_FLAGS & TERM_CAN_ME)
#define EL_CAN_UP		(EL_FLAGS & TERM_CAN_UP)
#define	EL_HAS_META		(EL_FLAGS & TERM_HAS_META)
#define	EL_HAS_AUTO_MARGINS	(EL_FLAGS & TERM_HAS_AUTO_MARGINS)
#define	EL_HAS_MAGIC_MARGINS	(EL_FLAGS & TERM_HAS_MAGIC_MARGINS)

#endif /* _h_el_terminal */
