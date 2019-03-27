/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sascha Wildner <saw@online.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/signalvar.h>
#include <sys/tty.h>
#include <sys/kernel.h>
#include <sys/fbio.h>
#include <sys/consio.h>
#include <sys/filedesc.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/mutex.h>
#include <sys/proc.h>

#include <dev/fb/fbreg.h>
#include <dev/syscons/syscons.h>

SET_DECLARE(scrndr_set, const sc_renderer_t);

/* for compatibility with previous versions */
/* 3.0-RELEASE used the following structure */
typedef struct old_video_adapter {
    int			va_index;
    int			va_type;
    int			va_flags;
/* flag bits are the same as the -CURRENT
#define V_ADP_COLOR	(1<<0)
#define V_ADP_MODECHANGE (1<<1)
#define V_ADP_STATESAVE	(1<<2)
#define V_ADP_STATELOAD	(1<<3)
#define V_ADP_FONT	(1<<4)
#define V_ADP_PALETTE	(1<<5)
#define V_ADP_BORDER	(1<<6)
#define V_ADP_VESA	(1<<7)
*/
    int			va_crtc_addr;
    u_int		va_window;	/* virtual address */
    size_t		va_window_size;
    size_t		va_window_gran;
    u_int		va_buffer;	/* virtual address */
    size_t		va_buffer_size;
    int			va_initial_mode;
    int			va_initial_bios_mode;
    int			va_mode;
} old_video_adapter_t;

#define OLD_CONS_ADPINFO _IOWR('c', 101, old_video_adapter_t)

/* 3.1-RELEASE used the following structure */
typedef struct old_video_adapter_info {
    int			va_index;
    int			va_type;
    char		va_name[16];
    int			va_unit;
    int			va_flags;
    int			va_io_base;
    int			va_io_size;
    int			va_crtc_addr;
    int			va_mem_base;
    int			va_mem_size;
    u_int		va_window;	/* virtual address */
    size_t		va_window_size;
    size_t		va_window_gran;
    u_int		va_buffer;
    size_t		va_buffer_size;
    int			va_initial_mode;
    int			va_initial_bios_mode;
    int			va_mode;
    int			va_line_width;
} old_video_adapter_info_t;

#define OLD_CONS_ADPINFO2 _IOWR('c', 101, old_video_adapter_info_t)

/* 3.0-RELEASE and 3.1-RELEASE used the following structure */
typedef struct old_video_info {
    int			vi_mode;
    int			vi_flags;
/* flag bits are the same as the -CURRENT
#define V_INFO_COLOR	(1<<0)
#define V_INFO_GRAPHICS	(1<<1)
#define V_INFO_LINEAR	(1<<2)
#define V_INFO_VESA	(1<<3)
*/
    int			vi_width;
    int			vi_height;
    int			vi_cwidth;
    int			vi_cheight;
    int			vi_depth;
    int			vi_planes;
    u_int		vi_window;	/* physical address */
    size_t		vi_window_size;
    size_t		vi_window_gran;
    u_int		vi_buffer;	/* physical address */
    size_t		vi_buffer_size;
} old_video_info_t;

#define OLD_CONS_MODEINFO _IOWR('c', 102, old_video_info_t)
#define OLD_CONS_FINDMODE _IOWR('c', 103, old_video_info_t)

