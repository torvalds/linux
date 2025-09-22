/*	$OpenBSD: endian.h,v 1.9 2014/07/12 16:25:08 guenther Exp $ */

/*
 * Copyright (c) 2001-2002 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _MIPS64_ENDIAN_H_
#define _MIPS64_ENDIAN_H_

#if defined(__MIPSEL__)
#define _BYTE_ORDER _LITTLE_ENDIAN
#endif
#if defined(__MIPSEB__)
#define _BYTE_ORDER _BIG_ENDIAN
#endif

#if !defined(_BYTE_ORDER)
#error "__MIPSEL__ or __MIPSEB__ must be defined to define BYTE_ORDER!!!"
#endif

#define	__STRICT_ALIGNMENT

#ifndef __FROM_SYS__ENDIAN
#include <sys/endian.h>
#endif

#endif /* _MIPS64_ENDIAN_H_ */
