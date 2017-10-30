/** @file
 * IPRT - RTUINT64U methods for old 32-bit and 16-bit compilers.
 */

/*
 * Copyright (C) 2011-2017 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___iprt_uint64_h
#define ___iprt_uint64_h

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/asm.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_uint64 RTUInt64 - 64-bit Unsigned Integer Methods for ancient compilers
 * @ingroup grp_rt
 * @{
 */


/**
 * Test if a 128-bit unsigned integer value is zero.
 *
 * @returns true if they are, false if they aren't.
 * @param   pValue          The input and output value.
 */
DECLINLINE(bool) RTUInt64IsZero(PRTUINT64U pValue)
{
#if ARCH_BITS >= 32
    return pValue->s.Lo == 0
        && pValue->s.Hi == 0;
#else
    return pValue->Words.w0 == 0
        && pValue->Words.w1 == 0
        && pValue->Words.w2 == 0
        && pValue->Words.w3 == 0;
#endif
}


/**
 * Set a 128-bit unsigned integer value to zero.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 */
DECLINLINE(PRTUINT64U) RTUInt64SetZero(PRTUINT64U pResult)
{
#if ARCH_BITS >= 32
    pResult->s.Hi = 0;
    pResult->s.Lo = 0;
#else
    pResult->Words.w0 = 0;
    pResult->Words.w1 = 0;
    pResult->Words.w2 = 0;
    pResult->Words.w3 = 0;
#endif
    return pResult;
}


/**
 * Set a 32-bit unsigned integer value to the maximum value.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 */
DECLINLINE(PRTUINT64U) RTUInt64SetMax(PRTUINT64U pResult)
{
#if ARCH_BITS >= 32
    pResult->s.Hi = UINT32_MAX;
    pResult->s.Lo = UINT32_MAX;
#else
    pResult->Words.w0 = UINT16_MAX;
    pResult->Words.w1 = UINT16_MAX;
    pResult->Words.w2 = UINT16_MAX;
    pResult->Words.w3 = UINT16_MAX;
#endif
    return pResult;
}




/**
 * Adds two 64-bit unsigned integer values.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue1             The first value.
 * @param   pValue2             The second value.
 */
DECLINLINE(PRTUINT64U) RTUInt64Add(PRTUINT64U pResult, PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
    pResult->s.Hi = pValue1->s.Hi + pValue2->s.Hi;
    pResult->s.Lo = pValue1->s.Lo + pValue2->s.Lo;
    if (pResult->s.Lo < pValue1->s.Lo)
        pResult->s.Hi++;
    return pResult;
}


/**
 * Adds a 64-bit and a 32-bit unsigned integer values.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue1             The first value.
 * @param   uValue2             The second value, 32-bit.
 */
DECLINLINE(PRTUINT64U) RTUInt64AddU32(PRTUINT64U pResult, PCRTUINT64U pValue1, uint32_t uValue2)
{
    pResult->s.Hi = pValue1->s.Hi;
    pResult->s.Lo = pValue1->s.Lo + uValue2;
    if (pResult->s.Lo < pValue1->s.Lo)
        pResult->s.Hi++;
    return pResult;
}


/**
 * Subtracts a 64-bit unsigned integer value from another.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue1             The minuend value.
 * @param   pValue2             The subtrahend value.
 */
DECLINLINE(PRTUINT64U) RTUInt64Sub(PRTUINT64U pResult, PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
    pResult->s.Lo = pValue1->s.Lo - pValue2->s.Lo;
    pResult->s.Hi = pValue1->s.Hi - pValue2->s.Hi;
    if (pResult->s.Lo > pValue1->s.Lo)
        pResult->s.Hi--;
    return pResult;
}


/**
 * Multiplies two 64-bit unsigned integer values.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue1             The first value.
 * @param   pValue2             The second value.
 */
DECLINLINE(PRTUINT64U) RTUInt64Mul(PRTUINT64U pResult, PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
    RTUINT32U uTmp;

    /* multiply all words in v1 by v2.w0. */
    pResult->s.Lo = (uint32_t)pValue1->Words.w0 * pValue2->Words.w0;

    uTmp.u = (uint32_t)pValue1->Words.w1 * pValue2->Words.w0;
    pResult->Words.w3 = 0;
    pResult->Words.w2 = uTmp.Words.w1;
    pResult->Words.w1 += uTmp.Words.w0;
    if (pResult->Words.w1 < uTmp.Words.w0)
        if (pResult->Words.w2++ == UINT16_MAX)
            pResult->Words.w3++;

    pResult->s.Hi += (uint32_t)pValue1->Words.w2 * pValue2->Words.w0;
    pResult->Words.w3       += pValue1->Words.w3 * pValue2->Words.w0;

    /* multiply w0, w1 & w2 in v1 by v2.w1. */
    uTmp.u = (uint32_t)pValue1->Words.w0 * pValue2->Words.w1;
    pResult->Words.w1 += uTmp.Words.w0;
    if (pResult->Words.w1 < uTmp.Words.w0)
        if (pResult->Words.w2++ == UINT16_MAX)
            pResult->Words.w3++;

    pResult->Words.w2 += uTmp.Words.w1;
    if (pResult->Words.w2 < uTmp.Words.w1)
        pResult->Words.w3++;

    pResult->s.Hi += (uint32_t)pValue1->Words.w1 * pValue2->Words.w1;
    pResult->Words.w3       += pValue1->Words.w2 * pValue2->Words.w1;

    /* multiply w0 & w1 in v1 by v2.w2. */
    pResult->s.Hi += (uint32_t)pValue1->Words.w0 * pValue2->Words.w2;
    pResult->Words.w3       += pValue1->Words.w1 * pValue2->Words.w2;

    /* multiply w0 in v1 by v2.w3. */
    pResult->Words.w3       += pValue1->Words.w0 * pValue2->Words.w3;

    return pResult;
}


