/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Ed Schouten under sponsorship from the
 * FreeBSD Foundation.
 *
 * Portions of this software were developed by Oleksandr Rybalko
 * under sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_VT_VT_H_
#define	_DEV_VT_VT_H_

#include <sys/param.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/consio.h>
#include <sys/kbio.h>
#include <sys/mouse.h>
#include <sys/terminal.h>
#include <sys/sysctl.h>

#include "opt_syscons.h"
#include "opt_splash.h"

#ifndef	VT_MAXWINDOWS
#ifdef	MAXCONS
#define	VT_MAXWINDOWS	MAXCONS
#else
#define	VT_MAXWINDOWS	12
#endif
#endif

#ifndef VT_ALT_TO_ESC_HACK
#define	VT_ALT_TO_ESC_HACK	1
#endif

#define	VT_CONSWINDOW	0

#if defined(SC_TWOBUTTON_MOUSE) || defined(VT_TWOBUTTON_MOUSE)
#define VT_MOUSE_PASTEBUTTON	MOUSE_BUTTON3DOWN	/* right button */
#define VT_MOUSE_EXTENDBUTTON	MOUSE_BUTTON2DOWN	/* not really used */
#else
#define VT_MOUSE_PASTEBUTTON	MOUSE_BUTTON2DOWN	/* middle button */
#define VT_MOUSE_EXTENDBUTTON	MOUSE_BUTTON3DOWN	/* right button */
#endif /* defined(SC_TWOBUTTON_MOUSE) || defined(VT_TWOBUTTON_MOUSE) */

#define	SC_DRIVER_NAME	"vt"
#ifdef VT_DEBUG
#define	DPRINTF(_l, ...)	if (vt_debug > (_l)) printf( __VA_ARGS__ )
#define VT_CONSOLECTL_DEBUG
#define VT_SYSMOUSE_DEBUG
#else
#define	DPRINTF(_l, ...)	do {} while (0)
#endif
#define	ISSIGVALID(sig)	((sig) > 0 && (sig) < NSIG)

