/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Ed Schouten under sponsorship from the
 * FreeBSD Foundation.
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
#include <sys/cons.h>
#include <sys/consio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/terminal.h>
#include <sys/tty.h>

#include <machine/stdarg.h>

static MALLOC_DEFINE(M_TERMINAL, "terminal", "terminal device");

/*
 * Locking.
 *
 * Normally we don't need to lock down the terminal emulator, because
 * the TTY lock is already held when calling teken_input().
 * Unfortunately this is not the case when the terminal acts as a
 * console device, because cnputc() can be called at the same time.
 * This means terminals may need to be locked down using a spin lock.
 */
#define	TERMINAL_LOCK(tm)	do {					\
	if ((tm)->tm_flags & TF_CONS)					\
		mtx_lock_spin(&(tm)->tm_mtx);				\
	else if ((tm)->tm_tty != NULL)					\
		tty_lock((tm)->tm_tty);					\
} while (0)
#define	TERMINAL_UNLOCK(tm)	do {					\
	if ((tm)->tm_flags & TF_CONS)					\
		mtx_unlock_spin(&(tm)->tm_mtx);				\
	else if ((tm)->tm_tty != NULL)					\
		tty_unlock((tm)->tm_tty);				\
} while (0)
#define	TERMINAL_LOCK_TTY(tm)	do {					\
	if ((tm)->tm_flags & TF_CONS)					\
		mtx_lock_spin(&(tm)->tm_mtx);				\
} while (0)
#define	TERMINAL_UNLOCK_TTY(tm)	do {					\
	if ((tm)->tm_flags & TF_CONS)					\
		mtx_unlock_spin(&(tm)->tm_mtx);				\
} while (0)
#define	TERMINAL_LOCK_CONS(tm)		mtx_lock_spin(&(tm)->tm_mtx)
#define	TERMINAL_UNLOCK_CONS(tm)	mtx_unlock_spin(&(tm)->tm_mtx)

/*
 * TTY routines.
 */

static tsw_open_t	termtty_open;
static tsw_close_t	termtty_close;
static tsw_outwakeup_t	termtty_outwakeup;
static tsw_ioctl_t	termtty_ioctl;
static tsw_mmap_t	termtty_mmap;

static struct ttydevsw terminal_tty_class = {
	.tsw_open	= termtty_open,
	.tsw_close	= termtty_close,
	.tsw_outwakeup	= termtty_outwakeup,
	.tsw_ioctl	= termtty_ioctl,
	.tsw_mmap	= termtty_mmap,
};

/*
 * Terminal emulator routines.
 */

static tf_bell_t	termteken_bell;
static tf_cursor_t	termteken_cursor;
static tf_putchar_t	termteken_putchar;
static tf_fill_t	termteken_fill;
static tf_copy_t	termteken_copy;
static tf_pre_input_t	termteken_pre_input;
static tf_post_input_t	termteken_post_input;
static tf_param_t	termteken_param;
static tf_respond_t	termteken_respond;

static teken_funcs_t terminal_drawmethods = {
	.tf_bell	= termteken_bell,
	.tf_cursor	= termteken_cursor,
	.tf_putchar	= termteken_putchar,
	.tf_fill	= termteken_fill,
	.tf_copy	= termteken_copy,
	.tf_pre_input	= termteken_pre_input,
	.tf_post_input	= termteken_post_input,
	.tf_param	= termteken_param,
	.tf_respond	= termteken_respond,
};

/* Kernel message formatting. */
static const teken_attr_t kernel_message = {
	.ta_fgcolor	= TCHAR_FGCOLOR(TERMINAL_KERN_ATTR),
	.ta_bgcolor	= TCHAR_BGCOLOR(TERMINAL_KERN_ATTR),
	.ta_format	= TCHAR_FORMAT(TERMINAL_KERN_ATTR)
};

static const teken_attr_t default_message = {
	.ta_fgcolor	= TCHAR_FGCOLOR(TERMINAL_NORM_ATTR),
	.ta_bgcolor	= TCHAR_BGCOLOR(TERMINAL_NORM_ATTR),
	.ta_format	= TCHAR_FORMAT(TERMINAL_NORM_ATTR)
};

/* Fudge fg brightness as TF_BOLD (shifted). */
#define	TCOLOR_FG_FUDGED(color) __extension__ ({			\
	teken_color_t _c;						\
									\
	_c = (color);							\
	TCOLOR_FG(_c & 7) | ((_c & 8) << 18);				\
})

/* Fudge bg brightness as TF_BLINK (shifted). */
#define	TCOLOR_BG_FUDGED(color) __extension__ ({			\
	teken_color_t _c;						\
									\
	_c = (color);							\
	TCOLOR_BG(_c & 7) | ((_c & 8) << 20);				\
})