int
sc_set_text_mode(scr_stat *scp, struct tty *tp, int mode, int xsize, int ysize,
		 int fontsize, int fontwidth)
{
    video_info_t info;
    struct winsize wsz;
    u_char *font;
    int prev_ysize;
    int error;
    int s;

    if (vidd_get_info(scp->sc->adp, mode, &info))
	return ENODEV;

    /* adjust argument values */
    if (fontwidth <= 0)
	fontwidth = info.vi_cwidth;
    if (fontsize <= 0)
	fontsize = info.vi_cheight;
    if (fontsize < 14)
	fontsize = 8;
    else if (fontsize >= 16)
	fontsize = 16;
    else
	fontsize = 14;
#ifndef SC_NO_FONT_LOADING
    switch (fontsize) {
    case 8:
	if ((scp->sc->fonts_loaded & FONT_8) == 0)
	    return (EINVAL);
	font = scp->sc->font_8;
	break;
    case 14:
	if ((scp->sc->fonts_loaded & FONT_14) == 0)
	    return (EINVAL);
	font = scp->sc->font_14;
	break;
    case 16:
	if ((scp->sc->fonts_loaded & FONT_16) == 0)
	    return (EINVAL);
	font = scp->sc->font_16;
	break;
    }
#else
    font = NULL;
#endif
    if ((xsize <= 0) || (xsize > info.vi_width))
	xsize = info.vi_width;
    if ((ysize <= 0) || (ysize > info.vi_height))
	ysize = info.vi_height;

    /* stop screen saver, etc */
    s = spltty();
    if ((error = sc_clean_up(scp))) {
	splx(s);
	return error;
    }

    if (sc_render_match(scp, scp->sc->adp->va_name, 0) == NULL) {
	splx(s);
	return ENODEV;
    }

    /* set up scp */
#ifndef SC_NO_HISTORY
    if (scp->history != NULL)
	sc_hist_save(scp);
#endif
    prev_ysize = scp->ysize;
    /*
     * This is a kludge to fend off scrn_update() while we
     * muck around with scp. XXX
     */
    scp->status |= UNKNOWN_MODE | MOUSE_HIDDEN;
    scp->status &= ~(GRAPHICS_MODE | PIXEL_MODE | MOUSE_VISIBLE);
    scp->mode = mode;
    scp->xsize = xsize;
    scp->ysize = ysize;
    scp->xoff = 0;
    scp->yoff = 0;
    scp->xpixel = scp->xsize*8;
    scp->ypixel = scp->ysize*fontsize;
    scp->font = font;
    scp->font_size = fontsize;
    scp->font_width = fontwidth;

    /* allocate buffers */
    sc_alloc_scr_buffer(scp, TRUE, TRUE);
    sc_init_emulator(scp, NULL);
#ifndef SC_NO_CUTPASTE
    sc_alloc_cut_buffer(scp, FALSE);
#endif
#ifndef SC_NO_HISTORY
    sc_alloc_history_buffer(scp, 0, prev_ysize, FALSE);
#endif
    splx(s);

    if (scp == scp->sc->cur_scp)
	set_mode(scp);
    scp->status &= ~UNKNOWN_MODE;

    if (tp == NULL)
	return 0;
    wsz.ws_col = scp->xsize;
    wsz.ws_row = scp->ysize;
    tty_set_winsize(tp, &wsz);
    return 0;
}

int
sc_set_graphics_mode(scr_stat *scp, struct tty *tp, int mode)
{
#ifdef SC_NO_MODE_CHANGE
    return ENODEV;
#else
    video_info_t info;
    struct winsize wsz;
    int error;
    int s;

    if (vidd_get_info(scp->sc->adp, mode, &info))
	return ENODEV;

    /* stop screen saver, etc */
    s = spltty();
    if ((error = sc_clean_up(scp))) {
	splx(s);
	return error;
    }

    if (sc_render_match(scp, scp->sc->adp->va_name, GRAPHICS_MODE) == NULL) {
	splx(s);
	return ENODEV;
    }

    /* set up scp */
    scp->status |= (UNKNOWN_MODE | GRAPHICS_MODE | MOUSE_HIDDEN);
    scp->status &= ~(PIXEL_MODE | MOUSE_VISIBLE);
    scp->mode = mode;
    /*
     * Don't change xsize and ysize; preserve the previous vty
     * and history buffers.
     */
    scp->xoff = 0;
    scp->yoff = 0;
    scp->xpixel = info.vi_width;
    scp->ypixel = info.vi_height;
    scp->font = NULL;
    scp->font_size = 0;
#ifndef SC_NO_SYSMOUSE
    /* move the mouse cursor at the center of the screen */
    sc_mouse_move(scp, scp->xpixel / 2, scp->ypixel / 2);
#endif
    sc_init_emulator(scp, NULL);
    splx(s);

    if (scp == scp->sc->cur_scp)
	set_mode(scp);
    /* clear_graphics();*/
    scp->status &= ~UNKNOWN_MODE;

    if (tp == NULL)
	return 0;
    wsz.ws_col = scp->xsize;
    wsz.ws_row = scp->ysize;
    tty_set_winsize(tp, &wsz);
    return 0;
#endif /* SC_NO_MODE_CHANGE */
}

