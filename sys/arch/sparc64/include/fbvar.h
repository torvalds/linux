/*	$OpenBSD: fbvar.h,v 1.8 2008/12/29 22:07:35 miod Exp $	*/
/*	$NetBSD: fbvar.h,v 1.9 1997/07/07 23:31:30 pk Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)fbvar.h	8.1 (Berkeley) 6/11/93
 */

/*
 * Common frame buffer variables.
 * All framebuffer softc structures must start with such a structure.
 */
struct sunfb {
	struct	device sf_dev;		/* base device */

	int	sf_width;
	int	sf_height;
	int	sf_depth;
	int	sf_linebytes;

	int	sf_fbsize;		/* sf_height * sf_linebytes */

	int	*sf_crowp, *sf_ccolp;	/* PROM cursor position */

	struct	rasops_info sf_ro;

	struct	wsscreen_descr sf_wsd;
	struct	wsscreen_list sf_wsl;
	struct	wsscreen_descr *sf_scrlist[1];
	int	sf_nscreens;
};

/*
 * Selected framebuffer node on OBP systems if k/d console.
 */
extern int fbnode;

void	fb_setsize(struct sunfb*, int, int, int, int, int);
void	fbwscons_init(struct sunfb *, int, int);
void	fbwscons_console_init(struct sunfb *, int);
void	fbwscons_setcolormap(struct sunfb *,
    void (*)(void *, u_int, u_int8_t, u_int8_t, u_int8_t));
void	fbwscons_attach(struct sunfb *,	struct wsdisplay_accessops *, int);

int	ifb_ident(void *);
