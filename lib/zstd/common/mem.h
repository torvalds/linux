/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef MEM_H_MODULE
#define MEM_H_MODULE

/*-****************************************
*  Dependencies
******************************************/
#include <linux/unaligned.h>  /* get_unaligned, put_unaligned* */
#include <linux/compiler.h>  /* inline */
#include <linux/swab.h>  /* swab32, swab64 */
#include <linux/types.h>  /* size_t, ptrdiff_t */
#include "debug.h"  /* DEBUG_STATIC_ASSERT */

/*-****************************************
*  Compiler specifics
******************************************/
#undef MEM_STATIC /* may be already defined from common/compiler.h */
#define MEM_STATIC static inline

/*-**************************************************************
*  Basic Types
*****************************************************************/
typedef uint8_t  BYTE;
typedef uint8_t  U8;
typedef int8_t   S8;
typedef uint16_t U16;
typedef int16_t  S16;
typedef uint32_t U32;
typedef int32_t  S32;
typedef uint64_t U64;
typedef int64_t  S64;

/*-**************************************************************
*  Memory I/O API
*****************************************************************/
/*=== Static platform detection ===*/
MEM_STATIC unsigned MEM_32bits(void);
MEM_STATIC unsigned MEM_64bits(void);
MEM_STATIC unsigned MEM_isLittleEndian(void);

/*=== Native unaligned read/write ===*/
MEM_STATIC U16 MEM_read16(const void* memPtr);
MEM_STATIC U32 MEM_read32(const void* memPtr);
MEM_STATIC U64 MEM_read64(const void* memPtr);
MEM_STATIC size_t MEM_readST(const void* memPtr);

MEM_STATIC void MEM_write16(void* memPtr, U16 value);
MEM_STATIC void MEM_write32(void* memPtr, U32 value);
MEM_STATIC void MEM_write64(void* memPtr, U64 value);

/*=== Little endian unaligned read/write ===*/
MEM_STATIC U16 MEM_readLE16(const void* memPtr);
MEM_STATIC U32 MEM_readLE24(const void* memPtr);
MEM_STATIC U32 MEM_readLE32(const void* memPtr);
MEM_STATIC U64 MEM_readLE64(const void* memPtr);
MEM_STATIC size_t MEM_readLEST(const void* memPtr);

MEM_STATIC void MEM_writeLE16(void* memPtr, U16 val);
MEM_STATIC void MEM_writeLE24(void* memPtr, U32 val);
MEM_STATIC void MEM_writeLE32(void* memPtr, U32 val32);
MEM_STATIC void MEM_writeLE64(void* memPtr, U64 val64);
MEM_STATIC void MEM_writeLEST(void* memPtr, size_t val);

/*=== Big endian unaligned read/write ===*/
MEM_STATIC U32 MEM_readBE32(const void* memPtr);
MEM_STATIC U64 MEM_readBE64(const void* memPtr);
MEM_STATIC size_t MEM_readBEST(const void* memPtr);

MEM_STATIC void MEM_writeBE32(void* memPtr, U32 val32);
MEM_STATIC void MEM_writeBE64(void* memPtr, U64 val64);
MEM_STATIC void MEM_writeBEST(void* memPtr, size_t val);

/*=== Byteswap ===*/
MEM_STATIC U32 MEM_swap32(U32 in);
MEM_STATIC U64 MEM_swap64(U64 in);
MEM_STATIC size_t MEM_swapST(size_t in);

/*-**************************************************************
*  Memory I/O Implementation
*****************************************************************/
MEM_STATIC unsigned MEM_32bits(void)
{
    return sizeof(size_t) == 4;
}

MEM_STATIC unsigned MEM_64bits(void)
{
    return sizeof(size_t) == 8;
}

#if defined(__LITTLE_ENDIAN)
#define MEM_LITTLE_ENDIAN 1
#else
#define MEM_LITTLE_ENDIAN 0
#endif

MEM_STATIC unsigned MEM_isLittleEndian(void)
{
    return MEM_LITTLE_ENDIAN;
}

MEM_STATIC U16 MEM_read16(const void *memPtr)
{
    return get_unaligned((const U16 *)memPtr);
}

