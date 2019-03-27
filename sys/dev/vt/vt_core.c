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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/consio.h>
#include <sys/eventhandler.h>
#include <sys/fbio.h>
#include <sys/kbio.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/power.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/random.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/terminal.h>

#include <dev/kbd/kbdreg.h>
#include <dev/vt/vt.h>

#if defined(__i386__) || defined(__amd64__)
#include <machine/psl.h>
#include <machine/frame.h>
#endif

static tc_bell_t	vtterm_bell;
static tc_cursor_t	vtterm_cursor;
static tc_putchar_t	vtterm_putchar;
static tc_fill_t	vtterm_fill;
static tc_copy_t	vtterm_copy;
static tc_pre_input_t	vtterm_pre_input;
static tc_post_input_t	vtterm_post_input;
static tc_param_t	vtterm_param;
static tc_done_t	vtterm_done;

static tc_cnprobe_t	vtterm_cnprobe;
static tc_cngetc_t	vtterm_cngetc;

static tc_cngrab_t	vtterm_cngrab;
static tc_cnungrab_t	vtterm_cnungrab;

static tc_opened_t	vtterm_opened;
static tc_ioctl_t	vtterm_ioctl;
static tc_mmap_t	vtterm_mmap;

const struct terminal_class vt_termclass = {
	.tc_bell	= vtterm_bell,
	.tc_cursor	= vtterm_cursor,
	.tc_putchar	= vtterm_putchar,
	.tc_fill	= vtterm_fill,
	.tc_copy	= vtterm_copy,
	.tc_pre_input	= vtterm_pre_input,
	.tc_post_input	= vtterm_post_input,
	.tc_param	= vtterm_param,
	.tc_done	= vtterm_done,

	.tc_cnprobe	= vtterm_cnprobe,
	.tc_cngetc	= vtterm_cngetc,

	.tc_cngrab	= vtterm_cngrab,
	.tc_cnungrab	= vtterm_cnungrab,

	.tc_opened	= vtterm_opened,
	.tc_ioctl	= vtterm_ioctl,
	.tc_mmap	= vtterm_mmap,
};

/*
 * Use a constant timer of 25 Hz to redraw the screen.
 *
 * XXX: In theory we should only fire up the timer when there is really
 * activity. Unfortunately we cannot always start timers. We really
 * don't want to process kernel messages synchronously, because it
 * really slows down the system.
 */
#define	VT_TIMERFREQ	25

/* Bell pitch/duration. */
#define	VT_BELLDURATION	((5 * hz + 99) / 100)
#define	VT_BELLPITCH	800

#define	VT_UNIT(vw)	((vw)->vw_device->vd_unit * VT_MAXWINDOWS + \
			(vw)->vw_number)

static SYSCTL_NODE(_kern, OID_AUTO, vt, CTLFLAG_RD, 0, "vt(9) parameters");
static VT_SYSCTL_INT(enable_altgr, 1, "Enable AltGr key (Do not assume R.Alt as Alt)");
static VT_SYSCTL_INT(enable_bell, 1, "Enable bell");
static VT_SYSCTL_INT(debug, 0, "vt(9) debug level");
static VT_SYSCTL_INT(deadtimer, 15, "Time to wait busy process in VT_PROCESS mode");
static VT_SYSCTL_INT(suspendswitch, 1, "Switch to VT0 before suspend");

/* Allow to disable some keyboard combinations. */
static VT_SYSCTL_INT(kbd_halt, 1, "Enable halt keyboard combination.  "
    "See kbdmap(5) to configure.");
static VT_SYSCTL_INT(kbd_poweroff, 1, "Enable Power Off keyboard combination.  "
    "See kbdmap(5) to configure.");
static VT_SYSCTL_INT(kbd_reboot, 1, "Enable reboot keyboard combination.  "
    "See kbdmap(5) to configure (typically Ctrl-Alt-Delete).");
static VT_SYSCTL_INT(kbd_debug, 1, "Enable key combination to enter debugger.  "
    "See kbdmap(5) to configure (typically Ctrl-Alt-Esc).");
static VT_SYSCTL_INT(kbd_panic, 0, "Enable request to panic.  "
    "See kbdmap(5) to configure.");

/* Used internally, not a tunable. */
int vt_draw_logo_cpus;
VT_SYSCTL_INT(splash_cpu, 0, "Show logo CPUs during boot");
VT_SYSCTL_INT(splash_ncpu, 0, "Override number of logos displayed "
    "(0 = do not override)");
VT_SYSCTL_INT(splash_cpu_style, 2, "Draw logo style "
    "(0 = Alternate beastie, 1 = Beastie, 2 = Orb)");
VT_SYSCTL_INT(splash_cpu_duration, 10, "Hide logos after (seconds)");

static unsigned int vt_unit = 0;
static MALLOC_DEFINE(M_VT, "vt", "vt device");
struct vt_device *main_vd = &vt_consdev;

/* Boot logo. */
extern unsigned int vt_logo_width;
extern unsigned int vt_logo_height;
extern unsigned int vt_logo_depth;
extern unsigned char vt_logo_image[];
#ifndef DEV_SPLASH
#define	vtterm_draw_cpu_logos(...)	do {} while (0)
const unsigned int vt_logo_sprite_height;
#endif

/* Font. */
extern struct vt_font vt_font_default;
#ifndef SC_NO_CUTPASTE
extern struct vt_mouse_cursor vt_default_mouse_pointer;
#endif

static int signal_vt_rel(struct vt_window *);
static int signal_vt_acq(struct vt_window *);
static int finish_vt_rel(struct vt_window *, int, int *);
static int finish_vt_acq(struct vt_window *);
static int vt_window_switch(struct vt_window *);
static int vt_late_window_switch(struct vt_window *);
static int vt_proc_alive(struct vt_window *);
static void vt_resize(struct vt_device *);
static void vt_update_static(void *);
#ifndef SC_NO_CUTPASTE
static void vt_mouse_paste(void);
#endif
static void vt_suspend_handler(void *priv);
static void vt_resume_handler(void *priv);

SET_DECLARE(vt_drv_set, struct vt_driver);

#define	_VTDEFH	MAX(100, PIXEL_HEIGHT(VT_FB_MAX_HEIGHT))
#define	_VTDEFW	MAX(200, PIXEL_WIDTH(VT_FB_MAX_WIDTH))

struct terminal	vt_consterm;
static struct vt_window	vt_conswindow;
#ifndef SC_NO_CONSDRAWN
static term_char_t vt_consdrawn[PIXEL_HEIGHT(VT_FB_MAX_HEIGHT) * PIXEL_WIDTH(VT_FB_MAX_WIDTH)];
static term_color_t vt_consdrawnfg[PIXEL_HEIGHT(VT_FB_MAX_HEIGHT) * PIXEL_WIDTH(VT_FB_MAX_WIDTH)];
static term_color_t vt_consdrawnbg[PIXEL_HEIGHT(VT_FB_MAX_HEIGHT) * PIXEL_WIDTH(VT_FB_MAX_WIDTH)];
#endif
struct vt_device	vt_consdev = {
	.vd_driver = NULL,
	.vd_softc = NULL,
	.vd_prev_driver = NULL,
	.vd_prev_softc = NULL,
	.vd_flags = VDF_INVALID,
	.vd_windows = { [VT_CONSWINDOW] =  &vt_conswindow, },
	.vd_curwindow = &vt_conswindow,
	.vd_kbstate = 0,

#ifndef SC_NO_CUTPASTE
	.vd_pastebuf = {
		.vpb_buf = NULL,
		.vpb_bufsz = 0,
		.vpb_len = 0
	},
	.vd_mcursor = &vt_default_mouse_pointer,
	.vd_mcursor_fg = TC_WHITE,
	.vd_mcursor_bg = TC_BLACK,
#endif

#ifndef SC_NO_CONSDRAWN
	.vd_drawn = vt_consdrawn,
	.vd_drawnfg = vt_consdrawnfg,
	.vd_drawnbg = vt_consdrawnbg,
#endif
};
static term_char_t vt_constextbuf[(_VTDEFW) * (VBF_DEFAULT_HISTORY_SIZE)];
static term_char_t *vt_constextbufrows[VBF_DEFAULT_HISTORY_SIZE];
static struct vt_window	vt_conswindow = {
	.vw_number = VT_CONSWINDOW,
	.vw_flags = VWF_CONSOLE,
	.vw_buf = {
		.vb_buffer = &vt_constextbuf[0],
		.vb_rows = &vt_constextbufrows[0],
		.vb_history_size = VBF_DEFAULT_HISTORY_SIZE,
		.vb_curroffset = 0,
		.vb_roffset = 0,
		.vb_flags = VBF_STATIC,
		.vb_mark_start = {.tp_row = 0, .tp_col = 0,},
		.vb_mark_end = {.tp_row = 0, .tp_col = 0,},
		.vb_scr_size = {
			.tp_row = _VTDEFH,
			.tp_col = _VTDEFW,
		},
	},
	.vw_device = &vt_consdev,
	.vw_terminal = &vt_consterm,
	.vw_kbdmode = K_XLATE,
	.vw_grabbed = 0,
};
struct terminal vt_consterm = {
	.tm_class = &vt_termclass,
	.tm_softc = &vt_conswindow,
	.tm_flags = TF_CONS,
};
static struct consdev vt_consterm_consdev = {
	.cn_ops = &termcn_cnops,
	.cn_arg = &vt_consterm,
	.cn_name = "ttyv0",
};

/* Add to set of consoles. */
DATA_SET(cons_set, vt_consterm_consdev);

/*
 * Right after kmem is done to allow early drivers to use locking and allocate
 * memory.
 */
SYSINIT(vt_update_static, SI_SUB_KMEM, SI_ORDER_ANY, vt_update_static,
    &vt_consdev);
/* Delay until all devices attached, to not waste time. */
SYSINIT(vt_early_cons, SI_SUB_INT_CONFIG_HOOKS, SI_ORDER_ANY, vt_upgrade,
    &vt_consdev);

/* Initialize locks/mem depended members. */
static void
vt_update_static(void *dummy)
{

	if (!vty_enabled(VTY_VT))
		return;
	if (main_vd->vd_driver != NULL)
		printf("VT(%s): %s %ux%u\n", main_vd->vd_driver->vd_name,
		    (main_vd->vd_flags & VDF_TEXTMODE) ? "text" : "resolution",
		    main_vd->vd_width, main_vd->vd_height);
	else
		printf("VT: init without driver.\n");

	mtx_init(&main_vd->vd_lock, "vtdev", NULL, MTX_DEF);
	cv_init(&main_vd->vd_winswitch, "vtwswt");
}

static void
vt_schedule_flush(struct vt_device *vd, int ms)
{

	if (ms <= 0)
		/* Default to initial value. */
		ms = 1000 / VT_TIMERFREQ;

	callout_schedule(&vd->vd_timer, hz / (1000 / ms));
}

void
vt_resume_flush_timer(struct vt_window *vw, int ms)
{
	struct vt_device *vd = vw->vw_device;

	if (vd->vd_curwindow != vw)
		return;

	if (!(vd->vd_flags & VDF_ASYNC) ||
	    !atomic_cmpset_int(&vd->vd_timer_armed, 0, 1))
		return;

	vt_schedule_flush(vd, ms);
}

static void
vt_suspend_flush_timer(struct vt_device *vd)
{
	/*
	 * As long as this function is called locked, callout_stop()
	 * has the same effect like callout_drain() with regard to
	 * preventing the callback function from executing.
	 */
	VT_LOCK_ASSERT(vd, MA_OWNED);

	if (!(vd->vd_flags & VDF_ASYNC) ||
	    !atomic_cmpset_int(&vd->vd_timer_armed, 1, 0))
		return;

	callout_stop(&vd->vd_timer);
}

static void
vt_switch_timer(void *arg)
{

	vt_late_window_switch((struct vt_window *)arg);
}

static int
vt_save_kbd_mode(struct vt_window *vw, keyboard_t *kbd)
{
	int mode, ret;

	mode = 0;
	ret = kbdd_ioctl(kbd, KDGKBMODE, (caddr_t)&mode);
	if (ret == ENOIOCTL)
		ret = ENODEV;
	if (ret != 0)
		return (ret);

	vw->vw_kbdmode = mode;

	return (0);
}

