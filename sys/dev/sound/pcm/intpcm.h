/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2009 Ariff Abdullah <ariff@FreeBSD.org>
 * All rights reserved.
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

#ifndef _SND_INTPCM_H_
#define _SND_INTPCM_H_

typedef intpcm_t intpcm_read_t(uint8_t *);
typedef void intpcm_write_t(uint8_t *, intpcm_t);

extern intpcm_read_t *feeder_format_read_op(uint32_t);
extern intpcm_write_t *feeder_format_write_op(uint32_t);

#define INTPCM_DECLARE_OP_WRITE(SIGN, BIT, ENDIAN, SHIFT)		\
static __inline void							\
intpcm_write_##SIGN##BIT##ENDIAN(uint8_t *dst, intpcm_t v)		\
{									\
									\
	_PCM_WRITE_##SIGN##BIT##_##ENDIAN(dst, v >> SHIFT);		\
}

#define INTPCM_DECLARE_OP_8(SIGN, ENDIAN)				\
static __inline intpcm_t						\
intpcm_read_##SIGN##8##ENDIAN(uint8_t *src)				\
{									\
									\
	return (_PCM_READ_##SIGN##8##_##ENDIAN(src) << 24);		\
}									\
INTPCM_DECLARE_OP_WRITE(SIGN, 8, ENDIAN, 24)

#define INTPCM_DECLARE_OP_16(SIGN, ENDIAN)				\
static __inline intpcm_t						\
intpcm_read_##SIGN##16##ENDIAN(uint8_t *src)				\
{									\
									\
	return (_PCM_READ_##SIGN##16##_##ENDIAN(src) << 16);		\
}									\
INTPCM_DECLARE_OP_WRITE(SIGN, 16, ENDIAN, 16)

#define INTPCM_DECLARE_OP_24(SIGN, ENDIAN)				\
static __inline intpcm_t						\
intpcm_read_##SIGN##24##ENDIAN(uint8_t *src)				\
{									\
									\
	return (_PCM_READ_##SIGN##24##_##ENDIAN(src) << 8);		\
}									\
INTPCM_DECLARE_OP_WRITE(SIGN, 24, ENDIAN, 8)

#define INTPCM_DECLARE_OP_32(SIGN, ENDIAN)				\
static __inline intpcm_t						\
intpcm_read_##SIGN##32##ENDIAN(uint8_t *src)				\
{									\
									\
	return (_PCM_READ_##SIGN##32##_##ENDIAN(src));			\
}									\
									\
static __inline void							\
intpcm_write_##SIGN##32##ENDIAN(uint8_t *dst, intpcm_t v)		\
{									\
									\
	_PCM_WRITE_##SIGN##32##_##ENDIAN(dst, v);			\
}


#define INTPCM_DECLARE(t)						\
									\
G711_DECLARE_TABLE(t);							\
									\
static __inline intpcm_t						\
intpcm_read_ulaw(uint8_t *src)						\
{									\
									\
	return (_G711_TO_INTPCM((t).ulaw_to_u8, *src) << 24);		\
}									\
									\
static __inline intpcm_t						\
intpcm_read_alaw(uint8_t *src)						\
{									\
									\
	return (_G711_TO_INTPCM((t).alaw_to_u8, *src) << 24);		\
}									\
									\
static __inline void							\
intpcm_write_ulaw(uint8_t *dst, intpcm_t v)				\
{									\
									\
	*dst = _INTPCM_TO_G711((t).u8_to_ulaw, v >> 24);		\
}									\
									\
static __inline void							\
intpcm_write_alaw(uint8_t *dst, intpcm_t v)				\
{									\
									\
	*dst = _INTPCM_TO_G711((t).u8_to_alaw, v >> 24);		\
}									\
									\
INTPCM_DECLARE_OP_8(S, NE)						\
INTPCM_DECLARE_OP_16(S, LE)						\
INTPCM_DECLARE_OP_16(S, BE)						\
INTPCM_DECLARE_OP_24(S, LE)						\
INTPCM_DECLARE_OP_24(S, BE)						\
INTPCM_DECLARE_OP_32(S, LE)						\
INTPCM_DECLARE_OP_32(S, BE)						\
INTPCM_DECLARE_OP_8(U,  NE)						\
INTPCM_DECLARE_OP_16(U, LE)						\
INTPCM_DECLARE_OP_16(U, BE)						\
INTPCM_DECLARE_OP_24(U, LE)						\
INTPCM_DECLARE_OP_24(U, BE)						\
INTPCM_DECLARE_OP_32(U, LE)						\
INTPCM_DECLARE_OP_32(U, BE)

#endif	/* !_SND_INTPCM_H_ */