/**
 * Multiplies an 64-bit unsigned integer by a 32-bit unsigned integer value.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue1             The first value.
 * @param   uValue2             The second value, 32-bit.
 */
DECLINLINE(PRTUINT64U) RTUInt64MulByU32(PRTUINT64U pResult, PCRTUINT64U pValue1, uint32_t uValue2)
{
    uint16_t const uLoValue2 = (uint16_t)uValue2;
    uint16_t const uHiValue2 = (uint16_t)(uValue2 >> 16);
    RTUINT32U uTmp;

    /* multiply all words in v1 by uLoValue1. */
    pResult->s.Lo = (uint32_t)pValue1->Words.w0 * uLoValue2;

    uTmp.u = (uint32_t)pValue1->Words.w1 * uLoValue2;
    pResult->Words.w3 = 0;
    pResult->Words.w2 = uTmp.Words.w1;
    pResult->Words.w1 += uTmp.Words.w0;
    if (pResult->Words.w1 < uTmp.Words.w0)
        if (pResult->Words.w2++ == UINT16_MAX)
            pResult->Words.w3++;

    pResult->s.Hi += (uint32_t)pValue1->Words.w2 * uLoValue2;
    pResult->Words.w3       += pValue1->Words.w3 * uLoValue2;

    /* multiply w0, w1 & w2 in v1 by uHiValue2. */
    uTmp.u = (uint32_t)pValue1->Words.w0 * uHiValue2;
    pResult->Words.w1 += uTmp.Words.w0;
    if (pResult->Words.w1 < uTmp.Words.w0)
        if (pResult->Words.w2++ == UINT16_MAX)
            pResult->Words.w3++;

    pResult->Words.w2 += uTmp.Words.w1;
    if (pResult->Words.w2 < uTmp.Words.w1)
        pResult->Words.w3++;

    pResult->s.Hi += (uint32_t)pValue1->Words.w1 * uHiValue2;
    pResult->Words.w3     += pValue1->Words.w2 * uHiValue2;

    return pResult;
}


/**
 * Multiplies two 32-bit unsigned integer values with 64-bit precision.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   uValue1             The first value. 32-bit.
 * @param   uValue2             The second value, 32-bit.
 */
DECLINLINE(PRTUINT64U) RTUInt64MulU32ByU32(PRTUINT64U pResult, uint32_t uValue1, uint32_t uValue2)
{
    uint16_t const uLoValue1 = (uint16_t)uValue1;
    uint16_t const uHiValue1 = (uint16_t)(uValue1 >> 16);
    uint16_t const uLoValue2 = (uint16_t)uValue2;
    uint16_t const uHiValue2 = (uint16_t)(uValue2 >> 16);
    RTUINT32U uTmp;

    /* Multiply uLoValue1 and uHiValue1 by uLoValue1. */
    pResult->s.Lo = (uint32_t)uLoValue1 * uLoValue2;

    uTmp.u = (uint32_t)uHiValue1 * uLoValue2;
    pResult->Words.w3 = 0;
    pResult->Words.w2 = uTmp.Words.w1;
    pResult->Words.w1 += uTmp.Words.w0;
    if (pResult->Words.w1 < uTmp.Words.w0)
        if (pResult->Words.w2++ == UINT16_MAX)
            pResult->Words.w3++;

    /* Multiply uLoValue1 and uHiValue1 by uHiValue2. */
    uTmp.u = (uint32_t)uLoValue1 * uHiValue2;
    pResult->Words.w1 += uTmp.Words.w0;
    if (pResult->Words.w1 < uTmp.Words.w0)
        if (pResult->Words.w2++ == UINT16_MAX)
            pResult->Words.w3++;

    pResult->Words.w2 += uTmp.Words.w1;
    if (pResult->Words.w2 < uTmp.Words.w1)
        pResult->Words.w3++;

    pResult->s.Hi += (uint32_t)uHiValue1 * uHiValue2;
    return pResult;
}


DECLINLINE(PRTUINT64U) RTUInt64DivRem(PRTUINT64U pQuotient, PRTUINT64U pRemainder, PCRTUINT64U pValue1, PCRTUINT64U pValue2);

/**
 * Divides a 64-bit unsigned integer value by another.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue1             The dividend value.
 * @param   pValue2             The divisor value.
 */
DECLINLINE(PRTUINT64U) RTUInt64Div(PRTUINT64U pResult, PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
    RTUINT64U Ignored;
    return RTUInt64DivRem(pResult, &Ignored, pValue1, pValue2);
}


/**
 * Divides a 64-bit unsigned integer value by another, returning the remainder.
 *
 * @returns pResult
 * @param   pResult             The result variable (remainder).
 * @param   pValue1             The dividend value.
 * @param   pValue2             The divisor value.
 */
DECLINLINE(PRTUINT64U) RTUInt64Mod(PRTUINT64U pResult, PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
    RTUINT64U Ignored;
    RTUInt64DivRem(&Ignored, pResult, pValue1, pValue2);
    return pResult;
}


