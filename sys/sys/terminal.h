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
 *
 * $FreeBSD$
 */

#ifndef _SYS_TERMINAL_H_
#define	_SYS_TERMINAL_H_

#include <sys/param.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/cons.h>
#include <sys/linker_set.h>
#include <sys/ttycom.h>

#include <teken/teken.h>

#include "opt_syscons.h"
#include "opt_teken.h"

struct terminal;
struct thread;
struct tty;

/*
 * The terminal layer is an abstraction on top of the TTY layer and the
 * console interface.  It can be used by system console drivers to
 * easily interact with the kernel console and TTYs.
 *
 * Terminals contain terminal emulators, which means console drivers
 * don't need to implement their own terminal emulator. The terminal
 * emulator deals with UTF-8 exclusively. This means that term_char_t,
 * the data type used to store input/output characters will always
 * contain Unicode codepoints.
 *
 * To save memory usage, the top bits of term_char_t will contain other
 * attributes, like colors. Right now term_char_t is composed as
 * follows:
 *
 *  Bits  Meaning
 *  0-20: Character value
 * 21-25: Bold, underline, blink, reverse, right part of CJK fullwidth character
 * 26-28: Foreground color
 * 29-31: Background color
 */

typedef uint32_t term_char_t;
#define	TCHAR_CHARACTER(c)	((c) & 0x1fffff)
#define	TCHAR_FORMAT(c)		(((c) >> 21) & 0x1f)
#define	TCHAR_FGCOLOR(c)	(((c) >> 26) & 0x7)
#define	TCHAR_BGCOLOR(c)	(((c) >> 29) & 0x7)

typedef teken_attr_t term_attr_t;

typedef teken_color_t term_color_t;
#define	TCOLOR_FG(c)	(((c) & 0x7) << 26)
#define	TCOLOR_BG(c)	(((c) & 0x7) << 29)
#define	TCOLOR_LIGHT(c)	((c) | 0x8)
#define	TCOLOR_DARK(c)	((c) & ~0x8)

#define	TFORMAT(c)	(((c) & 0x1f) << 21)

/* syscons(4) compatible color attributes for foreground text */
#define	FG_BLACK		TCOLOR_FG(TC_BLACK)
#define	FG_BLUE			TCOLOR_FG(TC_BLUE)
#define	FG_GREEN		TCOLOR_FG(TC_GREEN)
#define	FG_CYAN			TCOLOR_FG(TC_CYAN)
#define	FG_RED			TCOLOR_FG(TC_RED)
#define	FG_MAGENTA		TCOLOR_FG(TC_MAGENTA)
#define	FG_BROWN		TCOLOR_FG(TC_BROWN)
#define	FG_LIGHTGREY		TCOLOR_FG(TC_WHITE)
#define	FG_DARKGREY		(TFORMAT(TF_BOLD) | TCOLOR_FG(TC_BLACK))
#define	FG_LIGHTBLUE		(TFORMAT(TF_BOLD) | TCOLOR_FG(TC_BLUE))
#define	FG_LIGHTGREEN		(TFORMAT(TF_BOLD) | TCOLOR_FG(TC_GREEN))
#define	FG_LIGHTCYAN		(TFORMAT(TF_BOLD) | TCOLOR_FG(TC_CYAN))
#define	FG_LIGHTRED		(TFORMAT(TF_BOLD) | TCOLOR_FG(TC_RED))
#define	FG_LIGHTMAGENTA		(TFORMAT(TF_BOLD) | TCOLOR_FG(TC_MAGENTA))
#define	FG_YELLOW		(TFORMAT(TF_BOLD) | TCOLOR_FG(TC_BROWN))
#define	FG_WHITE		(TFORMAT(TF_BOLD) | TCOLOR_FG(TC_WHITE))
#define	FG_BLINK		TFORMAT(TF_BLINK)

/* syscons(4) compatible color attributes for text background */
#define	BG_BLACK		TCOLOR_BG(TC_BLACK)
#define	BG_BLUE			TCOLOR_BG(TC_BLUE)
#define	BG_GREEN		TCOLOR_BG(TC_GREEN)
#define	BG_CYAN			TCOLOR_BG(TC_CYAN)
#define	BG_RED			TCOLOR_BG(TC_RED)
#define	BG_MAGENTA		TCOLOR_BG(TC_MAGENTA)
#define	BG_BROWN		TCOLOR_BG(TC_BROWN)
#define	BG_LIGHTGREY		TCOLOR_BG(TC_WHITE)
#define	BG_DARKGREY		(TFORMAT(TF_BOLD) | TCOLOR_BG(TC_BLACK))
#define	BG_LIGHTBLUE		(TFORMAT(TF_BOLD) | TCOLOR_BG(TC_BLUE))
#define	BG_LIGHTGREEN		(TFORMAT(TF_BOLD) | TCOLOR_BG(TC_GREEN))
#define	BG_LIGHTCYAN		(TFORMAT(TF_BOLD) | TCOLOR_BG(TC_CYAN))
#define	BG_LIGHTRED		(TFORMAT(TF_BOLD) | TCOLOR_BG(TC_RED))
#define	BG_LIGHTMAGENTA		(TFORMAT(TF_BOLD) | TCOLOR_BG(TC_MAGENTA))
#define	BG_YELLOW		(TFORMAT(TF_BOLD) | TCOLOR_BG(TC_BROWN))
#define	BG_WHITE		(TFORMAT(TF_BOLD) | TCOLOR_BG(TC_WHITE))

