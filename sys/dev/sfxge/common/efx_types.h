/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 *
 * Ackowledgement to Fen Systems Ltd.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_EFX_TYPES_H
#define	_SYS_EFX_TYPES_H

#include "efsys.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Bitfield access
 *
 * Solarflare NICs make extensive use of bitfields up to 128 bits
 * wide.  Since there is no native 128-bit datatype on most systems,
 * and since 64-bit datatypes are inefficient on 32-bit systems and
 * vice versa, we wrap accesses in a way that uses the most efficient
 * datatype.
 *
 * The NICs are PCI devices and therefore little-endian.  Since most
 * of the quantities that we deal with are DMAed to/from host memory,
 * we define	our datatypes (efx_oword_t, efx_qword_t and efx_dword_t)
 * to be little-endian.
 *
 * In the less common case of using PIO for individual register
 * writes, we construct the little-endian datatype in host memory and
 * then use non-swapping register access primitives, rather than
 * constructing a native-endian datatype and relying on implicit
 * byte-swapping.  (We use a similar strategy for register reads.)
 */

/*
 * NOTE: Field definitions here and elsewhere are done in terms of a lowest
 *       bit number (LBN) and a width.
 */

#define	EFX_DUMMY_FIELD_LBN 0
#define	EFX_DUMMY_FIELD_WIDTH 0

#define	EFX_BYTE_0_LBN 0
#define	EFX_BYTE_0_WIDTH 8

#define	EFX_BYTE_1_LBN 8
#define	EFX_BYTE_1_WIDTH 8

#define	EFX_BYTE_2_LBN 16
#define	EFX_BYTE_2_WIDTH 8

#define	EFX_BYTE_3_LBN 24
#define	EFX_BYTE_3_WIDTH 8

#define	EFX_BYTE_4_LBN 32
#define	EFX_BYTE_4_WIDTH 8

#define	EFX_BYTE_5_LBN 40
#define	EFX_BYTE_5_WIDTH 8

#define	EFX_BYTE_6_LBN 48
#define	EFX_BYTE_6_WIDTH 8

#define	EFX_BYTE_7_LBN 56
#define	EFX_BYTE_7_WIDTH 8

#define	EFX_WORD_0_LBN 0
#define	EFX_WORD_0_WIDTH 16

#define	EFX_WORD_1_LBN 16
#define	EFX_WORD_1_WIDTH 16

#define	EFX_WORD_2_LBN 32
#define	EFX_WORD_2_WIDTH 16

#define	EFX_WORD_3_LBN 48
#define	EFX_WORD_3_WIDTH 16

#define	EFX_DWORD_0_LBN 0
#define	EFX_DWORD_0_WIDTH 32

#define	EFX_DWORD_1_LBN 32
#define	EFX_DWORD_1_WIDTH 32

#define	EFX_DWORD_2_LBN 64
#define	EFX_DWORD_2_WIDTH 32

#define	EFX_DWORD_3_LBN 96
#define	EFX_DWORD_3_WIDTH 32

/*
 * There are intentionally no EFX_QWORD_0 or EFX_QWORD_1 field definitions
 * here as the implementaion of EFX_QWORD_FIELD and EFX_OWORD_FIELD do not
 * support field widths larger than 32 bits.
 */

/* Specified attribute (i.e. LBN ow WIDTH) of the specified field */
#define	EFX_VAL(_field, _attribute)					\
	_field ## _ ## _attribute

/* Lowest bit number of the specified field */
#define	EFX_LOW_BIT(_field)						\
	EFX_VAL(_field, LBN)

/* Width of the specified field */
#define	EFX_WIDTH(_field)						\
	EFX_VAL(_field, WIDTH)

/* Highest bit number of the specified field */
#define	EFX_HIGH_BIT(_field)						\
	(EFX_LOW_BIT(_field) + EFX_WIDTH(_field) - 1)

/*
 * 64-bit mask equal in width to the specified field.
 *
 * For example, a field with width 5 would have a mask of 0x000000000000001f.
 */
#define	EFX_MASK64(_field)						\
	((EFX_WIDTH(_field) == 64) ? ~((uint64_t)0) :			\
	    (((((uint64_t)1) << EFX_WIDTH(_field))) - 1))
/*
 * 32-bit mask equal in width to the specified field.
 *
 * For example, a field with width 5 would have a mask of 0x0000001f.
 */
#define	EFX_MASK32(_field)						\
	((EFX_WIDTH(_field) == 32) ? ~((uint32_t)0) :			\
	    (((((uint32_t)1) << EFX_WIDTH(_field))) - 1))

/*
 * 16-bit mask equal in width to the specified field.
 *
 * For example, a field with width 5 would have a mask of 0x001f.
 */
#define	EFX_MASK16(_field)						\
	((EFX_WIDTH(_field) == 16) ? 0xffffu :				\
	    (uint16_t)((1 << EFX_WIDTH(_field)) - 1))

/*
 * 8-bit mask equal in width to the specified field.
 *
 * For example, a field with width 5 would have a mask of 0x1f.
 */
#define	EFX_MASK8(_field)						\
	((uint8_t)((1 << EFX_WIDTH(_field)) - 1))

#pragma pack(1)

/*
 * A byte (i.e. 8-bit) datatype
 */
typedef union efx_byte_u {
	uint8_t eb_u8[1];
} efx_byte_t;

/*
 * A word (i.e. 16-bit) datatype
 *
 * This datatype is defined to be little-endian.
 */
typedef union efx_word_u {
	efx_byte_t ew_byte[2];
	uint16_t ew_u16[1];
	uint8_t ew_u8[2];
} efx_word_t;

/*
 * A doubleword (i.e. 32-bit) datatype
 *
 * This datatype is defined to be little-endian.
 */
typedef union efx_dword_u {
	efx_byte_t ed_byte[4];
	efx_word_t ed_word[2];
	uint32_t ed_u32[1];
	uint16_t ed_u16[2];
	uint8_t ed_u8[4];
} efx_dword_t;

/*
 * A quadword (i.e. 64-bit) datatype
 *
 * This datatype is defined to be little-endian.
 */
typedef union efx_qword_u {
	efx_byte_t eq_byte[8];
	efx_word_t eq_word[4];
	efx_dword_t eq_dword[2];
#if EFSYS_HAS_UINT64
	uint64_t eq_u64[1];
#endif
	uint32_t eq_u32[2];
	uint16_t eq_u16[4];
	uint8_t eq_u8[8];
} efx_qword_t;

/*
 * An octword (i.e. 128-bit) datatype
 *
 * This datatype is defined to be little-endian.
 */
typedef union efx_oword_u {
	efx_byte_t eo_byte[16];
	efx_word_t eo_word[8];
	efx_dword_t eo_dword[4];
	efx_qword_t eo_qword[2];
#if EFSYS_HAS_SSE2_M128
	__m128i eo_u128[1];
#endif
#if EFSYS_HAS_UINT64
	uint64_t eo_u64[2];
#endif
	uint32_t eo_u32[4];
	uint16_t eo_u16[8];
	uint8_t eo_u8[16];
} efx_oword_t;

#pragma pack()

#define	__SWAP16(_x)				\
	((((_x) & 0xff) << 8) |			\
	(((_x) >> 8) & 0xff))

#define	__SWAP32(_x)				\
	((__SWAP16((_x) & 0xffff) << 16) |	\
	__SWAP16(((_x) >> 16) & 0xffff))

#define	__SWAP64(_x)				\
	((__SWAP32((_x) & 0xffffffff) << 32) |	\
	__SWAP32(((_x) >> 32) & 0xffffffff))