#define	TCOLOR_256TO16(color) __extension__ ({				\
	teken_color_t _c;						\
									\
	_c = (color);							\
	if (_c >= 16)							\
		_c = teken_256to16(_c);					\
	_c;								\
})

#define	TCHAR_CREATE(c, a)	((c) | TFORMAT((a)->ta_format) |	\
	TCOLOR_FG_FUDGED(TCOLOR_256TO16((a)->ta_fgcolor)) |		\
	TCOLOR_BG_FUDGED(TCOLOR_256TO16((a)->ta_bgcolor)))

static void
terminal_init(struct terminal *tm)
{

	if (tm->tm_flags & TF_CONS)
		mtx_init(&tm->tm_mtx, "trmlck", NULL, MTX_SPIN);
	teken_init(&tm->tm_emulator, &terminal_drawmethods, tm);
	teken_set_defattr(&tm->tm_emulator, &default_message);
}

struct terminal *
terminal_alloc(const struct terminal_class *tc, void *softc)
{
	struct terminal *tm;

	tm = malloc(sizeof(struct terminal), M_TERMINAL, M_WAITOK|M_ZERO);
	terminal_init(tm);

	tm->tm_class = tc;
	tm->tm_softc = softc;

	return (tm);
}

static void
terminal_sync_ttysize(struct terminal *tm)
{
	struct tty *tp;

	tp = tm->tm_tty;
	if (tp == NULL)
		return;

	tty_lock(tp);
	tty_set_winsize(tp, &tm->tm_winsize);
	tty_unlock(tp);
}

void
terminal_maketty(struct terminal *tm, const char *fmt, ...)
{
	struct tty *tp;
	char name[8];
	va_list ap;

	va_start(ap, fmt);
	vsnrprintf(name, sizeof name, 32, fmt, ap);
	va_end(ap);

	tp = tty_alloc(&terminal_tty_class, tm);
	tty_makedev(tp, NULL, "%s", name);
	tm->tm_tty = tp;
	terminal_sync_ttysize(tm);
}

void
terminal_set_cursor(struct terminal *tm, const term_pos_t *pos)
{

	teken_set_cursor(&tm->tm_emulator, pos);
}

void
terminal_set_winsize_blank(struct terminal *tm, const struct winsize *size,
    int blank, const term_attr_t *attr)
{
	term_rect_t r;

	tm->tm_winsize = *size;

	r.tr_begin.tp_row = r.tr_begin.tp_col = 0;
	r.tr_end.tp_row = size->ws_row;
	r.tr_end.tp_col = size->ws_col;

	TERMINAL_LOCK(tm);
	if (blank == 0)
		teken_set_winsize_noreset(&tm->tm_emulator, &r.tr_end);
	else
		teken_set_winsize(&tm->tm_emulator, &r.tr_end);
	TERMINAL_UNLOCK(tm);

	if ((blank != 0) && !(tm->tm_flags & TF_MUTE))
		tm->tm_class->tc_fill(tm, &r,
		    TCHAR_CREATE((teken_char_t)' ', attr));

	terminal_sync_ttysize(tm);
}

void
terminal_set_winsize(struct terminal *tm, const struct winsize *size)
{

	terminal_set_winsize_blank(tm, size, 1,
	    (const term_attr_t *)&default_message);
}

/*
 * XXX: This function is a kludge.  Drivers like vt(4) need to
 * temporarily stop input when resizing, etc.  This should ideally be
 * handled within the driver.
 */

void
terminal_mute(struct terminal *tm, int yes)
{

	TERMINAL_LOCK(tm);
	if (yes)
		tm->tm_flags |= TF_MUTE;
	else
		tm->tm_flags &= ~TF_MUTE;
	TERMINAL_UNLOCK(tm);
}

void
terminal_input_char(struct terminal *tm, term_char_t c)
{
	struct tty *tp;

	tp = tm->tm_tty;
	if (tp == NULL)
		return;

	/*
	 * Strip off any attributes. Also ignore input of second part of
	 * CJK fullwidth characters, as we don't want to return these
	 * characters twice.
	 */
	if (TCHAR_FORMAT(c) & TF_CJK_RIGHT)
		return;
	c = TCHAR_CHARACTER(c);

	tty_lock(tp);
	/*
	 * Conversion to UTF-8.
	 */
	if (c < 0x80) {
		ttydisc_rint(tp, c, 0);
	} else if (c < 0x800) {
		char str[2] = {
			0xc0 | (c >> 6),
			0x80 | (c & 0x3f)
		};

		ttydisc_rint_simple(tp, str, sizeof str);
	} else if (c < 0x10000) {
		char str[3] = {
			0xe0 | (c >> 12),
			0x80 | ((c >> 6) & 0x3f),
			0x80 | (c & 0x3f)
		};

		ttydisc_rint_simple(tp, str, sizeof str);
	} else {
		char str[4] = {
			0xf0 | (c >> 18),
			0x80 | ((c >> 12) & 0x3f),
			0x80 | ((c >> 6) & 0x3f),
			0x80 | (c & 0x3f)
		};

		ttydisc_rint_simple(tp, str, sizeof str);
	}
	ttydisc_rint_done(tp);
	tty_unlock(tp);
}

