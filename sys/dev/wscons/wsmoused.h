/* $OpenBSD: wsmoused.h,v 1.10 2014/10/27 13:55:05 mpi Exp $ */

/*
 * Copyright (c) 2001 Jean-Baptiste Marchand, Julien Montagne and Jerome Verdon
 *
 * All rights reserved.
 *
 * This code is for mouse console support under the wscons console driver.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by
 *	Hellmuth Michaelis, Brian Dunford-Shore, Joerg Wunsch, Scott Turner
 *	and Charles Hannum.
 * 4. The name authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

int	wsmoused(struct wsdisplay_softc *, caddr_t, int, struct proc *);

void	motion_event(struct wsscreen *, u_int, int);
void	button_event(struct wsscreen *, int, int);
int	ctrl_event(struct wsdisplay_softc *, u_int, int, struct proc *);

void	mouse_moverel(struct wsscreen *, int, int);

void	inverse_char(struct wsscreen *, u_int);
void	inverse_region(struct wsscreen *, u_int, u_int);

u_int	skip_spc_right(struct wsscreen *, int);
u_int	skip_spc_left(struct wsscreen *);
u_int	skip_char_right(struct wsscreen *, u_int);
u_int	skip_char_left(struct wsscreen *, u_int);
u_int	class_cmp(struct wsscreen *, u_int, u_int);

void	mouse_copy_start(struct wsscreen *);
void	mouse_copy_word(struct wsscreen *);
void	mouse_copy_line(struct wsscreen *);
void	mouse_copy_end(struct wsscreen *);
void	mouse_copy_extend(struct wsscreen *);
void	mouse_copy_extend_char(struct wsscreen *);
void	mouse_copy_extend_word(struct wsscreen *);
void	mouse_copy_extend_line(struct wsscreen *);
void	mouse_copy_extend_after(struct wsscreen *);
void	mouse_hide(struct wsscreen *);
void	remove_selection(struct wsscreen *);
void	mouse_copy_selection(struct wsscreen *);
void	mouse_paste(struct wsscreen *);

void	mouse_zaxis(struct wsscreen *, int);
void	allocate_copybuffer(struct wsdisplay_softc *);
void	mouse_remove(struct wsscreen *);

#define NO_BORDER 0
#define BORDER 1

#define N_COLS(dconf) 	((dconf)->scrdata->ncols)
#define N_ROWS(dconf) 	((dconf)->scrdata->nrows)

#define WS_NCOLS(scr)	N_COLS((scr)->scr_dconf)
#define WS_NROWS(scr)	N_ROWS((scr)->scr_dconf)

#define MAXCOL(dconf)	(N_COLS(dconf) - 1)
#define MAXROW(dconf)	(N_ROWS(dconf) - 1)

/* Shortcuts to the various display operations */
#define	GETCHAR(scr, pos, cellp) \
	((*(scr)->sc->sc_accessops->getchar) \
	    ((scr)->sc->sc_accesscookie, (pos) / N_COLS((scr)->scr_dconf), \
	     (pos) % N_COLS((scr)->scr_dconf), cellp))
#define PUTCHAR(dconf, pos, uc, attr) \
	((*(dconf)->emulops->putchar) \
	    ((dconf)->emulcookie, ((pos) / N_COLS(dconf)), \
	    ((pos) % N_COLS(dconf)), (uc), (attr)))

#define MOUSE_COPY_BUTTON 	0
#define MOUSE_PASTE_BUTTON 	1
#define MOUSE_EXTEND_BUTTON	2

#define IS_ALPHANUM(c) ((c) != ' ')
#define IS_SPACE(c) ((c) == ' ')
