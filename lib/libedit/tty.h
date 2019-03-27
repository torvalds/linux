/*	$NetBSD: tty.h,v 1.19 2016/02/27 18:13:21 christos Exp $	*/

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
 *	@(#)tty.h	8.1 (Berkeley) 6/4/93
 * $FreeBSD$
 */

/*
 * el.tty.h: Local terminal header
 */
#ifndef _h_el_tty
#define	_h_el_tty

#include <termios.h>
#include <unistd.h>

/* Define our own since everyone gets it wrong! */
#define	CONTROL(A)	((A) & 037)

/*
 * Aix compatible names
 */
# if defined(VWERSE) && !defined(VWERASE)
#  define VWERASE VWERSE
# endif /* VWERSE && !VWERASE */

# if defined(VDISCRD) && !defined(VDISCARD)
#  define VDISCARD VDISCRD
# endif /* VDISCRD && !VDISCARD */

# if defined(VFLUSHO) && !defined(VDISCARD)
#  define VDISCARD VFLUSHO
# endif  /* VFLUSHO && VDISCARD */

# if defined(VSTRT) && !defined(VSTART)
#  define VSTART VSTRT
# endif /* VSTRT && ! VSTART */

# if defined(VSTAT) && !defined(VSTATUS)
#  define VSTATUS VSTAT
# endif /* VSTAT && ! VSTATUS */

# ifndef ONLRET
#  define ONLRET 0
# endif /* ONLRET */

# ifndef TAB3
#  ifdef OXTABS
#   define TAB3 OXTABS
#  else
#   define TAB3 0
#  endif /* OXTABS */
# endif /* !TAB3 */

# if defined(OXTABS) && !defined(XTABS)
#  define XTABS OXTABS
# endif /* OXTABS && !XTABS */

# ifndef ONLCR
#  define ONLCR 0
# endif /* ONLCR */

# ifndef IEXTEN
#  define IEXTEN 0
# endif /* IEXTEN */

# ifndef ECHOCTL
#  define ECHOCTL 0
# endif /* ECHOCTL */

# ifndef PARENB
#  define PARENB 0
# endif /* PARENB */

# ifndef EXTPROC
#  define EXTPROC 0
# endif /* EXTPROC */

# ifndef FLUSHO
#  define FLUSHO  0
# endif /* FLUSHO */


# if defined(VDISABLE) && !defined(_POSIX_VDISABLE)
#  define _POSIX_VDISABLE VDISABLE
# endif /* VDISABLE && ! _POSIX_VDISABLE */

/*
 * Work around ISC's definition of IEXTEN which is
 * XCASE!
 */
# ifdef ISC
#  if defined(IEXTEN) && defined(XCASE)
#   if IEXTEN == XCASE
#    undef IEXTEN
#    define IEXTEN 0
#   endif /* IEXTEN == XCASE */
#  endif /* IEXTEN && XCASE */
#  if defined(IEXTEN) && !defined(XCASE)
#   define XCASE IEXTEN
#   undef IEXTEN
#   define IEXTEN 0
#  endif /* IEXTEN && !XCASE */
# endif /* ISC */

/*
 * Work around convex weirdness where turning off IEXTEN makes us
 * lose all postprocessing!
 */
#if defined(convex) || defined(__convex__)
# if defined(IEXTEN) && IEXTEN != 0
#  undef IEXTEN
#  define IEXTEN 0
# endif /* IEXTEN != 0 */
#endif /* convex || __convex__ */

/*
 * So that we don't lose job control.
 */
#ifdef __SVR4
# undef CSWTCH
#endif

#ifndef _POSIX_VDISABLE
# define _POSIX_VDISABLE ((unsigned char) -1)
#endif /* _POSIX_VDISABLE */

#if !defined(CREPRINT) && defined(CRPRNT)
# define CREPRINT CRPRNT
#endif /* !CREPRINT && CRPRNT */
#if !defined(CDISCARD) && defined(CFLUSH)
# define CDISCARD CFLUSH
#endif /* !CDISCARD && CFLUSH */

#ifndef CINTR
# define CINTR		CONTROL('c')
#endif /* CINTR */
#ifndef CQUIT
# define CQUIT		034	/* ^\ */
#endif /* CQUIT */
#ifndef CERASE
# define CERASE		0177	/* ^? */
#endif /* CERASE */
#ifndef CKILL
# define CKILL		CONTROL('u')
#endif /* CKILL */
#ifndef CEOF
# define CEOF		CONTROL('d')
#endif /* CEOF */
#ifndef CEOL
# define CEOL		_POSIX_VDISABLE
#endif /* CEOL */
#ifndef CEOL2
# define CEOL2		_POSIX_VDISABLE
#endif /* CEOL2 */
#ifndef CSWTCH
# define CSWTCH		_POSIX_VDISABLE
#endif /* CSWTCH */
#ifndef CDSWTCH
# define CDSWTCH	_POSIX_VDISABLE
#endif /* CDSWTCH */
#ifndef CERASE2
# define CERASE2	_POSIX_VDISABLE
#endif /* CERASE2 */
#ifndef CSTART
# define CSTART		CONTROL('q')
#endif /* CSTART */
#ifndef CSTOP
# define CSTOP		CONTROL('s')
#endif /* CSTOP */
#ifndef CSUSP
# define CSUSP		CONTROL('z')
#endif /* CSUSP */
#ifndef CDSUSP
# define CDSUSP		CONTROL('y')
#endif /* CDSUSP */

#ifdef hpux

# ifndef CREPRINT
#  define CREPRINT	_POSIX_VDISABLE
# endif /* CREPRINT */
# ifndef CDISCARD
#  define CDISCARD	_POSIX_VDISABLE
# endif /* CDISCARD */
# ifndef CLNEXT
#  define CLNEXT	_POSIX_VDISABLE
# endif /* CLNEXT */
# ifndef CWERASE
#  define CWERASE	_POSIX_VDISABLE
# endif /* CWERASE */

#else /* !hpux */

# ifndef CREPRINT
#  define CREPRINT	CONTROL('r')
# endif /* CREPRINT */
# ifndef CDISCARD
#  define CDISCARD	CONTROL('o')
# endif /* CDISCARD */
# ifndef CLNEXT
#  define CLNEXT	CONTROL('v')
# endif /* CLNEXT */
# ifndef CWERASE
#  define CWERASE	CONTROL('w')
# endif /* CWERASE */

#endif /* hpux */

#ifndef CSTATUS
# define CSTATUS	CONTROL('t')
#endif /* CSTATUS */
#ifndef CPAGE
# define CPAGE		' '
#endif /* CPAGE */
#ifndef CPGOFF
# define CPGOFF		CONTROL('m')
#endif /* CPGOFF */
#ifndef CKILL2
# define CKILL2		_POSIX_VDISABLE
#endif /* CKILL2 */
#ifndef CBRK
# ifndef masscomp
#  define CBRK		0377
# else
#  define CBRK		'\0'
# endif /* masscomp */
#endif /* CBRK */
#ifndef CMIN
# define CMIN		CEOF
#endif /* CMIN */
#ifndef CTIME
# define CTIME		CEOL
#endif /* CTIME */

/*
 * Fix for sun inconsistency. On termio VSUSP and the rest of the
 * ttychars > NCC are defined. So we undefine them.
 */
#if defined(TERMIO) || defined(POSIX)
# if defined(POSIX) && defined(NCCS)
#  define NUMCC		NCCS
# else
#  ifdef NCC
#   define NUMCC	NCC
#  endif /* NCC */
# endif /* POSIX && NCCS */
# ifdef NUMCC
#  ifdef VINTR
#   if NUMCC <= VINTR
#    undef VINTR
#   endif /* NUMCC <= VINTR */
#  endif /* VINTR */
#  ifdef VQUIT
#   if NUMCC <= VQUIT
#    undef VQUIT
#   endif /* NUMCC <= VQUIT */
#  endif /* VQUIT */
#  ifdef VERASE
#   if NUMCC <= VERASE
#    undef VERASE
#   endif /* NUMCC <= VERASE */
#  endif /* VERASE */
#  ifdef VKILL
#   if NUMCC <= VKILL
#    undef VKILL
#   endif /* NUMCC <= VKILL */
#  endif /* VKILL */
#  ifdef VEOF
#   if NUMCC <= VEOF
#    undef VEOF
#   endif /* NUMCC <= VEOF */
#  endif /* VEOF */
#  ifdef VEOL
#   if NUMCC <= VEOL
#    undef VEOL
#   endif /* NUMCC <= VEOL */
#  endif /* VEOL */
#  ifdef VEOL2
#   if NUMCC <= VEOL2
#    undef VEOL2
#   endif /* NUMCC <= VEOL2 */
#  endif /* VEOL2 */
#  ifdef VSWTCH
#   if NUMCC <= VSWTCH
#    undef VSWTCH
#   endif /* NUMCC <= VSWTCH */
#  endif /* VSWTCH */
#  ifdef VDSWTCH
#   if NUMCC <= VDSWTCH
#    undef VDSWTCH
#   endif /* NUMCC <= VDSWTCH */
#  endif /* VDSWTCH */
#  ifdef VERASE2
#   if NUMCC <= VERASE2
#    undef VERASE2
#   endif /* NUMCC <= VERASE2 */
#  endif /* VERASE2 */
#  ifdef VSTART
#   if NUMCC <= VSTART
#    undef VSTART
#   endif /* NUMCC <= VSTART */
#  endif /* VSTART */
#  ifdef VSTOP
#   if NUMCC <= VSTOP
#    undef VSTOP
#   endif /* NUMCC <= VSTOP */
#  endif /* VSTOP */
#  ifdef VWERASE
#   if NUMCC <= VWERASE
#    undef VWERASE
#   endif /* NUMCC <= VWERASE */
#  endif /* VWERASE */
#  ifdef VSUSP
#   if NUMCC <= VSUSP
#    undef VSUSP
#   endif /* NUMCC <= VSUSP */
#  endif /* VSUSP */
#  ifdef VDSUSP
#   if NUMCC <= VDSUSP
#    undef VDSUSP
#   endif /* NUMCC <= VDSUSP */
#  endif /* VDSUSP */
#  ifdef VREPRINT
#   if NUMCC <= VREPRINT
#    undef VREPRINT
#   endif /* NUMCC <= VREPRINT */
#  endif /* VREPRINT */
#  ifdef VDISCARD
#   if NUMCC <= VDISCARD
#    undef VDISCARD
#   endif /* NUMCC <= VDISCARD */
#  endif /* VDISCARD */
#  ifdef VLNEXT
#   if NUMCC <= VLNEXT
#    undef VLNEXT
#   endif /* NUMCC <= VLNEXT */
#  endif /* VLNEXT */
#  ifdef VSTATUS
#   if NUMCC <= VSTATUS
#    undef VSTATUS
#   endif /* NUMCC <= VSTATUS */
#  endif /* VSTATUS */
#  ifdef VPAGE
#   if NUMCC <= VPAGE
#    undef VPAGE
#   endif /* NUMCC <= VPAGE */
#  endif /* VPAGE */
#  ifdef VPGOFF
#   if NUMCC <= VPGOFF
#    undef VPGOFF
#   endif /* NUMCC <= VPGOFF */
#  endif /* VPGOFF */
#  ifdef VKILL2
#   if NUMCC <= VKILL2
#    undef VKILL2
#   endif /* NUMCC <= VKILL2 */
#  endif /* VKILL2 */
#  ifdef VBRK
#   if NUMCC <= VBRK
#    undef VBRK
#   endif /* NUMCC <= VBRK */
#  endif /* VBRK */
#  ifdef VMIN
#   if NUMCC <= VMIN
#    undef VMIN
#   endif /* NUMCC <= VMIN */
#  endif /* VMIN */
#  ifdef VTIME
#   if NUMCC <= VTIME
#    undef VTIME
#   endif /* NUMCC <= VTIME */
#  endif /* VTIME */
# endif /* NUMCC */
#endif /* !POSIX */

#define	C_INTR		 0
#define	C_QUIT		 1
#define	C_ERASE		 2
#define	C_KILL		 3
#define	C_EOF		 4
#define	C_EOL		 5
#define	C_EOL2		 6
#define	C_SWTCH		 7
#define	C_DSWTCH	 8
#define	C_ERASE2	 9
#define	C_START		10
#define	C_STOP		11
#define	C_WERASE	12
#define	C_SUSP		13
#define	C_DSUSP		14
#define	C_REPRINT	15
#define	C_DISCARD	16
#define	C_LNEXT		17
#define	C_STATUS	18
#define	C_PAGE		19
#define	C_PGOFF		20
#define	C_KILL2		21
#define	C_BRK		22
#define	C_MIN		23
#define	C_TIME		24
#define	C_NCC		25
#define	C_SH(A)		((unsigned int)(1 << (A)))

/*
 * Terminal dependend data structures
 */
#define	EX_IO	0	/* while we are executing	*/
#define	ED_IO	1	/* while we are editing		*/
#define	TS_IO	2	/* new mode from terminal	*/
#define	QU_IO	2	/* used only for quoted chars	*/
#define	NN_IO	3	/* The number of entries	*/

/* Don't re-order */
#define	MD_INP	0
#define	MD_OUT	1
#define	MD_CTL	2
#define	MD_LIN	3
#define	MD_CHAR	4
#define	MD_NN	5

typedef struct {
	const char	*t_name;
	unsigned int	 t_setmask;
	unsigned int	 t_clrmask;
} ttyperm_t[NN_IO][MD_NN];

typedef unsigned char ttychar_t[NN_IO][C_NCC];

protected int	tty_init(EditLine *);
protected void	tty_end(EditLine *);
protected int	tty_stty(EditLine *, int, const Char **);
protected int	tty_rawmode(EditLine *);
protected int	tty_cookedmode(EditLine *);
protected int	tty_quotemode(EditLine *);
protected int	tty_noquotemode(EditLine *);
protected void	tty_bind_char(EditLine *, int);

typedef struct {
    ttyperm_t t_t;
    ttychar_t t_c;
    struct termios t_or, t_ex, t_ed, t_ts;
    int t_tabs;
    int t_eight;
    speed_t t_speed;
    unsigned char t_mode;
    unsigned char t_vdisable;
    unsigned char t_initialized;
} el_tty_t;


#endif /* _h_el_tty */