#define	__NOSWAP16(_x)		(_x)
#define	__NOSWAP32(_x)		(_x)
#define	__NOSWAP64(_x)		(_x)

#if EFSYS_IS_BIG_ENDIAN

#define	__CPU_TO_LE_16(_x)	((uint16_t)__SWAP16(_x))
#define	__LE_TO_CPU_16(_x)	((uint16_t)__SWAP16(_x))
#define	__CPU_TO_BE_16(_x)	((uint16_t)__NOSWAP16(_x))
#define	__BE_TO_CPU_16(_x)	((uint16_t)__NOSWAP16(_x))

#define	__CPU_TO_LE_32(_x)	((uint32_t)__SWAP32(_x))
#define	__LE_TO_CPU_32(_x)	((uint32_t)__SWAP32(_x))
#define	__CPU_TO_BE_32(_x)	((uint32_t)__NOSWAP32(_x))
#define	__BE_TO_CPU_32(_x)	((uint32_t)__NOSWAP32(_x))

#define	__CPU_TO_LE_64(_x)	((uint64_t)__SWAP64(_x))
#define	__LE_TO_CPU_64(_x)	((uint64_t)__SWAP64(_x))
#define	__CPU_TO_BE_64(_x)	((uint64_t)__NOSWAP64(_x))
#define	__BE_TO_CPU_64(_x)	((uint64_t)__NOSWAP64(_x))

#elif EFSYS_IS_LITTLE_ENDIAN

#define	__CPU_TO_LE_16(_x)	((uint16_t)__NOSWAP16(_x))
#define	__LE_TO_CPU_16(_x)	((uint16_t)__NOSWAP16(_x))
#define	__CPU_TO_BE_16(_x)	((uint16_t)__SWAP16(_x))
#define	__BE_TO_CPU_16(_x)	((uint16_t)__SWAP16(_x))

#define	__CPU_TO_LE_32(_x)	((uint32_t)__NOSWAP32(_x))
#define	__LE_TO_CPU_32(_x)	((uint32_t)__NOSWAP32(_x))
#define	__CPU_TO_BE_32(_x)	((uint32_t)__SWAP32(_x))
#define	__BE_TO_CPU_32(_x)	((uint32_t)__SWAP32(_x))

#define	__CPU_TO_LE_64(_x)	((uint64_t)__NOSWAP64(_x))
#define	__LE_TO_CPU_64(_x)	((uint64_t)__NOSWAP64(_x))
#define	__CPU_TO_BE_64(_x)	((uint64_t)__SWAP64(_x))
#define	__BE_TO_CPU_64(_x)	((uint64_t)__SWAP64(_x))

#else

#error "Neither of EFSYS_IS_{BIG,LITTLE}_ENDIAN is set"

#endif

#define	__NATIVE_8(_x)	(uint8_t)(_x)

/* Format string for printing an efx_byte_t */
#define	EFX_BYTE_FMT "0x%02x"

/* Format string for printing an efx_word_t */
#define	EFX_WORD_FMT "0x%04x"

/* Format string for printing an efx_dword_t */
#define	EFX_DWORD_FMT "0x%08x"

/* Format string for printing an efx_qword_t */
#define	EFX_QWORD_FMT "0x%08x:%08x"

/* Format string for printing an efx_oword_t */
#define	EFX_OWORD_FMT "0x%08x:%08x:%08x:%08x"

/* Parameters for printing an efx_byte_t */
#define	EFX_BYTE_VAL(_byte)					\
	((unsigned int)__NATIVE_8((_byte).eb_u8[0]))

/* Parameters for printing an efx_word_t */
#define	EFX_WORD_VAL(_word)					\
	((unsigned int)__LE_TO_CPU_16((_word).ew_u16[0]))

/* Parameters for printing an efx_dword_t */
#define	EFX_DWORD_VAL(_dword)					\
	((unsigned int)__LE_TO_CPU_32((_dword).ed_u32[0]))

/* Parameters for printing an efx_qword_t */
#define	EFX_QWORD_VAL(_qword)					\
	((unsigned int)__LE_TO_CPU_32((_qword).eq_u32[1])),	\
	((unsigned int)__LE_TO_CPU_32((_qword).eq_u32[0]))

/* Parameters for printing an efx_oword_t */
#define	EFX_OWORD_VAL(_oword)					\
	((unsigned int)__LE_TO_CPU_32((_oword).eo_u32[3])),	\
	((unsigned int)__LE_TO_CPU_32((_oword).eo_u32[2])),	\
	((unsigned int)__LE_TO_CPU_32((_oword).eo_u32[1])),	\
	((unsigned int)__LE_TO_CPU_32((_oword).eo_u32[0]))

/*
 * Stop lint complaining about some shifts.
 */
#ifdef	__lint
extern int fix_lint;
#define	FIX_LINT(_x)	(_x + fix_lint)
#else
#define	FIX_LINT(_x)	(_x)
#endif

/*
 * Saturation arithmetic subtract with minimum equal to zero.
 *
 * Use saturating arithmetic to ensure a non-negative result. This
 * avoids undefined behaviour (and compiler warnings) when used as a
 * shift count.
 */
#define	EFX_SSUB(_val, _sub) \
	((_val) > (_sub) ? ((_val) - (_sub)) : 0)

/*
 * Extract bit field portion [low,high) from the native-endian element
 * which contains bits [min,max).
 *
 * For example, suppose "element" represents the high 32 bits of a
 * 64-bit value, and we wish to extract the bits belonging to the bit
 * field occupying bits 28-45 of this 64-bit value.
 *
 * Then EFX_EXTRACT(_element, 32, 63, 28, 45) would give
 *
 *   (_element) << 4
 *
 * The result will contain the relevant bits filled in in the range
 * [0,high-low), with garbage in bits [high-low+1,...).
 */
#define	EFX_EXTRACT_NATIVE(_element, _min, _max, _low, _high)		\
	((FIX_LINT(_low > _max) || FIX_LINT(_high < _min)) ?		\
		0U :							\
		((_low > _min) ?					\
			((_element) >> EFX_SSUB(_low, _min)) :		\
			((_element) << EFX_SSUB(_min, _low))))

/*
 * Extract bit field portion [low,high) from the 64-bit little-endian
 * element which contains bits [min,max)
 */
#define	EFX_EXTRACT64(_element, _min, _max, _low, _high)		\
	EFX_EXTRACT_NATIVE(__LE_TO_CPU_64(_element), _min, _max, _low, _high)

/*
 * Extract bit field portion [low,high) from the 32-bit little-endian
 * element which contains bits [min,max)
 */
#define	EFX_EXTRACT32(_element, _min, _max, _low, _high)		\
	EFX_EXTRACT_NATIVE(__LE_TO_CPU_32(_element), _min, _max, _low, _high)

/*
 * Extract bit field portion [low,high) from the 16-bit little-endian
 * element which contains bits [min,max)
 */
#define	EFX_EXTRACT16(_element, _min, _max, _low, _high)		\
	EFX_EXTRACT_NATIVE(__LE_TO_CPU_16(_element), _min, _max, _low, _high)

/*
 * Extract bit field portion [low,high) from the 8-bit
 * element which contains bits [min,max)
 */
#define	EFX_EXTRACT8(_element, _min, _max, _low, _high)			\
	EFX_EXTRACT_NATIVE(__NATIVE_8(_element), _min, _max, _low, _high)

