/*-
 * Cronyx DDK: platform dependent definitions.
 *
 * Copyright (C) 1998-1999 Cronyx Engineering
 * Author: Alexander Kvitchenko, <aak@cronyx.ru>
 *
 * Copyright (C) 2001-2003 Cronyx Engineering.
 * Author: Roman Kurakin, <rik@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: machdep.h,v 1.3.4.3 2003/11/27 14:21:58 rik Exp $
 * $FreeBSD$
 */

/*
 * DOS (Borland Turbo C++ 1.0)
 */
#if defined (MSDOS) || defined (__MSDOS__)
#   include <dos.h>
#   include <string.h>
#   define inb(port)		inportb(port)
#   define inw(port)		inport(port)
#   define outb(port,b)		outportb(port,b)
#   define outw(port,w)		outport(port,w)
#   define GETTICKS()		biostime(0,0L)
#else

/*
 * Windows NT
 */
#ifdef NDIS_MINIPORT_DRIVER
#   include <string.h>
#   define inb(port)		inp((unsigned short)(port))
#   define inw(port)		inpw((unsigned short)(port))
#   define outb(port,b)		outp((unsigned short)(port),b)
#   define outw(port,w)		outpw((unsigned short)(port),(unsigned short)(w))
#pragma warning (disable: 4761)
#pragma warning (disable: 4242)
#pragma warning (disable: 4244)
#define ulong64			unsigned __int64
#else

/*
 * Linux
 */
#ifdef __linux__
#   undef REALLY_SLOW_IO
#   include <asm/io.h>		/* should swap outb() arguments */
#   include <linux/string.h>
#   include <linux/delay.h>
    static inline void __ddk_outb (unsigned port, unsigned char byte)
    { outb (byte, port); }
    static inline void __ddk_outw (unsigned port, unsigned short word)
    { outw (word, port); }
#   undef outb
#   undef outw
#   define outb(port,val)	__ddk_outb(port, val)
#   define outw(port,val)	__ddk_outw(port, val)
#   define GETTICKS()		(jiffies * 200 / 11 / HZ)
#else

/*
 * FreeBSD and BSD/OS
 */
#ifdef __FreeBSD__
#   include <sys/param.h>
#   include <machine/cpufunc.h>
#   include <sys/libkern.h>
#   include <sys/systm.h>
#   define port_t int

#ifndef _SYS_CDEFS_H_
#error this file needs sys/cdefs.h as a prerequisite
#endif
#endif

#endif
#endif
#endif

#ifndef inline
#   ifdef __CC_SUPPORTS___INLINE__
#      define inline __inline__
#   else
#      define inline /**/
#   endif
#endif

#ifndef ulong64
#define ulong64 unsigned long long
#endif