static int
vt_update_kbd_mode(struct vt_window *vw, keyboard_t *kbd)
{
	int ret;

	ret = kbdd_ioctl(kbd, KDSKBMODE, (caddr_t)&vw->vw_kbdmode);
	if (ret == ENOIOCTL)
		ret = ENODEV;

	return (ret);
}

static int
vt_save_kbd_state(struct vt_window *vw, keyboard_t *kbd)
{
	int state, ret;

	state = 0;
	ret = kbdd_ioctl(kbd, KDGKBSTATE, (caddr_t)&state);
	if (ret == ENOIOCTL)
		ret = ENODEV;
	if (ret != 0)
		return (ret);

	vw->vw_kbdstate &= ~LOCK_MASK;
	vw->vw_kbdstate |= state & LOCK_MASK;

	return (0);
}

static int
vt_update_kbd_state(struct vt_window *vw, keyboard_t *kbd)
{
	int state, ret;

	state = vw->vw_kbdstate & LOCK_MASK;
	ret = kbdd_ioctl(kbd, KDSKBSTATE, (caddr_t)&state);
	if (ret == ENOIOCTL)
		ret = ENODEV;

	return (ret);
}

static int
vt_save_kbd_leds(struct vt_window *vw, keyboard_t *kbd)
{
	int leds, ret;

	leds = 0;
	ret = kbdd_ioctl(kbd, KDGETLED, (caddr_t)&leds);
	if (ret == ENOIOCTL)
		ret = ENODEV;
	if (ret != 0)
		return (ret);

	vw->vw_kbdstate &= ~LED_MASK;
	vw->vw_kbdstate |= leds & LED_MASK;

	return (0);
}

static int
vt_update_kbd_leds(struct vt_window *vw, keyboard_t *kbd)
{
	int leds, ret;

	leds = vw->vw_kbdstate & LED_MASK;
	ret = kbdd_ioctl(kbd, KDSETLED, (caddr_t)&leds);
	if (ret == ENOIOCTL)
		ret = ENODEV;

	return (ret);
}

static int
vt_window_preswitch(struct vt_window *vw, struct vt_window *curvw)
{

	DPRINTF(40, "%s\n", __func__);
	curvw->vw_switch_to = vw;
	/* Set timer to allow switch in case when process hang. */
	callout_reset(&vw->vw_proc_dead_timer, hz * vt_deadtimer,
	    vt_switch_timer, (void *)vw);
	/* Notify process about vt switch attempt. */
	DPRINTF(30, "%s: Notify process.\n", __func__);
	signal_vt_rel(curvw);

	return (0);
}

static int
vt_window_postswitch(struct vt_window *vw)
{

	signal_vt_acq(vw);
	return (0);
}

/* vt_late_window_switch will done VT switching for regular case. */
static int
vt_late_window_switch(struct vt_window *vw)
{
	int ret;

	callout_stop(&vw->vw_proc_dead_timer);

	ret = vt_window_switch(vw);
	if (ret)
		return (ret);

	/* Notify owner process about terminal availability. */
	if (vw->vw_smode.mode == VT_PROCESS) {
		ret = vt_window_postswitch(vw);
	}
	return (ret);
}

/* Switch window. */
static int
vt_proc_window_switch(struct vt_window *vw)
{
	struct vt_window *curvw;
	struct vt_device *vd;
	int ret;

	/* Prevent switching to NULL */
	if (vw == NULL) {
		DPRINTF(30, "%s: Cannot switch: vw is NULL.", __func__);
		return (EINVAL);
	}
	vd = vw->vw_device;
	curvw = vd->vd_curwindow;

	/* Check if virtual terminal is locked */
	if (curvw->vw_flags & VWF_VTYLOCK)
		return (EBUSY);

	/* Check if switch already in progress */
	if (curvw->vw_flags & VWF_SWWAIT_REL) {
		/* Check if switching to same window */
		if (curvw->vw_switch_to == vw) {
			DPRINTF(30, "%s: Switch in progress to same vw.", __func__);
			return (0);	/* success */
		}
		DPRINTF(30, "%s: Switch in progress to different vw.", __func__);
		return (EBUSY);
	}

	/* Avoid switching to already selected window */
	if (vw == curvw) {
		DPRINTF(30, "%s: Cannot switch: vw == curvw.", __func__);
		return (0);	/* success */
	}

	/* Ask current process permission to switch away. */
	if (curvw->vw_smode.mode == VT_PROCESS) {
		DPRINTF(30, "%s: VT_PROCESS ", __func__);
		if (vt_proc_alive(curvw) == FALSE) {
			DPRINTF(30, "Dead. Cleaning.");
			/* Dead */
		} else {
			DPRINTF(30, "%s: Signaling process.\n", __func__);
			/* Alive, try to ask him. */
			ret = vt_window_preswitch(vw, curvw);
			/* Wait for process answer or timeout. */
			return (ret);
		}
		DPRINTF(30, "\n");
	}

	ret = vt_late_window_switch(vw);
	return (ret);
}

/* Switch window ignoring process locking. */
static int
vt_window_switch(struct vt_window *vw)
{
	struct vt_device *vd = vw->vw_device;
	struct vt_window *curvw = vd->vd_curwindow;
	keyboard_t *kbd;

	if (kdb_active) {
		/*
		 * When grabbing the console for the debugger, avoid
		 * locks as that can result in deadlock.  While this
		 * could use try locks, that wouldn't really make a
		 * difference as there are sufficient barriers in
		 * debugger entry/exit to be equivalent to
		 * successfully try-locking here.
		 */
		if (curvw == vw)
			return (0);
		if (!(vw->vw_flags & (VWF_OPENED|VWF_CONSOLE)))
			return (EINVAL);

		vd->vd_curwindow = vw;
		vd->vd_flags |= VDF_INVALID;
		if (vd->vd_driver->vd_postswitch)
			vd->vd_driver->vd_postswitch(vd);
		return (0);
	}

	VT_LOCK(vd);
	if (curvw == vw) {
		/* Nothing to do. */
		VT_UNLOCK(vd);
		return (0);
	}
	if (!(vw->vw_flags & (VWF_OPENED|VWF_CONSOLE))) {
		VT_UNLOCK(vd);
		return (EINVAL);
	}

	vt_suspend_flush_timer(vd);

	vd->vd_curwindow = vw;
	vd->vd_flags |= VDF_INVALID;
	cv_broadcast(&vd->vd_winswitch);
	VT_UNLOCK(vd);

	if (vd->vd_driver->vd_postswitch)
		vd->vd_driver->vd_postswitch(vd);

	vt_resume_flush_timer(vw, 0);

	/* Restore per-window keyboard mode. */
	mtx_lock(&Giant);
	kbd = kbd_get_keyboard(vd->vd_keyboard);
	if (kbd != NULL) {
		if (curvw->vw_kbdmode == K_XLATE)
			vt_save_kbd_state(curvw, kbd);

		vt_update_kbd_mode(vw, kbd);
		vt_update_kbd_state(vw, kbd);
	}
	mtx_unlock(&Giant);
	DPRINTF(10, "%s(ttyv%d) done\n", __func__, vw->vw_number);

	return (0);
}

void
vt_termsize(struct vt_device *vd, struct vt_font *vf, term_pos_t *size)
{

	size->tp_row = vd->vd_height;
	if (vt_draw_logo_cpus)
		size->tp_row -= vt_logo_sprite_height;
	size->tp_col = vd->vd_width;
	if (vf != NULL) {
		size->tp_row /= vf->vf_height;
		size->tp_col /= vf->vf_width;
	}
}

static inline void
vt_termrect(struct vt_device *vd, struct vt_font *vf, term_rect_t *rect)
{

	rect->tr_begin.tp_row = rect->tr_begin.tp_col = 0;
	if (vt_draw_logo_cpus)
		rect->tr_begin.tp_row = vt_logo_sprite_height;

	rect->tr_end.tp_row = vd->vd_height;
	rect->tr_end.tp_col = vd->vd_width;

	if (vf != NULL) {
		rect->tr_begin.tp_row =
		    howmany(rect->tr_begin.tp_row, vf->vf_height);

		rect->tr_end.tp_row /= vf->vf_height;
		rect->tr_end.tp_col /= vf->vf_width;
	}
}

void
vt_winsize(struct vt_device *vd, struct vt_font *vf, struct winsize *size)
{

	size->ws_ypixel = vd->vd_height;
	if (vt_draw_logo_cpus)
		size->ws_ypixel -= vt_logo_sprite_height;
	size->ws_row = size->ws_ypixel;
	size->ws_col = size->ws_xpixel = vd->vd_width;
	if (vf != NULL) {
		size->ws_row /= vf->vf_height;
		size->ws_col /= vf->vf_width;
	}
}

void
vt_compute_drawable_area(struct vt_window *vw)
{
	struct vt_device *vd;
	struct vt_font *vf;
	vt_axis_t height;

	vd = vw->vw_device;

	if (vw->vw_font == NULL) {
		vw->vw_draw_area.tr_begin.tp_col = 0;
		vw->vw_draw_area.tr_begin.tp_row = 0;
		if (vt_draw_logo_cpus)
			vw->vw_draw_area.tr_begin.tp_row = vt_logo_sprite_height;
		vw->vw_draw_area.tr_end.tp_col = vd->vd_width;
		vw->vw_draw_area.tr_end.tp_row = vd->vd_height;
		return;
	}

	vf = vw->vw_font;

	/*
	 * Compute the drawable area, so that the text is centered on
	 * the screen.
	 */

	height = vd->vd_height;
	if (vt_draw_logo_cpus)
		height -= vt_logo_sprite_height;
	vw->vw_draw_area.tr_begin.tp_col = (vd->vd_width % vf->vf_width) / 2;
	vw->vw_draw_area.tr_begin.tp_row = (height % vf->vf_height) / 2;
	if (vt_draw_logo_cpus)
		vw->vw_draw_area.tr_begin.tp_row += vt_logo_sprite_height;
	vw->vw_draw_area.tr_end.tp_col = vw->vw_draw_area.tr_begin.tp_col +
	    rounddown(vd->vd_width, vf->vf_width);
	vw->vw_draw_area.tr_end.tp_row = vw->vw_draw_area.tr_begin.tp_row +
	    rounddown(height, vf->vf_height);
}

static void
vt_scroll(struct vt_window *vw, int offset, int whence)
{
	int diff;
	term_pos_t size;

	if ((vw->vw_flags & VWF_SCROLL) == 0)
		return;

	vt_termsize(vw->vw_device, vw->vw_font, &size);

	diff = vthistory_seek(&vw->vw_buf, offset, whence);
	if (diff)
		vw->vw_device->vd_flags |= VDF_INVALID;
	vt_resume_flush_timer(vw, 0);
}

static int
vt_machine_kbdevent(int c)
{

	switch (c) {
	case SPCLKEY | DBG: /* kbdmap(5) keyword `debug`. */
		if (vt_kbd_debug)
			kdb_enter(KDB_WHY_BREAK, "manual escape to debugger");
		return (1);
	case SPCLKEY | HALT: /* kbdmap(5) keyword `halt`. */
		if (vt_kbd_halt)
			shutdown_nice(RB_HALT);
		return (1);
	case SPCLKEY | PASTE: /* kbdmap(5) keyword `paste`. */
#ifndef SC_NO_CUTPASTE
		/* Insert text from cut-paste buffer. */
		vt_mouse_paste();
#endif
		break;
	case SPCLKEY | PDWN: /* kbdmap(5) keyword `pdwn`. */
		if (vt_kbd_poweroff)
			shutdown_nice(RB_HALT|RB_POWEROFF);
		return (1);
	case SPCLKEY | PNC: /* kbdmap(5) keyword `panic`. */
		/*
		 * Request to immediate panic if sysctl
		 * kern.vt.enable_panic_key allow it.
		 */
		if (vt_kbd_panic)
			panic("Forced by the panic key");
		return (1);
	case SPCLKEY | RBT: /* kbdmap(5) keyword `boot`. */
		if (vt_kbd_reboot)
			shutdown_nice(RB_AUTOBOOT);
		return (1);
	case SPCLKEY | SPSC: /* kbdmap(5) keyword `spsc`. */
		/* Force activatation/deactivation of the screen saver. */
		/* TODO */
		return (1);
	case SPCLKEY | STBY: /* XXX Not present in kbdcontrol parser. */
		/* Put machine into Stand-By mode. */
		power_pm_suspend(POWER_SLEEP_STATE_STANDBY);
		return (1);
	case SPCLKEY | SUSP: /* kbdmap(5) keyword `susp`. */
		/* Suspend machine. */
		power_pm_suspend(POWER_SLEEP_STATE_SUSPEND);
		return (1);
	}

	return (0);
}

