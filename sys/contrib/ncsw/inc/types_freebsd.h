/*-
 * Copyright (c) 2011 Semihalf.
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
 */

#ifndef TYPES_FREEBSD_H_
#define TYPES_FREEBSD_H_

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/stddef.h>
#include <sys/types.h>

#include <machine/pio.h>

#if !defined(__bool_true_false_are_defined)
typedef	boolean_t	bool;
#endif
#define	TRUE		1
#define	FALSE		0

typedef vm_paddr_t	physAddress_t;

#define	_Packed
#define	_PackedType	__attribute__ ((packed))

/**
 * Accessor defines.
 * TODO: These are only stubs and have to be redefined (use bus_space
 * facilities).
 */
#define GET_UINT32(arg)			in32(&(arg))
#define GET_UINT64(arg)			in64(&(arg))

#define _WRITE_UINT32(arg, data)	out32(&(arg), (data))
#define _WRITE_UINT64(arg, data)	out64(&(arg), (data))

#ifndef QE_32_BIT_ACCESS_RESTRICTION

#define GET_UINT8(arg)			in8(&(arg))
#define GET_UINT16(arg)			in16(&(arg))

#define _WRITE_UINT8(arg, data)		out8(&(arg), (data))
#define _WRITE_UINT16(arg, data)	out16(&(arg), (data))

#else  /* QE_32_BIT_ACCESS_RESTRICTION */

#define QE_32_BIT_ADDR(_arg)        (uint32_t)((uint32_t)&(_arg) & 0xFFFFFFFC)
#define QE_32_BIT_SHIFT8(__arg)     (uint32_t)((3 - ((uint32_t)&(__arg) & 0x3)) * 8)
#define QE_32_BIT_SHIFT16(__arg)    (uint32_t)((2 - ((uint32_t)&(__arg) & 0x3)) * 8)

#define GET_UINT8(arg)              (uint8_t)(in32(QE_32_BIT_ADDR(arg)) >> QE_32_BIT_SHIFT8(arg))
#define GET_UINT16(arg)             (uint16_t)(in32(QE_32_BIT_ADDR(arg)) >> QE_32_BIT_SHIFT16(arg))

#define _WRITE_UINT8(arg, data)                                                                         \
    do                                                                                                  \
    {                                                                                                   \
        uint32_t addr = QE_32_BIT_ADDR(arg);                                                            \
        uint32_t shift = QE_32_BIT_SHIFT8(arg);                                                         \
        uint32_t tmp = in32(addr);                                                                      \
        tmp = (uint32_t)((tmp & ~(0x000000FF << shift)) | ((uint32_t)(data & 0x000000FF) << shift));    \
        out32(addr, tmp);                                                                               \
    } while (0)

#define _WRITE_UINT16(arg, data)                                                                        \
    do                                                                                                  \
    {                                                                                                   \
        uint32_t addr = QE_32_BIT_ADDR(arg);                                                            \
        uint32_t shift = QE_32_BIT_SHIFT16(arg);                                                        \
        uint32_t tmp = in32(addr);                                                                      \
        tmp = (uint32_t)((tmp & ~(0x0000FFFF << shift)) | ((uint32_t)(data & 0x0000FFFF) << shift));    \
        out32(addr, tmp);                                                                               \
    } while (0)

#endif /* QE_32_BIT_ACCESS_RESTRICTION */

#define WRITE_UINT8                 _WRITE_UINT8
#define WRITE_UINT16                _WRITE_UINT16
#define WRITE_UINT32                _WRITE_UINT32
#define WRITE_UINT64                _WRITE_UINT64

#endif /* TYPES_FREEBSD_H_ */
