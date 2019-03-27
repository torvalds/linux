/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2012 Alexander Leidinger <netchild@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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
 *
 * $FreeBSD$
 */

#ifndef _LINUX_DTRACE_H_
#define _LINUX_DTRACE_H_

/**
 * DTrace support macros for the linuxulator.
 *
 * Some wrapper macros to make it more easy to handle the linuxulator
 * providers and to allow to make the name depend upon the bitsize.
 *
 * Basically this is the same as the normal SDT macros in sys/sdt.h. The
 * difference is that the provider name is automatically inserted, and
 * we do not use a different name for the probe-description.
 */

#define	LIN_SDT_PROVIDER_DEFINE(x)	SDT_PROVIDER_DEFINE(x)
#define LIN_SDT_PROVIDER_DECLARE(x)	SDT_PROVIDER_DECLARE(x)

#define	_LIN_SDT_PROBE_DECLARE(a, b, c, d)	SDT_PROBE_DECLARE(a, b, c, d)
#define	LIN_SDT_PROBE_DECLARE(a, b, c)		_LIN_SDT_PROBE_DECLARE( \
    LINUX_DTRACE, a, b, c)

#define	_LIN_SDT_PROBE_DEFINE0(a, b, c, d)		SDT_PROBE_DEFINE(a, \
    b, c, d)
#define	LIN_SDT_PROBE_DEFINE0(a, b, c)			_LIN_SDT_PROBE_DEFINE0(\
    LINUX_DTRACE, a, b, c)
#define	_LIN_SDT_PROBE_DEFINE1(a, b, c, d, e)		SDT_PROBE_DEFINE1(a, \
    b, c, d, e)
#define	LIN_SDT_PROBE_DEFINE1(a, b, c, d)		_LIN_SDT_PROBE_DEFINE1(\
    LINUX_DTRACE, a, b, c, d)
#define	_LIN_SDT_PROBE_DEFINE2(a, b, c, d, e, f)	SDT_PROBE_DEFINE2(a, \
    b, c, d, e, f)
#define	LIN_SDT_PROBE_DEFINE2(a, b, c, d, e)		_LIN_SDT_PROBE_DEFINE2(\
    LINUX_DTRACE, a, b, c, d, e)
#define	_LIN_SDT_PROBE_DEFINE3(a, b, c, d, e, f, g)	SDT_PROBE_DEFINE3(a, \
    b, c, d, e, f, g)
#define	LIN_SDT_PROBE_DEFINE3(a, b, c, d, e, f)		_LIN_SDT_PROBE_DEFINE3(\
    LINUX_DTRACE, a, b, c, d, e, f)
#define	_LIN_SDT_PROBE_DEFINE4(a, b, c, d, e, f, g, h)	SDT_PROBE_DEFINE4(a, \
    b, c, d, e, f, g, h)
#define	LIN_SDT_PROBE_DEFINE4(a, b, c, d, e, f, g)	_LIN_SDT_PROBE_DEFINE4(\
    LINUX_DTRACE, a, b, c, d, e, f, g)
#define	_LIN_SDT_PROBE_DEFINE5(a, b, c, d, e, f, g, h, i) \
    SDT_PROBE_DEFINE5(a, b, c, d, e, f, g, h, i)
#define	LIN_SDT_PROBE_DEFINE5(a, b, c, d, e, f, g, h)	_LIN_SDT_PROBE_DEFINE5(\
    LINUX_DTRACE, a, b, c, d, e, f, g, h)

#define	LIN_SDT_PROBE0(a, b, c)			SDT_PROBE0(LINUX_DTRACE, a, b, \
    c)
#define	LIN_SDT_PROBE1(a, b, c, d)		SDT_PROBE1(LINUX_DTRACE, a, b, \
    c, d)
#define	LIN_SDT_PROBE2(a, b, c, d, e)		SDT_PROBE2(LINUX_DTRACE, a, b, \
    c, d, e)
#define	LIN_SDT_PROBE3(a, b, c, d, e, f)	SDT_PROBE3(LINUX_DTRACE, a, b, \
    c, d, e, f)
#define	LIN_SDT_PROBE4(a, b, c, d, e, f, g)	SDT_PROBE4(LINUX_DTRACE, a, b, \
    c, d, e, f, g)
#define	_LIN_SDT_PROBE5(a, b, c, d, e, f, g, h, i)	SDT_PROBE5(a, b, c, d, \
    e, f, g, h, i)
#define	LIN_SDT_PROBE5(a, b, c, d, e, f, g, h)	_LIN_SDT_PROBE5(LINUX_DTRACE, \
    a, b, c, d, e, f, g, h)

#endif /* _LINUX_DTRACE_H_ */