int
sc_set_pixel_mode(scr_stat *scp, struct tty *tp, int xsize, int ysize, 
		  int fontsize, int fontwidth)
{
#ifndef SC_PIXEL_MODE
    return ENODEV;
#else
    video_info_t info;
    struct winsize wsz;
    u_char *font;
    int prev_ysize;
    int error;
    int s;

    if (vidd_get_info(scp->sc->adp, scp->mode, &info))
	return ENODEV;		/* this shouldn't happen */

    /* adjust argument values */
    if (fontsize <= 0)
	fontsize = info.vi_cheight;
    if (fontsize < 14)
	fontsize = 8;
    else if (fontsize >= 16)
	fontsize = 16;
    else
	fontsize = 14;
#ifndef SC_NO_FONT_LOADING
    switch (fontsize) {
    case 8:
	if ((scp->sc->fonts_loaded & FONT_8) == 0)
	    return (EINVAL);
	font = scp->sc->font_8;
	break;
    case 14:
	if ((scp->sc->fonts_loaded & FONT_14) == 0)
	    return (EINVAL);
	font = scp->sc->font_14;
	break;
    case 16:
	if ((scp->sc->fonts_loaded & FONT_16) == 0)
	    return (EINVAL);
	font = scp->sc->font_16;
	break;
    }
#else
    font = NULL;
#endif
    if (xsize <= 0)
	xsize = info.vi_width/8;
    if (ysize <= 0)
	ysize = info.vi_height/fontsize;

    if ((info.vi_width < xsize*8) || (info.vi_height < ysize*fontsize))
	return EINVAL;

    if (!sc_support_pixel_mode(&info))
	return ENODEV;

    /* stop screen saver, etc */
    s = spltty();
    if ((error = sc_clean_up(scp))) {
	splx(s);
	return error;
    }

    if (sc_render_match(scp, scp->sc->adp->va_name, PIXEL_MODE) == NULL) {
	splx(s);
	return ENODEV;
    }

#if 0
    if (scp->tsw)
	(*scp->tsw->te_term)(scp, scp->ts);
    scp->tsw = NULL;
    scp->ts = NULL;
#endif

    /* set up scp */
#ifndef SC_NO_HISTORY
    if (scp->history != NULL)
	sc_hist_save(scp);
#endif
    prev_ysize = scp->ysize;
    scp->status |= (UNKNOWN_MODE | PIXEL_MODE | MOUSE_HIDDEN);
    scp->status &= ~(GRAPHICS_MODE | MOUSE_VISIBLE);
    scp->xsize = xsize;
    scp->ysize = ysize;
    scp->xoff = (scp->xpixel/8 - xsize)/2;
    scp->yoff = (scp->ypixel/fontsize - ysize)/2;
    scp->font = font;
    scp->font_size = fontsize;
    scp->font_width = fontwidth;

    /* allocate buffers */
    sc_alloc_scr_buffer(scp, TRUE, TRUE);
    sc_init_emulator(scp, NULL);
#ifndef SC_NO_CUTPASTE
    sc_alloc_cut_buffer(scp, FALSE);
#endif
#ifndef SC_NO_HISTORY
    sc_alloc_history_buffer(scp, 0, prev_ysize, FALSE);
#endif
    splx(s);

    if (scp == scp->sc->cur_scp) {
	sc_set_border(scp, scp->border);
	sc_set_cursor_image(scp);
    }

    scp->status &= ~UNKNOWN_MODE;

    if (tp == NULL)
	return 0;
    wsz.ws_col = scp->xsize;
    wsz.ws_row = scp->ysize;
    tty_set_winsize(tp, &wsz);
    return 0;
#endif /* SC_PIXEL_MODE */
}