#define	EFX_EXTRACT_OWORD64(_oword, _low, _high)			\
	(EFX_EXTRACT64((_oword).eo_u64[0], FIX_LINT(0), FIX_LINT(63),	\
	    _low, _high) |						\
	EFX_EXTRACT64((_oword).eo_u64[1], FIX_LINT(64), FIX_LINT(127),	\
	    _low, _high))

#define	EFX_EXTRACT_OWORD32(_oword, _low, _high)			\
	(EFX_EXTRACT32((_oword).eo_u32[0], FIX_LINT(0), FIX_LINT(31),	\
	    _low, _high) |						\
	EFX_EXTRACT32((_oword).eo_u32[1], FIX_LINT(32), FIX_LINT(63),	\
	    _low, _high) |						\
	EFX_EXTRACT32((_oword).eo_u32[2], FIX_LINT(64), FIX_LINT(95),	\
	    _low, _high) |						\
	EFX_EXTRACT32((_oword).eo_u32[3], FIX_LINT(96), FIX_LINT(127),	\
	    _low, _high))

#define	EFX_EXTRACT_QWORD64(_qword, _low, _high)			\
	(EFX_EXTRACT64((_qword).eq_u64[0], FIX_LINT(0), FIX_LINT(63),	\
	    _low, _high))

#define	EFX_EXTRACT_QWORD32(_qword, _low, _high)			\
	(EFX_EXTRACT32((_qword).eq_u32[0], FIX_LINT(0), FIX_LINT(31),	\
	    _low, _high) |						\
	EFX_EXTRACT32((_qword).eq_u32[1], FIX_LINT(32), FIX_LINT(63),	\
	    _low, _high))

#define	EFX_EXTRACT_DWORD(_dword, _low, _high)				\
	(EFX_EXTRACT32((_dword).ed_u32[0], FIX_LINT(0), FIX_LINT(31),	\
	    _low, _high))

#define	EFX_EXTRACT_WORD(_word, _low, _high)				\
	(EFX_EXTRACT16((_word).ew_u16[0], FIX_LINT(0), FIX_LINT(15),	\
	    _low, _high))

#define	EFX_EXTRACT_BYTE(_byte, _low, _high)				\
	(EFX_EXTRACT8((_byte).eb_u8[0], FIX_LINT(0), FIX_LINT(7),	\
	    _low, _high))


#define	EFX_OWORD_FIELD64(_oword, _field)				\
	((uint32_t)EFX_EXTRACT_OWORD64(_oword, EFX_LOW_BIT(_field),	\
	    EFX_HIGH_BIT(_field)) & EFX_MASK32(_field))

#define	EFX_OWORD_FIELD32(_oword, _field)				\
	(EFX_EXTRACT_OWORD32(_oword, EFX_LOW_BIT(_field),		\
	    EFX_HIGH_BIT(_field)) & EFX_MASK32(_field))

#define	EFX_QWORD_FIELD64(_qword, _field)				\
	((uint32_t)EFX_EXTRACT_QWORD64(_qword, EFX_LOW_BIT(_field),	\
	    EFX_HIGH_BIT(_field)) & EFX_MASK32(_field))

#define	EFX_QWORD_FIELD32(_qword, _field)				\
	(EFX_EXTRACT_QWORD32(_qword, EFX_LOW_BIT(_field),		\
	    EFX_HIGH_BIT(_field)) & EFX_MASK32(_field))

#define	EFX_DWORD_FIELD(_dword, _field)					\
	(EFX_EXTRACT_DWORD(_dword, EFX_LOW_BIT(_field),			\
	    EFX_HIGH_BIT(_field)) & EFX_MASK32(_field))

#define	EFX_WORD_FIELD(_word, _field)					\
	(EFX_EXTRACT_WORD(_word, EFX_LOW_BIT(_field),			\
	    EFX_HIGH_BIT(_field)) & EFX_MASK16(_field))

#define	EFX_BYTE_FIELD(_byte, _field)					\
	(EFX_EXTRACT_BYTE(_byte, EFX_LOW_BIT(_field),			\
	    EFX_HIGH_BIT(_field)) & EFX_MASK8(_field))


#define	EFX_OWORD_IS_EQUAL64(_oword_a, _oword_b)			\
	((_oword_a).eo_u64[0] == (_oword_b).eo_u64[0] &&		\
	    (_oword_a).eo_u64[1] == (_oword_b).eo_u64[1])

#define	EFX_OWORD_IS_EQUAL32(_oword_a, _oword_b)			\
	((_oword_a).eo_u32[0] == (_oword_b).eo_u32[0] &&		\
	    (_oword_a).eo_u32[1] == (_oword_b).eo_u32[1] &&		\
	    (_oword_a).eo_u32[2] == (_oword_b).eo_u32[2] &&		\
	    (_oword_a).eo_u32[3] == (_oword_b).eo_u32[3])

#define	EFX_QWORD_IS_EQUAL64(_qword_a, _qword_b)			\
	((_qword_a).eq_u64[0] == (_qword_b).eq_u64[0])

#define	EFX_QWORD_IS_EQUAL32(_qword_a, _qword_b)			\
	((_qword_a).eq_u32[0] == (_qword_b).eq_u32[0] &&		\
	    (_qword_a).eq_u32[1] == (_qword_b).eq_u32[1])

#define	EFX_DWORD_IS_EQUAL(_dword_a, _dword_b)				\
	((_dword_a).ed_u32[0] == (_dword_b).ed_u32[0])

#define	EFX_WORD_IS_EQUAL(_word_a, _word_b)				\
	((_word_a).ew_u16[0] == (_word_b).ew_u16[0])

#define	EFX_BYTE_IS_EQUAL(_byte_a, _byte_b)				\
	((_byte_a).eb_u8[0] == (_byte_b).eb_u8[0])


#define	EFX_OWORD_IS_ZERO64(_oword)					\
	(((_oword).eo_u64[0] |						\
	    (_oword).eo_u64[1]) == 0)

#define	EFX_OWORD_IS_ZERO32(_oword)					\
	(((_oword).eo_u32[0] |						\
	    (_oword).eo_u32[1] |					\
	    (_oword).eo_u32[2] |					\
	    (_oword).eo_u32[3]) == 0)

#define	EFX_QWORD_IS_ZERO64(_qword)					\
	(((_qword).eq_u64[0]) == 0)

#define	EFX_QWORD_IS_ZERO32(_qword)					\
	(((_qword).eq_u32[0] |						\
	    (_qword).eq_u32[1]) == 0)

#define	EFX_DWORD_IS_ZERO(_dword)					\
	(((_dword).ed_u32[0]) == 0)

#define	EFX_WORD_IS_ZERO(_word)						\
	(((_word).ew_u16[0]) == 0)

#define	EFX_BYTE_IS_ZERO(_byte)						\
	(((_byte).eb_u8[0]) == 0)


#define	EFX_OWORD_IS_SET64(_oword)					\
	(((_oword).eo_u64[0] &						\
	    (_oword).eo_u64[1]) == ~((uint64_t)0))

#define	EFX_OWORD_IS_SET32(_oword)					\
	(((_oword).eo_u32[0] &						\
	    (_oword).eo_u32[1] &					\
	    (_oword).eo_u32[2] &					\
	    (_oword).eo_u32[3]) == ~((uint32_t)0))

#define	EFX_QWORD_IS_SET64(_qword)					\
	(((_qword).eq_u64[0]) == ~((uint64_t)0))

#define	EFX_QWORD_IS_SET32(_qword)					\
	(((_qword).eq_u32[0] &						\
	    (_qword).eq_u32[1]) == ~((uint32_t)0))

