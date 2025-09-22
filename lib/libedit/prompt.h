/*	$OpenBSD: prompt.h,v 1.10 2016/04/11 20:43:33 schwarze Exp $	*/
/*	$NetBSD: prompt.h,v 1.13 2016/02/17 19:47:49 christos Exp $	*/

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
 *	@(#)prompt.h	8.1 (Berkeley) 6/4/93
 */

/*
 * el.prompt.h: Prompt printing stuff
 */
#ifndef _h_el_prompt
#define	_h_el_prompt

typedef wchar_t    *(*el_pfunc_t)(EditLine *);

typedef struct el_prompt_t {
	el_pfunc_t	p_func;		/* Function to return the prompt */
	coord_t		p_pos;		/* position in the line after prompt */
	wchar_t		p_ignore;	/* character to start/end literal */
	int		p_wide;
} el_prompt_t;

protected void	prompt_print(EditLine *, int);
protected int	prompt_set(EditLine *, el_pfunc_t, wchar_t, int, int);
protected int	prompt_get(EditLine *, el_pfunc_t *, wchar_t *, int);
protected int	prompt_init(EditLine *);
protected void	prompt_end(EditLine *);

#endif /* _h_el_prompt */