#define	VT_SYSCTL_INT(_name, _default, _descr)				\
int vt_##_name = (_default);						\
SYSCTL_INT(_kern_vt, OID_AUTO, _name, CTLFLAG_RWTUN, &vt_##_name, 0, _descr)

struct vt_driver;

void vt_allocate(const struct vt_driver *, void *);
void vt_deallocate(const struct vt_driver *, void *);

typedef unsigned int	vt_axis_t;

/*
 * List of locks
 * (d)	locked by vd_lock
 * (b)	locked by vb_lock
 * (G)	locked by Giant
 * (u)	unlocked, locked by higher levels
 * (c)	const until freeing
 * (?)	yet to be determined
 */

/*
 * Per-device datastructure.
 */

#ifndef SC_NO_CUTPASTE
struct vt_mouse_cursor;
#endif

struct vt_pastebuf {
	term_char_t		*vpb_buf;	/* Copy-paste buffer. */
	unsigned int		 vpb_bufsz;	/* Buffer size. */
	unsigned int		 vpb_len;	/* Length of a last selection. */
};

struct vt_device {
	struct vt_window	*vd_windows[VT_MAXWINDOWS]; /* (c) Windows. */
	struct vt_window	*vd_curwindow;	/* (d) Current window. */
	struct vt_window	*vd_savedwindow;/* (?) Saved for suspend. */
	struct vt_pastebuf	 vd_pastebuf;	/* (?) Copy/paste buf. */
	const struct vt_driver	*vd_driver;	/* (c) Graphics driver. */
	void			*vd_softc;	/* (u) Driver data. */
	const struct vt_driver	*vd_prev_driver;/* (?) Previous driver. */
	void			*vd_prev_softc;	/* (?) Previous driver data. */
	device_t		 vd_video_dev;	/* (?) Video adapter. */
#ifndef SC_NO_CUTPASTE
	struct vt_mouse_cursor	*vd_mcursor;	/* (?) Cursor bitmap. */
	term_color_t		 vd_mcursor_fg;	/* (?) Cursor fg color. */
	term_color_t		 vd_mcursor_bg;	/* (?) Cursor bg color. */
	vt_axis_t		 vd_mx_drawn;	/* (?) Mouse X and Y      */
	vt_axis_t		 vd_my_drawn;	/*     as of last redraw. */
	int			 vd_mshown;	/* (?) Mouse shown during */
#endif						/*     last redrawn.      */
	uint16_t		 vd_mx;		/* (?) Current mouse X. */
	uint16_t		 vd_my;		/* (?) current mouse Y. */
	uint32_t		 vd_mstate;	/* (?) Mouse state. */
	vt_axis_t		 vd_width;	/* (?) Screen width. */
	vt_axis_t		 vd_height;	/* (?) Screen height. */
	size_t			 vd_transpose;	/* (?) Screen offset in FB */
	struct mtx		 vd_lock;	/* Per-device lock. */
	struct cv		 vd_winswitch;	/* (d) Window switch notify. */
	struct callout		 vd_timer;	/* (d) Display timer. */
	volatile unsigned int	 vd_timer_armed;/* (?) Display timer started.*/
	int			 vd_flags;	/* (d) Device flags. */
#define	VDF_TEXTMODE	0x01	/* Do text mode rendering. */
#define	VDF_SPLASH	0x02	/* Splash screen active. */
#define	VDF_ASYNC	0x04	/* vt_timer() running. */
#define	VDF_INVALID	0x08	/* Entire screen should be re-rendered. */
#define	VDF_DEAD	0x10	/* Early probing found nothing. */
#define	VDF_INITIALIZED	0x20	/* vtterm_cnprobe already done. */
#define	VDF_MOUSECURSOR	0x40	/* Mouse cursor visible. */
#define	VDF_QUIET_BELL	0x80	/* Disable bell. */
#define	VDF_SUSPENDED	0x100	/* Device has been suspended. */
#define	VDF_DOWNGRADE	0x8000	/* The driver is being downgraded. */
	int			 vd_keyboard;	/* (G) Keyboard index. */
	unsigned int		 vd_kbstate;	/* (?) Device unit. */
	unsigned int		 vd_unit;	/* (c) Device unit. */
	int			 vd_altbrk;	/* (?) Alt break seq. state */
	term_char_t		*vd_drawn;	/* (?) Most recent char drawn. */
	term_color_t		*vd_drawnfg;	/* (?) Most recent fg color drawn. */
	term_color_t		*vd_drawnbg;	/* (?) Most recent bg color drawn. */
};

#define	VD_PASTEBUF(vd)	((vd)->vd_pastebuf.vpb_buf)
#define	VD_PASTEBUFSZ(vd)	((vd)->vd_pastebuf.vpb_bufsz)
#define	VD_PASTEBUFLEN(vd)	((vd)->vd_pastebuf.vpb_len)

#define	VT_LOCK(vd)	mtx_lock(&(vd)->vd_lock)
#define	VT_UNLOCK(vd)	mtx_unlock(&(vd)->vd_lock)
#define	VT_LOCK_ASSERT(vd, what)	mtx_assert(&(vd)->vd_lock, what)

void vt_resume(struct vt_device *vd);
void vt_resume_flush_timer(struct vt_window *vw, int ms);
void vt_suspend(struct vt_device *vd);

/*
 * Per-window terminal screen buffer.
 *
 * Because redrawing is performed asynchronously, the buffer keeps track
 * of a rectangle that needs to be redrawn (vb_dirtyrect).  Because this
 * approach seemed to cause suboptimal performance (when the top left
 * and the bottom right of the screen are modified), it also uses a set
 * of bitmasks to keep track of the rows and columns (mod 64) that have
 * been modified.
 */

struct vt_buf {
	struct mtx		 vb_lock;	/* Buffer lock. */
	term_pos_t		 vb_scr_size;	/* (b) Screen dimensions. */
	int			 vb_flags;	/* (b) Flags. */
#define	VBF_CURSOR	0x1	/* Cursor visible. */
#define	VBF_STATIC	0x2	/* Buffer is statically allocated. */
#define	VBF_MTX_INIT	0x4	/* Mutex initialized. */
#define	VBF_SCROLL	0x8	/* scroll locked mode. */
#define	VBF_HISTORY_FULL 0x10	/* All rows filled. */
	unsigned int		 vb_history_size;
	unsigned int		 vb_roffset;	/* (b) History rows offset. */
	unsigned int		 vb_curroffset;	/* (b) Saved rows offset. */
	term_pos_t		 vb_cursor;	/* (u) Cursor position. */
	term_pos_t		 vb_mark_start;	/* (b) Copy region start. */
	term_pos_t		 vb_mark_end;	/* (b) Copy region end. */
	int			 vb_mark_last;	/* Last mouse event. */
	term_rect_t		 vb_dirtyrect;	/* (b) Dirty rectangle. */
	term_char_t		*vb_buffer;	/* (u) Data buffer. */
	term_char_t		**vb_rows;	/* (u) Array of rows */
};

#ifdef SC_HISTORY_SIZE
#define	VBF_DEFAULT_HISTORY_SIZE	SC_HISTORY_SIZE
#else
#define	VBF_DEFAULT_HISTORY_SIZE	500
#endif

void vtbuf_lock(struct vt_buf *);
void vtbuf_unlock(struct vt_buf *);
void vtbuf_copy(struct vt_buf *, const term_rect_t *, const term_pos_t *);
void vtbuf_fill(struct vt_buf *, const term_rect_t *, term_char_t);
void vtbuf_init_early(struct vt_buf *);
void vtbuf_init(struct vt_buf *, const term_pos_t *);
void vtbuf_grow(struct vt_buf *, const term_pos_t *, unsigned int);
void vtbuf_putchar(struct vt_buf *, const term_pos_t *, term_char_t);
void vtbuf_cursor_position(struct vt_buf *, const term_pos_t *);
void vtbuf_scroll_mode(struct vt_buf *vb, int yes);
void vtbuf_dirty(struct vt_buf *vb, const term_rect_t *area);
void vtbuf_undirty(struct vt_buf *, term_rect_t *);
void vtbuf_sethistory_size(struct vt_buf *, unsigned int);
int vtbuf_iscursor(const struct vt_buf *vb, int row, int col);
void vtbuf_cursor_visibility(struct vt_buf *, int);
#ifndef SC_NO_CUTPASTE
int vtbuf_set_mark(struct vt_buf *vb, int type, int col, int row);
int vtbuf_get_marked_len(struct vt_buf *vb);
void vtbuf_extract_marked(struct vt_buf *vb, term_char_t *buf, int sz);
#endif

#define	VTB_MARK_NONE		0
#define	VTB_MARK_END		1
#define	VTB_MARK_START		2
#define	VTB_MARK_WORD		3
#define	VTB_MARK_ROW		4
#define	VTB_MARK_EXTEND		5
#define	VTB_MARK_MOVE		6

#define	VTBUF_SLCK_ENABLE(vb)	vtbuf_scroll_mode((vb), 1)
#define	VTBUF_SLCK_DISABLE(vb)	vtbuf_scroll_mode((vb), 0)

#define	VTBUF_MAX_HEIGHT(vb) \
	((vb)->vb_history_size)
#define	VTBUF_GET_ROW(vb, r) \
	((vb)->vb_rows[((vb)->vb_roffset + (r)) % VTBUF_MAX_HEIGHT(vb)])
#define	VTBUF_GET_FIELD(vb, r, c) \
	((vb)->vb_rows[((vb)->vb_roffset + (r)) % VTBUF_MAX_HEIGHT(vb)][(c)])
#define	VTBUF_FIELD(vb, r, c) \
	((vb)->vb_rows[((vb)->vb_curroffset + (r)) % VTBUF_MAX_HEIGHT(vb)][(c)])
#define	VTBUF_ISCURSOR(vb, r, c) \
	vtbuf_iscursor((vb), (r), (c))
#define	VTBUF_DIRTYROW(mask, row) \
	((mask)->vbm_row & ((uint64_t)1 << ((row) % 64)))
#define	VTBUF_DIRTYCOL(mask, col) \
	((mask)->vbm_col & ((uint64_t)1 << ((col) % 64)))
#define	VTBUF_SPACE_CHAR(attr)	(' ' | (attr))

#define	VHS_SET	0
#define	VHS_CUR	1
#define	VHS_END	2
int vthistory_seek(struct vt_buf *, int offset, int whence);
void vthistory_addlines(struct vt_buf *vb, int offset);
void vthistory_getpos(const struct vt_buf *, unsigned int *offset);

/*
 * Per-window datastructure.
 */

struct vt_window {
	struct vt_device	*vw_device;	/* (c) Device. */
	struct terminal		*vw_terminal;	/* (c) Terminal. */
	struct vt_buf		 vw_buf;	/* (u) Screen buffer. */
	struct vt_font		*vw_font;	/* (d) Graphical font. */
	term_rect_t		 vw_draw_area;	/* (?) Drawable area. */
	unsigned int		 vw_number;	/* (c) Window number. */
	int			 vw_kbdmode;	/* (?) Keyboard mode. */
	int			 vw_prev_kbdmode;/* (?) Previous mode. */
	int			 vw_kbdstate;	/* (?) Keyboard state. */
	int			 vw_grabbed;	/* (?) Grab count. */
	char			*vw_kbdsq;	/* Escape sequence queue*/
	unsigned int		 vw_flags;	/* (d) Per-window flags. */
	int			 vw_mouse_level;/* Mouse op mode. */
#define	VWF_BUSY	0x1	/* Busy reconfiguring device. */
#define	VWF_OPENED	0x2	/* TTY in use. */
#define	VWF_SCROLL	0x4	/* Keys influence scrollback. */
#define	VWF_CONSOLE	0x8	/* Kernel message console window. */
#define	VWF_VTYLOCK	0x10	/* Prevent window switch. */
#define	VWF_MOUSE_HIDE	0x20	/* Disable mouse events processing. */
#define	VWF_READY	0x40	/* Window fully initialized. */
#define	VWF_GRAPHICS	0x80	/* Window in graphics mode (KDSETMODE). */
#define	VWF_SWWAIT_REL	0x10000	/* Program wait for VT acquire is done. */
#define	VWF_SWWAIT_ACQ	0x20000	/* Program wait for VT release is done. */
	pid_t			 vw_pid;	/* Terminal holding process */
	struct proc		*vw_proc;
	struct vt_mode		 vw_smode;	/* switch mode */
	struct callout		 vw_proc_dead_timer;
	struct vt_window	*vw_switch_to;
};

#define	VT_AUTO		0		/* switching is automatic */
#define	VT_PROCESS	1		/* switching controlled by prog */
#define	VT_KERNEL	255		/* switching controlled in kernel */

#define	IS_VT_PROC_MODE(vw)	((vw)->vw_smode.mode == VT_PROCESS)

/*
 * Per-device driver routines.
 */

typedef int vd_init_t(struct vt_device *vd);
typedef int vd_probe_t(struct vt_device *vd);
typedef void vd_fini_t(struct vt_device *vd, void *softc);
typedef void vd_postswitch_t(struct vt_device *vd);
typedef void vd_blank_t(struct vt_device *vd, term_color_t color);
typedef void vd_bitblt_text_t(struct vt_device *vd, const struct vt_window *vw,
    const term_rect_t *area);
typedef void vd_invalidate_text_t(struct vt_device *vd,
    const term_rect_t *area);
typedef void vd_bitblt_bmp_t(struct vt_device *vd, const struct vt_window *vw,
    const uint8_t *pattern, const uint8_t *mask,
    unsigned int width, unsigned int height,
    unsigned int x, unsigned int y, term_color_t fg, term_color_t bg);
typedef int vd_fb_ioctl_t(struct vt_device *, u_long, caddr_t, struct thread *);
typedef int vd_fb_mmap_t(struct vt_device *, vm_ooffset_t, vm_paddr_t *, int,
    vm_memattr_t *);
typedef void vd_drawrect_t(struct vt_device *, int, int, int, int, int,
    term_color_t);
typedef void vd_setpixel_t(struct vt_device *, int, int, term_color_t);
typedef void vd_suspend_t(struct vt_device *);
typedef void vd_resume_t(struct vt_device *);

struct vt_driver {
	char		 vd_name[16];
	/* Console attachment. */
	vd_probe_t	*vd_probe;
	vd_init_t	*vd_init;
	vd_fini_t	*vd_fini;

	/* Drawing. */
	vd_blank_t	*vd_blank;
	vd_drawrect_t	*vd_drawrect;
	vd_setpixel_t	*vd_setpixel;
	vd_bitblt_text_t *vd_bitblt_text;
	vd_invalidate_text_t *vd_invalidate_text;
	vd_bitblt_bmp_t	*vd_bitblt_bmp;

	/* Framebuffer ioctls, if present. */
	vd_fb_ioctl_t	*vd_fb_ioctl;

	/* Framebuffer mmap, if present. */
	vd_fb_mmap_t	*vd_fb_mmap;

	/* Update display setting on vt switch. */
	vd_postswitch_t	*vd_postswitch;

	/* Suspend/resume handlers. */
	vd_suspend_t	*vd_suspend;
	vd_resume_t	*vd_resume;

	/* Priority to know which one can override */
	int		vd_priority;
#define	VD_PRIORITY_DUMB	10
#define	VD_PRIORITY_GENERIC	100
#define	VD_PRIORITY_SPECIFIC	1000
};

/*
 * Console device madness.
 *
 * Utility macro to make early vt(4) instances work.
 */

extern struct vt_device vt_consdev;
extern struct terminal vt_consterm;
extern const struct terminal_class vt_termclass;
void vt_upgrade(struct vt_device *vd);

#define	PIXEL_WIDTH(w)	((w) / 8)
#define	PIXEL_HEIGHT(h)	((h) / 16)

#ifndef VT_FB_MAX_WIDTH
#define	VT_FB_MAX_WIDTH	4096
#endif
#ifndef VT_FB_MAX_HEIGHT
#define	VT_FB_MAX_HEIGHT	2400
#endif

/* name argument is not used yet. */
#define VT_DRIVER_DECLARE(name, drv) DATA_SET(vt_drv_set, drv)

/*
 * Fonts.
 *
 * Remapping tables are used to map Unicode points to glyphs.  They need
 * to be sorted, because vtfont_lookup() performs a binary search.  Each
 * font has two remapping tables, for normal and bold.  When a character
 * is not present in bold, it uses a normal glyph.  When no glyph is
 * available, it uses glyph 0, which is normally equal to U+FFFD.
 */

struct vt_font_map {
	uint32_t		 vfm_src;
	uint16_t		 vfm_dst;
	uint16_t		 vfm_len;
};

struct vt_font {
	struct vt_font_map	*vf_map[VFNT_MAPS];
	uint8_t			*vf_bytes;
	unsigned int		 vf_height, vf_width;
	unsigned int		 vf_map_count[VFNT_MAPS];
	unsigned int		 vf_refcount;
};

#ifndef SC_NO_CUTPASTE
struct vt_mouse_cursor {
	uint8_t map[64 * 64 / 8];
	uint8_t mask[64 * 64 / 8];
	uint8_t width;
	uint8_t height;
};
#endif

const uint8_t	*vtfont_lookup(const struct vt_font *vf, term_char_t c);
struct vt_font	*vtfont_ref(struct vt_font *vf);
void		 vtfont_unref(struct vt_font *vf);
int		 vtfont_load(vfnt_t *f, struct vt_font **ret);

/* Sysmouse. */
void sysmouse_process_event(mouse_info_t *mi);
#ifndef SC_NO_CUTPASTE
void vt_mouse_event(int type, int x, int y, int event, int cnt, int mlevel);
void vt_mouse_state(int show);
#endif
#define	VT_MOUSE_SHOW 1
#define	VT_MOUSE_HIDE 0

/* Utilities. */
void	vt_compute_drawable_area(struct vt_window *);
void	vt_determine_colors(term_char_t c, int cursor,
	    term_color_t *fg, term_color_t *bg);
int	vt_is_cursor_in_area(const struct vt_device *vd,
	    const term_rect_t *area);
void	vt_termsize(struct vt_device *, struct vt_font *, term_pos_t *);
void	vt_winsize(struct vt_device *, struct vt_font *, struct winsize *);

/* Logos-on-boot. */
#define	VT_LOGOS_DRAW_BEASTIE		0
#define	VT_LOGOS_DRAW_ALT_BEASTIE	1
#define	VT_LOGOS_DRAW_ORB		2

extern int vt_draw_logo_cpus;
extern int vt_splash_cpu;
extern int vt_splash_ncpu;
extern int vt_splash_cpu_style;
extern int vt_splash_cpu_duration;

extern const unsigned int vt_logo_sprite_height;
extern const unsigned int vt_logo_sprite_width;

void vtterm_draw_cpu_logos(struct vt_device *);

#endif /* !_DEV_VT_VT_H_ */