MEM_STATIC U32 MEM_read32(const void *memPtr)
{
    return get_unaligned((const U32 *)memPtr);
}

MEM_STATIC U64 MEM_read64(const void *memPtr)
{
    return get_unaligned((const U64 *)memPtr);
}

MEM_STATIC size_t MEM_readST(const void *memPtr)
{
    return get_unaligned((const size_t *)memPtr);
}

MEM_STATIC void MEM_write16(void *memPtr, U16 value)
{
    put_unaligned(value, (U16 *)memPtr);
}

MEM_STATIC void MEM_write32(void *memPtr, U32 value)
{
    put_unaligned(value, (U32 *)memPtr);
}

MEM_STATIC void MEM_write64(void *memPtr, U64 value)
{
    put_unaligned(value, (U64 *)memPtr);
}

/*=== Little endian r/w ===*/

MEM_STATIC U16 MEM_readLE16(const void *memPtr)
{
    return get_unaligned_le16(memPtr);
}

MEM_STATIC void MEM_writeLE16(void *memPtr, U16 val)
{
    put_unaligned_le16(val, memPtr);
}

MEM_STATIC U32 MEM_readLE24(const void *memPtr)
{
    return MEM_readLE16(memPtr) + (((const BYTE *)memPtr)[2] << 16);
}

MEM_STATIC void MEM_writeLE24(void *memPtr, U32 val)
{
	MEM_writeLE16(memPtr, (U16)val);
	((BYTE *)memPtr)[2] = (BYTE)(val >> 16);
}

MEM_STATIC U32 MEM_readLE32(const void *memPtr)
{
    return get_unaligned_le32(memPtr);
}

MEM_STATIC void MEM_writeLE32(void *memPtr, U32 val32)
{
    put_unaligned_le32(val32, memPtr);
}

MEM_STATIC U64 MEM_readLE64(const void *memPtr)
{
    return get_unaligned_le64(memPtr);
}

MEM_STATIC void MEM_writeLE64(void *memPtr, U64 val64)
{
    put_unaligned_le64(val64, memPtr);
}

MEM_STATIC size_t MEM_readLEST(const void *memPtr)
{
	if (MEM_32bits())
		return (size_t)MEM_readLE32(memPtr);
	else
		return (size_t)MEM_readLE64(memPtr);
}

MEM_STATIC void MEM_writeLEST(void *memPtr, size_t val)
{
	if (MEM_32bits())
		MEM_writeLE32(memPtr, (U32)val);
	else
		MEM_writeLE64(memPtr, (U64)val);
}

/*=== Big endian r/w ===*/

MEM_STATIC U32 MEM_readBE32(const void *memPtr)
{
    return get_unaligned_be32(memPtr);
}

MEM_STATIC void MEM_writeBE32(void *memPtr, U32 val32)
{
    put_unaligned_be32(val32, memPtr);
}

MEM_STATIC U64 MEM_readBE64(const void *memPtr)
{
    return get_unaligned_be64(memPtr);
}

MEM_STATIC void MEM_writeBE64(void *memPtr, U64 val64)
{
    put_unaligned_be64(val64, memPtr);
}

MEM_STATIC size_t MEM_readBEST(const void *memPtr)
{
	if (MEM_32bits())
		return (size_t)MEM_readBE32(memPtr);
	else
		return (size_t)MEM_readBE64(memPtr);
}

MEM_STATIC void MEM_writeBEST(void *memPtr, size_t val)
{
	if (MEM_32bits())
		MEM_writeBE32(memPtr, (U32)val);
	else
		MEM_writeBE64(memPtr, (U64)val);
}

MEM_STATIC U32 MEM_swap32(U32 in)
{
    return swab32(in);
}

MEM_STATIC U64 MEM_swap64(U64 in)
{
    return swab64(in);
}

MEM_STATIC size_t MEM_swapST(size_t in)
{
    if (MEM_32bits())
        return (size_t)MEM_swap32((U32)in);
    else
        return (size_t)MEM_swap64((U64)in);
}

#endif /* MEM_H_MODULE */