#ifndef TERMINAL_NORM_ATTR
#ifdef SC_NORM_ATTR
#define	TERMINAL_NORM_ATTR	SC_NORM_ATTR
#else
#define	TERMINAL_NORM_ATTR	(FG_LIGHTGREY | BG_BLACK)
#endif
#endif

#ifndef TERMINAL_KERN_ATTR
#ifdef SC_KERNEL_CONS_ATTR
#define	TERMINAL_KERN_ATTR	SC_KERNEL_CONS_ATTR
#else
#define	TERMINAL_KERN_ATTR	(FG_WHITE | BG_BLACK)
#endif
#endif

typedef teken_pos_t term_pos_t;
typedef teken_rect_t term_rect_t;

typedef void tc_cursor_t(struct terminal *tm, const term_pos_t *p);
typedef void tc_putchar_t(struct terminal *tm, const term_pos_t *p,
    term_char_t c);
typedef void tc_fill_t(struct terminal *tm, const term_rect_t *r,
    term_char_t c);
typedef void tc_copy_t(struct terminal *tm, const term_rect_t *r,
    const term_pos_t *p);
typedef void tc_pre_input_t(struct terminal *tm);
typedef void tc_post_input_t(struct terminal *tm);
typedef void tc_param_t(struct terminal *tm, int cmd, unsigned int arg);
typedef void tc_done_t(struct terminal *tm);

typedef void tc_cnprobe_t(struct terminal *tm, struct consdev *cd);
typedef int tc_cngetc_t(struct terminal *tm);

typedef void tc_cngrab_t(struct terminal *tm);
typedef void tc_cnungrab_t(struct terminal *tm);

typedef void tc_opened_t(struct terminal *tm, int opened);
typedef int tc_ioctl_t(struct terminal *tm, u_long cmd, caddr_t data,
    struct thread *td);
typedef int tc_mmap_t(struct terminal *tm, vm_ooffset_t offset,
    vm_paddr_t * paddr, int nprot, vm_memattr_t *memattr);
typedef void tc_bell_t(struct terminal *tm);

struct terminal_class {
	/* Terminal emulator. */
	tc_cursor_t	*tc_cursor;
	tc_putchar_t	*tc_putchar;
	tc_fill_t	*tc_fill;
	tc_copy_t	*tc_copy;
	tc_pre_input_t	*tc_pre_input;
	tc_post_input_t	*tc_post_input;
	tc_param_t	*tc_param;
	tc_done_t	*tc_done;

	/* Low-level console interface. */
	tc_cnprobe_t	*tc_cnprobe;
	tc_cngetc_t	*tc_cngetc;

	/* DDB & panic handling. */
	tc_cngrab_t	*tc_cngrab;
	tc_cnungrab_t	*tc_cnungrab;

	/* Misc. */
	tc_opened_t	*tc_opened;
	tc_ioctl_t	*tc_ioctl;
	tc_mmap_t	*tc_mmap;
	tc_bell_t	*tc_bell;
};

struct terminal {
	const struct terminal_class *tm_class;
	void		*tm_softc;
	struct mtx	 tm_mtx;
	struct tty	*tm_tty;
	teken_t		 tm_emulator;
	struct winsize	 tm_winsize;
	unsigned int	 tm_flags;
#define	TF_MUTE		0x1	/* Drop incoming data. */
#define	TF_BELL		0x2	/* Bell needs to be sent. */
#define	TF_CONS		0x4	/* Console device (needs spinlock). */
	struct consdev	*consdev;
};

#ifdef _KERNEL

struct terminal *terminal_alloc(const struct terminal_class *tc, void *softc);
void	terminal_maketty(struct terminal *tm, const char *fmt, ...);
void	terminal_set_cursor(struct terminal *tm, const term_pos_t *pos);
void	terminal_set_winsize_blank(struct terminal *tm,
    const struct winsize *size, int blank, const term_attr_t *attr);
void	terminal_set_winsize(struct terminal *tm, const struct winsize *size);
void	terminal_mute(struct terminal *tm, int yes);
void	terminal_input_char(struct terminal *tm, term_char_t c);
void	terminal_input_raw(struct terminal *tm, char c);
void	terminal_input_special(struct terminal *tm, unsigned int k);

void	termcn_cnregister(struct terminal *tm);

/* Kernel console helper interface. */
extern const struct consdev_ops termcn_cnops;

#define	TERMINAL_DECLARE_EARLY(name, class, softc)			\
	static struct terminal name = {					\
		.tm_class = &class,					\
		.tm_softc = softc,					\
		.tm_flags = TF_CONS,					\
	};								\
	CONSOLE_DEVICE(name ## _consdev, termcn_cnops, &name)

#endif /* _KERNEL */

#endif /* !_SYS_TERMINAL_H_ */
