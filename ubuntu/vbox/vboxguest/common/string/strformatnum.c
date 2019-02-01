/* $Id: strformatnum.cpp $ */
/** @file
 * IPRT - String Formatter, Single Numbers.
 */

/*
 * Copyright (C) 2010-2019 Oracle Corporation
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_STRING
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include "internal/string.h"


RTDECL(ssize_t) RTStrFormatU8(char *pszBuf, size_t cbBuf, uint8_t u8Value, unsigned int uiBase,
                              signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    fFlags &= ~RTSTR_F_BIT_MASK;
    fFlags |= RTSTR_F_8BIT;

    ssize_t cchRet;
    if (cbBuf >= 64)
        cchRet = RTStrFormatNumber(pszBuf, u8Value, uiBase, cchWidth, cchPrecision, fFlags);
    else
    {
        char szTmp[64];
        cchRet = RTStrFormatNumber(szTmp, u8Value, uiBase, cchWidth, cchPrecision, fFlags);
        if ((size_t)cchRet < cbBuf)
            memcpy(pszBuf, szTmp, cchRet + 1);
        else
        {
            if (cbBuf)
            {
                memcpy(pszBuf, szTmp, cbBuf - 1);
                pszBuf[cbBuf - 1] = '\0';
            }
            cchRet = VERR_BUFFER_OVERFLOW;
        }
    }
    return cchRet;
}


RTDECL(ssize_t) RTStrFormatU16(char *pszBuf, size_t cbBuf, uint16_t u16Value, unsigned int uiBase,
                              signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    fFlags &= ~RTSTR_F_BIT_MASK;
    fFlags |= RTSTR_F_16BIT;

    ssize_t cchRet;
    if (cbBuf >= 64)
        cchRet = RTStrFormatNumber(pszBuf, u16Value, uiBase, cchWidth, cchPrecision, fFlags);
    else
    {
        char szTmp[64];
        cchRet = RTStrFormatNumber(szTmp, u16Value, uiBase, cchWidth, cchPrecision, fFlags);
        if ((size_t)cchRet <= cbBuf)
            memcpy(pszBuf, szTmp, cchRet + 1);
        else
        {
            if (cbBuf)
            {
                memcpy(pszBuf, szTmp, cbBuf - 1);
                pszBuf[cbBuf - 1] = '\0';
            }
            cchRet = VERR_BUFFER_OVERFLOW;
        }
    }
    return cchRet;
}


RTDECL(ssize_t) RTStrFormatU32(char *pszBuf, size_t cbBuf, uint32_t u32Value, unsigned int uiBase,
                              signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    fFlags &= ~RTSTR_F_BIT_MASK;
    fFlags |= RTSTR_F_32BIT;

    ssize_t cchRet;
    if (cbBuf >= 64)
        cchRet = RTStrFormatNumber(pszBuf, u32Value, uiBase, cchWidth, cchPrecision, fFlags);
    else
    {
        char szTmp[64];
        cchRet = RTStrFormatNumber(szTmp, u32Value, uiBase, cchWidth, cchPrecision, fFlags);
        if ((size_t)cchRet <= cbBuf)
            memcpy(pszBuf, szTmp, cchRet + 1);
        else
        {
            if (cbBuf)
            {
                memcpy(pszBuf, szTmp, cbBuf - 1);
                pszBuf[cbBuf - 1] = '\0';
            }
            cchRet = VERR_BUFFER_OVERFLOW;
        }
    }
    return cchRet;
}


RTDECL(ssize_t) RTStrFormatU64(char *pszBuf, size_t cbBuf, uint64_t u64Value, unsigned int uiBase,
                              signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    fFlags &= ~RTSTR_F_BIT_MASK;
    fFlags |= RTSTR_F_64BIT;

    ssize_t cchRet;
    if (cbBuf >= 64)
        cchRet = RTStrFormatNumber(pszBuf, u64Value, uiBase, cchWidth, cchPrecision, fFlags);
    else
    {
        char szTmp[64];
        cchRet = RTStrFormatNumber(szTmp, u64Value, uiBase, cchWidth, cchPrecision, fFlags);
        if ((size_t)cchRet <= cbBuf)
            memcpy(pszBuf, szTmp, cchRet + 1);
        else
        {
            if (cbBuf)
            {
                memcpy(pszBuf, szTmp, cbBuf - 1);
                pszBuf[cbBuf - 1] = '\0';
            }
            cchRet = VERR_BUFFER_OVERFLOW;
        }
    }
    return cchRet;
}


RTDECL(ssize_t) RTStrFormatU128(char *pszBuf, size_t cbBuf, PCRTUINT128U pu128, unsigned int uiBase,
                                signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    NOREF(cchWidth); NOREF(cchPrecision);
    if (uiBase != 16)
        fFlags |= RTSTR_F_SPECIAL;
    fFlags &= ~RTSTR_F_BIT_MASK;

    char szTmp[64+32+32+32];
    char *pszTmp = cbBuf >= sizeof(szTmp) ? pszBuf : szTmp;
    size_t cchResult = RTStrFormatNumber(pszTmp, pu128->QWords.qw1, 16, 0, 0, fFlags | RTSTR_F_64BIT);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu128->QWords.qw0, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    if (pszTmp == pszBuf)
        return cchResult;
    int rc = RTStrCopy(pszBuf, cbBuf, pszTmp);
    if (RT_SUCCESS(rc))
        return cchResult;
    return rc;
}


RTDECL(ssize_t) RTStrFormatU256(char *pszBuf, size_t cbBuf, PCRTUINT256U pu256, unsigned int uiBase,
                                signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    NOREF(cchWidth); NOREF(cchPrecision);
    if (uiBase != 16)
        fFlags |= RTSTR_F_SPECIAL;
    fFlags &= ~RTSTR_F_BIT_MASK;

    char szTmp[64+32+32+32];
    char *pszTmp = cbBuf >= sizeof(szTmp) ? pszBuf : szTmp;
    size_t cchResult = RTStrFormatNumber(pszTmp, pu256->QWords.qw3, 16, 0, 0, fFlags | RTSTR_F_64BIT);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu256->QWords.qw2, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu256->QWords.qw1, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu256->QWords.qw0, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    if (pszTmp == pszBuf)
        return cchResult;
    int rc = RTStrCopy(pszBuf, cbBuf, pszTmp);
    if (RT_SUCCESS(rc))
        return cchResult;
    return rc;
}


RTDECL(ssize_t) RTStrFormatU512(char *pszBuf, size_t cbBuf, PCRTUINT512U pu512, unsigned int uiBase,
                                signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    NOREF(cchWidth); NOREF(cchPrecision);
    if (uiBase != 16)
        fFlags |= RTSTR_F_SPECIAL;
    fFlags &= ~RTSTR_F_BIT_MASK;

    char szTmp[64+32+32+32 + 32+32+32+32];
    char *pszTmp = cbBuf >= sizeof(szTmp) ? pszBuf : szTmp;
    size_t cchResult = RTStrFormatNumber(pszTmp, pu512->QWords.qw7, 16, 0, 0, fFlags | RTSTR_F_64BIT);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw6, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw5, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw4, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw3, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw2, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw1, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    cchResult += RTStrFormatNumber(&pszTmp[cchResult], pu512->QWords.qw0, 16, 8, 0,
                                   (fFlags | RTSTR_F_64BIT | RTSTR_F_ZEROPAD) & ~RTSTR_F_SPECIAL);
    if (pszTmp == pszBuf)
        return cchResult;
    int rc = RTStrCopy(pszBuf, cbBuf, pszTmp);
    if (RT_SUCCESS(rc))
        return cchResult;
    return rc;
}


RTDECL(ssize_t) RTStrFormatR80u2(char *pszBuf, size_t cbBuf, PCRTFLOAT80U2 pr80Value, signed int cchWidth,
                                 signed int cchPrecision, uint32_t fFlags)
{
    NOREF(cchWidth); NOREF(cchPrecision); NOREF(fFlags);
    char szTmp[160];

    char *pszTmp = szTmp;
    if (pr80Value->s.fSign)
        *pszTmp++ = '-';
    else
        *pszTmp++ = '+';

    if (pr80Value->s.uExponent == 0)
    {
#ifdef RT_COMPILER_GROKS_64BIT_BITFIELDS
        if (   !pr80Value->sj64.u63Fraction
            && pr80Value->sj64.fInteger)
#else
        if (   !pr80Value->sj.u32FractionLow
            && !pr80Value->sj.u31FractionHigh
            && pr80Value->sj.fInteger)
#endif
            *pszTmp++ = '0';
        /* else: Denormal, handled way below. */
    }