void
terminal_input_raw(struct terminal *tm, char c)
{
	struct tty *tp;

	tp = tm->tm_tty;
	if (tp == NULL)
		return;

	tty_lock(tp);
	ttydisc_rint(tp, c, 0);
	ttydisc_rint_done(tp);
	tty_unlock(tp);
}

void
terminal_input_special(struct terminal *tm, unsigned int k)
{
	struct tty *tp;
	const char *str;

	tp = tm->tm_tty;
	if (tp == NULL)
		return;

	str = teken_get_sequence(&tm->tm_emulator, k);
	if (str == NULL)
		return;

	tty_lock(tp);
	ttydisc_rint_simple(tp, str, strlen(str));
	ttydisc_rint_done(tp);
	tty_unlock(tp);
}

/*
 * Binding with the TTY layer.
 */

static int
termtty_open(struct tty *tp)
{
	struct terminal *tm = tty_softc(tp);

	tm->tm_class->tc_opened(tm, 1);
	return (0);
}

static void
termtty_close(struct tty *tp)
{
	struct terminal *tm = tty_softc(tp);

	tm->tm_class->tc_opened(tm, 0);
}

static void
termtty_outwakeup(struct tty *tp)
{
	struct terminal *tm = tty_softc(tp);
	char obuf[128];
	size_t olen;
	unsigned int flags = 0;

	while ((olen = ttydisc_getc(tp, obuf, sizeof obuf)) > 0) {
		TERMINAL_LOCK_TTY(tm);
		if (!(tm->tm_flags & TF_MUTE)) {
			tm->tm_flags &= ~TF_BELL;
			teken_input(&tm->tm_emulator, obuf, olen);
			flags |= tm->tm_flags;
		}
		TERMINAL_UNLOCK_TTY(tm);
	}

	TERMINAL_LOCK_TTY(tm);
	if (!(tm->tm_flags & TF_MUTE))
		tm->tm_class->tc_done(tm);
	TERMINAL_UNLOCK_TTY(tm);
	if (flags & TF_BELL)
		tm->tm_class->tc_bell(tm);
}

static int
termtty_ioctl(struct tty *tp, u_long cmd, caddr_t data, struct thread *td)
{
	struct terminal *tm = tty_softc(tp);
	int error;

	switch (cmd) {
	case CONS_GETINFO: {
		vid_info_t *vi = (vid_info_t *)data;
		const teken_pos_t *p;
		int fg, bg;

		if (vi->size != sizeof(vid_info_t))
			return (EINVAL);

		/* Already help the console driver by filling in some data. */
		p = teken_get_cursor(&tm->tm_emulator);
		vi->mv_row = p->tp_row;
		vi->mv_col = p->tp_col;

		p = teken_get_winsize(&tm->tm_emulator);
		vi->mv_rsz = p->tp_row;
		vi->mv_csz = p->tp_col;

		teken_get_defattr_cons25(&tm->tm_emulator, &fg, &bg);
		vi->mv_norm.fore = fg;
		vi->mv_norm.back = bg;
		/* XXX: keep vidcontrol happy; bold backgrounds. */
		vi->mv_rev.fore = bg;
		vi->mv_rev.back = fg & 0x7;
		break;
	}
	}

	/*
	 * Unlike various other drivers, this driver will never
	 * deallocate TTYs.  This means it's safe to temporarily unlock
	 * the TTY when handling ioctls.
	 */
	tty_unlock(tp);
	error = tm->tm_class->tc_ioctl(tm, cmd, data, td);
	tty_lock(tp);
	return (error);
}

static int
termtty_mmap(struct tty *tp, vm_ooffset_t offset, vm_paddr_t * paddr,
    int nprot, vm_memattr_t *memattr)
{
	struct terminal *tm = tty_softc(tp);

	return (tm->tm_class->tc_mmap(tm, offset, paddr, nprot, memattr));
}

/*
 * Binding with the kernel and debug console.
 */

static cn_probe_t	termcn_cnprobe;
static cn_init_t	termcn_cninit;
static cn_term_t	termcn_cnterm;
static cn_getc_t	termcn_cngetc;
static cn_putc_t	termcn_cnputc;
static cn_grab_t	termcn_cngrab;
static cn_ungrab_t	termcn_cnungrab;