int
sc_support_pixel_mode(void *arg)
{
#ifdef SC_PIXEL_MODE
	video_info_t *info = arg;

	if ((info->vi_flags & V_INFO_GRAPHICS) == 0)
		return (0);

	/*
	 * We currently support the following graphic modes:
	 *
	 * - 4 bpp planar modes whose memory size does not exceed 64K
	 * - 15, 16, 24 and 32 bpp linear modes
	 */
	switch (info->vi_mem_model) {
	case V_INFO_MM_PLANAR:
		if (info->vi_planes != 4)
			break;
		/*
		 * A memory size >64K requires bank switching to access
		 * the entire screen. XXX
		 */
		if (info->vi_width * info->vi_height / 8 > info->vi_window_size)
			break;
		return (1);
	case V_INFO_MM_DIRECT:
		if ((info->vi_flags & V_INFO_LINEAR) == 0 &&
		    info->vi_depth != 15 && info->vi_depth != 16 &&
		    info->vi_depth != 24 && info->vi_depth != 32)
			break;
		return (1);
	case V_INFO_MM_PACKED:
		if ((info->vi_flags & V_INFO_LINEAR) == 0 &&
		    info->vi_depth != 8)
			break;
		return (1);
	}
#endif
	return (0);
}

#define fb_ioctl(a, c, d)		\
	(((a) == NULL) ? ENODEV : 	\
			 vidd_ioctl((a), (c), (caddr_t)(d)))

