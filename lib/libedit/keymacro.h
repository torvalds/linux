/*	$OpenBSD: keymacro.h,v 1.4 2016/04/12 09:04:02 schwarze Exp $	*/
/*	$NetBSD: keymacro.h,v 1.5 2016/04/12 00:16:06 christos Exp $	*/

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
 *	@(#)key.h	8.1 (Berkeley) 6/4/93
 */

/*
 * el.keymacro.h: Key macro header
 */
#ifndef _h_el_keymacro
#define	_h_el_keymacro

typedef union keymacro_value_t {
	el_action_t	 cmd;	/* If it is a command the #	*/
	wchar_t		*str;	/* If it is a string...		*/
} keymacro_value_t;

typedef struct keymacro_node_t keymacro_node_t;

typedef struct el_keymacro_t {
	wchar_t		*buf;	/* Key print buffer		*/
	keymacro_node_t	*map;	/* Key map			*/
	keymacro_value_t val;	/* Local conversion buffer	*/
} el_keymacro_t;

#define	XK_CMD	0
#define	XK_STR	1
#define	XK_NOD	2

protected int keymacro_init(EditLine *);
protected void keymacro_end(EditLine *);
protected keymacro_value_t *keymacro_map_cmd(EditLine *, int);
protected keymacro_value_t *keymacro_map_str(EditLine *, wchar_t *);
protected void keymacro_reset(EditLine *);
protected int keymacro_get(EditLine *, wchar_t *, keymacro_value_t *);
protected void keymacro_add(EditLine *, const wchar_t *,
    keymacro_value_t *, int);
protected void keymacro_clear(EditLine *, el_action_t *, const wchar_t *);
protected int keymacro_delete(EditLine *, const wchar_t *);
protected void keymacro_print(EditLine *, const wchar_t *);
protected void keymacro_kprint(EditLine *, const wchar_t *,
    keymacro_value_t *, int);
protected size_t keymacro__decode_str(const wchar_t *, char *, size_t,
    const char *);

#endif /* _h_el_keymacro */