/**
 * Bitwise AND of two 64-bit unsigned integer values.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue1             The first value.
 * @param   pValue2             The second value.
 */
DECLINLINE(PRTUINT64U) RTUInt64And(PRTUINT64U pResult, PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
    pResult->s.Hi = pValue1->s.Hi & pValue2->s.Hi;
    pResult->s.Lo = pValue1->s.Lo & pValue2->s.Lo;
    return pResult;
}


/**
 * Bitwise OR of two 64-bit unsigned integer values.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue1             The first value.
 * @param   pValue2             The second value.
 */
DECLINLINE(PRTUINT64U) RTUInt64Or( PRTUINT64U pResult, PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
    pResult->s.Hi = pValue1->s.Hi | pValue2->s.Hi;
    pResult->s.Lo = pValue1->s.Lo | pValue2->s.Lo;
    return pResult;
}


/**
 * Bitwise XOR of two 64-bit unsigned integer values.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue1             The first value.
 * @param   pValue2             The second value.
 */
DECLINLINE(PRTUINT64U) RTUInt64Xor(PRTUINT64U pResult, PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
    pResult->s.Hi = pValue1->s.Hi ^ pValue2->s.Hi;
    pResult->s.Lo = pValue1->s.Lo ^ pValue2->s.Lo;
    return pResult;
}


/**
 * Shifts a 64-bit unsigned integer value @a cBits to the left.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue              The value to shift.
 * @param   cBits               The number of bits to shift it.
 */
DECLINLINE(PRTUINT64U) RTUInt64ShiftLeft(PRTUINT64U pResult, PCRTUINT64U pValue, int cBits)
{
    cBits &= 63;
    if (cBits < 32)
    {
        pResult->s.Lo = pValue->s.Lo << cBits;
        pResult->s.Hi = (pValue->s.Hi << cBits) | (pValue->s.Lo >> (32 - cBits));
    }
    else
    {
        pResult->s.Lo = 0;
        pResult->s.Hi = pValue->s.Lo << (cBits - 32);
    }
    return pResult;
}


/**
 * Shifts a 64-bit unsigned integer value @a cBits to the right.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue              The value to shift.
 * @param   cBits               The number of bits to shift it.
 */
DECLINLINE(PRTUINT64U) RTUInt64ShiftRight(PRTUINT64U pResult, PCRTUINT64U pValue, int cBits)
{
    cBits &= 63;
    if (cBits < 32)
    {
        pResult->s.Hi = pValue->s.Hi >> cBits;
        pResult->s.Lo = (pValue->s.Lo >> cBits) | (pValue->s.Hi << (32 - cBits));
    }
    else
    {
        pResult->s.Hi = 0;
        pResult->s.Lo = pValue->s.Hi >> (cBits - 32);
    }
    return pResult;
}


/**
 * Boolean not (result 0 or 1).
 *
 * @returns pResult.
 * @param   pResult             The result variable.
 * @param   pValue              The value.
 */
DECLINLINE(PRTUINT64U) RTUInt64BooleanNot(PRTUINT64U pResult, PCRTUINT64U pValue)
{
    pResult->s.Lo = pValue->s.Lo || pValue->s.Hi ? 0 : 1;
    pResult->s.Hi = 0;
    return pResult;
}


/**
 * Bitwise not (flips each bit of the 64 bits).
 *
 * @returns pResult.
 * @param   pResult             The result variable.
 * @param   pValue              The value.
 */
DECLINLINE(PRTUINT64U) RTUInt64BitwiseNot(PRTUINT64U pResult, PCRTUINT64U pValue)
{
    pResult->s.Hi = ~pValue->s.Hi;
    pResult->s.Lo = ~pValue->s.Lo;
    return pResult;
}


/**
 * Assigns one 64-bit unsigned integer value to another.
 *
 * @returns pResult
 * @param   pResult             The result variable.
 * @param   pValue              The value to assign.
 */
DECLINLINE(PRTUINT64U) RTUInt64Assign(PRTUINT64U pResult, PCRTUINT64U pValue)
{
#if ARCH_BITS >= 32
    pResult->s.Hi = pValue->s.Hi;
    pResult->s.Lo = pValue->s.Lo;
#else
    pResult->Words.w0 = pValue->Words.w0;
    pResult->Words.w1 = pValue->Words.w1;
    pResult->Words.w2 = pValue->Words.w2;
    pResult->Words.w3 = pValue->Words.w3;
#endif
    return pResult;
}