int
sc_vid_ioctl(struct tty *tp, u_long cmd, caddr_t data, struct thread *td)
{
    scr_stat *scp;
    video_adapter_t *adp;
    video_info_t info;
    video_adapter_info_t adp_info;
    int error;
    int s;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    int ival;
#endif

    scp = SC_STAT(tp);
    if (scp == NULL)		/* tp == SC_MOUSE */
	return ENOIOCTL;
    adp = scp->sc->adp;
    if (adp == NULL)		/* shouldn't happen??? */
	return ENODEV;

    switch (cmd) {

    case CONS_CURRENTADP:	/* get current adapter index */
    case FBIO_ADAPTER:
	return fb_ioctl(adp, FBIO_ADAPTER, data);

    case CONS_CURRENT:  	/* get current adapter type */
    case FBIO_ADPTYPE:
	return fb_ioctl(adp, FBIO_ADPTYPE, data);

    case OLD_CONS_ADPINFO:	/* adapter information (old interface) */
	if (((old_video_adapter_t *)data)->va_index >= 0) {
	    adp = vid_get_adapter(((old_video_adapter_t *)data)->va_index);
	    if (adp == NULL)
		return ENODEV;
	}
	((old_video_adapter_t *)data)->va_index = adp->va_index;
	((old_video_adapter_t *)data)->va_type = adp->va_type;
	((old_video_adapter_t *)data)->va_flags = adp->va_flags;
	((old_video_adapter_t *)data)->va_crtc_addr = adp->va_crtc_addr;
	((old_video_adapter_t *)data)->va_window = adp->va_window;
	((old_video_adapter_t *)data)->va_window_size = adp->va_window_size;
	((old_video_adapter_t *)data)->va_window_gran = adp->va_window_gran;
	((old_video_adapter_t *)data)->va_buffer = adp->va_buffer;
	((old_video_adapter_t *)data)->va_buffer_size = adp->va_buffer_size;
	((old_video_adapter_t *)data)->va_mode = adp->va_mode;
	((old_video_adapter_t *)data)->va_initial_mode = adp->va_initial_mode;
	((old_video_adapter_t *)data)->va_initial_bios_mode
	    = adp->va_initial_bios_mode;
	return 0;

    case OLD_CONS_ADPINFO2:	/* adapter information (yet another old I/F) */
	adp_info.va_index = ((old_video_adapter_info_t *)data)->va_index;
	if (adp_info.va_index >= 0) {
	    adp = vid_get_adapter(adp_info.va_index);
	    if (adp == NULL)
		return ENODEV;
	}
	error = fb_ioctl(adp, FBIO_ADPINFO, &adp_info);
	if (error == 0)
	    bcopy(&adp_info, data, sizeof(old_video_adapter_info_t));
	return error;

    case CONS_ADPINFO:		/* adapter information */
    case FBIO_ADPINFO:
	if (((video_adapter_info_t *)data)->va_index >= 0) {
	    adp = vid_get_adapter(((video_adapter_info_t *)data)->va_index);
	    if (adp == NULL)
		return ENODEV;
	}
	return fb_ioctl(adp, FBIO_ADPINFO, data);

    case CONS_GET:      	/* get current video mode */
    case FBIO_GETMODE:
	*(int *)data = scp->mode;
	return 0;

#ifndef SC_NO_MODE_CHANGE
    case FBIO_SETMODE:		/* set video mode */
	if (!(adp->va_flags & V_ADP_MODECHANGE))
 	    return ENODEV;
	info.vi_mode = *(int *)data;
	error = fb_ioctl(adp, FBIO_MODEINFO, &info);
	if (error)
	    return error;
	if (info.vi_flags & V_INFO_GRAPHICS)
	    return sc_set_graphics_mode(scp, tp, *(int *)data);
	else
	    return sc_set_text_mode(scp, tp, *(int *)data, 0, 0, 0, 0);
#endif /* SC_NO_MODE_CHANGE */

    case OLD_CONS_MODEINFO:	/* get mode information (old infterface) */
	info.vi_mode = ((old_video_info_t *)data)->vi_mode;
	error = fb_ioctl(adp, FBIO_MODEINFO, &info);
	if (error == 0)
	    bcopy(&info, (old_video_info_t *)data, sizeof(old_video_info_t));
	return error;

    case CONS_MODEINFO:		/* get mode information */
    case FBIO_MODEINFO:
	return fb_ioctl(adp, FBIO_MODEINFO, data);

    case OLD_CONS_FINDMODE:	/* find a matching video mode (old interface) */
	bzero(&info, sizeof(info));
	bcopy((old_video_info_t *)data, &info, sizeof(old_video_info_t));
	error = fb_ioctl(adp, FBIO_FINDMODE, &info);
	if (error == 0)
	    bcopy(&info, (old_video_info_t *)data, sizeof(old_video_info_t));
	return error;

    case CONS_FINDMODE:		/* find a matching video mode */
    case FBIO_FINDMODE:
	return fb_ioctl(adp, FBIO_FINDMODE, data);

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('c', 104):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case CONS_SETWINORG:	/* set frame buffer window origin */
    case FBIO_SETWINORG:
	if (scp != scp->sc->cur_scp)
	    return ENODEV;	/* XXX */
	return fb_ioctl(adp, FBIO_SETWINORG, data);

    case FBIO_GETWINORG:	/* get frame buffer window origin */
	if (scp != scp->sc->cur_scp)
	    return ENODEV;	/* XXX */
	return fb_ioctl(adp, FBIO_GETWINORG, data);

    case FBIO_GETDISPSTART:
    case FBIO_SETDISPSTART:
    case FBIO_GETLINEWIDTH:
    case FBIO_SETLINEWIDTH:
	if (scp != scp->sc->cur_scp)
	    return ENODEV;	/* XXX */
	return fb_ioctl(adp, cmd, data);

    case FBIO_GETPALETTE:
    case FBIO_SETPALETTE:
    case FBIOPUTCMAP:
    case FBIOGETCMAP:
    case FBIOGTYPE:
    case FBIOGATTR:
    case FBIOSVIDEO:
    case FBIOGVIDEO:
    case FBIOSCURSOR:
    case FBIOGCURSOR:
    case FBIOSCURPOS:
    case FBIOGCURPOS:
    case FBIOGCURMAX:
	if (scp != scp->sc->cur_scp)
	    return ENODEV;	/* XXX */
	return fb_ioctl(adp, cmd, data);

    case FBIO_BLANK:
	if (scp != scp->sc->cur_scp)
	    return ENODEV;	/* XXX */
	return fb_ioctl(adp, cmd, data);

#ifndef SC_NO_MODE_CHANGE
    /* generic text modes */
    case SW_TEXT_80x25:	case SW_TEXT_80x30:
    case SW_TEXT_80x43: case SW_TEXT_80x50:
    case SW_TEXT_80x60:
	/* FALLTHROUGH */

    /* VGA TEXT MODES */
    case SW_VGA_C40x25:
    case SW_VGA_C80x25: case SW_VGA_M80x25:
    case SW_VGA_C80x30: case SW_VGA_M80x30:
    case SW_VGA_C80x50: case SW_VGA_M80x50:
    case SW_VGA_C80x60: case SW_VGA_M80x60:
    case SW_VGA_C90x25: case SW_VGA_M90x25:
    case SW_VGA_C90x30: case SW_VGA_M90x30:
    case SW_VGA_C90x43: case SW_VGA_M90x43:
    case SW_VGA_C90x50: case SW_VGA_M90x50:
    case SW_VGA_C90x60: case SW_VGA_M90x60:
    case SW_B40x25:     case SW_C40x25:
    case SW_B80x25:     case SW_C80x25:
    case SW_ENH_B40x25: case SW_ENH_C40x25:
    case SW_ENH_B80x25: case SW_ENH_C80x25:
    case SW_ENH_B80x43: case SW_ENH_C80x43:
    case SW_EGAMONO80x25:
	if (!(adp->va_flags & V_ADP_MODECHANGE))
 	    return ENODEV;
	return sc_set_text_mode(scp, tp, cmd & 0xff, 0, 0, 0, 0);

    /* GRAPHICS MODES */
    case SW_BG320:     case SW_BG640:
    case SW_CG320:     case SW_CG320_D:   case SW_CG640_E:
    case SW_CG640x350: case SW_ENH_CG640:
    case SW_BG640x480: case SW_CG640x480: case SW_VGA_CG320:
    case SW_VGA_MODEX:
	if (!(adp->va_flags & V_ADP_MODECHANGE))
	    return ENODEV;
	return sc_set_graphics_mode(scp, tp, cmd & 0xff);
#endif /* SC_NO_MODE_CHANGE */

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('K', 10):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case KDSETMODE:     	/* set current mode of this (virtual) console */
	switch (*(int *)data) {
	case KD_TEXT:   	/* switch to TEXT (known) mode */
	    /*
	     * If scp->mode is of graphics modes, we don't know which
	     * text mode to switch back to...
	     */
	    if (scp->status & GRAPHICS_MODE)
		return EINVAL;
	    /* restore fonts & palette ! */
#if 0
#ifndef SC_NO_FONT_LOADING
	    if (ISFONTAVAIL(adp->va_flags) 
		&& !(scp->status & (GRAPHICS_MODE | PIXEL_MODE)))
		/*
		 * FONT KLUDGE
		 * Don't load fonts for now... XXX
		 */
		if (scp->sc->fonts_loaded & FONT_8)
		    sc_load_font(scp, 0, 8, 8, scp->sc->font_8, 0, 256);
		if (scp->sc->fonts_loaded & FONT_14)
		    sc_load_font(scp, 0, 14, 8, scp->sc->font_14, 0, 256);
		if (scp->sc->fonts_loaded & FONT_16)
		    sc_load_font(scp, 0, 16, 8, scp->sc->font_16, 0, 256);
	    }
#endif /* SC_NO_FONT_LOADING */
#endif

#ifndef SC_NO_PALETTE_LOADING
#ifdef SC_PIXEL_MODE
	    if (adp->va_info.vi_mem_model == V_INFO_MM_DIRECT)
		vidd_load_palette(adp, scp->sc->palette2);
	    else
#endif
	    vidd_load_palette(adp, scp->sc->palette);
#endif

	    /* move hardware cursor out of the way */
	    vidd_set_hw_cursor(adp, -1, -1);

	    /* FALLTHROUGH */

	case KD_TEXT1:  	/* switch to TEXT (known) mode */
	    /*
	     * If scp->mode is of graphics modes, we don't know which
	     * text/pixel mode to switch back to...
	     */
	    if (scp->status & GRAPHICS_MODE)
		return EINVAL;
	    s = spltty();
	    if ((error = sc_clean_up(scp))) {
		splx(s);
		return error;
	    }
	    scp->status |= UNKNOWN_MODE | MOUSE_HIDDEN;
	    splx(s);
	    /* no restore fonts & palette */
	    if (scp == scp->sc->cur_scp)
		set_mode(scp);
	    sc_clear_screen(scp);
	    scp->status &= ~UNKNOWN_MODE;
	    return 0;

#ifdef SC_PIXEL_MODE
	case KD_PIXEL:		/* pixel (raster) display */
	    if (!(scp->status & (GRAPHICS_MODE | PIXEL_MODE)))
		return EINVAL;
	    if (scp->status & GRAPHICS_MODE)
		return sc_set_pixel_mode(scp, tp, scp->xsize, scp->ysize, 
					 scp->font_size, scp->font_width);
	    s = spltty();
	    if ((error = sc_clean_up(scp))) {
		splx(s);
		return error;
	    }
	    scp->status |= (UNKNOWN_MODE | PIXEL_MODE | MOUSE_HIDDEN);
	    splx(s);
	    if (scp == scp->sc->cur_scp) {
		set_mode(scp);
#ifndef SC_NO_PALETTE_LOADING
		if (adp->va_info.vi_mem_model == V_INFO_MM_DIRECT)
		    vidd_load_palette(adp, scp->sc->palette2);
		else
		    vidd_load_palette(adp, scp->sc->palette);
#endif
	    }
	    sc_clear_screen(scp);
	    scp->status &= ~UNKNOWN_MODE;
	    return 0;
#endif /* SC_PIXEL_MODE */

	case KD_GRAPHICS:	/* switch to GRAPHICS (unknown) mode */
	    s = spltty();
	    if ((error = sc_clean_up(scp))) {
		splx(s);
		return error;
	    }
	    scp->status |= UNKNOWN_MODE | MOUSE_HIDDEN;
	    splx(s);
	    return 0;

	default:
	    return EINVAL;
	}
	/* NOT REACHED */

#ifdef SC_PIXEL_MODE
    case KDRASTER:		/* set pixel (raster) display mode */
	if (ISUNKNOWNSC(scp) || ISTEXTSC(scp))
	    return ENODEV;
	return sc_set_pixel_mode(scp, tp, ((int *)data)[0], ((int *)data)[1], 
				 ((int *)data)[2], 8);
#endif /* SC_PIXEL_MODE */

    case KDGETMODE:     	/* get current mode of this (virtual) console */
	/* 
	 * From the user program's point of view, KD_PIXEL is the same 
	 * as KD_TEXT... 
	 */
	*data = ISGRAPHSC(scp) ? KD_GRAPHICS : KD_TEXT;
	return 0;

#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
    case _IO('K', 13):
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
	/* FALLTHROUGH */
#endif
    case KDSBORDER:     	/* set border color of this (virtual) console */
	scp->border = *(int *)data;
	if (scp == scp->sc->cur_scp)
	    sc_set_border(scp, scp->border);
	return 0;
    }

    return ENOIOCTL;
}