#define	EFX_DWORD_IS_SET(_dword)					\
	((_dword).ed_u32[0] == ~((uint32_t)0))

#define	EFX_WORD_IS_SET(_word)						\
	((_word).ew_u16[0] == ~((uint16_t)0))

#define	EFX_BYTE_IS_SET(_byte)						\
	((_byte).eb_u8[0] == ~((uint8_t)0))

/*
 * Construct bit field portion
 *
 * Creates the portion of the bit field [low,high) that lies within
 * the range [min,max).
 */

#define	EFX_INSERT_NATIVE64(_min, _max, _low, _high, _value)		\
	(((_low > _max) || (_high < _min)) ?				\
		0U :							\
		((_low > _min) ?					\
			(((uint64_t)(_value)) << EFX_SSUB(_low, _min)) :\
			(((uint64_t)(_value)) >> EFX_SSUB(_min, _low))))

#define	EFX_INSERT_NATIVE32(_min, _max, _low, _high, _value)		\
	(((_low > _max) || (_high < _min)) ?				\
		0U :							\
		((_low > _min) ?					\
			(((uint32_t)(_value)) << EFX_SSUB(_low, _min)) :\
			(((uint32_t)(_value)) >> EFX_SSUB(_min, _low))))

#define	EFX_INSERT_NATIVE16(_min, _max, _low, _high, _value)		\
	(((_low > _max) || (_high < _min)) ?				\
		0U :							\
		(uint16_t)((_low > _min) ?				\
				((_value) << EFX_SSUB(_low, _min)) :	\
				((_value) >> EFX_SSUB(_min, _low))))

#define	EFX_INSERT_NATIVE8(_min, _max, _low, _high, _value)		\
	(((_low > _max) || (_high < _min)) ?				\
		0U :							\
		(uint8_t)((_low > _min) ?				\
				((_value) << EFX_SSUB(_low, _min)) :	\
				((_value) >> EFX_SSUB(_min, _low))))

/*
 * Construct bit field portion
 *
 * Creates the portion of the named bit field that lies within the
 * range [min,max).
 */
#define	EFX_INSERT_FIELD_NATIVE64(_min, _max, _field, _value)		\
	EFX_INSERT_NATIVE64(_min, _max, EFX_LOW_BIT(_field),		\
	    EFX_HIGH_BIT(_field), _value)

#define	EFX_INSERT_FIELD_NATIVE32(_min, _max, _field, _value)		\
	EFX_INSERT_NATIVE32(_min, _max, EFX_LOW_BIT(_field),		\
	    EFX_HIGH_BIT(_field), _value)

#define	EFX_INSERT_FIELD_NATIVE16(_min, _max, _field, _value)		\
	EFX_INSERT_NATIVE16(_min, _max, EFX_LOW_BIT(_field),		\
	    EFX_HIGH_BIT(_field), _value)

#define	EFX_INSERT_FIELD_NATIVE8(_min, _max, _field, _value)		\
	EFX_INSERT_NATIVE8(_min, _max, EFX_LOW_BIT(_field),		\
	    EFX_HIGH_BIT(_field), _value)

/*
 * Construct bit field
 *
 * Creates the portion of the named bit fields that lie within the
 * range [min,max).
 */
#define	EFX_INSERT_FIELDS64(_min, _max,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9,	\
	    _field10, _value10)						\
	__CPU_TO_LE_64(							\
	    EFX_INSERT_FIELD_NATIVE64(_min, _max, _field1, _value1) |	\
	    EFX_INSERT_FIELD_NATIVE64(_min, _max, _field2, _value2) |	\
	    EFX_INSERT_FIELD_NATIVE64(_min, _max, _field3, _value3) |	\
	    EFX_INSERT_FIELD_NATIVE64(_min, _max, _field4, _value4) |	\
	    EFX_INSERT_FIELD_NATIVE64(_min, _max, _field5, _value5) |	\
	    EFX_INSERT_FIELD_NATIVE64(_min, _max, _field6, _value6) |	\
	    EFX_INSERT_FIELD_NATIVE64(_min, _max, _field7, _value7) |	\
	    EFX_INSERT_FIELD_NATIVE64(_min, _max, _field8, _value8) |	\
	    EFX_INSERT_FIELD_NATIVE64(_min, _max, _field9, _value9) |	\
	    EFX_INSERT_FIELD_NATIVE64(_min, _max, _field10, _value10))

#define	EFX_INSERT_FIELDS32(_min, _max,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9,	\
	    _field10, _value10)						\
	__CPU_TO_LE_32(							\
	    EFX_INSERT_FIELD_NATIVE32(_min, _max, _field1, _value1) |	\
	    EFX_INSERT_FIELD_NATIVE32(_min, _max, _field2, _value2) |	\
	    EFX_INSERT_FIELD_NATIVE32(_min, _max, _field3, _value3) |	\
	    EFX_INSERT_FIELD_NATIVE32(_min, _max, _field4, _value4) |	\
	    EFX_INSERT_FIELD_NATIVE32(_min, _max, _field5, _value5) |	\
	    EFX_INSERT_FIELD_NATIVE32(_min, _max, _field6, _value6) |	\
	    EFX_INSERT_FIELD_NATIVE32(_min, _max, _field7, _value7) |	\
	    EFX_INSERT_FIELD_NATIVE32(_min, _max, _field8, _value8) |	\
	    EFX_INSERT_FIELD_NATIVE32(_min, _max, _field9, _value9) |	\
	    EFX_INSERT_FIELD_NATIVE32(_min, _max, _field10, _value10))

#define	EFX_INSERT_FIELDS16(_min, _max,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9,	\
	    _field10, _value10)						\
	__CPU_TO_LE_16(							\
	    EFX_INSERT_FIELD_NATIVE16(_min, _max, _field1, _value1) |	\
	    EFX_INSERT_FIELD_NATIVE16(_min, _max, _field2, _value2) |	\
	    EFX_INSERT_FIELD_NATIVE16(_min, _max, _field3, _value3) |	\
	    EFX_INSERT_FIELD_NATIVE16(_min, _max, _field4, _value4) |	\
	    EFX_INSERT_FIELD_NATIVE16(_min, _max, _field5, _value5) |	\
	    EFX_INSERT_FIELD_NATIVE16(_min, _max, _field6, _value6) |	\
	    EFX_INSERT_FIELD_NATIVE16(_min, _max, _field7, _value7) |	\
	    EFX_INSERT_FIELD_NATIVE16(_min, _max, _field8, _value8) |	\
	    EFX_INSERT_FIELD_NATIVE16(_min, _max, _field9, _value9) |	\
	    EFX_INSERT_FIELD_NATIVE16(_min, _max, _field10, _value10))

#define	EFX_INSERT_FIELDS8(_min, _max,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9,	\
	    _field10, _value10)						\
	__NATIVE_8(							\
	    EFX_INSERT_FIELD_NATIVE8(_min, _max, _field1, _value1) |	\
	    EFX_INSERT_FIELD_NATIVE8(_min, _max, _field2, _value2) |	\
	    EFX_INSERT_FIELD_NATIVE8(_min, _max, _field3, _value3) |	\
	    EFX_INSERT_FIELD_NATIVE8(_min, _max, _field4, _value4) |	\
	    EFX_INSERT_FIELD_NATIVE8(_min, _max, _field5, _value5) |	\
	    EFX_INSERT_FIELD_NATIVE8(_min, _max, _field6, _value6) |	\
	    EFX_INSERT_FIELD_NATIVE8(_min, _max, _field7, _value7) |	\
	    EFX_INSERT_FIELD_NATIVE8(_min, _max, _field8, _value8) |	\
	    EFX_INSERT_FIELD_NATIVE8(_min, _max, _field9, _value9) |	\
	    EFX_INSERT_FIELD_NATIVE8(_min, _max, _field10, _value10))