/**
 * Assigns a boolean value to 64-bit unsigned integer.
 *
 * @returns pValueResult
 * @param   pValueResult        The result variable.
 * @param   fValue              The boolean value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignBoolean(PRTUINT64U pValueResult, bool fValue)
{
#if ARCH_BITS >= 32
    pValueResult->s.Lo = fValue;
    pValueResult->s.Hi = 0;
#else
    pValueResult->Words.w0 = fValue;
    pValueResult->Words.w1 = 0;
    pValueResult->Words.w2 = 0;
    pValueResult->Words.w3 = 0;
#endif
    return pValueResult;
}


/**
 * Assigns a 8-bit unsigned integer value to 64-bit unsigned integer.
 *
 * @returns pValueResult
 * @param   pValueResult        The result variable.
 * @param   u8Value             The 8-bit unsigned integer value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignU8(PRTUINT64U pValueResult, uint8_t u8Value)
{
#if ARCH_BITS >= 32
    pValueResult->s.Lo = u8Value;
    pValueResult->s.Hi = 0;
#else
    pValueResult->Words.w0 = u8Value;
    pValueResult->Words.w1 = 0;
    pValueResult->Words.w2 = 0;
    pValueResult->Words.w3 = 0;
#endif
    return pValueResult;
}


/**
 * Assigns a 16-bit unsigned integer value to 64-bit unsigned integer.
 *
 * @returns pValueResult
 * @param   pValueResult        The result variable.
 * @param   u16Value            The 16-bit unsigned integer value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignU16(PRTUINT64U pValueResult, uint16_t u16Value)
{
#if ARCH_BITS >= 32
    pValueResult->s.Lo = u16Value;
    pValueResult->s.Hi = 0;
#else
    pValueResult->Words.w0 = u16Value;
    pValueResult->Words.w1 = 0;
    pValueResult->Words.w2 = 0;
    pValueResult->Words.w3 = 0;
#endif
    return pValueResult;
}


/**
 * Assigns a 32-bit unsigned integer value to 64-bit unsigned integer.
 *
 * @returns pValueResult
 * @param   pValueResult        The result variable.
 * @param   u32Value            The 32-bit unsigned integer value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignU32(PRTUINT64U pValueResult, uint32_t u32Value)
{
#if ARCH_BITS >= 32
    pValueResult->s.Lo = u32Value;
    pValueResult->s.Hi = 0;
#else
    pValueResult->Words.w0 = (uint16_t)u32Value;
    pValueResult->Words.w1 = u32Value >> 16;
    pValueResult->Words.w2 = 0;
    pValueResult->Words.w3 = 0;
#endif
    return pValueResult;
}


/**
 * Adds two 64-bit unsigned integer values, storing the result in the first.
 *
 * @returns pValue1Result.
 * @param   pValue1Result   The first value and result.
 * @param   pValue2         The second value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignAdd(PRTUINT64U pValue1Result, PCRTUINT64U pValue2)
{
    uint32_t const uTmp = pValue1Result->s.Lo;
    pValue1Result->s.Lo += pValue2->s.Lo;
    if (pValue1Result->s.Lo < uTmp)
        pValue1Result->s.Hi++;
    pValue1Result->s.Hi += pValue2->s.Hi;
    return pValue1Result;
}


/**
 * Subtracts two 64-bit unsigned integer values, storing the result in the
 * first.
 *
 * @returns pValue1Result.
 * @param   pValue1Result   The minuend value and result.
 * @param   pValue2         The subtrahend value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignSub(PRTUINT64U pValue1Result, PCRTUINT64U pValue2)
{
    uint32_t const uTmp = pValue1Result->s.Lo;
    pValue1Result->s.Lo -= pValue2->s.Lo;
    if (pValue1Result->s.Lo > uTmp)
        pValue1Result->s.Hi--;
    pValue1Result->s.Hi -= pValue2->s.Hi;
    return pValue1Result;
}


/**
 * Multiplies two 64-bit unsigned integer values, storing the result in the
 * first.
 *
 * @returns pValue1Result.
 * @param   pValue1Result   The first value and result.
 * @param   pValue2         The second value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignMul(PRTUINT64U pValue1Result, PCRTUINT64U pValue2)
{
    RTUINT64U Result;
    RTUInt64Mul(&Result, pValue1Result, pValue2);
    *pValue1Result = Result;
    return pValue1Result;
}


/**
 * Divides a 64-bit unsigned integer value by another, storing the result in
 * the first.
 *
 * @returns pValue1Result.
 * @param   pValue1Result   The dividend value and result.
 * @param   pValue2         The divisor value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignDiv(PRTUINT64U pValue1Result, PCRTUINT64U pValue2)
{
    RTUINT64U Result;
    RTUINT64U Ignored;
    RTUInt64DivRem(&Result, &Ignored, pValue1Result, pValue2);
    *pValue1Result = Result;
    return pValue1Result;
}


/**
 * Divides a 64-bit unsigned integer value by another, storing the remainder in
 * the first.
 *
 * @returns pValue1Result.
 * @param   pValue1Result   The dividend value and result (remainder).
 * @param   pValue2         The divisor value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignMod(PRTUINT64U pValue1Result, PCRTUINT64U pValue2)
{
    RTUINT64U Ignored;
    RTUINT64U Result;
    RTUInt64DivRem(&Ignored, &Result, pValue1Result, pValue2);
    *pValue1Result = Result;
    return pValue1Result;
}


/**
 * Performs a bitwise AND of two 64-bit unsigned integer values and assigned
 * the result to the first one.
 *
 * @returns pValue1Result.
 * @param   pValue1Result   The first value and result.
 * @param   pValue2         The second value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignAnd(PRTUINT64U pValue1Result, PCRTUINT64U pValue2)
{
#if ARCH_BITS >= 32
    pValue1Result->s.Hi &= pValue2->s.Hi;
    pValue1Result->s.Lo &= pValue2->s.Lo;
#else
    pValue1Result->Words.w0 &= pValue2->Words.w0;
    pValue1Result->Words.w1 &= pValue2->Words.w1;
    pValue1Result->Words.w2 &= pValue2->Words.w2;
    pValue1Result->Words.w3 &= pValue2->Words.w3;
#endif
    return pValue1Result;
}


/**
 * Performs a bitwise AND of a 64-bit unsigned integer value and a mask made
 * up of the first N bits, assigning the result to the the 64-bit value.
 *
 * @returns pValueResult.
 * @param   pValueResult    The value and result.
 * @param   cBits           The number of bits to AND (counting from the first
 *                          bit).
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignAndNFirstBits(PRTUINT64U pValueResult, unsigned cBits)
{
    if (cBits <= 32)
    {
        if (cBits != 32)
            pValueResult->s.Lo &= (RT_BIT_32(cBits) - 1);
        pValueResult->s.Hi = 0;
    }
    else if (cBits < 64)
        pValueResult->s.Hi &= (RT_BIT_32(cBits - 32) - 1);
    return pValueResult;
}


/**
 * Performs a bitwise OR of two 64-bit unsigned integer values and assigned
 * the result to the first one.
 *
 * @returns pValue1Result.
 * @param   pValue1Result   The first value and result.
 * @param   pValue2         The second value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignOr(PRTUINT64U pValue1Result, PCRTUINT64U pValue2)
{
#if ARCH_BITS >= 32
    pValue1Result->s.Hi |= pValue2->s.Hi;
    pValue1Result->s.Lo |= pValue2->s.Lo;
#else
    pValue1Result->Words.w0 |= pValue2->Words.w0;
    pValue1Result->Words.w1 |= pValue2->Words.w1;
    pValue1Result->Words.w2 |= pValue2->Words.w2;
    pValue1Result->Words.w3 |= pValue2->Words.w3;
#endif
    return pValue1Result;
}


/**
 * ORs in a bit and assign the result to the input value.
 *
 * @returns pValue1Result.
 * @param   pValue1Result   The first value and result.
 * @param   iBit            The bit to set (0 based).
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignOrBit(PRTUINT64U pValue1Result, unsigned iBit)
{
#if ARCH_BITS >= 32
    if (iBit >= 32)
        pValue1Result->s.Hi |= RT_BIT_32(iBit - 32);
    else
        pValue1Result->s.Lo |= RT_BIT_32(iBit);
#else
    if (iBit >= 32)
    {
        if (iBit >= 48)
            pValue1Result->Words.w3 |= UINT16_C(1) << (iBit - 48);
        else
            pValue1Result->Words.w2 |= UINT16_C(1) << (iBit - 32);
    }
    else
    {
        if (iBit >= 16)
            pValue1Result->Words.w1 |= UINT16_C(1) << (iBit - 16);
        else
            pValue1Result->Words.w0 |= UINT16_C(1) << (iBit);
    }
#endif
    return pValue1Result;
}



/**
 * Performs a bitwise XOR of two 64-bit unsigned integer values and assigned
 * the result to the first one.
 *
 * @returns pValue1Result.
 * @param   pValue1Result   The first value and result.
 * @param   pValue2         The second value.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignXor(PRTUINT64U pValue1Result, PCRTUINT64U pValue2)
{
#if ARCH_BITS >= 32
    pValue1Result->s.Hi ^= pValue2->s.Hi;
    pValue1Result->s.Lo ^= pValue2->s.Lo;
#else
    pValue1Result->Words.w0 ^= pValue2->Words.w0;
    pValue1Result->Words.w1 ^= pValue2->Words.w1;
    pValue1Result->Words.w2 ^= pValue2->Words.w2;
    pValue1Result->Words.w3 ^= pValue2->Words.w3;
#endif
    return pValue1Result;
}


/**
 * Performs a bitwise left shift on a 64-bit unsigned integer value, assigning
 * the result to it.
 *
 * @returns pValueResult.
 * @param   pValueResult    The first value and result.
 * @param   cBits           The number of bits to shift.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignShiftLeft(PRTUINT64U pValueResult, int cBits)
{
    RTUINT64U const InVal = *pValueResult;
    if (cBits > 0)
    {
        /* (left shift) */
        cBits &= 31;
        if (cBits >= 32)
        {
            pValueResult->s.Lo  = 0;
            pValueResult->s.Hi  = InVal.s.Lo << (cBits - 32);
        }
        else
        {
            pValueResult->s.Hi  = InVal.s.Hi << cBits;
            pValueResult->s.Hi |= InVal.s.Lo >> (32 - cBits);
            pValueResult->s.Lo  = InVal.s.Lo << cBits;
        }
    }
    else if (cBits < 0)
    {
        /* (right shift) */
        cBits = -cBits;
        cBits &= 31;
        if (cBits >= 32)
        {
            pValueResult->s.Hi  = 0;
            pValueResult->s.Lo  = InVal.s.Hi >> (cBits - 32);
        }
        else
        {
            pValueResult->s.Lo  = InVal.s.Lo >> cBits;
            pValueResult->s.Lo |= InVal.s.Hi << (32 - cBits);
            pValueResult->s.Hi  = InVal.s.Hi >> cBits;
        }
    }
    return pValueResult;
}