#ifdef RT_COMPILER_GROKS_64BIT_BITFIELDS
    else if (pr80Value->sj64.uExponent == UINT16_C(0x7fff))
#else
    else if (pr80Value->sj.uExponent == UINT16_C(0x7fff))
#endif
    {
        /** @todo Figure out Pseudo inf/nan... */
#ifdef RT_COMPILER_GROKS_64BIT_BITFIELDS
        if (pr80Value->sj64.fInteger)
#else
        if (pr80Value->sj.fInteger)
#endif
            *pszTmp++ = 'P';
#ifdef RT_COMPILER_GROKS_64BIT_BITFIELDS
        if (pr80Value->sj64.u63Fraction == 0)
#else
        if (   pr80Value->sj.u32FractionLow == 0
            && pr80Value->sj.u31FractionHigh == 0)
#endif
        {
            *pszTmp++ = 'I';
            *pszTmp++ = 'n';
            *pszTmp++ = 'f';
        }
        else
        {
            *pszTmp++ = 'N';
            *pszTmp++ = 'a';
            *pszTmp++ = 'N';
        }
    }
    if (pszTmp != &szTmp[1])
        *pszTmp = '\0';
    else
    {
#ifdef RT_COMPILER_GROKS_64BIT_BITFIELDS
        *pszTmp++ = pr80Value->sj64.fInteger ? '1' : '0';
#else
        *pszTmp++ = pr80Value->sj.fInteger ? '1' : '0';
#endif
        *pszTmp++ = 'm';
#ifdef RT_COMPILER_GROKS_64BIT_BITFIELDS
        pszTmp += RTStrFormatNumber(pszTmp, pr80Value->sj64.u63Fraction, 16, 2+16, 0,
                                    RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD | RTSTR_F_64BIT);
#else
        pszTmp += RTStrFormatNumber(pszTmp, RT_MAKE_U64(pr80Value->sj.u32FractionLow, pr80Value->sj.u31FractionHigh), 16, 2+16, 0,
                                    RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD | RTSTR_F_64BIT);
#endif

        *pszTmp++ = 'e';
#ifdef RT_COMPILER_GROKS_64BIT_BITFIELDS
        pszTmp += RTStrFormatNumber(pszTmp, (int32_t)pr80Value->sj64.uExponent - 16383, 10, 0, 0,
                                    RTSTR_F_ZEROPAD | RTSTR_F_32BIT | RTSTR_F_VALSIGNED);
#else
        pszTmp += RTStrFormatNumber(pszTmp, (int32_t)pr80Value->sj.uExponent - 16383, 10, 0, 0,
                                    RTSTR_F_ZEROPAD | RTSTR_F_32BIT | RTSTR_F_VALSIGNED);
#endif
    }

    /*
     * Copy out the result.
     */
    ssize_t cchRet = pszTmp - &szTmp[0];
    if ((size_t)cchRet <= cbBuf)
        memcpy(pszBuf, szTmp, cchRet + 1);
    else
    {
        if (cbBuf)
        {
            memcpy(pszBuf, szTmp, cbBuf - 1);
            pszBuf[cbBuf - 1] = '\0';
        }
        cchRet = VERR_BUFFER_OVERFLOW;
    }
    return cchRet;
}


RTDECL(ssize_t) RTStrFormatR80(char *pszBuf, size_t cbBuf, PCRTFLOAT80U pr80Value, signed int cchWidth,
                               signed int cchPrecision, uint32_t fFlags)
{
    RTFLOAT80U2 r80ValueU2;
    RT_ZERO(r80ValueU2);
    r80ValueU2.s.fSign       = pr80Value->s.fSign;
    r80ValueU2.s.uExponent   = pr80Value->s.uExponent;
    r80ValueU2.s.u64Mantissa = pr80Value->s.u64Mantissa;
    return RTStrFormatR80u2(pszBuf, cbBuf, &r80ValueU2, cchWidth, cchPrecision, fFlags);
}