#define	EFX_POPULATE_OWORD64(_oword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9,	\
	    _field10, _value10)						\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u64[0] = EFX_INSERT_FIELDS64(0, 63,		\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u64[1] = EFX_INSERT_FIELDS64(64, 127,	\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_POPULATE_OWORD32(_oword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9,	\
	    _field10, _value10)						\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u32[0] = EFX_INSERT_FIELDS32(0, 31,		\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u32[1] = EFX_INSERT_FIELDS32(32, 63,	\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u32[2] = EFX_INSERT_FIELDS32(64, 95,	\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u32[3] = EFX_INSERT_FIELDS32(96, 127,	\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_POPULATE_QWORD64(_qword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9,	\
	    _field10, _value10)						\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_qword).eq_u64[0] = EFX_INSERT_FIELDS64(0, 63,		\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_POPULATE_QWORD32(_qword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9,	\
	    _field10, _value10)						\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_qword).eq_u32[0] = EFX_INSERT_FIELDS32(0, 31,		\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
		_NOTE(CONSTANTCONDITION)				\
		(_qword).eq_u32[1] = EFX_INSERT_FIELDS32(32, 63,	\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_POPULATE_DWORD(_dword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9,	\
	    _field10, _value10)						\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_dword).ed_u32[0] = EFX_INSERT_FIELDS32(0, 31,		\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_POPULATE_WORD(_word,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9,	\
	    _field10, _value10)						\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_word).ew_u16[0] = EFX_INSERT_FIELDS16(0, 15,		\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_POPULATE_BYTE(_byte,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9,	\
	    _field10, _value10)						\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_byte).eb_u8[0] = EFX_INSERT_FIELDS8(0, 7,		\
		    _field1, _value1, _field2, _value2,			\
		    _field3, _value3, _field4, _value4,			\
		    _field5, _value5, _field6, _value6,			\
		    _field7, _value7, _field8, _value8,			\
		    _field9, _value9, _field10, _value10);		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/* Populate an octword field with various numbers of arguments */
#define	EFX_POPULATE_OWORD_10 EFX_POPULATE_OWORD

#define	EFX_POPULATE_OWORD_9(_oword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9)	\
	EFX_POPULATE_OWORD_10(_oword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9)

#define	EFX_POPULATE_OWORD_8(_oword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8)				\
	EFX_POPULATE_OWORD_9(_oword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8)

#define	EFX_POPULATE_OWORD_7(_oword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7)						\
	EFX_POPULATE_OWORD_8(_oword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7)

#define	EFX_POPULATE_OWORD_6(_oword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6)	\
	EFX_POPULATE_OWORD_7(_oword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6)

#define	EFX_POPULATE_OWORD_5(_oword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5)				\
	EFX_POPULATE_OWORD_6(_oword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5)

#define	EFX_POPULATE_OWORD_4(_oword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4)						\
	EFX_POPULATE_OWORD_5(_oword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4)

#define	EFX_POPULATE_OWORD_3(_oword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3)	\
	EFX_POPULATE_OWORD_4(_oword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3)

#define	EFX_POPULATE_OWORD_2(_oword,					\
	    _field1, _value1, _field2, _value2)				\
	EFX_POPULATE_OWORD_3(_oword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2)

#define	EFX_POPULATE_OWORD_1(_oword,					\
	    _field1, _value1)						\
	EFX_POPULATE_OWORD_2(_oword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1)

#define	EFX_ZERO_OWORD(_oword)						\
	EFX_POPULATE_OWORD_1(_oword, EFX_DUMMY_FIELD, 0)

#define	EFX_SET_OWORD(_oword)						\
	EFX_POPULATE_OWORD_4(_oword,					\
	    EFX_DWORD_0, 0xffffffff, EFX_DWORD_1, 0xffffffff,		\
	    EFX_DWORD_2, 0xffffffff, EFX_DWORD_3, 0xffffffff)

/* Populate a quadword field with various numbers of arguments */
#define	EFX_POPULATE_QWORD_10 EFX_POPULATE_QWORD

#define	EFX_POPULATE_QWORD_9(_qword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9)	\
	EFX_POPULATE_QWORD_10(_qword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9)

#define	EFX_POPULATE_QWORD_8(_qword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8)				\
	EFX_POPULATE_QWORD_9(_qword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8)

#define	EFX_POPULATE_QWORD_7(_qword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7)						\
	EFX_POPULATE_QWORD_8(_qword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7)

#define	EFX_POPULATE_QWORD_6(_qword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6)	\
	EFX_POPULATE_QWORD_7(_qword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6)

#define	EFX_POPULATE_QWORD_5(_qword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5)				\
	EFX_POPULATE_QWORD_6(_qword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5)

#define	EFX_POPULATE_QWORD_4(_qword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4)						\
	EFX_POPULATE_QWORD_5(_qword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4)

#define	EFX_POPULATE_QWORD_3(_qword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3)	\
	EFX_POPULATE_QWORD_4(_qword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3)

#define	EFX_POPULATE_QWORD_2(_qword,					\
	    _field1, _value1, _field2, _value2)				\
	EFX_POPULATE_QWORD_3(_qword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2)

#define	EFX_POPULATE_QWORD_1(_qword,					\
	    _field1, _value1)						\
	EFX_POPULATE_QWORD_2(_qword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1)

#define	EFX_ZERO_QWORD(_qword)						\
	EFX_POPULATE_QWORD_1(_qword, EFX_DUMMY_FIELD, 0)

#define	EFX_SET_QWORD(_qword)						\
	EFX_POPULATE_QWORD_2(_qword,					\
	    EFX_DWORD_0, 0xffffffff, EFX_DWORD_1, 0xffffffff)

/* Populate a dword field with various numbers of arguments */
#define	EFX_POPULATE_DWORD_10 EFX_POPULATE_DWORD

#define	EFX_POPULATE_DWORD_9(_dword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9)	\
	EFX_POPULATE_DWORD_10(_dword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9)

#define	EFX_POPULATE_DWORD_8(_dword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8)				\
	EFX_POPULATE_DWORD_9(_dword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8)

#define	EFX_POPULATE_DWORD_7(_dword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7)						\
	EFX_POPULATE_DWORD_8(_dword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7)

#define	EFX_POPULATE_DWORD_6(_dword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6)	\
	EFX_POPULATE_DWORD_7(_dword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6)

#define	EFX_POPULATE_DWORD_5(_dword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5)				\
	EFX_POPULATE_DWORD_6(_dword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5)

#define	EFX_POPULATE_DWORD_4(_dword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4)						\
	EFX_POPULATE_DWORD_5(_dword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4)

#define	EFX_POPULATE_DWORD_3(_dword,					\
	    _field1, _value1, _field2, _value2, _field3, _value3)	\
	EFX_POPULATE_DWORD_4(_dword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2, _field3, _value3)

#define	EFX_POPULATE_DWORD_2(_dword,					\
	    _field1, _value1, _field2, _value2)				\
	EFX_POPULATE_DWORD_3(_dword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1, _field2, _value2)

#define	EFX_POPULATE_DWORD_1(_dword,					\
	    _field1, _value1)						\
	EFX_POPULATE_DWORD_2(_dword, EFX_DUMMY_FIELD, 0,		\
	    _field1, _value1)