static void
vt_scrollmode_kbdevent(struct vt_window *vw, int c, int console)
{
	struct vt_device *vd;
	term_pos_t size;

	vd = vw->vw_device;
	/* Only special keys handled in ScrollLock mode */
	if ((c & SPCLKEY) == 0)
		return;

	c &= ~SPCLKEY;

	if (console == 0) {
		if (c >= F_SCR && c <= MIN(L_SCR, F_SCR + VT_MAXWINDOWS - 1)) {
			vw = vd->vd_windows[c - F_SCR];
			vt_proc_window_switch(vw);
			return;
		}
		VT_LOCK(vd);
	}

	switch (c) {
	case SLK: {
		/* Turn scrolling off. */
		vt_scroll(vw, 0, VHS_END);
		VTBUF_SLCK_DISABLE(&vw->vw_buf);
		vw->vw_flags &= ~VWF_SCROLL;
		break;
	}
	case FKEY | F(49): /* Home key. */
		vt_scroll(vw, 0, VHS_SET);
		break;
	case FKEY | F(50): /* Arrow up. */
		vt_scroll(vw, -1, VHS_CUR);
		break;
	case FKEY | F(51): /* Page up. */
		vt_termsize(vd, vw->vw_font, &size);
		vt_scroll(vw, -size.tp_row, VHS_CUR);
		break;
	case FKEY | F(57): /* End key. */
		vt_scroll(vw, 0, VHS_END);
		break;
	case FKEY | F(58): /* Arrow down. */
		vt_scroll(vw, 1, VHS_CUR);
		break;
	case FKEY | F(59): /* Page down. */
		vt_termsize(vd, vw->vw_font, &size);
		vt_scroll(vw, size.tp_row, VHS_CUR);
		break;
	}

	if (console == 0)
		VT_UNLOCK(vd);
}

static int
vt_processkey(keyboard_t *kbd, struct vt_device *vd, int c)
{
	struct vt_window *vw = vd->vd_curwindow;

	random_harvest_queue(&c, sizeof(c), RANDOM_KEYBOARD);
#if VT_ALT_TO_ESC_HACK
	if (c & RELKEY) {
		switch (c & ~RELKEY) {
		case (SPCLKEY | RALT):
			if (vt_enable_altgr != 0)
				break;
		case (SPCLKEY | LALT):
			vd->vd_kbstate &= ~ALKED;
		}
		/* Other keys ignored for RELKEY event. */
		return (0);
	} else {
		switch (c & ~RELKEY) {
		case (SPCLKEY | RALT):
			if (vt_enable_altgr != 0)
				break;
		case (SPCLKEY | LALT):
			vd->vd_kbstate |= ALKED;
		}
	}
#else
	if (c & RELKEY)
		/* Other keys ignored for RELKEY event. */
		return (0);
#endif

	if (vt_machine_kbdevent(c))
		return (0);

	if (vw->vw_flags & VWF_SCROLL) {
		vt_scrollmode_kbdevent(vw, c, 0/* Not a console */);
		/* Scroll mode keys handled, nothing to do more. */
		return (0);
	}

	if (c & SPCLKEY) {
		c &= ~SPCLKEY;

		if (c >= F_SCR && c <= MIN(L_SCR, F_SCR + VT_MAXWINDOWS - 1)) {
			vw = vd->vd_windows[c - F_SCR];
			vt_proc_window_switch(vw);
			return (0);
		}

		switch (c) {
		case NEXT:
			/* Switch to next VT. */
			c = (vw->vw_number + 1) % VT_MAXWINDOWS;
			vw = vd->vd_windows[c];
			vt_proc_window_switch(vw);
			return (0);
		case PREV:
			/* Switch to previous VT. */
			c = (vw->vw_number + VT_MAXWINDOWS - 1) % VT_MAXWINDOWS;
			vw = vd->vd_windows[c];
			vt_proc_window_switch(vw);
			return (0);
		case SLK: {
			vt_save_kbd_state(vw, kbd);
			VT_LOCK(vd);
			if (vw->vw_kbdstate & SLKED) {
				/* Turn scrolling on. */
				vw->vw_flags |= VWF_SCROLL;
				VTBUF_SLCK_ENABLE(&vw->vw_buf);
			} else {
				/* Turn scrolling off. */
				vw->vw_flags &= ~VWF_SCROLL;
				VTBUF_SLCK_DISABLE(&vw->vw_buf);
				vt_scroll(vw, 0, VHS_END);
			}
			VT_UNLOCK(vd);
			break;
		}
		case FKEY | F(1):  case FKEY | F(2):  case FKEY | F(3):
		case FKEY | F(4):  case FKEY | F(5):  case FKEY | F(6):
		case FKEY | F(7):  case FKEY | F(8):  case FKEY | F(9):
		case FKEY | F(10): case FKEY | F(11): case FKEY | F(12):
			/* F1 through F12 keys. */
			terminal_input_special(vw->vw_terminal,
			    TKEY_F1 + c - (FKEY | F(1)));
			break;
		case FKEY | F(49): /* Home key. */
			terminal_input_special(vw->vw_terminal, TKEY_HOME);
			break;
		case FKEY | F(50): /* Arrow up. */
			terminal_input_special(vw->vw_terminal, TKEY_UP);
			break;
		case FKEY | F(51): /* Page up. */
			terminal_input_special(vw->vw_terminal, TKEY_PAGE_UP);
			break;
		case FKEY | F(53): /* Arrow left. */
			terminal_input_special(vw->vw_terminal, TKEY_LEFT);
			break;
		case FKEY | F(55): /* Arrow right. */
			terminal_input_special(vw->vw_terminal, TKEY_RIGHT);
			break;
		case FKEY | F(57): /* End key. */
			terminal_input_special(vw->vw_terminal, TKEY_END);
			break;
		case FKEY | F(58): /* Arrow down. */
			terminal_input_special(vw->vw_terminal, TKEY_DOWN);
			break;
		case FKEY | F(59): /* Page down. */
			terminal_input_special(vw->vw_terminal, TKEY_PAGE_DOWN);
			break;
		case FKEY | F(60): /* Insert key. */
			terminal_input_special(vw->vw_terminal, TKEY_INSERT);
			break;
		case FKEY | F(61): /* Delete key. */
			terminal_input_special(vw->vw_terminal, TKEY_DELETE);
			break;
		}
	} else if (KEYFLAGS(c) == 0) {
		/* Don't do UTF-8 conversion when doing raw mode. */
		if (vw->vw_kbdmode == K_XLATE) {
#if VT_ALT_TO_ESC_HACK
			if (vd->vd_kbstate & ALKED) {
				/*
				 * Prepend ESC sequence if one of ALT keys down.
				 */
				terminal_input_char(vw->vw_terminal, 0x1b);
			}
#endif
#if defined(KDB)
			kdb_alt_break(c, &vd->vd_altbrk);
#endif
			terminal_input_char(vw->vw_terminal, KEYCHAR(c));
		} else
			terminal_input_raw(vw->vw_terminal, c);
	}
	return (0);
}

static int
vt_kbdevent(keyboard_t *kbd, int event, void *arg)
{
	struct vt_device *vd = arg;
	int c;

	switch (event) {
	case KBDIO_KEYINPUT:
		break;
	case KBDIO_UNLOADING:
		mtx_lock(&Giant);
		vd->vd_keyboard = -1;
		kbd_release(kbd, (void *)vd);
		mtx_unlock(&Giant);
		return (0);
	default:
		return (EINVAL);
	}

	while ((c = kbdd_read_char(kbd, 0)) != NOKEY)
		vt_processkey(kbd, vd, c);

	return (0);
}

static int
vt_allocate_keyboard(struct vt_device *vd)
{
	int		 grabbed, i, idx0, idx;
	keyboard_t	*k0, *k;
	keyboard_info_t	 ki;

	/*
	 * If vt_upgrade() happens while the console is grabbed, we are
	 * potentially going to switch keyboard devices while the keyboard is in
	 * use. Unwind the grabbing of the current keyboard first, then we will
	 * re-grab the new keyboard below, before we return.
	 */
	if (vd->vd_curwindow == &vt_conswindow) {
		grabbed = vd->vd_curwindow->vw_grabbed;
		for (i = 0; i < grabbed; ++i)
			vtterm_cnungrab(vd->vd_curwindow->vw_terminal);
	}

	idx0 = kbd_allocate("kbdmux", -1, vd, vt_kbdevent, vd);
	if (idx0 >= 0) {
		DPRINTF(20, "%s: kbdmux allocated, idx = %d\n", __func__, idx0);
		k0 = kbd_get_keyboard(idx0);

		for (idx = kbd_find_keyboard2("*", -1, 0);
		     idx != -1;
		     idx = kbd_find_keyboard2("*", -1, idx + 1)) {
			k = kbd_get_keyboard(idx);

			if (idx == idx0 || KBD_IS_BUSY(k))
				continue;

			bzero(&ki, sizeof(ki));
			strncpy(ki.kb_name, k->kb_name, sizeof(ki.kb_name));
			ki.kb_name[sizeof(ki.kb_name) - 1] = '\0';
			ki.kb_unit = k->kb_unit;

			kbdd_ioctl(k0, KBADDKBD, (caddr_t) &ki);
		}
	} else {
		DPRINTF(20, "%s: no kbdmux allocated\n", __func__);
		idx0 = kbd_allocate("*", -1, vd, vt_kbdevent, vd);
		if (idx0 < 0) {
			DPRINTF(10, "%s: No keyboard found.\n", __func__);
			return (-1);
		}
	}
	vd->vd_keyboard = idx0;
	DPRINTF(20, "%s: vd_keyboard = %d\n", __func__, vd->vd_keyboard);

	if (vd->vd_curwindow == &vt_conswindow) {
		for (i = 0; i < grabbed; ++i)
			vtterm_cngrab(vd->vd_curwindow->vw_terminal);
	}

	return (idx0);
}

static void
vtterm_bell(struct terminal *tm)
{
	struct vt_window *vw = tm->tm_softc;
	struct vt_device *vd = vw->vw_device;

	if (!vt_enable_bell)
		return;

	if (vd->vd_flags & VDF_QUIET_BELL)
		return;

	sysbeep(1193182 / VT_BELLPITCH, VT_BELLDURATION);
}

static void
vtterm_beep(struct terminal *tm, u_int param)
{
	u_int freq, period;

	if (!vt_enable_bell)
		return;

	if ((param == 0) || ((param & 0xffff) == 0)) {
		vtterm_bell(tm);
		return;
	}

	period = ((param >> 16) & 0xffff) * hz / 1000;
	freq = 1193182 / (param & 0xffff);

	sysbeep(freq, period);
}

static void
vtterm_cursor(struct terminal *tm, const term_pos_t *p)
{
	struct vt_window *vw = tm->tm_softc;

	vtbuf_cursor_position(&vw->vw_buf, p);
}

static void
vtterm_putchar(struct terminal *tm, const term_pos_t *p, term_char_t c)
{
	struct vt_window *vw = tm->tm_softc;

	vtbuf_putchar(&vw->vw_buf, p, c);
}

static void
vtterm_fill(struct terminal *tm, const term_rect_t *r, term_char_t c)
{
	struct vt_window *vw = tm->tm_softc;

	vtbuf_fill(&vw->vw_buf, r, c);
}

