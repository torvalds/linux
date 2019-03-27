/* $FreeBSD$ */
/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
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

#ifndef _USB_ENDIAN_H_
#define	_USB_ENDIAN_H_

#ifndef USB_GLOBAL_INCLUDE_FILE
#include <sys/stdint.h>
#include <sys/endian.h>
#endif

/*
 * Declare the basic USB record types. USB records have an alignment
 * of 1 byte and are always packed.
 */
typedef uint8_t uByte;
typedef uint8_t uWord[2];
typedef uint8_t uDWord[4];
typedef uint8_t uQWord[8];

/*
 * Define a set of macros that can get and set data independent of
 * CPU endianness and CPU alignment requirements:
 */
#define	UGETB(w)			\
  ((w)[0])

#define	UGETW(w)			\
  ((w)[0] |				\
  (((uint16_t)((w)[1])) << 8))

#define	UGETDW(w)			\
  ((w)[0] |				\
  (((uint16_t)((w)[1])) << 8) |		\
  (((uint32_t)((w)[2])) << 16) |	\
  (((uint32_t)((w)[3])) << 24))

#define	UGETQW(w)			\
  ((w)[0] |				\
  (((uint16_t)((w)[1])) << 8) |		\
  (((uint32_t)((w)[2])) << 16) |	\
  (((uint32_t)((w)[3])) << 24) |	\
  (((uint64_t)((w)[4])) << 32) |	\
  (((uint64_t)((w)[5])) << 40) |	\
  (((uint64_t)((w)[6])) << 48) |	\
  (((uint64_t)((w)[7])) << 56))

#define	USETB(w,v) do {			\
  (w)[0] = (uint8_t)(v);		\
} while (0)

#define	USETW(w,v) do {			\
  (w)[0] = (uint8_t)(v);		\
  (w)[1] = (uint8_t)((v) >> 8);		\
} while (0)

#define	USETDW(w,v) do {		\
  (w)[0] = (uint8_t)(v);		\
  (w)[1] = (uint8_t)((v) >> 8);		\
  (w)[2] = (uint8_t)((v) >> 16);	\
  (w)[3] = (uint8_t)((v) >> 24);	\
} while (0)

#define	USETQW(w,v) do {		\
  (w)[0] = (uint8_t)(v);		\
  (w)[1] = (uint8_t)((v) >> 8);		\
  (w)[2] = (uint8_t)((v) >> 16);	\
  (w)[3] = (uint8_t)((v) >> 24);	\
  (w)[4] = (uint8_t)((v) >> 32);	\
  (w)[5] = (uint8_t)((v) >> 40);	\
  (w)[6] = (uint8_t)((v) >> 48);	\
  (w)[7] = (uint8_t)((v) >> 56);	\
} while (0)

#define	USETW2(w,b1,b0) do {		\
  (w)[0] = (uint8_t)(b0);		\
  (w)[1] = (uint8_t)(b1);		\
} while (0)

#define	USETW4(w,b3,b2,b1,b0) do {	\
  (w)[0] = (uint8_t)(b0);		\
  (w)[1] = (uint8_t)(b1);		\
  (w)[2] = (uint8_t)(b2);		\
  (w)[3] = (uint8_t)(b3);		\
} while (0)

#define	USETW8(w,b7,b6,b5,b4,b3,b2,b1,b0) do {	\
  (w)[0] = (uint8_t)(b0);		\
  (w)[1] = (uint8_t)(b1);		\
  (w)[2] = (uint8_t)(b2);		\
  (w)[3] = (uint8_t)(b3);		\
  (w)[4] = (uint8_t)(b4);		\
  (w)[5] = (uint8_t)(b5);		\
  (w)[6] = (uint8_t)(b6);		\
  (w)[7] = (uint8_t)(b7);		\
} while (0)

#endif					/* _USB_ENDIAN_H_ */
