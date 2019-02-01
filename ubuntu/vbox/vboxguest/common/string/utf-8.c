/* $Id: utf-8.cpp $ */
/** @file
 * IPRT - UTF-8 Decoding.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
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
#include <iprt/string.h>
#include <iprt/latin1.h>
#include "internal/iprt.h"

#include <iprt/uni.h>
#include <iprt/asm.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include "internal/string.h"



/**
 * Get get length in code points of a UTF-8 encoded string.
 * The string is validated while doing this.
 *
 * @returns IPRT status code.
 * @param   psz             Pointer to the UTF-8 string.
 * @param   cch             The max length of the string. (btw cch = cb)
 *                          Use RTSTR_MAX if all of the string is to be examined.
 * @param   pcuc            Where to store the length in unicode code points.
 * @param   pcchActual      Where to store the actual size of the UTF-8 string
 *                          on success (cch = cb again). Optional.
 */
DECLHIDDEN(int) rtUtf8Length(const char *psz, size_t cch, size_t *pcuc, size_t *pcchActual)
{
    const unsigned char *puch = (const unsigned char *)psz;
    size_t cCodePoints = 0;
    while (cch > 0)
    {
        const unsigned char uch = *puch;
        if (!uch)
            break;
        if (uch & RT_BIT(7))
        {
            /* figure sequence length and validate the first byte */
/** @todo RT_USE_RTC_3629 */
            unsigned cb;
            if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5))) == (RT_BIT(7) | RT_BIT(6)))
                cb = 2;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5)))
                cb = 3;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4)))
                cb = 4;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3)))
                cb = 5;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2) | RT_BIT(1))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2)))
                cb = 6;
            else
            {
                RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(cch, 10), puch));
                return VERR_INVALID_UTF8_ENCODING;
            }

            /* check length */
            if (cb > cch)
            {
                RTStrAssertMsgFailed(("Invalid UTF-8 length: cb=%d cch=%d (%.*Rhxs)\n", cb, cch, RT_MIN(cch, 10), puch));
                return VERR_INVALID_UTF8_ENCODING;
            }

            /* validate the rest */
            switch (cb)
            {
                case 6:
                    RTStrAssertMsgReturn((puch[5] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("6/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RT_FALL_THRU();
                case 5:
                    RTStrAssertMsgReturn((puch[4] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("5/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RT_FALL_THRU();
                case 4:
                    RTStrAssertMsgReturn((puch[3] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("4/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RT_FALL_THRU();
                case 3:
                    RTStrAssertMsgReturn((puch[2] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RT_FALL_THRU();
                case 2:
                    RTStrAssertMsgReturn((puch[1] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                   break;
            }

            /* validate the code point. */
            RTUNICP uc;
            switch (cb)
            {
                case 6:
                    uc =            (puch[5] & 0x3f)
                        | ((RTUNICP)(puch[4] & 0x3f) << 6)
                        | ((RTUNICP)(puch[3] & 0x3f) << 12)
                        | ((RTUNICP)(puch[2] & 0x3f) << 18)
                        | ((RTUNICP)(puch[1] & 0x3f) << 24)
                        | ((RTUNICP)(uch     & 0x01) << 30);
                    RTStrAssertMsgReturn(uc >= 0x04000000 && uc <= 0x7fffffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    break;
                case 5:
                    uc =            (puch[4] & 0x3f)
                        | ((RTUNICP)(puch[3] & 0x3f) << 6)
                        | ((RTUNICP)(puch[2] & 0x3f) << 12)
                        | ((RTUNICP)(puch[1] & 0x3f) << 18)
                        | ((RTUNICP)(uch     & 0x03) << 24);
                    RTStrAssertMsgReturn(uc >= 0x00200000 && uc <= 0x03ffffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    break;
                case 4:
                    uc =            (puch[3] & 0x3f)
                        | ((RTUNICP)(puch[2] & 0x3f) << 6)
                        | ((RTUNICP)(puch[1] & 0x3f) << 12)
                        | ((RTUNICP)(uch     & 0x07) << 18);
                    RTStrAssertMsgReturn(uc >= 0x00010000 && uc <= 0x001fffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    break;
                case 3:
                    uc =            (puch[2] & 0x3f)
                        | ((RTUNICP)(puch[1] & 0x3f) << 6)
                        | ((RTUNICP)(uch     & 0x0f) << 12);
                    RTStrAssertMsgReturn(uc >= 0x00000800 && uc <= 0x0000fffd,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch),
                                         uc == 0xffff || uc == 0xfffe ? VERR_CODE_POINT_ENDIAN_INDICATOR : VERR_INVALID_UTF8_ENCODING);
                    RTStrAssertMsgReturn(uc < 0xd800 || uc > 0xdfff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_CODE_POINT_SURROGATE);
                    break;
                case 2:
                    uc =            (puch[1] & 0x3f)
                        | ((RTUNICP)(uch     & 0x1f) << 6);
                    RTStrAssertMsgReturn(uc >= 0x00000080 && uc <= 0x000007ff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    break;
            }

            /* advance */
            cch -= cb;
            puch += cb;
        }
        else
        {
             /* one ASCII byte */
            puch++;
            cch--;
        }
        cCodePoints++;
    }

    /* done */
    *pcuc = cCodePoints;
    if (pcchActual)
        *pcchActual = puch - (unsigned char const *)psz;
    return VINF_SUCCESS;
}


/**
 * Decodes and UTF-8 string into an array of unicode code point.
 *
 * Since we know the input is valid, we do *not* perform encoding or length checks.
 *
 * @returns iprt status code.
 * @param   psz     The UTF-8 string to recode. This is a valid encoding.
 * @param   cch     The number of chars (the type char, so bytes if you like) to process of the UTF-8 string.
 *                  The recoding will stop when cch or '\\0' is reached. Pass RTSTR_MAX to process up to '\\0'.
 * @param   paCps   Where to store the code points array.
 * @param   cCps    The number of RTUNICP items the paCps buffer can hold, excluding the terminator ('\\0').
 */
static int rtUtf8Decode(const char *psz, size_t cch, PRTUNICP paCps, size_t cCps)
{
    int                     rc   = VINF_SUCCESS;
    const unsigned char    *puch = (const unsigned char *)psz;
    PRTUNICP                pCp  = paCps;
    while (cch > 0)
    {
        /* read the next char and check for terminator. */
        const unsigned char uch = *puch;
        if (uch)
        { /* we only break once, so consider this the likely branch. */ }
        else
            break;

        /* check for output overflow */
        if (RT_LIKELY(cCps >= 1))
        { /* likely */ }
        else
        {
            rc = VERR_BUFFER_OVERFLOW;
            break;
        }
        cCps--;

        /* decode and recode the code point */
        if (!(uch & RT_BIT(7)))
        {
            *pCp++ = uch;
            puch++;
            cch--;
        }
#ifdef RT_STRICT
        else if (!(uch & RT_BIT(6)))
            AssertMsgFailed(("Internal error!\n"));
#endif
        else if (!(uch & RT_BIT(5)))
        {
            *pCp++ = (puch[1] & 0x3f)
                   | ((uint16_t)(uch     & 0x1f) << 6);
            puch += 2;
            cch -= 2;
        }
        else if (!(uch & RT_BIT(4)))
        {
            *pCp++ = (puch[2] & 0x3f)
                   | ((uint16_t)(puch[1] & 0x3f) << 6)
                   | ((uint16_t)(uch     & 0x0f) << 12);
            puch += 3;
            cch -= 3;
        }
        else if (!(uch & RT_BIT(3)))
        {
            *pCp++ = (puch[3] & 0x3f)
                   | ((RTUNICP)(puch[2] & 0x3f) << 6)
                   | ((RTUNICP)(puch[1] & 0x3f) << 12)
                   | ((RTUNICP)(uch     & 0x07) << 18);
            puch += 4;
            cch -= 4;
        }
        else if (!(uch & RT_BIT(2)))
        {
            *pCp++ = (puch[4] & 0x3f)
                   | ((RTUNICP)(puch[3] & 0x3f) << 6)
                   | ((RTUNICP)(puch[2] & 0x3f) << 12)
                   | ((RTUNICP)(puch[1] & 0x3f) << 18)
                   | ((RTUNICP)(uch     & 0x03) << 24);
            puch += 5;
            cch -= 6;
        }
        else
        {
            Assert(!(uch & RT_BIT(1)));
            *pCp++ = (puch[5] & 0x3f)
                   | ((RTUNICP)(puch[4] & 0x3f) << 6)
                   | ((RTUNICP)(puch[3] & 0x3f) << 12)
                   | ((RTUNICP)(puch[2] & 0x3f) << 18)
                   | ((RTUNICP)(puch[1] & 0x3f) << 24)
                   | ((RTUNICP)(uch     & 0x01) << 30);
            puch += 6;
            cch -= 6;
        }
    }

    /* done */
    *pCp = 0;
    return rc;
}


RTDECL(size_t) RTStrUniLen(const char *psz)
{
    size_t cCodePoints;
    int rc = rtUtf8Length(psz, RTSTR_MAX, &cCodePoints, NULL);
    return RT_SUCCESS(rc) ? cCodePoints : 0;
}
RT_EXPORT_SYMBOL(RTStrUniLen);


RTDECL(int) RTStrUniLenEx(const char *psz, size_t cch, size_t *pcCps)
{
    size_t cCodePoints;
    int rc = rtUtf8Length(psz, cch, &cCodePoints, NULL);
    if (pcCps)
        *pcCps = RT_SUCCESS(rc) ? cCodePoints : 0;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrUniLenEx);


RTDECL(int) RTStrValidateEncoding(const char *psz)
{
    return RTStrValidateEncodingEx(psz, RTSTR_MAX, 0);
}
RT_EXPORT_SYMBOL(RTStrValidateEncoding);


RTDECL(int) RTStrValidateEncodingEx(const char *psz, size_t cch, uint32_t fFlags)
{
    AssertReturn(!(fFlags & ~(RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH)),
                 VERR_INVALID_PARAMETER);
    AssertPtr(psz);

    /*
     * Use rtUtf8Length for the job.
     */
    size_t cchActual;
    size_t cCpsIgnored;
    int rc = rtUtf8Length(psz, cch, &cCpsIgnored, &cchActual);
    if (RT_SUCCESS(rc))
    {
        if (fFlags & RTSTR_VALIDATE_ENCODING_EXACT_LENGTH)
        {
            if (fFlags & RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED)
                cchActual++;
            if (cchActual == cch)
                rc = VINF_SUCCESS;
            else if (cchActual < cch)
                rc = VERR_BUFFER_UNDERFLOW;
            else
                rc = VERR_BUFFER_OVERFLOW;
        }
        else if (    (fFlags & RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED)
                 &&  cchActual >= cch)
            rc = VERR_BUFFER_OVERFLOW;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrValidateEncodingEx);


RTDECL(bool) RTStrIsValidEncoding(const char *psz)
{
    int rc = RTStrValidateEncodingEx(psz, RTSTR_MAX, 0);
    return RT_SUCCESS(rc);
}
RT_EXPORT_SYMBOL(RTStrIsValidEncoding);


RTDECL(size_t) RTStrPurgeEncoding(char *psz)
{
    size_t cErrors = 0;
    for (;;)
    {
        RTUNICP Cp;
        int rc = RTStrGetCpEx((const char **)&psz, &Cp);
        if (RT_SUCCESS(rc))
        {
            if (!Cp)
                break;
        }
        else
        {
            psz[-1] = '?';
            cErrors++;
        }
    }
    return cErrors;
}
RT_EXPORT_SYMBOL(RTStrPurgeEncoding);


/**
 * Helper for RTStrPurgeComplementSet.
 *
 * @returns true if @a Cp is valid, false if not.
 * @param   Cp              The code point to validate.
 * @param   puszValidPairs  Pair of valid code point sets.
 * @param   cValidPairs     Number of pairs.
 */
DECLINLINE(bool) rtStrPurgeIsInSet(RTUNICP Cp, PCRTUNICP puszValidPairs, uint32_t cValidPairs)
{
    while (cValidPairs-- > 0)
    {
        if (   Cp >= puszValidPairs[0]
            && Cp <= puszValidPairs[1])
            return true;
        puszValidPairs += 2;
    }
    return false;
}


RTDECL(ssize_t) RTStrPurgeComplementSet(char *psz, PCRTUNICP puszValidPairs, char chReplacement)
{
    AssertReturn(chReplacement && (unsigned)chReplacement < 128, -1);

    /*
     * Calc valid pairs and check that we've got an even number.
     */
    uint32_t cValidPairs = 0;
    while (puszValidPairs[cValidPairs * 2])
    {
        AssertReturn(puszValidPairs[cValidPairs * 2 + 1], -1);
        AssertMsg(puszValidPairs[cValidPairs * 2] <= puszValidPairs[cValidPairs * 2 + 1],
                  ("%#x vs %#x\n", puszValidPairs[cValidPairs * 2], puszValidPairs[cValidPairs * 2 + 1]));
        cValidPairs++;
    }

    /*
     * Do the replacing.
     */
    ssize_t cReplacements = 0;
    for (;;)
    {
        char    *pszCur = psz;
        RTUNICP  Cp;
        int rc = RTStrGetCpEx((const char **)&psz, &Cp);
        if (RT_SUCCESS(rc))
        {
            if (Cp)
            {
                if (!rtStrPurgeIsInSet(Cp, puszValidPairs, cValidPairs))
                {
                    for (; pszCur != psz; ++pszCur)
                        *pszCur = chReplacement;
                    ++cReplacements;
                }
            }
            else
                break;
        }
        else
            return -1;
    }
    return cReplacements;
}
RT_EXPORT_SYMBOL(RTStrPurgeComplementSet);


RTDECL(int) RTStrToUni(const char *pszString, PRTUNICP *ppaCps)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(pszString));
    Assert(VALID_PTR(ppaCps));
    *ppaCps = NULL;

    /*
     * Validate the UTF-8 input and count its code points.
     */
    size_t cCps;
    int rc = rtUtf8Length(pszString, RTSTR_MAX, &cCps, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer.
         */
        PRTUNICP paCps = (PRTUNICP)RTMemAlloc((cCps + 1) * sizeof(RTUNICP));
        if (paCps)
        {
            /*
             * Decode the string.
             */
            rc = rtUtf8Decode(pszString, RTSTR_MAX, paCps, cCps);
            if (RT_SUCCESS(rc))
            {
                *ppaCps = paCps;
                return rc;
            }
            RTMemFree(paCps);
        }
        else
            rc = VERR_NO_CODE_POINT_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUni);


RTDECL(int)  RTStrToUniEx(const char *pszString, size_t cchString, PRTUNICP *ppaCps, size_t cCps, size_t *pcCps)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(pszString));
    Assert(VALID_PTR(ppaCps));
    Assert(!pcCps || VALID_PTR(pcCps));

    /*
     * Validate the UTF-8 input and count the code points.
     */
    size_t cCpsResult;
    int rc = rtUtf8Length(pszString, cchString, &cCpsResult, NULL);
    if (RT_SUCCESS(rc))
    {
        if (pcCps)
            *pcCps = cCpsResult;

        /*
         * Check buffer size / Allocate buffer.
         */
        bool fShouldFree;
        PRTUNICP paCpsResult;
        if (cCps > 0 && *ppaCps)
        {
            fShouldFree = false;
            if (cCps <= cCpsResult)
                return VERR_BUFFER_OVERFLOW;
            paCpsResult = *ppaCps;
        }
        else
        {
            *ppaCps = NULL;
            fShouldFree = true;
            cCps = RT_MAX(cCpsResult + 1, cCps);
            paCpsResult = (PRTUNICP)RTMemAlloc(cCps * sizeof(RTUNICP));
        }
        if (paCpsResult)
        {
            /*
             * Encode the UTF-16 string.
             */
            rc = rtUtf8Decode(pszString, cchString, paCpsResult, cCps - 1);
            if (RT_SUCCESS(rc))
            {
                *ppaCps = paCpsResult;
                return rc;
            }
            if (fShouldFree)
                RTMemFree(paCpsResult);
        }
        else
            rc = VERR_NO_CODE_POINT_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUniEx);


/**
 * Calculates the UTF-16 length of a string, validating the encoding while doing so.
 *
 * @returns IPRT status code.
 * @param   psz     Pointer to the UTF-8 string.
 * @param   cch     The max length of the string. (btw cch = cb)
 * @param   pcwc    Where to store the length of the UTF-16 string as a number
 *                  of RTUTF16 characters.
 * @sa      rtUtf8CalcUtf16Length
 */
static int rtUtf8CalcUtf16LengthN(const char *psz, size_t cch, size_t *pcwc)
{
    const unsigned char *puch = (const unsigned char *)psz;
    size_t cwc = 0;
    while (cch > 0)
    {
        const unsigned char uch = *puch;
        if (!(uch & RT_BIT(7)))
        {
            /* one ASCII byte */
            if (uch)
            {
                cwc++;
                puch++;
                cch--;
            }
            else
                break;
        }
        else
        {
            /*
             * Multibyte sequence is more complicated when we have length
             * restrictions on the input.
             */
            /* figure sequence length and validate the first byte */
            unsigned cb;
            if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5))) == (RT_BIT(7) | RT_BIT(6)))
                cb = 2;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5)))
                cb = 3;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4)))
                cb = 4;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3)))
                cb = 5;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2) | RT_BIT(1))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2)))
                cb = 6;
            else
            {
                RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(cch, 10), puch));
                return VERR_INVALID_UTF8_ENCODING;
            }

            /* check length */
            if (cb > cch)
            {
                RTStrAssertMsgFailed(("Invalid UTF-8 length: cb=%d cch=%d (%.*Rhxs)\n", cb, cch, RT_MIN(cch, 10), puch));
                return VERR_INVALID_UTF8_ENCODING;
            }

            /* validate the rest */
            switch (cb)
            {
                case 6:
                    RTStrAssertMsgReturn((puch[5] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("6/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RT_FALL_THRU();
                case 5:
                    RTStrAssertMsgReturn((puch[4] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("5/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RT_FALL_THRU();
                case 4:
                    RTStrAssertMsgReturn((puch[3] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("4/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RT_FALL_THRU();
                case 3:
                    RTStrAssertMsgReturn((puch[2] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RT_FALL_THRU();
                case 2:
                    RTStrAssertMsgReturn((puch[1] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                   break;
            }

            /* validate the code point. */
            RTUNICP uc;
            switch (cb)
            {
                case 6:
                    uc =            (puch[5] & 0x3f)
                        | ((RTUNICP)(puch[4] & 0x3f) << 6)
                        | ((RTUNICP)(puch[3] & 0x3f) << 12)
                        | ((RTUNICP)(puch[2] & 0x3f) << 18)
                        | ((RTUNICP)(puch[1] & 0x3f) << 24)
                        | ((RTUNICP)(uch     & 0x01) << 30);
                    RTStrAssertMsgReturn(uc >= 0x04000000 && uc <= 0x7fffffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RTStrAssertMsgFailed(("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch));
                    return VERR_CANT_RECODE_AS_UTF16;
                case 5:
                    uc =            (puch[4] & 0x3f)
                        | ((RTUNICP)(puch[3] & 0x3f) << 6)
                        | ((RTUNICP)(puch[2] & 0x3f) << 12)
                        | ((RTUNICP)(puch[1] & 0x3f) << 18)
                        | ((RTUNICP)(uch     & 0x03) << 24);
                    RTStrAssertMsgReturn(uc >= 0x00200000 && uc <= 0x03ffffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RTStrAssertMsgFailed(("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch));
                    return VERR_CANT_RECODE_AS_UTF16;
                case 4:
                    uc =            (puch[3] & 0x3f)
                        | ((RTUNICP)(puch[2] & 0x3f) << 6)
                        | ((RTUNICP)(puch[1] & 0x3f) << 12)
                        | ((RTUNICP)(uch     & 0x07) << 18);
                    RTStrAssertMsgReturn(uc >= 0x00010000 && uc <= 0x001fffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RTStrAssertMsgReturn(uc <= 0x0010ffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_CANT_RECODE_AS_UTF16);
                    cwc++;
                    break;
                case 3:
                    uc =            (puch[2] & 0x3f)
                        | ((RTUNICP)(puch[1] & 0x3f) << 6)
                        | ((RTUNICP)(uch     & 0x0f) << 12);
                    RTStrAssertMsgReturn(uc >= 0x00000800 && uc <= 0x0000fffd,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch),
                                         uc == 0xffff || uc == 0xfffe ? VERR_CODE_POINT_ENDIAN_INDICATOR : VERR_INVALID_UTF8_ENCODING);
                    RTStrAssertMsgReturn(uc < 0xd800 || uc > 0xdfff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_CODE_POINT_SURROGATE);
                    break;
                case 2:
                    uc =            (puch[1] & 0x3f)
                        | ((RTUNICP)(uch     & 0x1f) << 6);
                    RTStrAssertMsgReturn(uc >= 0x00000080 && uc <= 0x000007ff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    break;
            }

            /* advance */
            cch -= cb;
            puch += cb;
            cwc++;
        }
    }

    /* done */
    *pcwc = cwc;
    return VINF_SUCCESS;
}


/**
 * Calculates the UTF-16 length of a string, validating the encoding while doing so.
 *
 * @returns IPRT status code.
 * @param   psz     Pointer to the UTF-8 string.
 * @param   pcwc    Where to store the length of the UTF-16 string as a number
 *                  of RTUTF16 characters.
 * @sa      rtUtf8CalcUtf16LengthN
 */
static int rtUtf8CalcUtf16Length(const char *psz, size_t *pcwc)
{
    const unsigned char *puch = (const unsigned char *)psz;
    size_t cwc = 0;
    for (;;)
    {
        const unsigned char uch = *puch;
        if (!(uch & RT_BIT(7)))
        {
            /* one ASCII byte */
            if (uch)
            {
                cwc++;
                puch++;
            }
            else
                break;
        }
        else
        {
            /*
             * Figure sequence length, implicitly validate the first byte.
             * Then validate the additional bytes.
             * Finally validate the code point.
             */
            unsigned cb;
            RTUNICP uc;
            if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5))) == (RT_BIT(7) | RT_BIT(6)))
            {
                RTStrAssertMsgReturn((puch[1] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                uc =            (puch[1] & 0x3f)
                    | ((RTUNICP)(uch     & 0x1f) << 6);
                RTStrAssertMsgReturn(uc >= 0x00000080 && uc <= 0x000007ff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                cb = 2;
            }
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5)))
            {
                RTStrAssertMsgReturn((puch[1] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn((puch[2] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                uc =            (puch[2] & 0x3f)
                    | ((RTUNICP)(puch[1] & 0x3f) << 6)
                    | ((RTUNICP)(uch     & 0x0f) << 12);
                RTStrAssertMsgReturn(uc >= 0x00000800 && uc <= 0x0000fffd,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch),
                                     uc == 0xffff || uc == 0xfffe ? VERR_CODE_POINT_ENDIAN_INDICATOR : VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn(uc < 0xd800 || uc > 0xdfff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_CODE_POINT_SURROGATE);
                cb = 3;
            }
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4)))
            {
                RTStrAssertMsgReturn((puch[1] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn((puch[2] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn((puch[3] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("4/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                uc =            (puch[3] & 0x3f)
                    | ((RTUNICP)(puch[2] & 0x3f) << 6)
                    | ((RTUNICP)(puch[1] & 0x3f) << 12)
                    | ((RTUNICP)(uch     & 0x07) << 18);
                RTStrAssertMsgReturn(uc >= 0x00010000 && uc <= 0x001fffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn(uc <= 0x0010ffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_CANT_RECODE_AS_UTF16);
                cwc++;
                cb = 4;
            }
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3)))
            {
                RTStrAssertMsgReturn((puch[1] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn((puch[2] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn((puch[3] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("4/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn((puch[4] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("5/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                uc =            (puch[4] & 0x3f)
                    | ((RTUNICP)(puch[3] & 0x3f) << 6)
                    | ((RTUNICP)(puch[2] & 0x3f) << 12)
                    | ((RTUNICP)(puch[1] & 0x3f) << 18)
                    | ((RTUNICP)(uch     & 0x03) << 24);
                RTStrAssertMsgReturn(uc >= 0x00200000 && uc <= 0x03ffffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgFailed(("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch));
                return VERR_CANT_RECODE_AS_UTF16;
                //cb = 5;
            }
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2) | RT_BIT(1))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2)))
            {
                RTStrAssertMsgReturn((puch[1] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn((puch[2] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn((puch[3] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("4/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn((puch[4] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("5/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgReturn((puch[5] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("6/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                uc =            (puch[5] & 0x3f)
                    | ((RTUNICP)(puch[4] & 0x3f) << 6)
                    | ((RTUNICP)(puch[3] & 0x3f) << 12)
                    | ((RTUNICP)(puch[2] & 0x3f) << 18)
                    | ((RTUNICP)(puch[1] & 0x3f) << 24)
                    | ((RTUNICP)(uch     & 0x01) << 30);
                RTStrAssertMsgReturn(uc >= 0x04000000 && uc <= 0x7fffffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                RTStrAssertMsgFailed(("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch));
                return VERR_CANT_RECODE_AS_UTF16;
                //cb = 6;
            }
            else
            {
                RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(cch, 10), puch));
                return VERR_INVALID_UTF8_ENCODING;
            }

            /* advance */
            puch += cb;
            cwc++;
        }
    }

    /* done */
    *pcwc = cwc;
    return VINF_SUCCESS;
}



/**
 * Recodes a valid UTF-8 string as UTF-16.
 *
 * Since we know the input is valid, we do *not* perform encoding or length checks.
 *
 * @returns iprt status code.
 * @param   psz     The UTF-8 string to recode. This is a valid encoding.
 * @param   cch     The number of chars (the type char, so bytes if you like) to process of the UTF-8 string.
 *                  The recoding will stop when cch or '\\0' is reached. Pass RTSTR_MAX to process up to '\\0'.
 * @param   pwsz    Where to store the UTF-16 string.
 * @param   cwc     The number of RTUTF16 items the pwsz buffer can hold, excluding the terminator ('\\0').
 *
 * @note    rtUtf8RecodeAsUtf16Big is a duplicate with RT_H2BE_U16 applied.
 */
static int rtUtf8RecodeAsUtf16(const char *psz, size_t cch, PRTUTF16 pwsz, size_t cwc)
{
    int                     rc   = VINF_SUCCESS;
    const unsigned char    *puch = (const unsigned char *)psz;
    PRTUTF16                pwc  = pwsz;
    while (cch > 0)
    {
        /* read the next char and check for terminator. */
        const unsigned char uch = *puch;
        if (uch)
        { /* we only break once, so consider this the likely branch. */ }
        else
            break;

        /* check for output overflow */
        if (RT_LIKELY(cwc >= 1))
        { /* likely */ }
        else
        {
            rc = VERR_BUFFER_OVERFLOW;
            break;
        }
        cwc--;

        /* decode and recode the code point */
        if (!(uch & RT_BIT(7)))
        {
            *pwc++ = uch;
            puch++;
            cch--;
        }
        else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5))) == (RT_BIT(7) | RT_BIT(6)))
        {
            uint16_t uc = (puch[1] & 0x3f)
                    | ((uint16_t)(uch     & 0x1f) << 6);
            *pwc++ = uc;
            puch += 2;
            cch -= 2;
        }
        else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5)))
        {
            uint16_t uc = (puch[2] & 0x3f)
                    | ((uint16_t)(puch[1] & 0x3f) << 6)
                    | ((uint16_t)(uch     & 0x0f) << 12);
            *pwc++ = uc;
            puch += 3;
            cch -= 3;
        }
        else
        {
            /* generate surrogate pair */
            Assert((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4)));
            RTUNICP uc =           (puch[3] & 0x3f)
                       | ((RTUNICP)(puch[2] & 0x3f) << 6)
                       | ((RTUNICP)(puch[1] & 0x3f) << 12)
                       | ((RTUNICP)(uch     & 0x07) << 18);
            if (RT_UNLIKELY(cwc < 1))
            {
                rc = VERR_BUFFER_OVERFLOW;
                break;
            }
            cwc--;

            uc -= 0x10000;
            *pwc++ = 0xd800 | (uc >> 10);
            *pwc++ = 0xdc00 | (uc & 0x3ff);
            puch += 4;
            cch -= 4;
        }
    }

    /* done */
    *pwc = '\0';
    return rc;
}


/**
 * Recodes a valid UTF-8 string as UTF-16BE.
 *
 * Since we know the input is valid, we do *not* perform encoding or length checks.
 *
 * @returns iprt status code.
 * @param   psz     The UTF-8 string to recode. This is a valid encoding.
 * @param   cch     The number of chars (the type char, so bytes if you like) to process of the UTF-8 string.
 *                  The recoding will stop when cch or '\\0' is reached. Pass RTSTR_MAX to process up to '\\0'.
 * @param   pwsz    Where to store the UTF-16BE string.
 * @param   cwc     The number of RTUTF16 items the pwsz buffer can hold, excluding the terminator ('\\0').
 *
 * @note    This is a copy of rtUtf8RecodeAsUtf16 with RT_H2BE_U16 applied.
 */
static int rtUtf8RecodeAsUtf16Big(const char *psz, size_t cch, PRTUTF16 pwsz, size_t cwc)
{
    int                     rc   = VINF_SUCCESS;
    const unsigned char    *puch = (const unsigned char *)psz;
    PRTUTF16                pwc  = pwsz;
    while (cch > 0)
    {
        /* read the next char and check for terminator. */
        const unsigned char uch = *puch;
        if (uch)
        { /* we only break once, so consider this the likely branch. */ }
        else
            break;

        /* check for output overflow */
        if (RT_LIKELY(cwc >= 1))
        { /* likely */ }
        else
        {
            rc = VERR_BUFFER_OVERFLOW;
            break;
        }
        cwc--;

        /* decode and recode the code point */
        if (!(uch & RT_BIT(7)))
        {
            *pwc++ = RT_H2BE_U16((RTUTF16)uch);
            puch++;
            cch--;
        }
        else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5))) == (RT_BIT(7) | RT_BIT(6)))
        {
            uint16_t uc = (puch[1] & 0x3f)
                    | ((uint16_t)(uch     & 0x1f) << 6);
            *pwc++ = RT_H2BE_U16(uc);
            puch += 2;
            cch -= 2;
        }
        else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5)))
        {
            uint16_t uc = (puch[2] & 0x3f)
                    | ((uint16_t)(puch[1] & 0x3f) << 6)
                    | ((uint16_t)(uch     & 0x0f) << 12);
            *pwc++ = RT_H2BE_U16(uc);
            puch += 3;
            cch -= 3;
        }
        else
        {
            /* generate surrogate pair */
            Assert((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4)));
            RTUNICP uc =           (puch[3] & 0x3f)
                       | ((RTUNICP)(puch[2] & 0x3f) << 6)
                       | ((RTUNICP)(puch[1] & 0x3f) << 12)
                       | ((RTUNICP)(uch     & 0x07) << 18);
            if (RT_UNLIKELY(cwc < 1))
            {
                rc = VERR_BUFFER_OVERFLOW;
                break;
            }
            cwc--;

            uc -= 0x10000;
            *pwc++ = RT_H2BE_U16(0xd800 | (uc >> 10));
            *pwc++ = RT_H2BE_U16(0xdc00 | (uc & 0x3ff));
            puch += 4;
            cch -= 4;
        }
    }

    /* done */
    *pwc = '\0';
    return rc;
}


RTDECL(int) RTStrToUtf16Tag(const char *pszString, PRTUTF16 *ppwszString, const char *pszTag)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(ppwszString));
    Assert(VALID_PTR(pszString));
    *ppwszString = NULL;

    /*
     * Validate the UTF-8 input and calculate the length of the UTF-16 string.
     */
    size_t cwc;
    int rc = rtUtf8CalcUtf16Length(pszString, &cwc);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer.
         */
        PRTUTF16 pwsz = (PRTUTF16)RTMemAllocTag((cwc + 1) * sizeof(RTUTF16), pszTag);
        if (pwsz)
        {
            /*
             * Encode the UTF-16 string.
             */
            rc = rtUtf8RecodeAsUtf16(pszString, RTSTR_MAX, pwsz, cwc);
            if (RT_SUCCESS(rc))
            {
                *ppwszString = pwsz;
                return rc;
            }
            RTMemFree(pwsz);
        }
        else
            rc = VERR_NO_UTF16_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUtf16Tag);


RTDECL(int) RTStrToUtf16BigTag(const char *pszString, PRTUTF16 *ppwszString, const char *pszTag)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(ppwszString));
    Assert(VALID_PTR(pszString));
    *ppwszString = NULL;

    /*
     * Validate the UTF-8 input and calculate the length of the UTF-16 string.
     */
    size_t cwc;
    int rc = rtUtf8CalcUtf16Length(pszString, &cwc);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer.
         */
        PRTUTF16 pwsz = (PRTUTF16)RTMemAllocTag((cwc + 1) * sizeof(RTUTF16), pszTag);
        if (pwsz)
        {
            /*
             * Encode the UTF-16 string.
             */
            rc = rtUtf8RecodeAsUtf16Big(pszString, RTSTR_MAX, pwsz, cwc);
            if (RT_SUCCESS(rc))
            {
                *ppwszString = pwsz;
                return rc;
            }
            RTMemFree(pwsz);
        }
        else
            rc = VERR_NO_UTF16_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUtf16BigTag);


RTDECL(int)  RTStrToUtf16ExTag(const char *pszString, size_t cchString,
                               PRTUTF16 *ppwsz, size_t cwc, size_t *pcwc, const char *pszTag)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(pszString));
    Assert(VALID_PTR(ppwsz));
    Assert(!pcwc || VALID_PTR(pcwc));

    /*
     * Validate the UTF-8 input and calculate the length of the UTF-16 string.
     */
    size_t cwcResult;
    int rc;
    if (cchString != RTSTR_MAX)
        rc = rtUtf8CalcUtf16LengthN(pszString, cchString, &cwcResult);
    else
        rc = rtUtf8CalcUtf16Length(pszString, &cwcResult);
    if (RT_SUCCESS(rc))
    {
        if (pcwc)
            *pcwc = cwcResult;

        /*
         * Check buffer size / Allocate buffer.
         */
        bool fShouldFree;
        PRTUTF16 pwszResult;
        if (cwc > 0 && *ppwsz)
        {
            fShouldFree = false;
            if (cwc <= cwcResult)
                return VERR_BUFFER_OVERFLOW;
            pwszResult = *ppwsz;
        }
        else
        {
            *ppwsz = NULL;
            fShouldFree = true;
            cwc = RT_MAX(cwcResult + 1, cwc);
            pwszResult = (PRTUTF16)RTMemAllocTag(cwc * sizeof(RTUTF16), pszTag);
        }
        if (pwszResult)
        {
            /*
             * Encode the UTF-16 string.
             */
            rc = rtUtf8RecodeAsUtf16(pszString, cchString, pwszResult, cwc - 1);
            if (RT_SUCCESS(rc))
            {
                *ppwsz = pwszResult;
                return rc;
            }
            if (fShouldFree)
                RTMemFree(pwszResult);
        }
        else
            rc = VERR_NO_UTF16_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUtf16ExTag);


RTDECL(int)  RTStrToUtf16BigExTag(const char *pszString, size_t cchString,
                                  PRTUTF16 *ppwsz, size_t cwc, size_t *pcwc, const char *pszTag)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(pszString));
    Assert(VALID_PTR(ppwsz));
    Assert(!pcwc || VALID_PTR(pcwc));

    /*
     * Validate the UTF-8 input and calculate the length of the UTF-16 string.
     */
    size_t cwcResult;
    int rc;
    if (cchString != RTSTR_MAX)
        rc = rtUtf8CalcUtf16LengthN(pszString, cchString, &cwcResult);
    else
        rc = rtUtf8CalcUtf16Length(pszString, &cwcResult);
    if (RT_SUCCESS(rc))
    {
        if (pcwc)
            *pcwc = cwcResult;

        /*
         * Check buffer size / Allocate buffer.
         */
        bool fShouldFree;
        PRTUTF16 pwszResult;
        if (cwc > 0 && *ppwsz)
        {
            fShouldFree = false;
            if (cwc <= cwcResult)
                return VERR_BUFFER_OVERFLOW;
            pwszResult = *ppwsz;
        }
        else
        {
            *ppwsz = NULL;
            fShouldFree = true;
            cwc = RT_MAX(cwcResult + 1, cwc);
            pwszResult = (PRTUTF16)RTMemAllocTag(cwc * sizeof(RTUTF16), pszTag);
        }
        if (pwszResult)
        {
            /*
             * Encode the UTF-16BE string.
             */
            rc = rtUtf8RecodeAsUtf16Big(pszString, cchString, pwszResult, cwc - 1);
            if (RT_SUCCESS(rc))
            {
                *ppwsz = pwszResult;
                return rc;
            }
            if (fShouldFree)
                RTMemFree(pwszResult);
        }
        else
            rc = VERR_NO_UTF16_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUtf16BigExTag);


RTDECL(size_t) RTStrCalcUtf16Len(const char *psz)
{
    size_t cwc;
    int rc = rtUtf8CalcUtf16Length(psz, &cwc);
    return RT_SUCCESS(rc) ? cwc : 0;
}
RT_EXPORT_SYMBOL(RTStrCalcUtf16Len);


RTDECL(int) RTStrCalcUtf16LenEx(const char *psz, size_t cch, size_t *pcwc)
{
    size_t cwc;
    int rc;
    if (cch != RTSTR_MAX)
        rc = rtUtf8CalcUtf16LengthN(psz, cch, &cwc);
    else
        rc = rtUtf8CalcUtf16Length(psz, &cwc);
    if (pcwc)
        *pcwc = RT_SUCCESS(rc) ? cwc : ~(size_t)0;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrCalcUtf16LenEx);


/**
 * Calculates the length of the UTF-8 encoding of a Latin-1 string.
 *
 * @returns iprt status code.
 * @param   psz         The Latin-1 string.
 * @param   cchIn       The max length of the Latin-1 string to consider.
 * @param   pcch        Where to store the length (excluding '\\0') of the UTF-8 string. (cch == cb, btw)
 */
static int rtLatin1CalcUtf8Length(const char *psz, size_t cchIn, size_t *pcch)
{
    size_t  cch = 0;
    for (;;)
    {
        RTUNICP Cp;
        int rc = RTLatin1GetCpNEx(&psz, &cchIn, &Cp);
        if (Cp == 0 || rc == VERR_END_OF_STRING)
            break;
        if (RT_FAILURE(rc))
            return rc;
        cch += RTStrCpSize(Cp); /* cannot fail */
    }

    /* done */
    *pcch = cch;
    return VINF_SUCCESS;
}


/**
 * Recodes a Latin-1 string as UTF-8.
 *
 * @returns iprt status code.
 * @param   pszIn       The Latin-1 string.
 * @param   cchIn       The number of characters to process from psz. The recoding
 *                      will stop when cch or '\\0' is reached.
 * @param   psz         Where to store the UTF-8 string.
 * @param   cch         The size of the UTF-8 buffer, excluding the terminator.
 */
static int rtLatin1RecodeAsUtf8(const char *pszIn, size_t cchIn, char *psz, size_t cch)
{
    int rc;
    for (;;)
    {
        RTUNICP Cp;
        size_t cchCp;
        rc = RTLatin1GetCpNEx(&pszIn, &cchIn, &Cp);
        if (Cp == 0 || RT_FAILURE(rc))
            break;
        cchCp = RTStrCpSize(Cp);
        if (RT_UNLIKELY(cch < cchCp))
        {
            RTStrAssertMsgFailed(("Buffer overflow! 1\n"));
            rc = VERR_BUFFER_OVERFLOW;
            break;
        }
        cch -= cchCp;
        psz = RTStrPutCp(psz, Cp);
    }

    /* done */
    if (rc == VERR_END_OF_STRING)
        rc = VINF_SUCCESS;
    *psz = '\0';
    return rc;
}



RTDECL(int)  RTLatin1ToUtf8Tag(const char *pszString, char **ppszString, const char *pszTag)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(ppszString));
    Assert(VALID_PTR(pszString));
    *ppszString = NULL;

    /*
     * Calculate the length of the UTF-8 encoding of the Latin-1 string.
     */
    size_t cch;
    int rc = rtLatin1CalcUtf8Length(pszString, RTSTR_MAX, &cch);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer and recode it.
         */
        char *pszResult = (char *)RTMemAllocTag(cch + 1, pszTag);
        if (pszResult)
        {
            rc = rtLatin1RecodeAsUtf8(pszString, RTSTR_MAX, pszResult, cch);
            if (RT_SUCCESS(rc))
            {
                *ppszString = pszResult;
                return rc;
            }

            RTMemFree(pszResult);
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTLatin1ToUtf8Tag);


RTDECL(int)  RTLatin1ToUtf8ExTag(const char *pszString, size_t cchString, char **ppsz, size_t cch, size_t *pcch, const char *pszTag)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(pszString));
    Assert(VALID_PTR(ppsz));
    Assert(!pcch || VALID_PTR(pcch));

    /*
     * Calculate the length of the UTF-8 encoding of the Latin-1 string.
     */
    size_t cchResult;
    int rc = rtLatin1CalcUtf8Length(pszString, cchString, &cchResult);
    if (RT_SUCCESS(rc))
    {
        if (pcch)
            *pcch = cchResult;

        /*
         * Check buffer size / Allocate buffer and recode it.
         */
        bool fShouldFree;
        char *pszResult;
        if (cch > 0 && *ppsz)
        {
            fShouldFree = false;
            if (RT_UNLIKELY(cch <= cchResult))
                return VERR_BUFFER_OVERFLOW;
            pszResult = *ppsz;
        }
        else
        {
            *ppsz = NULL;
            fShouldFree = true;
            cch = RT_MAX(cch, cchResult + 1);
            pszResult = (char *)RTStrAllocTag(cch, pszTag);
        }
        if (pszResult)
        {
            rc = rtLatin1RecodeAsUtf8(pszString, cchString, pszResult, cch - 1);
            if (RT_SUCCESS(rc))
            {
                *ppsz = pszResult;
                return rc;
            }

            if (fShouldFree)
                RTStrFree(pszResult);
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTLatin1ToUtf8ExTag);


RTDECL(size_t) RTLatin1CalcUtf8Len(const char *psz)
{
    size_t cch;
    int rc = rtLatin1CalcUtf8Length(psz, RTSTR_MAX, &cch);
    return RT_SUCCESS(rc) ? cch : 0;
}
RT_EXPORT_SYMBOL(RTLatin1CalcUtf8Len);


RTDECL(int) RTLatin1CalcUtf8LenEx(const char *psz, size_t cchIn, size_t *pcch)
{
    size_t cch;
    int rc = rtLatin1CalcUtf8Length(psz, cchIn, &cch);
    if (pcch)
        *pcch = RT_SUCCESS(rc) ? cch : ~(size_t)0;
    return rc;
}
RT_EXPORT_SYMBOL(RTLatin1CalcUtf8LenEx);


/**
 * Calculates the Latin-1 length of a string, validating the encoding while
 * doing so.
 *
 * @returns IPRT status code.
 * @param   psz     Pointer to the UTF-8 string.
 * @param   cchIn   The max length of the string. (btw cch = cb)
 *                  Use RTSTR_MAX if all of the string is to be examined.
 * @param   pcch    Where to store the length of the Latin-1 string in bytes.
 */
static int rtUtf8CalcLatin1Length(const char *psz, size_t cchIn, size_t *pcch)
{
    size_t  cch = 0;
    for (;;)
    {
        RTUNICP Cp;
        size_t cchCp;
        int rc = RTStrGetCpNEx(&psz, &cchIn, &Cp);
        if (Cp == 0 || rc == VERR_END_OF_STRING)
            break;
        if (RT_FAILURE(rc))
            return rc;
        cchCp = RTLatin1CpSize(Cp);
        if (cchCp == 0)
            return VERR_NO_TRANSLATION;
        cch += cchCp;
    }

    /* done */
    *pcch = cch;
    return VINF_SUCCESS;
}


/**
 * Recodes a valid UTF-8 string as Latin-1.
 *
 * Since we know the input is valid, we do *not* perform encoding or length checks.
 *
 * @returns iprt status code.
 * @param   pszIn   The UTF-8 string to recode. This is a valid encoding.
 * @param   cchIn   The number of chars (the type char, so bytes if you like) to process of the UTF-8 string.
 *                  The recoding will stop when cch or '\\0' is reached. Pass RTSTR_MAX to process up to '\\0'.
 * @param   psz     Where to store the Latin-1 string.
 * @param   cch     The number of characters the pszOut buffer can hold, excluding the terminator ('\\0').
 */
static int rtUtf8RecodeAsLatin1(const char *pszIn, size_t cchIn, char *psz, size_t cch)
{
    int rc;
    for (;;)
    {
        RTUNICP Cp;
        size_t cchCp;
        rc = RTStrGetCpNEx(&pszIn, &cchIn, &Cp);
        if (Cp == 0 || RT_FAILURE(rc))
            break;
        cchCp = RTLatin1CpSize(Cp);
        if (RT_UNLIKELY(cch < cchCp))
        {
            RTStrAssertMsgFailed(("Buffer overflow! 1\n"));
            rc = VERR_BUFFER_OVERFLOW;
            break;
        }
        cch -= cchCp;
        psz = RTLatin1PutCp(psz, Cp);
    }

    /* done */
    if (rc == VERR_END_OF_STRING)
        rc = VINF_SUCCESS;
    *psz = '\0';
    return rc;
}



RTDECL(int) RTStrToLatin1Tag(const char *pszString, char **ppszString, const char *pszTag)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(ppszString));
    Assert(VALID_PTR(pszString));
    *ppszString = NULL;

    /*
     * Validate the UTF-8 input and calculate the length of the Latin-1 string.
     */
    size_t cch;
    int rc = rtUtf8CalcLatin1Length(pszString, RTSTR_MAX, &cch);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer.
         */
        char *psz = (char *)RTMemAllocTag(cch + 1, pszTag);
        if (psz)
        {
            /*
             * Encode the UTF-16 string.
             */
            rc = rtUtf8RecodeAsLatin1(pszString, RTSTR_MAX, psz, cch);
            if (RT_SUCCESS(rc))
            {
                *ppszString = psz;
                return rc;
            }
            RTMemFree(psz);
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToLatin1Tag);


RTDECL(int)  RTStrToLatin1ExTag(const char *pszString, size_t cchString,
                                char **ppsz, size_t cch, size_t *pcch, const char *pszTag)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(pszString));
    Assert(VALID_PTR(ppsz));
    Assert(!pcch || VALID_PTR(pcch));

    /*
     * Validate the UTF-8 input and calculate the length of the UTF-16 string.
     */
    size_t cchResult;
    int rc = rtUtf8CalcLatin1Length(pszString, cchString, &cchResult);
    if (RT_SUCCESS(rc))
    {
        if (pcch)
            *pcch = cchResult;

        /*
         * Check buffer size / Allocate buffer.
         */
        bool fShouldFree;
        char *pszResult;
        if (cch > 0 && *ppsz)
        {
            fShouldFree = false;
            if (cch <= cchResult)
                return VERR_BUFFER_OVERFLOW;
            pszResult = *ppsz;
        }
        else
        {
            *ppsz = NULL;
            fShouldFree = true;
            cch = RT_MAX(cchResult + 1, cch);
            pszResult = (char *)RTMemAllocTag(cch, pszTag);
        }
        if (pszResult)
        {
            /*
             * Encode the Latin-1 string.
             */
            rc = rtUtf8RecodeAsLatin1(pszString, cchString, pszResult, cch - 1);
            if (RT_SUCCESS(rc))
            {
                *ppsz = pszResult;
                return rc;
            }
            if (fShouldFree)
                RTMemFree(pszResult);
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToLatin1ExTag);


RTDECL(size_t) RTStrCalcLatin1Len(const char *psz)
{
    size_t cch;
    int rc = rtUtf8CalcLatin1Length(psz, RTSTR_MAX, &cch);
    return RT_SUCCESS(rc) ? cch : 0;
}
RT_EXPORT_SYMBOL(RTStrCalcLatin1Len);


RTDECL(int) RTStrCalcLatin1LenEx(const char *psz, size_t cchIn, size_t *pcch)
{
    size_t cch;
    int rc = rtUtf8CalcLatin1Length(psz, cchIn, &cch);
    if (pcch)
        *pcch = RT_SUCCESS(rc) ? cch : ~(size_t)0;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrCalcLatin1LenEx);


/**
 * Handle invalid encodings passed to RTStrGetCp() and RTStrGetCpEx().
 * @returns rc
 * @param   ppsz        The pointer to the string position point.
 * @param   pCp         Where to store RTUNICP_INVALID.
 * @param   rc          The iprt error code.
 */
static int rtStrGetCpExFailure(const char **ppsz, PRTUNICP pCp, int rc)
{
    /*
     * Try find a valid encoding.
     */
    (*ppsz)++; /** @todo code this! */
    *pCp = RTUNICP_INVALID;
    return rc;
}


RTDECL(RTUNICP) RTStrGetCpInternal(const char *psz)
{
    RTUNICP Cp;
    RTStrGetCpExInternal(&psz, &Cp);
    return Cp;
}
RT_EXPORT_SYMBOL(RTStrGetCpInternal);


RTDECL(int) RTStrGetCpExInternal(const char **ppsz, PRTUNICP pCp)
{
    const unsigned char *puch = (const unsigned char *)*ppsz;
    const unsigned char uch = *puch;
    RTUNICP             uc;

    /* ASCII ? */
    if (!(uch & RT_BIT(7)))
    {
        uc = uch;
        puch++;
    }
    else if (uch & RT_BIT(6))
    {
        /* figure the length and validate the first octet. */
/** @todo RT_USE_RTC_3629 */
        unsigned cb;
        if (!(uch & RT_BIT(5)))
            cb = 2;
        else if (!(uch & RT_BIT(4)))
            cb = 3;
        else if (!(uch & RT_BIT(3)))
            cb = 4;
        else if (!(uch & RT_BIT(2)))
            cb = 5;
        else if (!(uch & RT_BIT(1)))
            cb = 6;
        else
        {
            RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(strlen((char *)puch), 10), puch));
            return rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING);
        }

        /* validate the rest */
        switch (cb)
        {
            case 6:
                RTStrAssertMsgReturn((puch[5] & 0xc0) == 0x80, ("6/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                RT_FALL_THRU();
            case 5:
                RTStrAssertMsgReturn((puch[4] & 0xc0) == 0x80, ("5/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                RT_FALL_THRU();
            case 4:
                RTStrAssertMsgReturn((puch[3] & 0xc0) == 0x80, ("4/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                RT_FALL_THRU();
            case 3:
                RTStrAssertMsgReturn((puch[2] & 0xc0) == 0x80, ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                RT_FALL_THRU();
            case 2:
                RTStrAssertMsgReturn((puch[1] & 0xc0) == 0x80, ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
               break;
        }

        /* get and validate the code point. */
        switch (cb)
        {
            case 6:
                uc =            (puch[5] & 0x3f)
                    | ((RTUNICP)(puch[4] & 0x3f) << 6)
                    | ((RTUNICP)(puch[3] & 0x3f) << 12)
                    | ((RTUNICP)(puch[2] & 0x3f) << 18)
                    | ((RTUNICP)(puch[1] & 0x3f) << 24)
                    | ((RTUNICP)(uch     & 0x01) << 30);
                RTStrAssertMsgReturn(uc >= 0x04000000 && uc <= 0x7fffffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 5:
                uc =            (puch[4] & 0x3f)
                    | ((RTUNICP)(puch[3] & 0x3f) << 6)
                    | ((RTUNICP)(puch[2] & 0x3f) << 12)
                    | ((RTUNICP)(puch[1] & 0x3f) << 18)
                    | ((RTUNICP)(uch     & 0x03) << 24);
                RTStrAssertMsgReturn(uc >= 0x00200000 && uc <= 0x03ffffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 4:
                uc =            (puch[3] & 0x3f)
                    | ((RTUNICP)(puch[2] & 0x3f) << 6)
                    | ((RTUNICP)(puch[1] & 0x3f) << 12)
                    | ((RTUNICP)(uch     & 0x07) << 18);
                RTStrAssertMsgReturn(uc >= 0x00010000 && uc <= 0x001fffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 3:
                uc =            (puch[2] & 0x3f)
                    | ((RTUNICP)(puch[1] & 0x3f) << 6)
                    | ((RTUNICP)(uch     & 0x0f) << 12);
                RTStrAssertMsgReturn(uc >= 0x00000800 && uc <= 0x0000fffd,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, uc == 0xffff || uc == 0xfffe ? VERR_CODE_POINT_ENDIAN_INDICATOR : VERR_INVALID_UTF8_ENCODING));
                RTStrAssertMsgReturn(uc < 0xd800 || uc > 0xdfff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_CODE_POINT_SURROGATE));
                break;
            case 2:
                uc =            (puch[1] & 0x3f)
                    | ((RTUNICP)(uch     & 0x1f) << 6);
                RTStrAssertMsgReturn(uc >= 0x00000080 && uc <= 0x000007ff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            default: /* impossible, but GCC is bitching. */
                uc = RTUNICP_INVALID;
                break;
        }
        puch += cb;
    }
    else
    {
        /* 6th bit is always set. */
        RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(strlen((char *)puch), 10), puch));
        return rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING);
    }
    *pCp = uc;
    *ppsz = (const char *)puch;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTStrGetCpExInternal);


/**
 * Handle invalid encodings passed to RTStrGetCpNEx().
 * @returns rc
 * @param   ppsz        The pointer to the string position point.
 * @param   pcch        Pointer to the string length.
 * @param   pCp         Where to store RTUNICP_INVALID.
 * @param   rc          The iprt error code.
 */
static int rtStrGetCpNExFailure(const char **ppsz, size_t *pcch, PRTUNICP pCp, int rc)
{
    /*
     * Try find a valid encoding.
     */
    (*ppsz)++; /** @todo code this! */
    (*pcch)--;
    *pCp = RTUNICP_INVALID;
    return rc;
}


RTDECL(int) RTStrGetCpNExInternal(const char **ppsz, size_t *pcch, PRTUNICP pCp)
{
    const unsigned char *puch = (const unsigned char *)*ppsz;
    const unsigned char uch = *puch;
    size_t              cch = *pcch;
    RTUNICP             uc;

    if (cch == 0)
    {
        *pCp = RTUNICP_INVALID;
        return VERR_END_OF_STRING;
    }

    /* ASCII ? */
    if (!(uch & RT_BIT(7)))
    {
        uc = uch;
        puch++;
        cch--;
    }
    else if (uch & RT_BIT(6))
    {
        /* figure the length and validate the first octet. */
/** @todo RT_USE_RTC_3629 */
        unsigned cb;
        if (!(uch & RT_BIT(5)))
            cb = 2;
        else if (!(uch & RT_BIT(4)))
            cb = 3;
        else if (!(uch & RT_BIT(3)))
            cb = 4;
        else if (!(uch & RT_BIT(2)))
            cb = 5;
        else if (!(uch & RT_BIT(1)))
            cb = 6;
        else
        {
            RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(strlen((char *)puch), 10), puch));
            return rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING);
        }

        if (cb > cch)
            return rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING);

        /* validate the rest */
        switch (cb)
        {
            case 6:
                RTStrAssertMsgReturn((puch[5] & 0xc0) == 0x80, ("6/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                RT_FALL_THRU();
            case 5:
                RTStrAssertMsgReturn((puch[4] & 0xc0) == 0x80, ("5/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                RT_FALL_THRU();
            case 4:
                RTStrAssertMsgReturn((puch[3] & 0xc0) == 0x80, ("4/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                RT_FALL_THRU();
            case 3:
                RTStrAssertMsgReturn((puch[2] & 0xc0) == 0x80, ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                RT_FALL_THRU();
            case 2:
                RTStrAssertMsgReturn((puch[1] & 0xc0) == 0x80, ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
               break;
        }

        /* get and validate the code point. */
        switch (cb)
        {
            case 6:
                uc =            (puch[5] & 0x3f)
                    | ((RTUNICP)(puch[4] & 0x3f) << 6)
                    | ((RTUNICP)(puch[3] & 0x3f) << 12)
                    | ((RTUNICP)(puch[2] & 0x3f) << 18)
                    | ((RTUNICP)(puch[1] & 0x3f) << 24)
                    | ((RTUNICP)(uch     & 0x01) << 30);
                RTStrAssertMsgReturn(uc >= 0x04000000 && uc <= 0x7fffffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 5:
                uc =            (puch[4] & 0x3f)
                    | ((RTUNICP)(puch[3] & 0x3f) << 6)
                    | ((RTUNICP)(puch[2] & 0x3f) << 12)
                    | ((RTUNICP)(puch[1] & 0x3f) << 18)
                    | ((RTUNICP)(uch     & 0x03) << 24);
                RTStrAssertMsgReturn(uc >= 0x00200000 && uc <= 0x03ffffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 4:
                uc =            (puch[3] & 0x3f)
                    | ((RTUNICP)(puch[2] & 0x3f) << 6)
                    | ((RTUNICP)(puch[1] & 0x3f) << 12)
                    | ((RTUNICP)(uch     & 0x07) << 18);
                RTStrAssertMsgReturn(uc >= 0x00010000 && uc <= 0x001fffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 3:
                uc =            (puch[2] & 0x3f)
                    | ((RTUNICP)(puch[1] & 0x3f) << 6)
                    | ((RTUNICP)(uch     & 0x0f) << 12);
                RTStrAssertMsgReturn(uc >= 0x00000800 && uc <= 0x0000fffd,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, uc == 0xffff || uc == 0xfffe ? VERR_CODE_POINT_ENDIAN_INDICATOR : VERR_INVALID_UTF8_ENCODING));
                RTStrAssertMsgReturn(uc < 0xd800 || uc > 0xdfff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_CODE_POINT_SURROGATE));
                break;
            case 2:
                uc =            (puch[1] & 0x3f)
                    | ((RTUNICP)(uch     & 0x1f) << 6);
                RTStrAssertMsgReturn(uc >= 0x00000080 && uc <= 0x000007ff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            default: /* impossible, but GCC is bitching. */
                uc = RTUNICP_INVALID;
                break;
        }
        puch += cb;
        cch  -= cb;
    }
    else
    {
        /* 6th bit is always set. */
        RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(strlen((char *)puch), 10), puch));
        return rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING);
    }
    *pCp = uc;
    *ppsz = (const char *)puch;
    (*pcch) = cch;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTStrGetCpNExInternal);


RTDECL(char *) RTStrPutCpInternal(char *psz, RTUNICP uc)
{
    unsigned char *puch = (unsigned char *)psz;
    if (uc < 0x80)
        *puch++ = (unsigned char )uc;
    else if (uc < 0x00000800)
    {
        *puch++ = 0xc0 | (uc >> 6);
        *puch++ = 0x80 | (uc & 0x3f);
    }
    else if (uc < 0x00010000)
    {
/** @todo RT_USE_RTC_3629 */
        if (   uc < 0x0000d8000
             || (   uc > 0x0000dfff
                 && uc < 0x0000fffe))
        {
            *puch++ = 0xe0 | (uc >> 12);
            *puch++ = 0x80 | ((uc >> 6) & 0x3f);
            *puch++ = 0x80 | (uc & 0x3f);
        }
        else
        {
            AssertMsgFailed(("Invalid code point U+%05x!\n", uc));
            *puch++ = 0x7f;
        }
    }
/** @todo RT_USE_RTC_3629 */
    else if (uc < 0x00200000)
    {
        *puch++ = 0xf0 | (uc >> 18);
        *puch++ = 0x80 | ((uc >> 12) & 0x3f);
        *puch++ = 0x80 | ((uc >> 6) & 0x3f);
        *puch++ = 0x80 | (uc & 0x3f);
    }
    else if (uc < 0x04000000)
    {
        *puch++ = 0xf8 | (uc >> 24);
        *puch++ = 0x80 | ((uc >> 18) & 0x3f);
        *puch++ = 0x80 | ((uc >> 12) & 0x3f);
        *puch++ = 0x80 | ((uc >> 6) & 0x3f);
        *puch++ = 0x80 | (uc & 0x3f);
    }
    else if (uc <= 0x7fffffff)
    {
        *puch++ = 0xfc | (uc >> 30);
        *puch++ = 0x80 | ((uc >> 24) & 0x3f);
        *puch++ = 0x80 | ((uc >> 18) & 0x3f);
        *puch++ = 0x80 | ((uc >> 12) & 0x3f);
        *puch++ = 0x80 | ((uc >> 6) & 0x3f);
        *puch++ = 0x80 | (uc & 0x3f);
    }
    else
    {
        AssertMsgFailed(("Invalid code point U+%08x!\n", uc));
        *puch++ = 0x7f;
    }

    return (char *)puch;
}
RT_EXPORT_SYMBOL(RTStrPutCpInternal);


RTDECL(char *) RTStrPrevCp(const char *pszStart, const char *psz)
{
    if (pszStart < psz)
    {
        /* simple char? */
        const unsigned char *puch = (const unsigned char *)psz;
        unsigned uch = *--puch;
        if (!(uch & RT_BIT(7)))
            return (char *)puch;
        RTStrAssertMsgReturn(!(uch & RT_BIT(6)), ("uch=%#x\n", uch), (char *)pszStart);

        /* two or more. */
        uint32_t uMask = 0xffffffc0;
        while (     (const unsigned char *)pszStart < puch
               &&   !(uMask & 1))
        {
            uch = *--puch;
            if ((uch & 0xc0) != 0x80)
            {
                RTStrAssertMsgReturn((uch & (uMask >> 1)) == (uMask & 0xff),
                                     ("Invalid UTF-8 encoding: %.*Rhxs puch=%p psz=%p\n", psz - (char *)puch, puch, psz),
                                     (char *)pszStart);
                return (char *)puch;
            }
            uMask >>= 1;
        }
        RTStrAssertMsgFailed(("Invalid UTF-8 encoding: %.*Rhxs puch=%p psz=%p\n", psz - (char *)puch, puch, psz));
    }
    return (char *)pszStart;
}
RT_EXPORT_SYMBOL(RTStrPrevCp);