static void
vtterm_copy(struct terminal *tm, const term_rect_t *r,
    const term_pos_t *p)
{
	struct vt_window *vw = tm->tm_softc;

	vtbuf_copy(&vw->vw_buf, r, p);
}

static void
vtterm_param(struct terminal *tm, int cmd, unsigned int arg)
{
	struct vt_window *vw = tm->tm_softc;

	switch (cmd) {
	case TP_SETLOCALCURSOR:
		/*
		 * 0 means normal (usually block), 1 means hidden, and
		 * 2 means blinking (always block) for compatibility with
		 * syscons.  We don't support any changes except hiding,
		 * so must map 2 to 0.
		 */
		arg = (arg == 1) ? 0 : 1;
		/* FALLTHROUGH */
	case TP_SHOWCURSOR:
		vtbuf_cursor_visibility(&vw->vw_buf, arg);
		vt_resume_flush_timer(vw, 0);
		break;
	case TP_MOUSE:
		vw->vw_mouse_level = arg;
		break;
	}
}

void
vt_determine_colors(term_char_t c, int cursor,
    term_color_t *fg, term_color_t *bg)
{
	term_color_t tmp;
	int invert;

	invert = 0;

	*fg = TCHAR_FGCOLOR(c);
	if (TCHAR_FORMAT(c) & TF_BOLD)
		*fg = TCOLOR_LIGHT(*fg);
	*bg = TCHAR_BGCOLOR(c);
	if (TCHAR_FORMAT(c) & TF_BLINK)
		*bg = TCOLOR_LIGHT(*bg);

	if (TCHAR_FORMAT(c) & TF_REVERSE)
		invert ^= 1;
	if (cursor)
		invert ^= 1;

	if (invert) {
		tmp = *fg;
		*fg = *bg;
		*bg = tmp;
	}
}

#ifndef SC_NO_CUTPASTE
int
vt_is_cursor_in_area(const struct vt_device *vd, const term_rect_t *area)
{
	unsigned int mx, my;

	/*
	 * We use the cursor position saved during the current refresh,
	 * in case the cursor moved since.
	 */
	mx = vd->vd_mx_drawn + vd->vd_curwindow->vw_draw_area.tr_begin.tp_col;
	my = vd->vd_my_drawn + vd->vd_curwindow->vw_draw_area.tr_begin.tp_row;

	if (mx >= area->tr_end.tp_col ||
	    mx + vd->vd_mcursor->width <= area->tr_begin.tp_col ||
	    my >= area->tr_end.tp_row ||
	    my + vd->vd_mcursor->height <= area->tr_begin.tp_row)
		return (0);
	return (1);
}

static void
vt_mark_mouse_position_as_dirty(struct vt_device *vd, int locked)
{
	term_rect_t area;
	struct vt_window *vw;
	struct vt_font *vf;
	int x, y;

	vw = vd->vd_curwindow;
	vf = vw->vw_font;

	x = vd->vd_mx_drawn;
	y = vd->vd_my_drawn;

	if (vf != NULL) {
		area.tr_begin.tp_col = x / vf->vf_width;
		area.tr_begin.tp_row = y / vf->vf_height;
		area.tr_end.tp_col =
		    ((x + vd->vd_mcursor->width) / vf->vf_width) + 1;
		area.tr_end.tp_row =
		    ((y + vd->vd_mcursor->height) / vf->vf_height) + 1;
	} else {
		/*
		 * No font loaded (ie. vt_vga operating in textmode).
		 *
		 * FIXME: This fake area needs to be revisited once the
		 * mouse cursor is supported in vt_vga's textmode.
		 */
		area.tr_begin.tp_col = x;
		area.tr_begin.tp_row = y;
		area.tr_end.tp_col = x + 2;
		area.tr_end.tp_row = y + 2;
	}

	if (!locked)
		vtbuf_lock(&vw->vw_buf);
	if (vd->vd_driver->vd_invalidate_text)
		vd->vd_driver->vd_invalidate_text(vd, &area);
	vtbuf_dirty(&vw->vw_buf, &area);
	if (!locked)
		vtbuf_unlock(&vw->vw_buf);
}
#endif

static void
vt_set_border(struct vt_device *vd, const term_rect_t *area,
    const term_color_t c)
{
	vd_drawrect_t *drawrect = vd->vd_driver->vd_drawrect;

	if (drawrect == NULL)
		return;

	/* Top bar */
	if (area->tr_begin.tp_row > 0)
		drawrect(vd, 0, 0, vd->vd_width - 1,
		    area->tr_begin.tp_row - 1, 1, c);

	/* Left bar */
	if (area->tr_begin.tp_col > 0)
		drawrect(vd, 0, area->tr_begin.tp_row,
		    area->tr_begin.tp_col - 1, area->tr_end.tp_row - 1, 1, c);

	/* Right bar */
	if (area->tr_end.tp_col < vd->vd_width)
		drawrect(vd, area->tr_end.tp_col, area->tr_begin.tp_row,
		    vd->vd_width - 1, area->tr_end.tp_row - 1, 1, c);

	/* Bottom bar */
	if (area->tr_end.tp_row < vd->vd_height)
		drawrect(vd, 0, area->tr_end.tp_row, vd->vd_width - 1,
		    vd->vd_height - 1, 1, c);
}

static int
vt_flush(struct vt_device *vd)
{
	struct vt_window *vw;
	struct vt_font *vf;
	term_rect_t tarea;
#ifndef SC_NO_CUTPASTE
	int cursor_was_shown, cursor_moved;
#endif

	vw = vd->vd_curwindow;
	if (vw == NULL)
		return (0);

	if (vd->vd_flags & VDF_SPLASH || vw->vw_flags & VWF_BUSY)
		return (0);

	vf = vw->vw_font;
	if (((vd->vd_flags & VDF_TEXTMODE) == 0) && (vf == NULL))
		return (0);

	vtbuf_lock(&vw->vw_buf);

#ifndef SC_NO_CUTPASTE
	cursor_was_shown = vd->vd_mshown;
	cursor_moved = (vd->vd_mx != vd->vd_mx_drawn ||
	    vd->vd_my != vd->vd_my_drawn);

	/* Check if the cursor should be displayed or not. */
	if ((vd->vd_flags & VDF_MOUSECURSOR) && /* Mouse support enabled. */
	    !(vw->vw_flags & VWF_MOUSE_HIDE) && /* Cursor displayed.      */
	    !kdb_active && panicstr == NULL) {  /* DDB inactive.          */
		vd->vd_mshown = 1;
	} else {
		vd->vd_mshown = 0;
	}

	/*
	 * If the cursor changed display state or moved, we must mark
	 * the old position as dirty, so that it's erased.
	 */
	if (cursor_was_shown != vd->vd_mshown ||
	    (vd->vd_mshown && cursor_moved))
		vt_mark_mouse_position_as_dirty(vd, true);

	/*
         * Save position of the mouse cursor. It's used by backends to
         * know where to draw the cursor and during the next refresh to
         * erase the previous position.
	 */
	vd->vd_mx_drawn = vd->vd_mx;
	vd->vd_my_drawn = vd->vd_my;

	/*
	 * If the cursor is displayed and has moved since last refresh,
	 * mark the new position as dirty.
	 */
	if (vd->vd_mshown && cursor_moved)
		vt_mark_mouse_position_as_dirty(vd, true);
#endif

	vtbuf_undirty(&vw->vw_buf, &tarea);

	/* Force a full redraw when the screen contents might be invalid. */
	if (vd->vd_flags & (VDF_INVALID | VDF_SUSPENDED)) {
		vd->vd_flags &= ~VDF_INVALID;

		vt_set_border(vd, &vw->vw_draw_area, TC_BLACK);
		vt_termrect(vd, vf, &tarea);
		if (vd->vd_driver->vd_invalidate_text)
			vd->vd_driver->vd_invalidate_text(vd, &tarea);
		if (vt_draw_logo_cpus)
			vtterm_draw_cpu_logos(vd);
	}

	if (tarea.tr_begin.tp_col < tarea.tr_end.tp_col) {
		vd->vd_driver->vd_bitblt_text(vd, vw, &tarea);
		vtbuf_unlock(&vw->vw_buf);
		return (1);
	}

	vtbuf_unlock(&vw->vw_buf);
	return (0);
}

static void
vt_timer(void *arg)
{
	struct vt_device *vd;
	int changed;

	vd = arg;
	/* Update screen if required. */
	changed = vt_flush(vd);

	/* Schedule for next update. */
	if (changed)
		vt_schedule_flush(vd, 0);
	else
		vd->vd_timer_armed = 0;
}

static void
vtterm_pre_input(struct terminal *tm)
{
	struct vt_window *vw = tm->tm_softc;

	vtbuf_lock(&vw->vw_buf);
}

static void
vtterm_post_input(struct terminal *tm)
{
	struct vt_window *vw = tm->tm_softc;

	vtbuf_unlock(&vw->vw_buf);
	vt_resume_flush_timer(vw, 0);
}

static void
vtterm_done(struct terminal *tm)
{
	struct vt_window *vw = tm->tm_softc;
	struct vt_device *vd = vw->vw_device;

	if (kdb_active || panicstr != NULL) {
		/* Switch to the debugger. */
		if (vd->vd_curwindow != vw) {
			vd->vd_curwindow = vw;
			vd->vd_flags |= VDF_INVALID;
			if (vd->vd_driver->vd_postswitch)
				vd->vd_driver->vd_postswitch(vd);
		}
		vd->vd_flags &= ~VDF_SPLASH;
		vt_flush(vd);
	} else if (!(vd->vd_flags & VDF_ASYNC)) {
		vt_flush(vd);
	}
}

#ifdef DEV_SPLASH
static void
vtterm_splash(struct vt_device *vd)
{
	vt_axis_t top, left;

	/* Display a nice boot splash. */
	if (!(vd->vd_flags & VDF_TEXTMODE) && (boothowto & RB_MUTE)) {

		top = (vd->vd_height - vt_logo_height) / 2;
		left = (vd->vd_width - vt_logo_width) / 2;
		switch (vt_logo_depth) {
		case 1:
			/* XXX: Unhardcode colors! */
			vd->vd_driver->vd_bitblt_bmp(vd, vd->vd_curwindow,
			    vt_logo_image, NULL, vt_logo_width, vt_logo_height,
			    left, top, TC_WHITE, TC_BLACK);
		}
		vd->vd_flags |= VDF_SPLASH;
	}
}
#endif


static void
vtterm_cnprobe(struct terminal *tm, struct consdev *cp)
{
	struct vt_driver *vtd, **vtdlist, *vtdbest = NULL;
	struct vt_window *vw = tm->tm_softc;
	struct vt_device *vd = vw->vw_device;
	struct winsize wsz;
	term_attr_t attr;
	term_char_t c;

	if (!vty_enabled(VTY_VT))
		return;

	if (vd->vd_flags & VDF_INITIALIZED)
		/* Initialization already done. */
		return;

	SET_FOREACH(vtdlist, vt_drv_set) {
		vtd = *vtdlist;
		if (vtd->vd_probe == NULL)
			continue;
		if (vtd->vd_probe(vd) == CN_DEAD)
			continue;
		if ((vtdbest == NULL) ||
		    (vtd->vd_priority > vtdbest->vd_priority))
			vtdbest = vtd;
	}
	if (vtdbest == NULL) {
		cp->cn_pri = CN_DEAD;
		vd->vd_flags |= VDF_DEAD;
	} else {
		vd->vd_driver = vtdbest;
		cp->cn_pri = vd->vd_driver->vd_init(vd);
	}

	/* Check if driver's vt_init return CN_DEAD. */
	if (cp->cn_pri == CN_DEAD) {
		vd->vd_flags |= VDF_DEAD;
	}

	/* Initialize any early-boot keyboard drivers */
	kbd_configure(KB_CONF_PROBE_ONLY);

	vd->vd_unit = atomic_fetchadd_int(&vt_unit, 1);
	vd->vd_windows[VT_CONSWINDOW] = vw;
	sprintf(cp->cn_name, "ttyv%r", VT_UNIT(vw));

	/* Attach default font if not in TEXTMODE. */
	if ((vd->vd_flags & VDF_TEXTMODE) == 0) {
		vw->vw_font = vtfont_ref(&vt_font_default);
		vt_compute_drawable_area(vw);
	}

	/*
	 * The original screen size was faked (_VTDEFW x _VTDEFH). Now
	 * that we have the real viewable size, fix it in the static
	 * buffer.
	 */
	if (vd->vd_width != 0 && vd->vd_height != 0)
		vt_termsize(vd, vw->vw_font, &vw->vw_buf.vb_scr_size);

	vtbuf_init_early(&vw->vw_buf);
	vt_winsize(vd, vw->vw_font, &wsz);
	c = (boothowto & RB_MUTE) == 0 ? TERMINAL_KERN_ATTR :
	    TERMINAL_NORM_ATTR;
	attr.ta_format = TCHAR_FORMAT(c);
	attr.ta_fgcolor = TCHAR_FGCOLOR(c);
	attr.ta_bgcolor = TCHAR_BGCOLOR(c);
	terminal_set_winsize_blank(tm, &wsz, 1, &attr);

	if (vtdbest != NULL) {
#ifdef DEV_SPLASH
		if (!vt_splash_cpu)
			vtterm_splash(vd);
#endif
		vd->vd_flags |= VDF_INITIALIZED;
	}
}