#define	EFX_ZERO_DWORD(_dword)						\
	EFX_POPULATE_DWORD_1(_dword, EFX_DUMMY_FIELD, 0)

#define	EFX_SET_DWORD(_dword)						\
	EFX_POPULATE_DWORD_1(_dword,					\
	    EFX_DWORD_0, 0xffffffff)

/* Populate a word field with various numbers of arguments */
#define	EFX_POPULATE_WORD_10 EFX_POPULATE_WORD

#define	EFX_POPULATE_WORD_9(_word,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9)	\
	EFX_POPULATE_WORD_10(_word, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9)

#define	EFX_POPULATE_WORD_8(_word,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8)				\
	EFX_POPULATE_WORD_9(_word, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8)

#define	EFX_POPULATE_WORD_7(_word,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7)						\
	EFX_POPULATE_WORD_8(_word, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7)

#define	EFX_POPULATE_WORD_6(_word,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6)	\
	EFX_POPULATE_WORD_7(_word, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6)

#define	EFX_POPULATE_WORD_5(_word,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5)				\
	EFX_POPULATE_WORD_6(_word, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5)

#define	EFX_POPULATE_WORD_4(_word,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4)						\
	EFX_POPULATE_WORD_5(_word, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4)

#define	EFX_POPULATE_WORD_3(_word,					\
	    _field1, _value1, _field2, _value2, _field3, _value3)	\
	EFX_POPULATE_WORD_4(_word, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3)

#define	EFX_POPULATE_WORD_2(_word,					\
	    _field1, _value1, _field2, _value2)				\
	EFX_POPULATE_WORD_3(_word, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2)

#define	EFX_POPULATE_WORD_1(_word,					\
	    _field1, _value1)						\
	EFX_POPULATE_WORD_2(_word, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1)

#define	EFX_ZERO_WORD(_word)						\
	EFX_POPULATE_WORD_1(_word, EFX_DUMMY_FIELD, 0)

#define	EFX_SET_WORD(_word)						\
	EFX_POPULATE_WORD_1(_word,					\
	    EFX_WORD_0, 0xffff)

/* Populate a byte field with various numbers of arguments */
#define	EFX_POPULATE_BYTE_10 EFX_POPULATE_BYTE

#define	EFX_POPULATE_BYTE_9(_byte,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9)	\
	EFX_POPULATE_BYTE_10(_byte, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8,	_field9, _value9)

#define	EFX_POPULATE_BYTE_8(_byte,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8)				\
	EFX_POPULATE_BYTE_9(_byte, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7, _field8, _value8)

#define	EFX_POPULATE_BYTE_7(_byte,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7)						\
	EFX_POPULATE_BYTE_8(_byte, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6,	\
	    _field7, _value7)

#define	EFX_POPULATE_BYTE_6(_byte,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6)	\
	EFX_POPULATE_BYTE_7(_byte, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5,	_field6, _value6)

#define	EFX_POPULATE_BYTE_5(_byte,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5)				\
	EFX_POPULATE_BYTE_6(_byte, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4, _field5, _value5)

#define	EFX_POPULATE_BYTE_4(_byte,					\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4)						\
	EFX_POPULATE_BYTE_5(_byte, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3,	\
	    _field4, _value4)

#define	EFX_POPULATE_BYTE_3(_byte,					\
	    _field1, _value1, _field2, _value2, _field3, _value3)	\
	EFX_POPULATE_BYTE_4(_byte, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2, _field3, _value3)

#define	EFX_POPULATE_BYTE_2(_byte,					\
	    _field1, _value1, _field2, _value2)				\
	EFX_POPULATE_BYTE_3(_byte, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1, _field2, _value2)

#define	EFX_POPULATE_BYTE_1(_byte,					\
	    _field1, _value1)						\
	EFX_POPULATE_BYTE_2(_byte, EFX_DUMMY_FIELD, 0,			\
	    _field1, _value1)

#define	EFX_ZERO_BYTE(_byte)						\
	EFX_POPULATE_BYTE_1(_byte, EFX_DUMMY_FIELD, 0)

#define	EFX_SET_BYTE(_byte)						\
	EFX_POPULATE_BYTE_1(_byte,					\
	    EFX_BYTE_0, 0xff)

/*
 * Modify a named field within an already-populated structure.  Used
 * for read-modify-write operations.
 */

#define	EFX_INSERT_FIELD64(_min, _max, _field, _value)			\
	__CPU_TO_LE_64(EFX_INSERT_FIELD_NATIVE64(_min, _max, _field, _value))

#define	EFX_INSERT_FIELD32(_min, _max, _field, _value)			\
	__CPU_TO_LE_32(EFX_INSERT_FIELD_NATIVE32(_min, _max, _field, _value))

#define	EFX_INSERT_FIELD16(_min, _max, _field, _value)			\
	__CPU_TO_LE_16(EFX_INSERT_FIELD_NATIVE16(_min, _max, _field, _value))

#define	EFX_INSERT_FIELD8(_min, _max, _field, _value)			\
	__NATIVE_8(EFX_INSERT_FIELD_NATIVE8(_min, _max, _field, _value))

#define	EFX_INPLACE_MASK64(_min, _max, _field)				\
	EFX_INSERT_FIELD64(_min, _max, _field, EFX_MASK64(_field))

#define	EFX_INPLACE_MASK32(_min, _max, _field)				\
	EFX_INSERT_FIELD32(_min, _max, _field, EFX_MASK32(_field))

#define	EFX_INPLACE_MASK16(_min, _max, _field)				\
	EFX_INSERT_FIELD16(_min, _max, _field, EFX_MASK16(_field))

#define	EFX_INPLACE_MASK8(_min, _max, _field)				\
	EFX_INSERT_FIELD8(_min, _max, _field, EFX_MASK8(_field))

#define	EFX_SET_OWORD_FIELD64(_oword, _field, _value)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u64[0] = (((_oword).eo_u64[0] &		\
		    ~EFX_INPLACE_MASK64(0, 63, _field)) |		\
		    EFX_INSERT_FIELD64(0, 63, _field, _value));		\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u64[1] = (((_oword).eo_u64[1] &		\
		    ~EFX_INPLACE_MASK64(64, 127, _field)) |		\
		    EFX_INSERT_FIELD64(64, 127, _field, _value));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_SET_OWORD_FIELD32(_oword, _field, _value)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u32[0] = (((_oword).eo_u32[0] &		\
		    ~EFX_INPLACE_MASK32(0, 31, _field)) |		\
		    EFX_INSERT_FIELD32(0, 31, _field, _value));		\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u32[1] = (((_oword).eo_u32[1] &		\
		    ~EFX_INPLACE_MASK32(32, 63, _field)) |		\
		    EFX_INSERT_FIELD32(32, 63, _field, _value));	\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u32[2] = (((_oword).eo_u32[2] &		\
		    ~EFX_INPLACE_MASK32(64, 95, _field)) |		\
		    EFX_INSERT_FIELD32(64, 95, _field, _value));	\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u32[3] = (((_oword).eo_u32[3] &		\
		    ~EFX_INPLACE_MASK32(96, 127, _field)) |		\
		    EFX_INSERT_FIELD32(96, 127, _field, _value));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_SET_QWORD_FIELD64(_qword, _field, _value)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_qword).eq_u64[0] = (((_qword).eq_u64[0] &		\
		    ~EFX_INPLACE_MASK64(0, 63, _field)) |		\
		    EFX_INSERT_FIELD64(0, 63, _field, _value));		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_SET_QWORD_FIELD32(_qword, _field, _value)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_qword).eq_u32[0] = (((_qword).eq_u32[0] &		\
		    ~EFX_INPLACE_MASK32(0, 31, _field)) |		\
		    EFX_INSERT_FIELD32(0, 31, _field, _value));		\
		_NOTE(CONSTANTCONDITION)				\
		(_qword).eq_u32[1] = (((_qword).eq_u32[1] &		\
		    ~EFX_INPLACE_MASK32(32, 63, _field)) |		\
		    EFX_INSERT_FIELD32(32, 63, _field, _value));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_SET_DWORD_FIELD(_dword, _field, _value)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_dword).ed_u32[0] = (((_dword).ed_u32[0] &		\
		    ~EFX_INPLACE_MASK32(0, 31, _field)) |		\
		    EFX_INSERT_FIELD32(0, 31, _field, _value));		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_SET_WORD_FIELD(_word, _field, _value)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_word).ew_u16[0] = (((_word).ew_u16[0] &		\
		    ~EFX_INPLACE_MASK16(0, 15, _field)) |		\
		    EFX_INSERT_FIELD16(0, 15, _field, _value));		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_SET_BYTE_FIELD(_byte, _field, _value)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_byte).eb_u8[0] = (((_byte).eb_u8[0] &			\
		    ~EFX_INPLACE_MASK8(0, 7, _field)) |			\
		    EFX_INSERT_FIELD8(0, 7, _field, _value));		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/*
 * Set or clear a numbered bit within an octword.
 */