/**
 * Performs a bitwise left shift on a 64-bit unsigned integer value, assigning
 * the result to it.
 *
 * @returns pValueResult.
 * @param   pValueResult    The first value and result.
 * @param   cBits           The number of bits to shift.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignShiftRight(PRTUINT64U pValueResult, int cBits)
{
    return RTUInt64AssignShiftLeft(pValueResult, -cBits);
}


/**
 * Performs a bitwise NOT on a 64-bit unsigned integer value, assigning the
 * result to it.
 *
 * @returns pValueResult
 * @param   pValueResult    The value and result.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignBitwiseNot(PRTUINT64U pValueResult)
{
#if ARCH_BITS >= 32
    pValueResult->s.Hi = ~pValueResult->s.Hi;
    pValueResult->s.Lo = ~pValueResult->s.Lo;
#else
    pValueResult->Words.w0 = ~pValueResult->Words.w0;
    pValueResult->Words.w1 = ~pValueResult->Words.w1;
    pValueResult->Words.w2 = ~pValueResult->Words.w2;
    pValueResult->Words.w3 = ~pValueResult->Words.w3;
#endif
    return pValueResult;
}


/**
 * Performs a boolean NOT on a 64-bit unsigned integer value, assigning the
 * result to it.
 *
 * @returns pValueResult
 * @param   pValueResult    The value and result.
 */