static int
vtterm_cngetc(struct terminal *tm)
{
	struct vt_window *vw = tm->tm_softc;
	struct vt_device *vd = vw->vw_device;
	keyboard_t *kbd;
	u_int c;

	if (vw->vw_kbdsq && *vw->vw_kbdsq)
		return (*vw->vw_kbdsq++);

	/* Make sure the splash screen is not there. */
	if (vd->vd_flags & VDF_SPLASH) {
		/* Remove splash */
		vd->vd_flags &= ~VDF_SPLASH;
		/* Mark screen as invalid to force update */
		vd->vd_flags |= VDF_INVALID;
		vt_flush(vd);
	}

	/* Stripped down keyboard handler. */
	kbd = kbd_get_keyboard(vd->vd_keyboard);
	if (kbd == NULL)
		return (-1);

	/* Force keyboard input mode to K_XLATE */
	vw->vw_kbdmode = K_XLATE;
	vt_update_kbd_mode(vw, kbd);

	/* Switch the keyboard to polling to make it work here. */
	kbdd_poll(kbd, TRUE);
	c = kbdd_read_char(kbd, 0);
	kbdd_poll(kbd, FALSE);
	if (c & RELKEY)
		return (-1);

	if (vw->vw_flags & VWF_SCROLL) {
		vt_scrollmode_kbdevent(vw, c, 1/* Console mode */);
		vt_flush(vd);
		return (-1);
	}

	/* Stripped down handling of vt_kbdevent(), without locking, etc. */
	if (c & SPCLKEY) {
		switch (c) {
		case SPCLKEY | SLK:
			vt_save_kbd_state(vw, kbd);
			if (vw->vw_kbdstate & SLKED) {
				/* Turn scrolling on. */
				vw->vw_flags |= VWF_SCROLL;
				VTBUF_SLCK_ENABLE(&vw->vw_buf);
			} else {
				/* Turn scrolling off. */
				vt_scroll(vw, 0, VHS_END);
				vw->vw_flags &= ~VWF_SCROLL;
				VTBUF_SLCK_DISABLE(&vw->vw_buf);
			}
			break;
		/* XXX: KDB can handle history. */
		case SPCLKEY | FKEY | F(50): /* Arrow up. */
			vw->vw_kbdsq = "\x1b[A";
			break;
		case SPCLKEY | FKEY | F(58): /* Arrow down. */
			vw->vw_kbdsq = "\x1b[B";
			break;
		case SPCLKEY | FKEY | F(55): /* Arrow right. */
			vw->vw_kbdsq = "\x1b[C";
			break;
		case SPCLKEY | FKEY | F(53): /* Arrow left. */
			vw->vw_kbdsq = "\x1b[D";
			break;
		}

		/* Force refresh to make scrollback work. */
		vt_flush(vd);
	} else if (KEYFLAGS(c) == 0) {
		return (KEYCHAR(c));
	}

	if (vw->vw_kbdsq && *vw->vw_kbdsq)
		return (*vw->vw_kbdsq++);

	return (-1);
}

static void
vtterm_cngrab(struct terminal *tm)
{
	struct vt_device *vd;
	struct vt_window *vw;
	keyboard_t *kbd;

	vw = tm->tm_softc;
	vd = vw->vw_device;

	if (!cold)
		vt_window_switch(vw);

	kbd = kbd_get_keyboard(vd->vd_keyboard);
	if (kbd == NULL)
		return;

	if (vw->vw_grabbed++ > 0)
		return;

	/*
	 * Make sure the keyboard is accessible even when the kbd device
	 * driver is disabled.
	 */
	kbdd_enable(kbd);

	/* We shall always use the keyboard in the XLATE mode here. */
	vw->vw_prev_kbdmode = vw->vw_kbdmode;
	vw->vw_kbdmode = K_XLATE;
	vt_update_kbd_mode(vw, kbd);

	kbdd_poll(kbd, TRUE);
}

static void
vtterm_cnungrab(struct terminal *tm)
{
	struct vt_device *vd;
	struct vt_window *vw;
	keyboard_t *kbd;

	vw = tm->tm_softc;
	vd = vw->vw_device;

	kbd = kbd_get_keyboard(vd->vd_keyboard);
	if (kbd == NULL)
		return;

	if (--vw->vw_grabbed > 0)
		return;

	kbdd_poll(kbd, FALSE);

	vw->vw_kbdmode = vw->vw_prev_kbdmode;
	vt_update_kbd_mode(vw, kbd);
	kbdd_disable(kbd);
}

static void
vtterm_opened(struct terminal *tm, int opened)
{
	struct vt_window *vw = tm->tm_softc;
	struct vt_device *vd = vw->vw_device;

	VT_LOCK(vd);
	vd->vd_flags &= ~VDF_SPLASH;
	if (opened)
		vw->vw_flags |= VWF_OPENED;
	else {
		vw->vw_flags &= ~VWF_OPENED;
		/* TODO: finish ACQ/REL */
	}
	VT_UNLOCK(vd);
}

static int
vt_change_font(struct vt_window *vw, struct vt_font *vf)
{
	struct vt_device *vd = vw->vw_device;
	struct terminal *tm = vw->vw_terminal;
	term_pos_t size;
	struct winsize wsz;

	/*
	 * Changing fonts.
	 *
	 * Changing fonts is a little tricky.  We must prevent
	 * simultaneous access to the device, so we must stop
	 * the display timer and the terminal from accessing.
	 * We need to switch fonts and grow our screen buffer.
	 *
	 * XXX: Right now the code uses terminal_mute() to
	 * prevent data from reaching the console driver while
	 * resizing the screen buffer.  This isn't elegant...
	 */

	VT_LOCK(vd);
	if (vw->vw_flags & VWF_BUSY) {
		/* Another process is changing the font. */
		VT_UNLOCK(vd);
		return (EBUSY);
	}
	vw->vw_flags |= VWF_BUSY;
	VT_UNLOCK(vd);

	vt_termsize(vd, vf, &size);
	vt_winsize(vd, vf, &wsz);

	/* Grow the screen buffer and terminal. */
	terminal_mute(tm, 1);
	vtbuf_grow(&vw->vw_buf, &size, vw->vw_buf.vb_history_size);
	terminal_set_winsize_blank(tm, &wsz, 0, NULL);
	terminal_set_cursor(tm, &vw->vw_buf.vb_cursor);
	terminal_mute(tm, 0);

	/* Actually apply the font to the current window. */
	VT_LOCK(vd);
	if (vw->vw_font != vf && vw->vw_font != NULL && vf != NULL) {
		/*
		 * In case vt_change_font called to update size we don't need
		 * to update font link.
		 */
		vtfont_unref(vw->vw_font);
		vw->vw_font = vtfont_ref(vf);
	}

	/*
	 * Compute the drawable area and move the mouse cursor inside
	 * it, in case the new area is smaller than the previous one.
	 */
	vt_compute_drawable_area(vw);
	vd->vd_mx = min(vd->vd_mx,
	    vw->vw_draw_area.tr_end.tp_col -
	    vw->vw_draw_area.tr_begin.tp_col - 1);
	vd->vd_my = min(vd->vd_my,
	    vw->vw_draw_area.tr_end.tp_row -
	    vw->vw_draw_area.tr_begin.tp_row - 1);

	/* Force a full redraw the next timer tick. */
	if (vd->vd_curwindow == vw) {
		vd->vd_flags |= VDF_INVALID;
		vt_resume_flush_timer(vw, 0);
	}
	vw->vw_flags &= ~VWF_BUSY;
	VT_UNLOCK(vd);
	return (0);
}

static int
vt_proc_alive(struct vt_window *vw)
{
	struct proc *p;

	if (vw->vw_smode.mode != VT_PROCESS)
		return (FALSE);

	if (vw->vw_proc) {
		if ((p = pfind(vw->vw_pid)) != NULL)
			PROC_UNLOCK(p);
		if (vw->vw_proc == p)
			return (TRUE);
		vw->vw_proc = NULL;
		vw->vw_smode.mode = VT_AUTO;
		DPRINTF(1, "vt controlling process %d died\n", vw->vw_pid);
		vw->vw_pid = 0;
	}
	return (FALSE);
}

static int
signal_vt_rel(struct vt_window *vw)
{

	if (vw->vw_smode.mode != VT_PROCESS)
		return (FALSE);
	if (vw->vw_proc == NULL || vt_proc_alive(vw) == FALSE) {
		vw->vw_proc = NULL;
		vw->vw_pid = 0;
		return (TRUE);
	}
	vw->vw_flags |= VWF_SWWAIT_REL;
	PROC_LOCK(vw->vw_proc);
	kern_psignal(vw->vw_proc, vw->vw_smode.relsig);
	PROC_UNLOCK(vw->vw_proc);
	DPRINTF(1, "sending relsig to %d\n", vw->vw_pid);
	return (TRUE);
}

static int
signal_vt_acq(struct vt_window *vw)
{

	if (vw->vw_smode.mode != VT_PROCESS)
		return (FALSE);
	if (vw == vw->vw_device->vd_windows[VT_CONSWINDOW])
		cnavailable(vw->vw_terminal->consdev, FALSE);
	if (vw->vw_proc == NULL || vt_proc_alive(vw) == FALSE) {
		vw->vw_proc = NULL;
		vw->vw_pid = 0;
		return (TRUE);
	}
	vw->vw_flags |= VWF_SWWAIT_ACQ;
	PROC_LOCK(vw->vw_proc);
	kern_psignal(vw->vw_proc, vw->vw_smode.acqsig);
	PROC_UNLOCK(vw->vw_proc);
	DPRINTF(1, "sending acqsig to %d\n", vw->vw_pid);
	return (TRUE);
}

static int
finish_vt_rel(struct vt_window *vw, int release, int *s)
{

	if (vw->vw_flags & VWF_SWWAIT_REL) {
		vw->vw_flags &= ~VWF_SWWAIT_REL;
		if (release) {
			callout_drain(&vw->vw_proc_dead_timer);
			vt_late_window_switch(vw->vw_switch_to);
		}
		return (0);
	}
	return (EINVAL);
}

static int
finish_vt_acq(struct vt_window *vw)
{

	if (vw->vw_flags & VWF_SWWAIT_ACQ) {
		vw->vw_flags &= ~VWF_SWWAIT_ACQ;
		return (0);
	}
	return (EINVAL);
}

