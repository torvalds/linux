/* @(#)mconfig.h	1.24 98/12/14 Copyright 1995 J. Schilling */
/*
 *	definitions for machine configuration
 *
 *	Copyright (c) 1995 J. Schilling
 *
 *	This file must be included before any other file.
 *	Use only cpp instructions.
 *
 *	NOTE: SING: (Schily Is Not Gnu)
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _MCONFIG_H
#define _MCONFIG_H

/*
 * This hack that is needed as long as VMS has no POSIX shell.
 */
#ifdef	VMS
#	define	USE_STATIC_CONF
#endif

#ifdef  VANILLA_AUTOCONF
#include <config.h>
#else
#ifdef	USE_STATIC_CONF
#include <xmconfig.h>	/* This is the current static autoconf stuff */
#else
#include <xconfig.h>	/* This is the current dynamic autoconf stuff */
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(unix) || defined(__unix) || defined(__unix__)
#	define	IS_UNIX
#endif

#ifdef	__MSDOS__
#	define	IS_MSDOS
#endif

#if defined(tos) || defined(__tos)
#	define	IS_TOS
#endif

#ifdef	THINK_C
#	define	IS_MAC
#endif

#if defined(sun) || defined(__sun) || defined(__sun__)
#	define	IS_SUN
#endif

#if defined(__CYGWIN32__)
#       define IS_GCC_WIN32
#endif

/*--------------------------------------------------------------------------*/
/*
 * Some magic that cannot (yet) be figured out with autoconf.
 */

#ifdef sparc
#	ifndef	HAVE_LDSTUB
#	define	HAVE_LDSTUB
#	endif
#	ifndef	HAVE_SCANSTACK
#	define	HAVE_SCANSTACK
#	endif
#endif
#if	defined(__i386_) || defined(i386)
#	ifndef	HAVE_XCHG
#	define	HAVE_XCHG
#	endif
#	ifndef	HAVE_SCANSTACK
#	define	HAVE_SCANSTACK
#	endif
#endif

#if	defined(SOL2) || defined(SOL2) || defined(S5R4) || defined(__S5R4) \
							|| defined(SVR4)
#	ifndef	__SVR4
#		define	__SVR4
#	endif
#endif

#ifdef	__SVR4
#	ifndef	SVR4
#		define	SVR4
#	endif
#endif

/*
 * SunOS 4.x / SunOS 5.x
 */
#if defined(IS_SUN)
#	define	HAVE_GETAV0
#endif

/*
 * AIX
 */
#if	defined(_IBMR2) || defined(_AIX)
#	define	IS_UNIX		/* ??? really ??? */
#endif

/*
 * Silicon Graphics	(must be before SVR4)
 */
#if defined(sgi) || defined(__sgi)
#	define	__NOT_SVR4__	/* Not a real SVR4 implementation */
#endif

/*
 * Data General
 */
#if defined(__DGUX__)
#ifdef	XXXXXXX
#	undef	HAVE_MTGET_DSREG
#	undef	HAVE_MTGET_RESID
#	undef	HAVE_MTGET_FILENO
#	undef	HAVE_MTGET_BLKNO
#endif
#	define	mt_type		mt_model
#	define	mt_dsreg	mt_status1
#	define	mt_erreg	mt_status2
	/*
	 * DGUX hides its flock as dg_flock.
	 */
#	define	HAVE_FLOCK
#	define	flock	dg_flock
	/*
	 * Use the BSD style wait on DGUX to get the resource usages of child
	 * processes.
	 */
#	define	_BSD_WAIT_FLAVOR
#endif

/*
 * Apple Rhapsody
 */
#if defined(__NeXT__) && defined(__TARGET_OSNAME) && __TARGET_OSNAME == rhapsody
#	define HAVE_OSDEF /* prevent later definitions to overwrite current */
#endif

/*
 * NextStep
 */
#if defined(__NeXT__) && !defined(HAVE_OSDEF)
#define	NO_PRINT_OVR
#undef	HAVE_USG_STDIO		/*
				 *  NeXT Step 3.x uses __flsbuf(unsigned char , FILE *)
				 * instead of __flsbuf(int, FILE *)
				 */
#endif

/*
 * NextStep 3.x has a broken linker that does not allow us to override
 * these functions.
 */
#ifndef	__OPRINTF__

#ifdef	NO_PRINT_OVR
#	define	printf	Xprintf
#	define	fprintf	Xfprintf
#	define	sprintf	Xsprintf
#endif

#endif	/* __OPRINTF__ */

/*--------------------------------------------------------------------------*/
/*
 * If there is no flock defined by the system, use emulation
 * through fcntl record locking.
 */
#ifndef HAVE_FLOCK
#define LOCK_SH         1       /* shared lock */
#define LOCK_EX         2       /* exclusive lock */
#define LOCK_NB         4       /* don't block when locking */
#define LOCK_UN         8       /* unlock */
#endif

#include <prototyp.h>

/*
 * gcc 2.x generally implements the long long type.
 */
#ifdef	__GNUC__
#	if	__GNUC__ > 1
#		ifndef	HAVE_LONGLONG
#			define	HAVE_LONGLONG
#		endif
#	endif
#endif

/*
 * Convert to GNU name
 */
#ifdef	HAVE_STDC_HEADERS
#	ifndef	STDC_HEADERS
#		define	STDC_HEADERS
#	endif
#endif
/*
 * Convert to SCHILY name
 */
#ifdef	STDC_HEADERS
#	ifndef	HAVE_STDC_HEADERS
#		define	HAVE_STDC_HEADERS
#	endif
#endif

#ifdef	IS_UNIX
#	define	PATH_DELIM	'/'
#	define	PATH_DELIM_STR	"/"
#	define	far
#	define	near
#endif

#ifdef	IS_GCC_WIN32
#	define	PATH_DELIM	'/'
#	define	PATH_DELIM_STR	"/"
#	define	far
#	define	near
#endif

#ifdef	IS_MSDOS
#	define	PATH_DELIM	'\\'
#	define	PATH_DELIM_STR	"\\"
#endif

#ifdef	IS_TOS
#	define	PATH_DELIM	'\\'
#	define	PATH_DELIM_STR	"\\"
#	define	far
#	define	near
#endif

#ifdef	IS_MAC
#	define	PATH_DELIM	':'
#	define	PATH_DELIM_STR	":"
#	define	far
#	define	near
#endif

#ifdef __cplusplus
}
#endif

#endif /* _MCONFIG_H */