#define	EFX_SHIFT64(_bit, _base)					\
	(((_bit) >= (_base) && (_bit) < (_base) + 64) ?			\
		((uint64_t)1 << EFX_SSUB((_bit), (_base))) :		\
		0U)

#define	EFX_SHIFT32(_bit, _base)					\
	(((_bit) >= (_base) && (_bit) < (_base) + 32) ?			\
		((uint32_t)1 << EFX_SSUB((_bit),(_base))) :		\
		0U)

#define	EFX_SHIFT16(_bit, _base)					\
	(((_bit) >= (_base) && (_bit) < (_base) + 16) ?			\
		(uint16_t)(1 << EFX_SSUB((_bit), (_base))) :		\
		0U)

#define	EFX_SHIFT8(_bit, _base)						\
	(((_bit) >= (_base) && (_bit) < (_base) + 8) ?			\
		(uint8_t)(1 << EFX_SSUB((_bit), (_base))) :		\
		0U)

#define	EFX_SET_OWORD_BIT64(_oword, _bit)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u64[0] |=					\
		    __CPU_TO_LE_64(EFX_SHIFT64(_bit, FIX_LINT(0)));	\
		(_oword).eo_u64[1] |=					\
		    __CPU_TO_LE_64(EFX_SHIFT64(_bit, FIX_LINT(64)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_SET_OWORD_BIT32(_oword, _bit)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u32[0] |=					\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(0)));	\
		(_oword).eo_u32[1] |=					\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(32)));	\
		(_oword).eo_u32[2] |=					\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(64)));	\
		(_oword).eo_u32[3] |=					\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(96)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_CLEAR_OWORD_BIT64(_oword, _bit)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u64[0] &=					\
		    __CPU_TO_LE_64(~EFX_SHIFT64(_bit, FIX_LINT(0)));	\
		(_oword).eo_u64[1] &=					\
		    __CPU_TO_LE_64(~EFX_SHIFT64(_bit, FIX_LINT(64)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_CLEAR_OWORD_BIT32(_oword, _bit)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_oword).eo_u32[0] &=					\
		    __CPU_TO_LE_32(~EFX_SHIFT32(_bit, FIX_LINT(0)));	\
		(_oword).eo_u32[1] &=					\
		    __CPU_TO_LE_32(~EFX_SHIFT32(_bit, FIX_LINT(32)));	\
		(_oword).eo_u32[2] &=					\
		    __CPU_TO_LE_32(~EFX_SHIFT32(_bit, FIX_LINT(64)));	\
		(_oword).eo_u32[3] &=					\
		    __CPU_TO_LE_32(~EFX_SHIFT32(_bit, FIX_LINT(96)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_TEST_OWORD_BIT64(_oword, _bit)				\
	(((_oword).eo_u64[0] &						\
		    __CPU_TO_LE_64(EFX_SHIFT64(_bit, FIX_LINT(0)))) ||	\
	((_oword).eo_u64[1] &						\
		    __CPU_TO_LE_64(EFX_SHIFT64(_bit, FIX_LINT(64)))))

#define	EFX_TEST_OWORD_BIT32(_oword, _bit)				\
	(((_oword).eo_u32[0] &						\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(0)))) ||	\
	((_oword).eo_u32[1] &						\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(32)))) ||	\
	((_oword).eo_u32[2] &						\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(64)))) ||	\
	((_oword).eo_u32[3] &						\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(96)))))


#define	EFX_SET_QWORD_BIT64(_qword, _bit)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_qword).eq_u64[0] |=					\
		    __CPU_TO_LE_64(EFX_SHIFT64(_bit, FIX_LINT(0)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_SET_QWORD_BIT32(_qword, _bit)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_qword).eq_u32[0] |=					\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(0)));	\
		(_qword).eq_u32[1] |=					\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(32)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_CLEAR_QWORD_BIT64(_qword, _bit)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_qword).eq_u64[0] &=					\
		    __CPU_TO_LE_64(~EFX_SHIFT64(_bit, FIX_LINT(0)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_CLEAR_QWORD_BIT32(_qword, _bit)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		(_qword).eq_u32[0] &=					\
		    __CPU_TO_LE_32(~EFX_SHIFT32(_bit, FIX_LINT(0)));	\
		(_qword).eq_u32[1] &=					\
		    __CPU_TO_LE_32(~EFX_SHIFT32(_bit, FIX_LINT(32)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_TEST_QWORD_BIT64(_qword, _bit)				\
	(((_qword).eq_u64[0] &						\
		    __CPU_TO_LE_64(EFX_SHIFT64(_bit, FIX_LINT(0)))) != 0)

#define	EFX_TEST_QWORD_BIT32(_qword, _bit)				\
	(((_qword).eq_u32[0] &						\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(0)))) ||	\
	((_qword).eq_u32[1] &						\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(32)))))