#ifndef SC_NO_CUTPASTE
static void
vt_mouse_terminput_button(struct vt_device *vd, int button)
{
	struct vt_window *vw;
	struct vt_font *vf;
	char mouseb[6] = "\x1B[M";
	int i, x, y;

	vw = vd->vd_curwindow;
	vf = vw->vw_font;

	/* Translate to char position. */
	x = vd->vd_mx / vf->vf_width;
	y = vd->vd_my / vf->vf_height;
	/* Avoid overflow. */
	x = MIN(x, 255 - '!');
	y = MIN(y, 255 - '!');

	mouseb[3] = ' ' + button;
	mouseb[4] = '!' + x;
	mouseb[5] = '!' + y;

	for (i = 0; i < sizeof(mouseb); i++)
		terminal_input_char(vw->vw_terminal, mouseb[i]);
}

static void
vt_mouse_terminput(struct vt_device *vd, int type, int x, int y, int event,
    int cnt)
{

	switch (type) {
	case MOUSE_BUTTON_EVENT:
		if (cnt > 0) {
			/* Mouse button pressed. */
			if (event & MOUSE_BUTTON1DOWN)
				vt_mouse_terminput_button(vd, 0);
			if (event & MOUSE_BUTTON2DOWN)
				vt_mouse_terminput_button(vd, 1);
			if (event & MOUSE_BUTTON3DOWN)
				vt_mouse_terminput_button(vd, 2);
		} else {
			/* Mouse button released. */
			vt_mouse_terminput_button(vd, 3);
		}
		break;
#ifdef notyet
	case MOUSE_MOTION_EVENT:
		if (mouse->u.data.z < 0) {
			/* Scroll up. */
			sc_mouse_input_button(vd, 64);
		} else if (mouse->u.data.z > 0) {
			/* Scroll down. */
			sc_mouse_input_button(vd, 65);
		}
		break;
#endif
	}
}

static void
vt_mouse_paste()
{
	term_char_t *buf;
	int i, len;

	len = VD_PASTEBUFLEN(main_vd);
	buf = VD_PASTEBUF(main_vd);
	len /= sizeof(term_char_t);
	for (i = 0; i < len; i++) {
		if (buf[i] == '\0')
			continue;
		terminal_input_char(main_vd->vd_curwindow->vw_terminal,
		    buf[i]);
	}
}

void
vt_mouse_event(int type, int x, int y, int event, int cnt, int mlevel)
{
	struct vt_device *vd;
	struct vt_window *vw;
	struct vt_font *vf;
	term_pos_t size;
	int len, mark;

	vd = main_vd;
	vw = vd->vd_curwindow;
	vf = vw->vw_font;
	mark = 0;

	if (vw->vw_flags & (VWF_MOUSE_HIDE | VWF_GRAPHICS))
		/*
		 * Either the mouse is disabled, or the window is in
		 * "graphics mode". The graphics mode is usually set by
		 * an X server, using the KDSETMODE ioctl.
		 */
		return;

	if (vf == NULL)	/* Text mode. */
		return;

	/*
	 * TODO: add flag about pointer position changed, to not redraw chars
	 * under mouse pointer when nothing changed.
	 */

	if (vw->vw_mouse_level > 0)
		vt_mouse_terminput(vd, type, x, y, event, cnt);

	switch (type) {
	case MOUSE_ACTION:
	case MOUSE_MOTION_EVENT:
		/* Movement */
		x += vd->vd_mx;
		y += vd->vd_my;

		vt_termsize(vd, vf, &size);

		/* Apply limits. */
		x = MAX(x, 0);
		y = MAX(y, 0);
		x = MIN(x, (size.tp_col * vf->vf_width) - 1);
		y = MIN(y, (size.tp_row * vf->vf_height) - 1);

		vd->vd_mx = x;
		vd->vd_my = y;
		if (vd->vd_mstate & MOUSE_BUTTON1DOWN)
			vtbuf_set_mark(&vw->vw_buf, VTB_MARK_MOVE,
			    vd->vd_mx / vf->vf_width,
			    vd->vd_my / vf->vf_height);

		vt_resume_flush_timer(vw, 0);
		return; /* Done */
	case MOUSE_BUTTON_EVENT:
		/* Buttons */
		break;
	default:
		return; /* Done */
	}

	switch (event) {
	case MOUSE_BUTTON1DOWN:
		switch (cnt % 4) {
		case 0:	/* up */
			mark = VTB_MARK_END;
			break;
		case 1: /* single click: start cut operation */
			mark = VTB_MARK_START;
			break;
		case 2:	/* double click: cut a word */
			mark = VTB_MARK_WORD;
			break;
		case 3:	/* triple click: cut a line */
			mark = VTB_MARK_ROW;
			break;
		}
		break;
	case VT_MOUSE_PASTEBUTTON:
		switch (cnt) {
		case 0:	/* up */
			break;
		default:
			vt_mouse_paste();
			break;
		}
		return; /* Done */
	case VT_MOUSE_EXTENDBUTTON:
		switch (cnt) {
		case 0:	/* up */
			if (!(vd->vd_mstate & MOUSE_BUTTON1DOWN))
				mark = VTB_MARK_EXTEND;
			else
				mark = 0;
			break;
		default:
			mark = VTB_MARK_EXTEND;
			break;
		}
		break;
	default:
		return; /* Done */
	}

	/* Save buttons state. */
	if (cnt > 0)
		vd->vd_mstate |= event;
	else
		vd->vd_mstate &= ~event;

	if (vtbuf_set_mark(&vw->vw_buf, mark, vd->vd_mx / vf->vf_width,
	    vd->vd_my / vf->vf_height) == 1) {
		/*
		 * We have something marked to copy, so update pointer to
		 * window with selection.
		 */
		vt_resume_flush_timer(vw, 0);

		switch (mark) {
		case VTB_MARK_END:
		case VTB_MARK_WORD:
		case VTB_MARK_ROW:
		case VTB_MARK_EXTEND:
			break;
		default:
			/* Other types of mark do not require to copy data. */
			return;
		}

		/* Get current selection size in bytes. */
		len = vtbuf_get_marked_len(&vw->vw_buf);
		if (len <= 0)
			return;

		/* Reallocate buffer only if old one is too small. */
		if (len > VD_PASTEBUFSZ(vd)) {
			VD_PASTEBUF(vd) = realloc(VD_PASTEBUF(vd), len, M_VT,
			    M_WAITOK | M_ZERO);
			/* Update buffer size. */
			VD_PASTEBUFSZ(vd) = len;
		}
		/* Request copy/paste buffer data, no more than `len' */
		vtbuf_extract_marked(&vw->vw_buf, VD_PASTEBUF(vd),
		    VD_PASTEBUFSZ(vd));

		VD_PASTEBUFLEN(vd) = len;

		/* XXX VD_PASTEBUF(vd) have to be freed on shutdown/unload. */
	}
}

void
vt_mouse_state(int show)
{
	struct vt_device *vd;
	struct vt_window *vw;

	vd = main_vd;
	vw = vd->vd_curwindow;

	switch (show) {
	case VT_MOUSE_HIDE:
		vw->vw_flags |= VWF_MOUSE_HIDE;
		break;
	case VT_MOUSE_SHOW:
		vw->vw_flags &= ~VWF_MOUSE_HIDE;
		break;
	}

	/* Mark mouse position as dirty. */
	vt_mark_mouse_position_as_dirty(vd, false);
	vt_resume_flush_timer(vw, 0);
}
#endif

static int
vtterm_mmap(struct terminal *tm, vm_ooffset_t offset, vm_paddr_t * paddr,
    int nprot, vm_memattr_t *memattr)
{
	struct vt_window *vw = tm->tm_softc;
	struct vt_device *vd = vw->vw_device;

	if (vd->vd_driver->vd_fb_mmap)
		return (vd->vd_driver->vd_fb_mmap(vd, offset, paddr, nprot,
		    memattr));

	return (ENXIO);
}

static int
vtterm_ioctl(struct terminal *tm, u_long cmd, caddr_t data,
    struct thread *td)
{
	struct vt_window *vw = tm->tm_softc;
	struct vt_device *vd = vw->vw_device;
	keyboard_t *kbd;
	int error, i, s;
#if defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD4) || defined(COMPAT_43)
	int ival;

	switch (cmd) {
	case _IO('v', 4):
		cmd = VT_RELDISP;
		break;
	case _IO('v', 5):
		cmd = VT_ACTIVATE;
		break;
	case _IO('v', 6):
		cmd = VT_WAITACTIVE;
		break;
	case _IO('K', 20):
		cmd = KDSKBSTATE;
		break;
	case _IO('K', 67):
		cmd = KDSETRAD;
		break;
	case _IO('K', 7):
		cmd = KDSKBMODE;
		break;
	case _IO('K', 8):
		cmd = KDMKTONE;
		break;
	case _IO('K', 10):
		cmd = KDSETMODE;
		break;
	case _IO('K', 13):
		cmd = KDSBORDER;
		break;
	case _IO('K', 63):
		cmd = KIOCSOUND;
		break;
	case _IO('K', 66):
		cmd = KDSETLED;
		break;
	case _IO('c', 104):
		cmd = CONS_SETWINORG;
		break;
	case _IO('c', 110):
		cmd = CONS_SETKBD;
		break;
	default:
		goto skip_thunk;
	}
	ival = IOCPARM_IVAL(data);
	data = (caddr_t)&ival;
skip_thunk:
#endif