DECLINLINE(PRTUINT64U) RTUInt64AssignBooleanNot(PRTUINT64U pValueResult)
{
    return RTUInt64AssignBoolean(pValueResult, RTUInt64IsZero(pValueResult));
}


/**
 * Compares two 64-bit unsigned integer values.
 *
 * @retval  0 if equal.
 * @retval  -1 if the first value is smaller than the second.
 * @retval  1  if the first value is larger than the second.
 *
 * @param   pValue1             The first value.
 * @param   pValue2             The second value.
 */
DECLINLINE(int) RTUInt64Compare(PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
#if ARCH_BITS >= 32
    if (pValue1->s.Hi != pValue2->s.Hi)
        return pValue1->s.Hi > pValue2->s.Hi ? 1 : -1;
    if (pValue1->s.Lo != pValue2->s.Lo)
        return pValue1->s.Lo > pValue2->s.Lo ? 1 : -1;
    return 0;
#else
    if (pValue1->Words.w3 != pValue2->Words.w3)
        return pValue1->Words.w3 > pValue2->Words.w3 ? 1 : -1;
    if (pValue1->Words.w2 != pValue2->Words.w2)
        return pValue1->Words.w2 > pValue2->Words.w2 ? 1 : -1;
    if (pValue1->Words.w1 != pValue2->Words.w1)
        return pValue1->Words.w1 > pValue2->Words.w1 ? 1 : -1;
    if (pValue1->Words.w0 != pValue2->Words.w0)
        return pValue1->Words.w0 > pValue2->Words.w0 ? 1 : -1;
    return 0;
#endif
}


/**
 * Tests if a 64-bit unsigned integer value is smaller than another.
 *
 * @returns true if the first value is smaller, false if not.
 * @param   pValue1             The first value.
 * @param   pValue2             The second value.
 */
DECLINLINE(bool) RTUInt64IsSmaller(PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
#if ARCH_BITS >= 32
    return pValue1->s.Hi < pValue2->s.Hi
        || (   pValue1->s.Hi == pValue2->s.Hi
            && pValue1->s.Lo  < pValue2->s.Lo);
#else
    return pValue1->Words.w3 < pValue2->Words.w3
        || (   pValue1->Words.w3 == pValue2->Words.w3
            && (   pValue1->Words.w2  < pValue2->Words.w2
                || (   pValue1->Words.w2 == pValue2->Words.w2
                    && (   pValue1->Words.w1  < pValue2->Words.w1
                        || (   pValue1->Words.w1 == pValue2->Words.w1
                            && pValue1->Words.w0  < pValue2->Words.w0)))));
#endif
}


/**
 * Tests if a 32-bit unsigned integer value is larger than another.
 *
 * @returns true if the first value is larger, false if not.
 * @param   pValue1             The first value.
 * @param   pValue2             The second value.
 */
DECLINLINE(bool) RTUInt64IsLarger(PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
#if ARCH_BITS >= 32
    return pValue1->s.Hi > pValue2->s.Hi
        || (   pValue1->s.Hi == pValue2->s.Hi
            && pValue1->s.Lo  > pValue2->s.Lo);
#else
    return pValue1->Words.w3 > pValue2->Words.w3
        || (   pValue1->Words.w3 == pValue2->Words.w3
            && (   pValue1->Words.w2  > pValue2->Words.w2
                || (   pValue1->Words.w2 == pValue2->Words.w2
                    && (   pValue1->Words.w1  > pValue2->Words.w1
                        || (   pValue1->Words.w1 == pValue2->Words.w1
                            && pValue1->Words.w0  > pValue2->Words.w0)))));
#endif
}


/**
 * Tests if a 64-bit unsigned integer value is larger or equal than another.
 *
 * @returns true if the first value is larger or equal, false if not.
 * @param   pValue1             The first value.
 * @param   pValue2             The second value.
 */
DECLINLINE(bool) RTUInt64IsLargerOrEqual(PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
#if ARCH_BITS >= 32
    return pValue1->s.Hi > pValue2->s.Hi
        || (   pValue1->s.Hi == pValue2->s.Hi
            && pValue1->s.Lo >= pValue2->s.Lo);
#else
    return pValue1->Words.w3 > pValue2->Words.w3
        || (   pValue1->Words.w3 == pValue2->Words.w3
            && (   pValue1->Words.w2  > pValue2->Words.w2
                || (   pValue1->Words.w2 == pValue2->Words.w2
                    && (   pValue1->Words.w1  > pValue2->Words.w1
                        || (   pValue1->Words.w1 == pValue2->Words.w1
                            && pValue1->Words.w0 >= pValue2->Words.w0)))));
#endif
}


/**
 * Tests if two 64-bit unsigned integer values not equal.
 *
 * @returns true if equal, false if not equal.
 * @param   pValue1             The first value.
 * @param   pValue2             The second value.
 */
