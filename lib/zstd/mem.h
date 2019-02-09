/**
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of https://github.com/facebook/zstd.
 * An additional grant of patent rights can be found in the PATENTS file in the
 * same directory.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation. This program is dual-licensed; you may select
 * either version 2 of the GNU General Public License ("GPL") or BSD license
 * ("BSD").
 */

#ifndef MEM_H_MODULE
#define MEM_H_MODULE

/*-****************************************
*  Dependencies
******************************************/
#include <asm/unaligned.h>
#include <linux/string.h> /* memcpy */
#include <linux/types.h>  /* size_t, ptrdiff_t */

/*-****************************************
*  Compiler specifics
******************************************/
#define ZSTD_STATIC static __inline __attribute__((unused))

/*-**************************************************************
*  Basic Types
*****************************************************************/
typedef uint8_t BYTE;
typedef uint16_t U16;
typedef int16_t S16;
typedef uint32_t U32;
typedef int32_t S32;
typedef uint64_t U64;
typedef int64_t S64;
typedef ptrdiff_t iPtrDiff;
typedef uintptr_t uPtrDiff;

/*-**************************************************************
*  Memory I/O
*****************************************************************/
ZSTD_STATIC unsigned ZSTD_32bits(void) { return sizeof(size_t) == 4; }
ZSTD_STATIC unsigned ZSTD_64bits(void) { return sizeof(size_t) == 8; }

#if defined(__LITTLE_ENDIAN)
#define ZSTD_LITTLE_ENDIAN 1
#else
#define ZSTD_LITTLE_ENDIAN 0
#endif

ZSTD_STATIC unsigned ZSTD_isLittleEndian(void) { return ZSTD_LITTLE_ENDIAN; }

ZSTD_STATIC U16 ZSTD_read16(const void *memPtr) { return get_unaligned((const U16 *)memPtr); }

ZSTD_STATIC U32 ZSTD_read32(const void *memPtr) { return get_unaligned((const U32 *)memPtr); }

ZSTD_STATIC U64 ZSTD_read64(const void *memPtr) { return get_unaligned((const U64 *)memPtr); }

ZSTD_STATIC size_t ZSTD_readST(const void *memPtr) { return get_unaligned((const size_t *)memPtr); }

ZSTD_STATIC void ZSTD_write16(void *memPtr, U16 value) { put_unaligned(value, (U16 *)memPtr); }

ZSTD_STATIC void ZSTD_write32(void *memPtr, U32 value) { put_unaligned(value, (U32 *)memPtr); }

ZSTD_STATIC void ZSTD_write64(void *memPtr, U64 value) { put_unaligned(value, (U64 *)memPtr); }

/*=== Little endian r/w ===*/

ZSTD_STATIC U16 ZSTD_readLE16(const void *memPtr) { return get_unaligned_le16(memPtr); }

ZSTD_STATIC void ZSTD_writeLE16(void *memPtr, U16 val) { put_unaligned_le16(val, memPtr); }

ZSTD_STATIC U32 ZSTD_readLE24(const void *memPtr) { return ZSTD_readLE16(memPtr) + (((const BYTE *)memPtr)[2] << 16); }

ZSTD_STATIC void ZSTD_writeLE24(void *memPtr, U32 val)
{
	ZSTD_writeLE16(memPtr, (U16)val);
	((BYTE *)memPtr)[2] = (BYTE)(val >> 16);
}

ZSTD_STATIC U32 ZSTD_readLE32(const void *memPtr) { return get_unaligned_le32(memPtr); }

ZSTD_STATIC void ZSTD_writeLE32(void *memPtr, U32 val32) { put_unaligned_le32(val32, memPtr); }

ZSTD_STATIC U64 ZSTD_readLE64(const void *memPtr) { return get_unaligned_le64(memPtr); }

ZSTD_STATIC void ZSTD_writeLE64(void *memPtr, U64 val64) { put_unaligned_le64(val64, memPtr); }

ZSTD_STATIC size_t ZSTD_readLEST(const void *memPtr)
{
	if (ZSTD_32bits())
		return (size_t)ZSTD_readLE32(memPtr);
	else
		return (size_t)ZSTD_readLE64(memPtr);
}

ZSTD_STATIC void ZSTD_writeLEST(void *memPtr, size_t val)
{
	if (ZSTD_32bits())
		ZSTD_writeLE32(memPtr, (U32)val);
	else
		ZSTD_writeLE64(memPtr, (U64)val);
}

/*=== Big endian r/w ===*/

ZSTD_STATIC U32 ZSTD_readBE32(const void *memPtr) { return get_unaligned_be32(memPtr); }

ZSTD_STATIC void ZSTD_writeBE32(void *memPtr, U32 val32) { put_unaligned_be32(val32, memPtr); }

ZSTD_STATIC U64 ZSTD_readBE64(const void *memPtr) { return get_unaligned_be64(memPtr); }

ZSTD_STATIC void ZSTD_writeBE64(void *memPtr, U64 val64) { put_unaligned_be64(val64, memPtr); }

ZSTD_STATIC size_t ZSTD_readBEST(const void *memPtr)
{
	if (ZSTD_32bits())
		return (size_t)ZSTD_readBE32(memPtr);
	else
		return (size_t)ZSTD_readBE64(memPtr);
}

ZSTD_STATIC void ZSTD_writeBEST(void *memPtr, size_t val)
{
	if (ZSTD_32bits())
		ZSTD_writeBE32(memPtr, (U32)val);
	else
		ZSTD_writeBE64(memPtr, (U64)val);
}

/* function safe only for comparisons */
ZSTD_STATIC U32 ZSTD_readMINMATCH(const void *memPtr, U32 length)
{
	switch (length) {
	default:
	case 4: return ZSTD_read32(memPtr);
	case 3:
		if (ZSTD_isLittleEndian())
			return ZSTD_read32(memPtr) << 8;
		else
			return ZSTD_read32(memPtr) >> 8;
	}
}

#endif /* MEM_H_MODULE */