	switch (cmd) {
	case KDSETRAD:		/* set keyboard repeat & delay rates (old) */
		if (*(int *)data & ~0x7f)
			return (EINVAL);
		/* FALLTHROUGH */
	case GIO_KEYMAP:
	case PIO_KEYMAP:
	case GIO_DEADKEYMAP:
	case PIO_DEADKEYMAP:
	case GETFKEY:
	case SETFKEY:
	case KDGKBINFO:
	case KDGKBTYPE:
	case KDGETREPEAT:	/* get keyboard repeat & delay rates */
	case KDSETREPEAT:	/* set keyboard repeat & delay rates (new) */
	case KBADDKBD:		/* add/remove keyboard to/from mux */
	case KBRELKBD: {
		error = 0;

		mtx_lock(&Giant);
		kbd = kbd_get_keyboard(vd->vd_keyboard);
		if (kbd != NULL)
			error = kbdd_ioctl(kbd, cmd, data);
		mtx_unlock(&Giant);
		if (error == ENOIOCTL) {
			if (cmd == KDGKBTYPE) {
				/* always return something? XXX */
				*(int *)data = 0;
			} else {
				return (ENODEV);
			}
		}
		return (error);
	}
	case KDGKBSTATE: {	/* get keyboard state (locks) */
		error = 0;

		if (vw == vd->vd_curwindow) {
			mtx_lock(&Giant);
			kbd = kbd_get_keyboard(vd->vd_keyboard);
			if (kbd != NULL)
				error = vt_save_kbd_state(vw, kbd);
			mtx_unlock(&Giant);

			if (error != 0)
				return (error);
		}

		*(int *)data = vw->vw_kbdstate & LOCK_MASK;

		return (error);
	}
	case KDSKBSTATE: {	/* set keyboard state (locks) */
		int state;

		state = *(int *)data;
		if (state & ~LOCK_MASK)
			return (EINVAL);

		vw->vw_kbdstate &= ~LOCK_MASK;
		vw->vw_kbdstate |= state;

		error = 0;
		if (vw == vd->vd_curwindow) {
			mtx_lock(&Giant);
			kbd = kbd_get_keyboard(vd->vd_keyboard);
			if (kbd != NULL)
				error = vt_update_kbd_state(vw, kbd);
			mtx_unlock(&Giant);
		}

		return (error);
	}
	case KDGETLED: {	/* get keyboard LED status */
		error = 0;

		if (vw == vd->vd_curwindow) {
			mtx_lock(&Giant);
			kbd = kbd_get_keyboard(vd->vd_keyboard);
			if (kbd != NULL)
				error = vt_save_kbd_leds(vw, kbd);
			mtx_unlock(&Giant);

			if (error != 0)
				return (error);
		}

		*(int *)data = vw->vw_kbdstate & LED_MASK;

		return (error);
	}
	case KDSETLED: {	/* set keyboard LED status */
		int leds;

		leds = *(int *)data;
		if (leds & ~LED_MASK)
			return (EINVAL);

		vw->vw_kbdstate &= ~LED_MASK;
		vw->vw_kbdstate |= leds;

		error = 0;
		if (vw == vd->vd_curwindow) {
			mtx_lock(&Giant);
			kbd = kbd_get_keyboard(vd->vd_keyboard);
			if (kbd != NULL)
				error = vt_update_kbd_leds(vw, kbd);
			mtx_unlock(&Giant);
		}

		return (error);
	}
	case KDGETMODE:
		*(int *)data = (vw->vw_flags & VWF_GRAPHICS) ?
		    KD_GRAPHICS : KD_TEXT;
		return (0);
	case KDGKBMODE: {
		error = 0;

		if (vw == vd->vd_curwindow) {
			mtx_lock(&Giant);
			kbd = kbd_get_keyboard(vd->vd_keyboard);
			if (kbd != NULL)
				error = vt_save_kbd_mode(vw, kbd);
			mtx_unlock(&Giant);

			if (error != 0)
				return (error);
		}

		*(int *)data = vw->vw_kbdmode;

		return (error);
	}
	case KDSKBMODE: {
		int mode;

		mode = *(int *)data;
		switch (mode) {
		case K_XLATE:
		case K_RAW:
		case K_CODE:
			vw->vw_kbdmode = mode;

			error = 0;
			if (vw == vd->vd_curwindow) {
				mtx_lock(&Giant);
				kbd = kbd_get_keyboard(vd->vd_keyboard);
				if (kbd != NULL)
					error = vt_update_kbd_mode(vw, kbd);
				mtx_unlock(&Giant);
			}

			return (error);
		default:
			return (EINVAL);
		}
	}
	case FBIOGTYPE:
	case FBIO_GETWINORG:	/* get frame buffer window origin */
	case FBIO_GETDISPSTART:	/* get display start address */
	case FBIO_GETLINEWIDTH:	/* get scan line width in bytes */
	case FBIO_BLANK:	/* blank display */
		if (vd->vd_driver->vd_fb_ioctl)
			return (vd->vd_driver->vd_fb_ioctl(vd, cmd, data, td));
		break;
	case CONS_BLANKTIME:
		/* XXX */
		return (0);
	case CONS_HISTORY:
		if (*(int *)data < 0)
			return EINVAL;
		if (*(int *)data != vd->vd_curwindow->vw_buf.vb_history_size)
			vtbuf_sethistory_size(&vd->vd_curwindow->vw_buf,
			    *(int *)data);
		return 0;
	case CONS_GET:
		/* XXX */
		*(int *)data = M_CG640x480;
		return (0);
	case CONS_BELLTYPE:	/* set bell type sound */
		if ((*(int *)data) & CONS_QUIET_BELL)
			vd->vd_flags |= VDF_QUIET_BELL;
		else
			vd->vd_flags &= ~VDF_QUIET_BELL;
		return (0);
	case CONS_GETINFO: {
		vid_info_t *vi = (vid_info_t *)data;
		if (vi->size != sizeof(struct vid_info))
			return (EINVAL);

		if (vw == vd->vd_curwindow) {
			mtx_lock(&Giant);
			kbd = kbd_get_keyboard(vd->vd_keyboard);
			if (kbd != NULL)
				vt_save_kbd_state(vw, kbd);
			mtx_unlock(&Giant);
		}

		vi->m_num = vd->vd_curwindow->vw_number + 1;
		vi->mk_keylock = vw->vw_kbdstate & LOCK_MASK;
		/* XXX: other fields! */
		return (0);
	}
	case CONS_GETVERS:
		*(int *)data = 0x200;
		return (0);
	case CONS_MODEINFO:
		/* XXX */
		return (0);
	case CONS_MOUSECTL: {
		mouse_info_t *mouse = (mouse_info_t*)data;

		/*
		 * All the commands except MOUSE_SHOW nd MOUSE_HIDE
		 * should not be applied to individual TTYs, but only to
		 * consolectl.
		 */
		switch (mouse->operation) {
		case MOUSE_HIDE:
			if (vd->vd_flags & VDF_MOUSECURSOR) {
				vd->vd_flags &= ~VDF_MOUSECURSOR;
#ifndef SC_NO_CUTPASTE
				vt_mouse_state(VT_MOUSE_HIDE);
#endif
			}
			return (0);
		case MOUSE_SHOW:
			if (!(vd->vd_flags & VDF_MOUSECURSOR)) {
				vd->vd_flags |= VDF_MOUSECURSOR;
				vd->vd_mx = vd->vd_width / 2;
				vd->vd_my = vd->vd_height / 2;
#ifndef SC_NO_CUTPASTE
				vt_mouse_state(VT_MOUSE_SHOW);
#endif
			}
			return (0);
		default:
			return (EINVAL);
		}
	}
	case PIO_VFONT: {
		struct vt_font *vf;

		if (vd->vd_flags & VDF_TEXTMODE)
			return (ENOTSUP);

		error = vtfont_load((void *)data, &vf);
		if (error != 0)
			return (error);

		error = vt_change_font(vw, vf);
		vtfont_unref(vf);
		return (error);
	}
	case PIO_VFONT_DEFAULT: {
		/* Reset to default font. */
		error = vt_change_font(vw, &vt_font_default);
		return (error);
	}
	case GIO_SCRNMAP: {
		scrmap_t *sm = (scrmap_t *)data;

		/* We don't have screen maps, so return a handcrafted one. */
		for (i = 0; i < 256; i++)
			sm->scrmap[i] = i;
		return (0);
	}
	case KDSETMODE:
		/*
		 * FIXME: This implementation is incomplete compared to
		 * syscons.
		 */
		switch (*(int *)data) {
		case KD_TEXT:
		case KD_TEXT1:
		case KD_PIXEL:
			vw->vw_flags &= ~VWF_GRAPHICS;
			break;
		case KD_GRAPHICS:
			vw->vw_flags |= VWF_GRAPHICS;
			break;
		}
		return (0);
	case KDENABIO:		/* allow io operations */
		error = priv_check(td, PRIV_IO);
		if (error != 0)
			return (error);
		error = securelevel_gt(td->td_ucred, 0);
		if (error != 0)
			return (error);
#if defined(__i386__)
		td->td_frame->tf_eflags |= PSL_IOPL;
#elif defined(__amd64__)
		td->td_frame->tf_rflags |= PSL_IOPL;
#endif
		return (0);
	case KDDISABIO:		/* disallow io operations (default) */
#if defined(__i386__)
		td->td_frame->tf_eflags &= ~PSL_IOPL;
#elif defined(__amd64__)
		td->td_frame->tf_rflags &= ~PSL_IOPL;
#endif
		return (0);
	case KDMKTONE:		/* sound the bell */
		vtterm_beep(tm, *(u_int *)data);
		return (0);
	case KIOCSOUND:		/* make tone (*data) hz */
		/* TODO */
		return (0);
	case CONS_SETKBD:	/* set the new keyboard */
		mtx_lock(&Giant);
		error = 0;
		if (vd->vd_keyboard != *(int *)data) {
			kbd = kbd_get_keyboard(*(int *)data);
			if (kbd == NULL) {
				mtx_unlock(&Giant);
				return (EINVAL);
			}
			i = kbd_allocate(kbd->kb_name, kbd->kb_unit,
			    (void *)vd, vt_kbdevent, vd);
			if (i >= 0) {
				if (vd->vd_keyboard != -1) {
					kbd = kbd_get_keyboard(vd->vd_keyboard);
					vt_save_kbd_state(vd->vd_curwindow, kbd);
					kbd_release(kbd, (void *)vd);
				}
				kbd = kbd_get_keyboard(i);
				vd->vd_keyboard = i;

				vt_update_kbd_mode(vd->vd_curwindow, kbd);
				vt_update_kbd_state(vd->vd_curwindow, kbd);
			} else {
				error = EPERM;	/* XXX */
			}
		}
		mtx_unlock(&Giant);
		return (error);
	case CONS_RELKBD:	/* release the current keyboard */
		mtx_lock(&Giant);
		error = 0;
		if (vd->vd_keyboard != -1) {
			kbd = kbd_get_keyboard(vd->vd_keyboard);
			if (kbd == NULL) {
				mtx_unlock(&Giant);
				return (EINVAL);
			}
			vt_save_kbd_state(vd->vd_curwindow, kbd);
			error = kbd_release(kbd, (void *)vd);
			if (error == 0) {
				vd->vd_keyboard = -1;
			}
		}
		mtx_unlock(&Giant);
		return (error);
	case VT_ACTIVATE: {
		int win;
		win = *(int *)data - 1;
		DPRINTF(5, "%s%d: VT_ACTIVATE ttyv%d ", SC_DRIVER_NAME,
		    VT_UNIT(vw), win);
		if ((win >= VT_MAXWINDOWS) || (win < 0))
			return (EINVAL);
		return (vt_proc_window_switch(vd->vd_windows[win]));
	}
	case VT_GETACTIVE:
		*(int *)data = vd->vd_curwindow->vw_number + 1;
		return (0);
	case VT_GETINDEX:
		*(int *)data = vw->vw_number + 1;
		return (0);
	case VT_LOCKSWITCH:
		/* TODO: Check current state, switching can be in progress. */
		if ((*(int *)data) == 0x01)
			vw->vw_flags |= VWF_VTYLOCK;
		else if ((*(int *)data) == 0x02)
			vw->vw_flags &= ~VWF_VTYLOCK;
		else
			return (EINVAL);
		return (0);
	case VT_OPENQRY:
		VT_LOCK(vd);
		for (i = 0; i < VT_MAXWINDOWS; i++) {
			vw = vd->vd_windows[i];
			if (vw == NULL)
				continue;
			if (!(vw->vw_flags & VWF_OPENED)) {
				*(int *)data = vw->vw_number + 1;
				VT_UNLOCK(vd);
				return (0);
			}
		}
		VT_UNLOCK(vd);
		return (EINVAL);
	case VT_WAITACTIVE: {
		unsigned int idx;

		error = 0;

		idx = *(unsigned int *)data;
		if (idx > VT_MAXWINDOWS)
			return (EINVAL);
		if (idx > 0)
			vw = vd->vd_windows[idx - 1];

		VT_LOCK(vd);
		while (vd->vd_curwindow != vw && error == 0)
			error = cv_wait_sig(&vd->vd_winswitch, &vd->vd_lock);
		VT_UNLOCK(vd);
		return (error);
	}
	case VT_SETMODE: {	/* set screen switcher mode */
		struct vt_mode *mode;
		struct proc *p1;

		mode = (struct vt_mode *)data;
		DPRINTF(5, "%s%d: VT_SETMODE ", SC_DRIVER_NAME, VT_UNIT(vw));
		if (vw->vw_smode.mode == VT_PROCESS) {
			p1 = pfind(vw->vw_pid);
			if (vw->vw_proc == p1 && vw->vw_proc != td->td_proc) {
				if (p1)
					PROC_UNLOCK(p1);
				DPRINTF(5, "error EPERM\n");
				return (EPERM);
			}
			if (p1)
				PROC_UNLOCK(p1);
		}
		if (mode->mode == VT_AUTO) {
			vw->vw_smode.mode = VT_AUTO;
			vw->vw_proc = NULL;
			vw->vw_pid = 0;
			DPRINTF(5, "VT_AUTO, ");
			if (vw == vw->vw_device->vd_windows[VT_CONSWINDOW])
				cnavailable(vw->vw_terminal->consdev, TRUE);
			/* were we in the middle of the vty switching process? */
			if (finish_vt_rel(vw, TRUE, &s) == 0)
				DPRINTF(5, "reset WAIT_REL, ");
			if (finish_vt_acq(vw) == 0)
				DPRINTF(5, "reset WAIT_ACQ, ");
			return (0);
		} else if (mode->mode == VT_PROCESS) {
			if (!ISSIGVALID(mode->relsig) ||
			    !ISSIGVALID(mode->acqsig) ||
			    !ISSIGVALID(mode->frsig)) {
				DPRINTF(5, "error EINVAL\n");
				return (EINVAL);
			}
			DPRINTF(5, "VT_PROCESS %d, ", td->td_proc->p_pid);
			bcopy(data, &vw->vw_smode, sizeof(struct vt_mode));
			vw->vw_proc = td->td_proc;
			vw->vw_pid = vw->vw_proc->p_pid;
			if (vw == vw->vw_device->vd_windows[VT_CONSWINDOW])
				cnavailable(vw->vw_terminal->consdev, FALSE);
		} else {
			DPRINTF(5, "VT_SETMODE failed, unknown mode %d\n",
			    mode->mode);
			return (EINVAL);
		}
		DPRINTF(5, "\n");
		return (0);
	}
	case VT_GETMODE:	/* get screen switcher mode */
		bcopy(&vw->vw_smode, data, sizeof(struct vt_mode));
		return (0);

	case VT_RELDISP:	/* screen switcher ioctl */
		/*
		 * This must be the current vty which is in the VT_PROCESS
		 * switching mode...
		 */
		if ((vw != vd->vd_curwindow) || (vw->vw_smode.mode !=
		    VT_PROCESS)) {
			return (EINVAL);
		}
		/* ...and this process is controlling it. */
		if (vw->vw_proc != td->td_proc) {
			return (EPERM);
		}
		error = EINVAL;
		switch(*(int *)data) {
		case VT_FALSE:	/* user refuses to release screen, abort */
			if ((error = finish_vt_rel(vw, FALSE, &s)) == 0)
				DPRINTF(5, "%s%d: VT_RELDISP: VT_FALSE\n",
				    SC_DRIVER_NAME, VT_UNIT(vw));
			break;
		case VT_TRUE:	/* user has released screen, go on */
			/* finish_vt_rel(..., TRUE, ...) should not be locked */
			if (vw->vw_flags & VWF_SWWAIT_REL) {
				if ((error = finish_vt_rel(vw, TRUE, &s)) == 0)
					DPRINTF(5, "%s%d: VT_RELDISP: VT_TRUE\n",
					    SC_DRIVER_NAME, VT_UNIT(vw));
			} else {
				error = EINVAL;
			}
			return (error);
		case VT_ACKACQ:	/* acquire acknowledged, switch completed */
			if ((error = finish_vt_acq(vw)) == 0)
				DPRINTF(5, "%s%d: VT_RELDISP: VT_ACKACQ\n",
				    SC_DRIVER_NAME, VT_UNIT(vw));
			break;
		default:
			break;
		}
		return (error);
	}