#define	EFX_SET_DWORD_BIT(_dword, _bit)					\
	do {								\
		(_dword).ed_u32[0] |=					\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(0)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_CLEAR_DWORD_BIT(_dword, _bit)				\
	do {								\
		(_dword).ed_u32[0] &=					\
		    __CPU_TO_LE_32(~EFX_SHIFT32(_bit, FIX_LINT(0)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_TEST_DWORD_BIT(_dword, _bit)				\
	(((_dword).ed_u32[0] &						\
		    __CPU_TO_LE_32(EFX_SHIFT32(_bit, FIX_LINT(0)))) != 0)


#define	EFX_SET_WORD_BIT(_word, _bit)					\
	do {								\
		(_word).ew_u16[0] |=					\
		    __CPU_TO_LE_16(EFX_SHIFT16(_bit, FIX_LINT(0)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_CLEAR_WORD_BIT(_word, _bit)					\
	do {								\
		(_word).ew_u32[0] &=					\
		    __CPU_TO_LE_16(~EFX_SHIFT16(_bit, FIX_LINT(0)));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_TEST_WORD_BIT(_word, _bit)					\
	(((_word).ew_u16[0] &						\
		    __CPU_TO_LE_16(EFX_SHIFT16(_bit, FIX_LINT(0)))) != 0)


#define	EFX_SET_BYTE_BIT(_byte, _bit)					\
	do {								\
		(_byte).eb_u8[0] |=					\
		    __NATIVE_8(EFX_SHIFT8(_bit, FIX_LINT(0)));		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_CLEAR_BYTE_BIT(_byte, _bit)					\
	do {								\
		(_byte).eb_u8[0] &=					\
		    __NATIVE_8(~EFX_SHIFT8(_bit, FIX_LINT(0)));		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_TEST_BYTE_BIT(_byte, _bit)					\
	(((_byte).eb_u8[0] &						\
		    __NATIVE_8(EFX_SHIFT8(_bit, FIX_LINT(0)))) != 0)


#define	EFX_OR_OWORD64(_oword1, _oword2)				\
	do {								\
		(_oword1).eo_u64[0] |= (_oword2).eo_u64[0];		\
		(_oword1).eo_u64[1] |= (_oword2).eo_u64[1];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_OR_OWORD32(_oword1, _oword2)				\
	do {								\
		(_oword1).eo_u32[0] |= (_oword2).eo_u32[0];		\
		(_oword1).eo_u32[1] |= (_oword2).eo_u32[1];		\
		(_oword1).eo_u32[2] |= (_oword2).eo_u32[2];		\
		(_oword1).eo_u32[3] |= (_oword2).eo_u32[3];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_AND_OWORD64(_oword1, _oword2)				\
	do {								\
		(_oword1).eo_u64[0] &= (_oword2).eo_u64[0];		\
		(_oword1).eo_u64[1] &= (_oword2).eo_u64[1];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_AND_OWORD32(_oword1, _oword2)				\
	do {								\
		(_oword1).eo_u32[0] &= (_oword2).eo_u32[0];		\
		(_oword1).eo_u32[1] &= (_oword2).eo_u32[1];		\
		(_oword1).eo_u32[2] &= (_oword2).eo_u32[2];		\
		(_oword1).eo_u32[3] &= (_oword2).eo_u32[3];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_OR_QWORD64(_qword1, _qword2)				\
	do {								\
		(_qword1).eq_u64[0] |= (_qword2).eq_u64[0];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_OR_QWORD32(_qword1, _qword2)				\
	do {								\
		(_qword1).eq_u32[0] |= (_qword2).eq_u32[0];		\
		(_qword1).eq_u32[1] |= (_qword2).eq_u32[1];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_AND_QWORD64(_qword1, _qword2)				\
	do {								\
		(_qword1).eq_u64[0] &= (_qword2).eq_u64[0];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_AND_QWORD32(_qword1, _qword2)				\
	do {								\
		(_qword1).eq_u32[0] &= (_qword2).eq_u32[0];		\
		(_qword1).eq_u32[1] &= (_qword2).eq_u32[1];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_OR_DWORD(_dword1, _dword2)					\
	do {								\
		(_dword1).ed_u32[0] |= (_dword2).ed_u32[0];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_AND_DWORD(_dword1, _dword2)					\
	do {								\
		(_dword1).ed_u32[0] &= (_dword2).ed_u32[0];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_OR_WORD(_word1, _word2)					\
	do {								\
		(_word1).ew_u16[0] |= (_word2).ew_u16[0];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_AND_WORD(_word1, _word2)					\
	do {								\
		(_word1).ew_u16[0] &= (_word2).ew_u16[0];		\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_OR_BYTE(_byte1, _byte2)					\
	do {								\
		(_byte1).eb_u8[0] |= (_byte2).eb_u8[0];			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFX_AND_BYTE(_byte1, _byte2)					\
	do {								\
		(_byte1).eb_u8[0] &= (_byte2).eb_u8[0];			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#if EFSYS_USE_UINT64
#define	EFX_OWORD_FIELD		EFX_OWORD_FIELD64
#define	EFX_QWORD_FIELD		EFX_QWORD_FIELD64
#define	EFX_OWORD_IS_EQUAL	EFX_OWORD_IS_EQUAL64
#define	EFX_QWORD_IS_EQUAL	EFX_QWORD_IS_EQUAL64
#define	EFX_OWORD_IS_ZERO	EFX_OWORD_IS_ZERO64
#define	EFX_QWORD_IS_ZERO	EFX_QWORD_IS_ZERO64
#define	EFX_OWORD_IS_SET	EFX_OWORD_IS_SET64
#define	EFX_QWORD_IS_SET	EFX_QWORD_IS_SET64
#define	EFX_POPULATE_OWORD	EFX_POPULATE_OWORD64
#define	EFX_POPULATE_QWORD	EFX_POPULATE_QWORD64
#define	EFX_SET_OWORD_FIELD	EFX_SET_OWORD_FIELD64
#define	EFX_SET_QWORD_FIELD	EFX_SET_QWORD_FIELD64
#define	EFX_SET_OWORD_BIT	EFX_SET_OWORD_BIT64
#define	EFX_CLEAR_OWORD_BIT	EFX_CLEAR_OWORD_BIT64
#define	EFX_TEST_OWORD_BIT	EFX_TEST_OWORD_BIT64
#define	EFX_SET_QWORD_BIT	EFX_SET_QWORD_BIT64
#define	EFX_CLEAR_QWORD_BIT	EFX_CLEAR_QWORD_BIT64
#define	EFX_TEST_QWORD_BIT	EFX_TEST_QWORD_BIT64
#define	EFX_OR_OWORD		EFX_OR_OWORD64
#define	EFX_AND_OWORD		EFX_AND_OWORD64
#define	EFX_OR_QWORD		EFX_OR_QWORD64
#define	EFX_AND_QWORD		EFX_AND_QWORD64
#else
#define	EFX_OWORD_FIELD		EFX_OWORD_FIELD32
#define	EFX_QWORD_FIELD		EFX_QWORD_FIELD32
#define	EFX_OWORD_IS_EQUAL	EFX_OWORD_IS_EQUAL32
#define	EFX_QWORD_IS_EQUAL	EFX_QWORD_IS_EQUAL32
#define	EFX_OWORD_IS_ZERO	EFX_OWORD_IS_ZERO32
#define	EFX_QWORD_IS_ZERO	EFX_QWORD_IS_ZERO32
#define	EFX_OWORD_IS_SET	EFX_OWORD_IS_SET32
#define	EFX_QWORD_IS_SET	EFX_QWORD_IS_SET32
#define	EFX_POPULATE_OWORD	EFX_POPULATE_OWORD32
#define	EFX_POPULATE_QWORD	EFX_POPULATE_QWORD32
#define	EFX_SET_OWORD_FIELD	EFX_SET_OWORD_FIELD32
#define	EFX_SET_QWORD_FIELD	EFX_SET_QWORD_FIELD32
#define	EFX_SET_OWORD_BIT	EFX_SET_OWORD_BIT32
#define	EFX_CLEAR_OWORD_BIT	EFX_CLEAR_OWORD_BIT32
#define	EFX_TEST_OWORD_BIT	EFX_TEST_OWORD_BIT32
#define	EFX_SET_QWORD_BIT	EFX_SET_QWORD_BIT32
#define	EFX_CLEAR_QWORD_BIT	EFX_CLEAR_QWORD_BIT32
#define	EFX_TEST_QWORD_BIT	EFX_TEST_QWORD_BIT32
#define	EFX_OR_OWORD		EFX_OR_OWORD32
#define	EFX_AND_OWORD		EFX_AND_OWORD32
#define	EFX_OR_QWORD		EFX_OR_QWORD32
#define	EFX_AND_QWORD		EFX_AND_QWORD32
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_EFX_TYPES_H */