DECLINLINE(bool) RTUInt64IsEqual(PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
#if ARCH_BITS >= 32
    return pValue1->s.Hi == pValue2->s.Hi
        && pValue1->s.Lo == pValue2->s.Lo;
#else
    return pValue1->Words.w0 == pValue2->Words.w0
        && pValue1->Words.w1 == pValue2->Words.w1
        && pValue1->Words.w2 == pValue2->Words.w2
        && pValue1->Words.w3 == pValue2->Words.w3;
#endif
}


/**
 * Tests if two 64-bit unsigned integer values are not equal.
 *
 * @returns true if not equal, false if equal.
 * @param   pValue1             The first value.
 * @param   pValue2             The second value.
 */
DECLINLINE(bool) RTUInt64IsNotEqual(PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
    return !RTUInt64IsEqual(pValue1, pValue2);
}


/**
 * Sets a bit in a 64-bit unsigned integer type.
 *
 * @returns pValueResult.
 * @param   pValueResult    The input and output value.
 * @param   iBit            The bit to set.
 */
DECLINLINE(PRTUINT64U) RTUInt64BitSet(PRTUINT64U pValueResult, unsigned iBit)
{
    if (iBit < 32)
    {
#if ARCH_BITS >= 32
        pValueResult->s.Lo |= RT_BIT_32(iBit);
#else
        if (iBit < 16)
            pValueResult->Words.w0 |= UINT16_C(1) << iBit;
        else
            pValueResult->Words.w1 |= UINT16_C(1) << (iBit - 32);
#endif
    }
    else if (iBit < 64)
    {
#if ARCH_BITS >= 32
        pValueResult->s.Hi |= RT_BIT_32(iBit - 32);
#else
        if (iBit < 48)
            pValueResult->Words.w2 |= UINT16_C(1) << (iBit - 64);
        else
            pValueResult->Words.w3 |= UINT16_C(1) << (iBit - 96);
#endif
    }
    return pValueResult;
}


/**
 * Sets a bit in a 64-bit unsigned integer type.
 *
 * @returns pValueResult.
 * @param   pValueResult    The input and output value.
 * @param   iBit            The bit to set.
 */
DECLINLINE(PRTUINT64U) RTUInt64BitClear(PRTUINT64U pValueResult, unsigned iBit)
{
    if (iBit < 32)
    {
#if ARCH_BITS >= 32
        pValueResult->s.Lo &= ~RT_BIT_32(iBit);
#else
        if (iBit < 48)
            pValueResult->Words.w0 &= ~(UINT16_C(1) << (iBit));
        else
            pValueResult->Words.w1 &= ~(UINT16_C(1) << (iBit - 32));
#endif
    }
    else if (iBit < 64)
    {
#if ARCH_BITS >= 32
        pValueResult->s.Hi &= ~RT_BIT_32(iBit - 32);
#else
        if (iBit < 48)
            pValueResult->Words.w2 &= ~(UINT16_C(1) << (iBit - 64));
        else
            pValueResult->Words.w3 &= ~(UINT16_C(1) << (iBit - 96));
#endif
    }
    return pValueResult;
}


/**
 * Tests if a bit in a 64-bit unsigned integer value is set.
 *
 * @returns pValueResult.
 * @param   pValueResult    The input and output value.
 * @param   iBit            The bit to test.
 */
DECLINLINE(bool) RTUInt64BitTest(PRTUINT64U pValueResult, unsigned iBit)
{
    bool fRc;
    if (iBit < 32)
    {
#if ARCH_BITS >= 32
        fRc = RT_BOOL(pValueResult->s.Lo & RT_BIT_32(iBit));
#else
        if (iBit < 16)
            fRc = RT_BOOL(pValueResult->Words.w0 & (UINT16_C(1) << (iBit)));
        else
            fRc = RT_BOOL(pValueResult->Words.w1 & (UINT16_C(1) << (iBit - 16)));
#endif
    }
    else if (iBit < 64)
    {
#if ARCH_BITS >= 32
        fRc = RT_BOOL(pValueResult->s.Hi & RT_BIT_32(iBit - 32));
#else
        if (iBit < 48)
            fRc = RT_BOOL(pValueResult->Words.w2 & (UINT16_C(1) << (iBit - 32)));
        else
            fRc = RT_BOOL(pValueResult->Words.w3 & (UINT16_C(1) << (iBit - 48)));
#endif
    }
    else
        fRc = false;
    return fRc;
}


/**
 * Set a range of bits a 64-bit unsigned integer value.
 *
 * @returns pValueResult.
 * @param   pValueResult    The input and output value.
 * @param   iFirstBit       The first bit to test.
 * @param   cBits           The number of bits to set.
 */
DECLINLINE(PRTUINT64U) RTUInt64BitSetRange(PRTUINT64U pValueResult, unsigned iFirstBit, unsigned cBits)
{
    /* bounds check & fix. */
    if (iFirstBit < 64)
    {
        if (iFirstBit + cBits > 64)
            cBits = 64 - iFirstBit;

#if ARCH_BITS >= 32
        if (iFirstBit + cBits < 32)
            pValueResult->s.Lo |= (RT_BIT_32(cBits) - 1) << iFirstBit;
        else if (iFirstBit + cBits < 64 && iFirstBit >= 32)
            pValueResult->s.Hi |= (RT_BIT_32(cBits) - 1) << (iFirstBit - 32);
        else
#else
        if (iFirstBit + cBits < 16)
            pValueResult->Words.w0 |= ((UINT16_C(1) << cBits) - 1) << iFirstBit;
        else if (iFirstBit + cBits < 32 && iFirstBit >= 16)
            pValueResult->Words.w1 |= ((UINT16_C(1) << cBits) - 1) << (iFirstBit - 16);
        else if (iFirstBit + cBits < 48 && iFirstBit >= 32)
            pValueResult->Words.w2 |= ((UINT16_C(1) << cBits) - 1) << (iFirstBit - 32);
        else if (iFirstBit + cBits < 64 && iFirstBit >= 48)
            pValueResult->Words.w3 |= ((UINT16_C(1) << cBits) - 1) << (iFirstBit - 48);
        else
#endif
            while (cBits-- > 0)
                RTUInt64BitSet(pValueResult, iFirstBit++);
    }
    return pValueResult;
}


