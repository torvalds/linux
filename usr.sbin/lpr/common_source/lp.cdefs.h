/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * Copyright (c) 2003,2013  - Garance Alistair Drosehn <gad@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the FreeBSD Project.
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * $FreeBSD$
 * ------+---------+---------+---------+---------+---------+---------+---------*
 */

/*
 * The main goal of this include file is to provide a platform-neutral way
 * to define some macros that lpr wants from FreeBSD's <sys/cdefs.h>.  This
 * will simply use the standard <sys/cdefs.h> when compiled in FreeBSD, but
 * other OS's may not have /usr/include/sys/cdefs.h (or even if that file
 * exists, it may not define all the macros that lpr will use).
 */

#if !defined(_LP_CDEFS_H_)
#define	_LP_CDEFS_H_

/*
 * For non-BSD platforms, you can compile lpr with -DHAVE_SYS_CDEFS_H
 * if <sys/cdefs.h> should be included.
 */
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#  define HAVE_SYS_CDEFS_H
#endif
#if defined(HAVE_SYS_CDEFS_H)
#  include <sys/cdefs.h>
#endif

/*
 * FreeBSD added a closefrom() routine in release 8.0.  When compiling
 * `lpr' on other platforms you might want to include bsd-closefrom.c
 * from the portable-openssh project.
 */
#ifndef	USE_CLOSEFROM
#  if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#    define	USE_CLOSEFROM	1
#  endif
#endif
/* The macro USE_CLOSEFROM must be defined with a value of 0 or 1. */
#ifndef	USE_CLOSEFROM
#  define	USE_CLOSEFROM	0
#endif

/*
 * __unused is a compiler-specific trick which can be used to avoid
 * warnings about a variable which is defined but never referenced.
 * Some lpr files use this, so define a null version if it was not
 * defined by <sys/cdefs.h>.
 */
#if !defined(__unused)
#  define	__unused
#endif

/*
 * All the lpr source files will want to reference __FBSDID() to
 * handle rcs id's.
 */
#if !defined(__FBSDID)
#  if defined(lint) || defined(STRIP_FBSDID)
#    define	__FBSDID(s)	struct skip_rcsid_struct
#  elif defined(__IDSTRING)			/* NetBSD */
#    define	__FBSDID(s)	__IDSTRING(rcsid,s)
#  else
#    define	__FBSDID(s)	static const char rcsid[] __unused = s
#  endif
#endif /* __FBSDID */

/*
 * Some lpr include files use __BEGIN_DECLS and __END_DECLS.
 */
#if !defined(__BEGIN_DECLS)
#  if defined(__cplusplus)
#    define	__BEGIN_DECLS	extern "C" {
#    define	__END_DECLS	}
#  else
#    define	__BEGIN_DECLS
#    define	__END_DECLS
#  endif
#endif

/*
 * __printflike and __printf0like are a compiler-specific tricks to
 * tell the compiler to check the format-codes in printf-like
 * routines wrt the args that will be formatted.
 */
#if !defined(__printflike)
#  define	__printflike(fmtarg, firstvararg)
#endif
#if !defined(__printf0like)
#  define	__printf0like(fmtarg, firstvararg)
#endif

#endif /* !_LP_CDEFS_H_ */