static LIST_HEAD(, sc_renderer) sc_rndr_list = 
	LIST_HEAD_INITIALIZER(sc_rndr_list);

int
sc_render_add(sc_renderer_t *rndr)
{
	LIST_INSERT_HEAD(&sc_rndr_list, rndr, link);
	return 0;
}

int
sc_render_remove(sc_renderer_t *rndr)
{
	/*
	LIST_REMOVE(rndr, link);
	*/
	return EBUSY;	/* XXX */
}

sc_rndr_sw_t
*sc_render_match(scr_stat *scp, char *name, int mode)
{
	const sc_renderer_t **list;
	const sc_renderer_t *p;

	if (!LIST_EMPTY(&sc_rndr_list)) {
		LIST_FOREACH(p, &sc_rndr_list, link) {
			if ((strcmp(p->name, name) == 0)
				&& (mode == p->mode)) {
				scp->status &=
				    ~(VR_CURSOR_ON | VR_CURSOR_BLINK);
				return p->rndrsw;
			}
		}
	} else {
		SET_FOREACH(list, scrndr_set) {
			p = *list;
			if ((strcmp(p->name, name) == 0)
				&& (mode == p->mode)) {
				scp->status &=
				    ~(VR_CURSOR_ON | VR_CURSOR_BLINK);
				return p->rndrsw;
			}
		}
	}

	return NULL;
}