/**
 * Test if all the bits of a 64-bit unsigned integer value are set.
 *
 * @returns true if they are, false if they aren't.
 * @param   pValue          The input and output value.
 */
DECLINLINE(bool) RTUInt64BitAreAllSet(PRTUINT64U pValue)
{
#if ARCH_BITS >= 32
    return pValue->s.Hi == UINT32_MAX
        && pValue->s.Lo == UINT32_MAX;
#else
    return pValue->Words.w0 == UINT16_MAX
        && pValue->Words.w1 == UINT16_MAX
        && pValue->Words.w2 == UINT16_MAX
        && pValue->Words.w3 == UINT16_MAX;
#endif
}


/**
 * Test if all the bits of a 64-bit unsigned integer value are clear.
 *
 * @returns true if they are, false if they aren't.
 * @param   pValue          The input and output value.
 */
DECLINLINE(bool) RTUInt64BitAreAllClear(PRTUINT64U pValue)
{
    return RTUInt64IsZero(pValue);
}


DECLINLINE(unsigned) RTUInt64BitCount(PCRTUINT64U pValue)
{
    unsigned cBits;
    if (pValue->s.Hi != 0)
    {
#if ARCH_BITS >= 32
        cBits = 32 + ASMBitLastSetU32(pValue->s.Hi);
#else
        if (pValue->Words.w3)
            cBits = 48 + ASMBitLastSetU16(pValue->Words.w3);
        else
            cBits = 32 + ASMBitLastSetU16(pValue->Words.w2);
#endif
    }
    else
    {
#if ARCH_BITS >= 32
        cBits = ASMBitLastSetU32(pValue->s.Lo);
#else
        if (pValue->Words.w1)
            cBits = 16 + ASMBitLastSetU16(pValue->Words.w1);
        else
            cBits =  0 + ASMBitLastSetU16(pValue->Words.w0);
#endif
    }
    return cBits;
}


/**
 * Divides a 64-bit unsigned integer value by another, returning both quotient
 * and remainder.
 *
 * @returns pQuotient, NULL if pValue2 is 0.
 * @param   pQuotient           Where to return the quotient.
 * @param   pRemainder          Where to return the remainder.
 * @param   pValue1             The dividend value.
 * @param   pValue2             The divisor value.
 */
DECLINLINE(PRTUINT64U) RTUInt64DivRem(PRTUINT64U pQuotient, PRTUINT64U pRemainder, PCRTUINT64U pValue1, PCRTUINT64U pValue2)
{
    int iDiff;

    /*
     * Sort out all the special cases first.
     */
    /* Divide by zero or 1? */
    if (!pValue2->s.Hi)
    {
        if (!pValue2->s.Lo)
            return NULL;

        if (pValue2->s.Lo == 1)
        {
            RTUInt64SetZero(pRemainder);
            *pQuotient = *pValue1;
            return pQuotient;
        }
        /** @todo RTUInt64DivModByU32 */
    }

    /* Dividend is smaller? */
    iDiff = RTUInt64Compare(pValue1, pValue2);
    if (iDiff < 0)
    {
        *pRemainder = *pValue1;
        RTUInt64SetZero(pQuotient);
    }

    /* The values are equal? */
    else if (iDiff == 0)
    {
        RTUInt64SetZero(pRemainder);
        RTUInt64AssignU8(pQuotient, 1);
    }
    else
    {
        /*
         * Prepare.
         */
        unsigned  iBitAdder = RTUInt64BitCount(pValue1) - RTUInt64BitCount(pValue2);
        RTUINT64U NormDivisor = *pValue2;
        if (iBitAdder)
        {
            RTUInt64ShiftLeft(&NormDivisor, pValue2, iBitAdder);
            if (RTUInt64IsLarger(&NormDivisor, pValue1))
            {
                RTUInt64AssignShiftRight(&NormDivisor, 1);
                iBitAdder--;
            }
        }
        else
            NormDivisor = *pValue2;

        RTUInt64SetZero(pQuotient);
        *pRemainder = *pValue1;

        /*
         * Do the division.
         */
        if (RTUInt64IsLargerOrEqual(pRemainder, pValue2))
        {
            for (;;)
            {
                if (RTUInt64IsLargerOrEqual(pRemainder, &NormDivisor))
                {
                    RTUInt64AssignSub(pRemainder, &NormDivisor);
                    RTUInt64AssignOrBit(pQuotient, iBitAdder);
                }
                if (RTUInt64IsSmaller(pRemainder, pValue2))
                    break;
                RTUInt64AssignShiftRight(&NormDivisor, 1);
                iBitAdder--;
            }
        }
    }
    return pQuotient;
}


/** @} */

RT_C_DECLS_END

#endif

