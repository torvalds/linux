/*	$OpenBSD: wsfont.h,v 1.12 2019/07/11 18:07:54 mpi Exp $ */
/*	$NetBSD: wsfont.h,v 1.12 2000/06/13 13:37:07 ad Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _WSFONT_H_
#define _WSFONT_H_ 1

/*
 * wsfont_find() can be called with any of the parameters as 0, meaning we
 * don't care about that aspect of the font. It returns a cookie which
 * we can use with the other functions. When more flexibility is required,
 * wsfont_enum() should be used. The last two parameters to wsfont_lock()
 * are the bit order and byte order required (WSDISPLAY_FONTORDER_L2R or
 * WSDISPLAY_FONTORDER_R2L).
 *
 * Example:
 *
 *	struct wsdisplay_font *font;
 *	int cookie;
 *
 *	if ((cookie = wsfont_find(NULL, 8, 16, 0, 0)) <= 0)
 *		panic("unable to get 8x16 font");
 *
 *	if (wsfont_lock(cookie, &font, WSDISPLAY_FONTORDER_L2R,
 *	    WSDISPLAY_FONTORDER_R2L) <= 0)
 *		panic("unable to lock font");
 *
 *	... do stuff ...
 *
 *	wsfont_unlock(cookie);
 */

struct wsdisplay_font;

/* For wsfont_add() */
#define WSFONT_BUILTIN	(0x01)
#define WSFONT_STATIC	(0x02)

/* wsfont.c */
void	wsfont_init(void);
int	wsfont_find(const char *, int, int, int);
int	wsfont_add(struct wsdisplay_font *, int);
void	wsfont_enum(int (*)(void *, struct wsdisplay_font *), void *);
int	wsfont_lock(int, struct wsdisplay_font **, int, int);
int	wsfont_unlock(int);
int	wsfont_map_unichar(struct wsdisplay_font *, int);
int	wsfont_rotate(int, int);

#endif	/* !_WSFONT_H_ */