const struct consdev_ops termcn_cnops = {
	.cn_probe	= termcn_cnprobe,
	.cn_init	= termcn_cninit,
	.cn_term	= termcn_cnterm,
	.cn_getc	= termcn_cngetc,
	.cn_putc	= termcn_cnputc,
	.cn_grab	= termcn_cngrab,
	.cn_ungrab	= termcn_cnungrab,
};

void
termcn_cnregister(struct terminal *tm)
{
	struct consdev *cp;

	cp = tm->consdev;
	if (cp == NULL) {
		cp = malloc(sizeof(struct consdev), M_TERMINAL,
		    M_WAITOK|M_ZERO);
		cp->cn_ops = &termcn_cnops;
		cp->cn_arg = tm;
		cp->cn_pri = CN_INTERNAL;
		sprintf(cp->cn_name, "ttyv0");

		tm->tm_flags = TF_CONS;
		tm->consdev = cp;

		terminal_init(tm);
	}

	/* Attach terminal as console. */
	cnadd(cp);
}

static void
termcn_cngrab(struct consdev *cp)
{
	struct terminal *tm = cp->cn_arg;

	tm->tm_class->tc_cngrab(tm);
}

static void
termcn_cnungrab(struct consdev *cp)
{
	struct terminal *tm = cp->cn_arg;

	tm->tm_class->tc_cnungrab(tm);
}

static void
termcn_cnprobe(struct consdev *cp)
{
	struct terminal *tm = cp->cn_arg;

	if (tm == NULL) {
		cp->cn_pri = CN_DEAD;
		return;
	}

	tm->consdev = cp;
	terminal_init(tm);

	tm->tm_class->tc_cnprobe(tm, cp);
}

static void
termcn_cninit(struct consdev *cp)
{

}

static void
termcn_cnterm(struct consdev *cp)
{

}

static int
termcn_cngetc(struct consdev *cp)
{
	struct terminal *tm = cp->cn_arg;

	return (tm->tm_class->tc_cngetc(tm));
}

static void
termcn_cnputc(struct consdev *cp, int c)
{
	struct terminal *tm = cp->cn_arg;
	teken_attr_t backup;
	char cv = c;

	TERMINAL_LOCK_CONS(tm);
	if (!(tm->tm_flags & TF_MUTE)) {
		backup = *teken_get_curattr(&tm->tm_emulator);
		teken_set_curattr(&tm->tm_emulator, &kernel_message);
		teken_input(&tm->tm_emulator, &cv, 1);
		teken_set_curattr(&tm->tm_emulator, &backup);
		tm->tm_class->tc_done(tm);
	}
	TERMINAL_UNLOCK_CONS(tm);
}

/*
 * Binding with the terminal emulator.
 */

static void
termteken_bell(void *softc)
{
	struct terminal *tm = softc;

	tm->tm_flags |= TF_BELL;
}

static void
termteken_cursor(void *softc, const teken_pos_t *p)
{
	struct terminal *tm = softc;

	tm->tm_class->tc_cursor(tm, p);
}

static void
termteken_putchar(void *softc, const teken_pos_t *p, teken_char_t c,
    const teken_attr_t *a)
{
	struct terminal *tm = softc;

	tm->tm_class->tc_putchar(tm, p, TCHAR_CREATE(c, a));
}

static void
termteken_fill(void *softc, const teken_rect_t *r, teken_char_t c,
    const teken_attr_t *a)
{
	struct terminal *tm = softc;

	tm->tm_class->tc_fill(tm, r, TCHAR_CREATE(c, a));
}

static void
termteken_copy(void *softc, const teken_rect_t *r, const teken_pos_t *p)
{
	struct terminal *tm = softc;

	tm->tm_class->tc_copy(tm, r, p);
}

static void
termteken_pre_input(void *softc)
{
	struct terminal *tm = softc;

	tm->tm_class->tc_pre_input(tm);
}

static void
termteken_post_input(void *softc)
{
	struct terminal *tm = softc;

	tm->tm_class->tc_post_input(tm);
}

static void
termteken_param(void *softc, int cmd, unsigned int arg)
{
	struct terminal *tm = softc;

	tm->tm_class->tc_param(tm, cmd, arg);
}

static void
termteken_respond(void *softc, const void *buf, size_t len)
{
#if 0
	struct terminal *tm = softc;
	struct tty *tp;

	/*
	 * Only inject a response into the TTY if the data actually
	 * originated from the TTY.
	 *
	 * XXX: This cannot be done right now.  The TTY could pick up
	 * other locks.  It could also in theory cause loops, when the
	 * TTY performs echoing of a command that generates even more
	 * input.
	 */
	tp = tm->tm_tty;
	if (tp == NULL)
		return;

	ttydisc_rint_simple(tp, buf, len);
	ttydisc_rint_done(tp);
#endif
}