	return (ENOIOCTL);
}

static struct vt_window *
vt_allocate_window(struct vt_device *vd, unsigned int window)
{
	struct vt_window *vw;
	struct terminal *tm;
	term_pos_t size;
	struct winsize wsz;

	vw = malloc(sizeof *vw, M_VT, M_WAITOK|M_ZERO);
	vw->vw_device = vd;
	vw->vw_number = window;
	vw->vw_kbdmode = K_XLATE;

	if ((vd->vd_flags & VDF_TEXTMODE) == 0) {
		vw->vw_font = vtfont_ref(&vt_font_default);
		vt_compute_drawable_area(vw);
	}

	vt_termsize(vd, vw->vw_font, &size);
	vt_winsize(vd, vw->vw_font, &wsz);
	vtbuf_init(&vw->vw_buf, &size);

	tm = vw->vw_terminal = terminal_alloc(&vt_termclass, vw);
	terminal_set_winsize(tm, &wsz);
	vd->vd_windows[window] = vw;
	callout_init(&vw->vw_proc_dead_timer, 0);

	return (vw);
}

void
vt_upgrade(struct vt_device *vd)
{
	struct vt_window *vw;
	unsigned int i;
	int register_handlers;

	if (!vty_enabled(VTY_VT))
		return;
	if (main_vd->vd_driver == NULL)
		return;

	for (i = 0; i < VT_MAXWINDOWS; i++) {
		vw = vd->vd_windows[i];
		if (vw == NULL) {
			/* New window. */
			vw = vt_allocate_window(vd, i);
		}
		if (!(vw->vw_flags & VWF_READY)) {
			callout_init(&vw->vw_proc_dead_timer, 0);
			terminal_maketty(vw->vw_terminal, "v%r", VT_UNIT(vw));
			vw->vw_flags |= VWF_READY;
			if (vw->vw_flags & VWF_CONSOLE) {
				/* For existing console window. */
				EVENTHANDLER_REGISTER(shutdown_pre_sync,
				    vt_window_switch, vw, SHUTDOWN_PRI_DEFAULT);
			}
		}

	}
	VT_LOCK(vd);
	if (vd->vd_curwindow == NULL)
		vd->vd_curwindow = vd->vd_windows[VT_CONSWINDOW];

	register_handlers = 0;
	if (!(vd->vd_flags & VDF_ASYNC)) {
		/* Attach keyboard. */
		vt_allocate_keyboard(vd);

		/* Init 25 Hz timer. */
		callout_init_mtx(&vd->vd_timer, &vd->vd_lock, 0);

		/*
		 * Start timer when everything ready.
		 * Note that the operations here are purposefully ordered.
		 * We need to ensure vd_timer_armed is non-zero before we set
		 * the VDF_ASYNC flag. That prevents this function from
		 * racing with vt_resume_flush_timer() to update the
		 * callout structure.
		 */
		atomic_add_acq_int(&vd->vd_timer_armed, 1);
		vd->vd_flags |= VDF_ASYNC;
		callout_reset(&vd->vd_timer, hz / VT_TIMERFREQ, vt_timer, vd);
		register_handlers = 1;
	}

	VT_UNLOCK(vd);

	/* Refill settings with new sizes. */
	vt_resize(vd);

	if (register_handlers) {
		/* Register suspend/resume handlers. */
		EVENTHANDLER_REGISTER(power_suspend_early, vt_suspend_handler,
		    vd, EVENTHANDLER_PRI_ANY);
		EVENTHANDLER_REGISTER(power_resume, vt_resume_handler, vd,
		    EVENTHANDLER_PRI_ANY);
	}
}

static void
vt_resize(struct vt_device *vd)
{
	struct vt_window *vw;
	int i;

	for (i = 0; i < VT_MAXWINDOWS; i++) {
		vw = vd->vd_windows[i];
		VT_LOCK(vd);
		/* Assign default font to window, if not textmode. */
		if (!(vd->vd_flags & VDF_TEXTMODE) && vw->vw_font == NULL)
			vw->vw_font = vtfont_ref(&vt_font_default);
		VT_UNLOCK(vd);

		/* Resize terminal windows */
		while (vt_change_font(vw, vw->vw_font) == EBUSY) {
			DPRINTF(100, "%s: vt_change_font() is busy, "
			    "window %d\n", __func__, i);
		}
	}
}

static void
vt_replace_backend(const struct vt_driver *drv, void *softc)
{
	struct vt_device *vd;

	vd = main_vd;

	if (vd->vd_flags & VDF_ASYNC) {
		/* Stop vt_flush periodic task. */
		VT_LOCK(vd);
		vt_suspend_flush_timer(vd);
		VT_UNLOCK(vd);
		/*
		 * Mute current terminal until we done. vt_change_font (called
		 * from vt_resize) will unmute it.
		 */
		terminal_mute(vd->vd_curwindow->vw_terminal, 1);
	}

	/*
	 * Reset VDF_TEXTMODE flag, driver who require that flag (vt_vga) will
	 * set it.
	 */
	VT_LOCK(vd);
	vd->vd_flags &= ~VDF_TEXTMODE;

	if (drv != NULL) {
		/*
		 * We want to upgrade from the current driver to the
		 * given driver.
		 */

		vd->vd_prev_driver = vd->vd_driver;
		vd->vd_prev_softc = vd->vd_softc;
		vd->vd_driver = drv;
		vd->vd_softc = softc;

		vd->vd_driver->vd_init(vd);
	} else if (vd->vd_prev_driver != NULL && vd->vd_prev_softc != NULL) {
		/*
		 * No driver given: we want to downgrade to the previous
		 * driver.
		 */
		const struct vt_driver *old_drv;
		void *old_softc;

		old_drv = vd->vd_driver;
		old_softc = vd->vd_softc;

		vd->vd_driver = vd->vd_prev_driver;
		vd->vd_softc = vd->vd_prev_softc;
		vd->vd_prev_driver = NULL;
		vd->vd_prev_softc = NULL;

		vd->vd_flags |= VDF_DOWNGRADE;

		vd->vd_driver->vd_init(vd);

		if (old_drv->vd_fini)
			old_drv->vd_fini(vd, old_softc);

		vd->vd_flags &= ~VDF_DOWNGRADE;
	}

	VT_UNLOCK(vd);

	/* Update windows sizes and initialize last items. */
	vt_upgrade(vd);

#ifdef DEV_SPLASH
	if (vd->vd_flags & VDF_SPLASH)
		vtterm_splash(vd);
#endif

	if (vd->vd_flags & VDF_ASYNC) {
		/* Allow to put chars now. */
		terminal_mute(vd->vd_curwindow->vw_terminal, 0);
		/* Rerun timer for screen updates. */
		vt_resume_flush_timer(vd->vd_curwindow, 0);
	}

	/*
	 * Register as console. If it already registered, cnadd() will ignore
	 * it.
	 */
	termcn_cnregister(vd->vd_windows[VT_CONSWINDOW]->vw_terminal);
}

static void
vt_suspend_handler(void *priv)
{
	struct vt_device *vd;

	vd = priv;
	vd->vd_flags |= VDF_SUSPENDED;
	if (vd->vd_driver != NULL && vd->vd_driver->vd_suspend != NULL)
		vd->vd_driver->vd_suspend(vd);
}

static void
vt_resume_handler(void *priv)
{
	struct vt_device *vd;

	vd = priv;
	if (vd->vd_driver != NULL && vd->vd_driver->vd_resume != NULL)
		vd->vd_driver->vd_resume(vd);
	vd->vd_flags &= ~VDF_SUSPENDED;
}

void
vt_allocate(const struct vt_driver *drv, void *softc)
{

	if (!vty_enabled(VTY_VT))
		return;

	if (main_vd->vd_driver == NULL) {
		main_vd->vd_driver = drv;
		printf("VT: initialize with new VT driver \"%s\".\n",
		    drv->vd_name);
	} else {
		/*
		 * Check if have rights to replace current driver. For example:
		 * it is bad idea to replace KMS driver with generic VGA one.
		 */
		if (drv->vd_priority <= main_vd->vd_driver->vd_priority) {
			printf("VT: Driver priority %d too low. Current %d\n ",
			    drv->vd_priority, main_vd->vd_driver->vd_priority);
			return;
		}
		printf("VT: Replacing driver \"%s\" with new \"%s\".\n",
		    main_vd->vd_driver->vd_name, drv->vd_name);
	}

	vt_replace_backend(drv, softc);
}

void
vt_deallocate(const struct vt_driver *drv, void *softc)
{

	if (!vty_enabled(VTY_VT))
		return;

	if (main_vd->vd_prev_driver == NULL ||
	    main_vd->vd_driver != drv ||
	    main_vd->vd_softc != softc)
		return;

	printf("VT: Switching back from \"%s\" to \"%s\".\n",
	    main_vd->vd_driver->vd_name, main_vd->vd_prev_driver->vd_name);

	vt_replace_backend(NULL, NULL);
}

void
vt_suspend(struct vt_device *vd)
{
	int error;

	if (vt_suspendswitch == 0)
		return;
	/* Save current window. */
	vd->vd_savedwindow = vd->vd_curwindow;
	/* Ask holding process to free window and switch to console window */
	vt_proc_window_switch(vd->vd_windows[VT_CONSWINDOW]);

	/* Wait for the window switch to complete. */
	error = 0;
	VT_LOCK(vd);
	while (vd->vd_curwindow != vd->vd_windows[VT_CONSWINDOW] && error == 0)
		error = cv_wait_sig(&vd->vd_winswitch, &vd->vd_lock);
	VT_UNLOCK(vd);
}

void
vt_resume(struct vt_device *vd)
{

	if (vt_suspendswitch == 0)
		return;
	/* Switch back to saved window, if any */
	vt_proc_window_switch(vd->vd_savedwindow);
	vd->vd_savedwindow = NULL;
}
