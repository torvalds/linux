/* $Id: strtonum.cpp $ */
/** @file
 * IPRT - String To Number Conversion.
 */

/*
 * Copyright (C) 2006-2017 Oracle Corporation
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
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/ctype.h> /* needed for RT_C_IS_DIGIT */
#include <iprt/err.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** 8-bit char -> digit. */
static const unsigned char g_auchDigits[256] =
{
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,255,255,255,255,255,255,
    255, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,255,255,255,255,255,
    255, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
};
/** Approximated overflow shift checks. */
static const char g_auchShift[36] =
{
  /*  0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35 */
     64, 64, 63, 63, 62, 62, 62, 62, 61, 61, 61, 61, 61, 61, 61, 61, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 59, 59, 59, 59
};

/*
#include <stdio.h>
int main()
{
    int i;
    printf("static const unsigned char g_auchDigits[256] =\n"
           "{");
    for (i = 0; i < 256; i++)
    {
        int ch = 255;
        if (i >= '0' && i <= '9')
            ch = i - '0';
        else if (i >= 'a' && i <= 'z')
            ch = i - 'a' + 10;
        else if (i >= 'A' && i <= 'Z')
            ch = i - 'A' + 10;
        if (i == 0)
            printf("\n    %3d", ch);
        else if ((i % 32) == 0)
            printf(",\n    %3d", ch);
        else
            printf(",%3d", ch);
    }
    printf("\n"
           "};\n");
    return 0;
}
*/


/**
 * Converts a string representation of a number to a 64-bit unsigned number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pu64        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt64Ex(const char *pszValue, char **ppszNext, unsigned uBase, uint64_t *pu64)
{
    const char   *psz = pszValue;
    int           iShift;
    int           rc;
    uint64_t      u64;
    unsigned char uch;

    /*
     * Positive/Negative stuff.
     */
    bool fPositive = true;
    for (;; psz++)
    {
        if (*psz == '+')
            fPositive = true;
        else if (*psz == '-')
            fPositive = !fPositive;
        else
            break;
    }

    /*
     * Check for hex prefix.
     */
    if (!uBase)
    {
        if (    psz[0] == '0'
            &&  (psz[1] == 'x' || psz[1] == 'X')
            &&  g_auchDigits[(unsigned char)psz[2]] < 16)
        {
            uBase = 16;
            psz += 2;
        }
        else if (   psz[0] == '0'
                 && g_auchDigits[(unsigned char)psz[1]] < 8)
        {
            uBase = 8;
            psz++;
        }
        else
            uBase = 10;
    }
    else if (   uBase == 16
             && psz[0] == '0'
             && (psz[1] == 'x' || psz[1] == 'X')
             && g_auchDigits[(unsigned char)psz[2]] < 16)
        psz += 2;

    /*
     * Interpret the value.
     * Note: We only support ascii digits at this time... :-)
     */
    iShift = g_auchShift[uBase];
    pszValue = psz; /* (Prefix and sign doesn't count in the digit counting.) */
    rc = VINF_SUCCESS;
    u64 = 0;
    while ((uch = (unsigned char)*psz) != 0)
    {
        unsigned char chDigit = g_auchDigits[uch];
        uint64_t u64Prev;

        if (chDigit >= uBase)
            break;

        u64Prev = u64;
        u64 *= uBase;
        u64 += chDigit;
        if (u64Prev > u64 || (u64Prev >> iShift))
            rc = VWRN_NUMBER_TOO_BIG;
        psz++;
    }

    if (!fPositive)
    {
        if (rc == VINF_SUCCESS)
            rc = VWRN_NEGATIVE_UNSIGNED;
        u64 = -(int64_t)u64;
    }

    if (pu64)
        *pu64 = u64;

    if (psz == pszValue)
        rc = VERR_NO_DIGITS;

    if (ppszNext)
        *ppszNext = (char *)psz;

    /*
     * Warn about trailing chars/spaces.
     */
    if (    rc == VINF_SUCCESS
        &&  *psz)
    {
        while (*psz == ' ' || *psz == '\t')
            psz++;
        rc = *psz ? VWRN_TRAILING_CHARS : VWRN_TRAILING_SPACES;
    }

    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt64Ex);


/**
 * Converts a string representation of a number to a 64-bit unsigned number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_TRAILING_CHARS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pu64        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt64Full(const char *pszValue, unsigned uBase, uint64_t *pu64)
{
    char *psz;
    int rc = RTStrToUInt64Ex(pszValue, &psz, uBase, pu64);
    if (RT_SUCCESS(rc) && *psz)
    {
        if (rc == VWRN_TRAILING_CHARS || rc == VWRN_TRAILING_SPACES)
            rc = -rc;
        else
        {
            while (*psz == ' ' || *psz == '\t')
                psz++;
            rc = *psz ? VERR_TRAILING_CHARS : VERR_TRAILING_SPACES;
        }
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt64Full);


/**
 * Converts a string representation of a number to a 64-bit unsigned number.
 * The base is guessed.
 *
 * @returns 64-bit unsigned number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(uint64_t) RTStrToUInt64(const char *pszValue)
{
    uint64_t u64;
    int rc = RTStrToUInt64Ex(pszValue, NULL, 0, &u64);
    if (RT_SUCCESS(rc))
        return u64;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToUInt64);


/**
 * Converts a string representation of a number to a 32-bit unsigned number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pu32        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt32Ex(const char *pszValue, char **ppszNext, unsigned uBase, uint32_t *pu32)
{
    uint64_t u64;
    int rc = RTStrToUInt64Ex(pszValue, ppszNext, uBase, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffffffffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu32)
        *pu32 = (uint32_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt32Ex);


/**
 * Converts a string representation of a number to a 32-bit unsigned number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_TRAILING_CHARS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pu32        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt32Full(const char *pszValue, unsigned uBase, uint32_t *pu32)
{
    uint64_t u64;
    int rc = RTStrToUInt64Full(pszValue, uBase, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffffffffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu32)
        *pu32 = (uint32_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt32Full);


/**
 * Converts a string representation of a number to a 64-bit unsigned number.
 * The base is guessed.
 *
 * @returns 32-bit unsigned number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(uint32_t) RTStrToUInt32(const char *pszValue)
{
    uint32_t u32;
    int rc = RTStrToUInt32Ex(pszValue, NULL, 0, &u32);
    if (RT_SUCCESS(rc))
        return u32;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToUInt32);


/**
 * Converts a string representation of a number to a 16-bit unsigned number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pu16        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt16Ex(const char *pszValue, char **ppszNext, unsigned uBase, uint16_t *pu16)
{
    uint64_t u64;
    int rc = RTStrToUInt64Ex(pszValue, ppszNext, uBase, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu16)
        *pu16 = (uint16_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt16Ex);


/**
 * Converts a string representation of a number to a 16-bit unsigned number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_TRAILING_CHARS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pu16        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt16Full(const char *pszValue, unsigned uBase, uint16_t *pu16)
{
    uint64_t u64;
    int rc = RTStrToUInt64Full(pszValue, uBase, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu16)
        *pu16 = (uint16_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt16Full);


/**
 * Converts a string representation of a number to a 16-bit unsigned number.
 * The base is guessed.
 *
 * @returns 16-bit unsigned number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(uint16_t) RTStrToUInt16(const char *pszValue)
{
    uint16_t u16;
    int rc = RTStrToUInt16Ex(pszValue, NULL, 0, &u16);
    if (RT_SUCCESS(rc))
        return u16;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToUInt16);


/**
 * Converts a string representation of a number to a 8-bit unsigned number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pu8         Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt8Ex(const char *pszValue, char **ppszNext, unsigned uBase, uint8_t *pu8)
{
    uint64_t u64;
    int rc = RTStrToUInt64Ex(pszValue, ppszNext, uBase, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu8)
        *pu8 = (uint8_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt8Ex);


/**
 * Converts a string representation of a number to a 8-bit unsigned number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_TRAILING_CHARS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pu8         Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt8Full(const char *pszValue, unsigned uBase, uint8_t *pu8)
{
    uint64_t u64;
    int rc = RTStrToUInt64Full(pszValue, uBase, &u64);
    if (RT_SUCCESS(rc))
    {
        if (u64 & ~0xffULL)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pu8)
        *pu8 = (uint8_t)u64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUInt8Full);


/**
 * Converts a string representation of a number to a 8-bit unsigned number.
 * The base is guessed.
 *
 * @returns 8-bit unsigned number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(uint8_t) RTStrToUInt8(const char *pszValue)
{
    uint8_t u8;
    int rc = RTStrToUInt8Ex(pszValue, NULL, 0, &u8);
    if (RT_SUCCESS(rc))
        return u8;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToUInt8);







/**
 * Converts a string representation of a number to a 64-bit signed number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pi64        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt64Ex(const char *pszValue, char **ppszNext, unsigned uBase, int64_t *pi64)
{
    const char   *psz = pszValue;
    int           iShift;
    int           rc;
    int64_t       i64;
    unsigned char uch;

    /*
     * Positive/Negative stuff.
     */
    bool fPositive = true;
    for (;; psz++)
    {
        if (*psz == '+')
            fPositive = true;
        else if (*psz == '-')
            fPositive = !fPositive;
        else
            break;
    }

    /*
     * Check for hex prefix.
     */
    if (!uBase)
    {
        if (    *psz == '0'
            &&  (psz[1] == 'x' || psz[1] == 'X')
            &&  g_auchDigits[(unsigned char)psz[2]] < 16)
        {
            uBase = 16;
            psz += 2;
        }
        else if (   *psz == '0'
                 && g_auchDigits[(unsigned char)psz[1]] < 8)
        {
            uBase = 8;
            psz++;
        }
        else
            uBase = 10;
    }
    else if (   uBase == 16
             && *psz == '0'
             && (psz[1] == 'x' || psz[1] == 'X')
             && g_auchDigits[(unsigned char)psz[2]] < 16)
        psz += 2;

    /*
     * Interpret the value.
     * Note: We only support ascii digits at this time... :-)
     */
    iShift = g_auchShift[uBase]; /** @todo test this, it's probably not 100% right yet. */
    pszValue = psz; /* (Prefix and sign doesn't count in the digit counting.) */
    rc = VINF_SUCCESS;
    i64 = 0;
    while ((uch = (unsigned char)*psz) != 0)
    {
        unsigned char chDigit = g_auchDigits[uch];
        int64_t i64Prev;

        if (chDigit >= uBase)
            break;

        i64Prev = i64;
        i64 *= uBase;
        i64 += chDigit;
        if (i64Prev > i64 || (i64Prev >> iShift))
            rc = VWRN_NUMBER_TOO_BIG;
        psz++;
    }

    if (!fPositive)
        i64 = -i64;

    if (pi64)
        *pi64 = i64;

    if (psz == pszValue)
        rc = VERR_NO_DIGITS;

    if (ppszNext)
        *ppszNext = (char *)psz;

    /*
     * Warn about trailing chars/spaces.
     */
    if (    rc == VINF_SUCCESS
        &&  *psz)
    {
        while (*psz == ' ' || *psz == '\t')
            psz++;
        rc = *psz ? VWRN_TRAILING_CHARS : VWRN_TRAILING_SPACES;
    }

    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt64Ex);


/**
 * Converts a string representation of a number to a 64-bit signed number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VINF_SUCCESS
 * @retval  VERR_TRAILING_CHARS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pi64        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt64Full(const char *pszValue, unsigned uBase, int64_t *pi64)
{
    char *psz;
    int rc = RTStrToInt64Ex(pszValue, &psz, uBase, pi64);
    if (RT_SUCCESS(rc) && *psz)
    {
        if (rc == VWRN_TRAILING_CHARS || rc == VWRN_TRAILING_SPACES)
            rc = -rc;
        else
        {
            while (*psz == ' ' || *psz == '\t')
                psz++;
            rc = *psz ? VERR_TRAILING_CHARS : VERR_TRAILING_SPACES;
        }
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt64Full);


/**
 * Converts a string representation of a number to a 64-bit signed number.
 * The base is guessed.
 *
 * @returns 64-bit signed number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(int64_t) RTStrToInt64(const char *pszValue)
{
    int64_t i64;
    int rc = RTStrToInt64Ex(pszValue, NULL, 0, &i64);
    if (RT_SUCCESS(rc))
        return i64;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToInt64);


/**
 * Converts a string representation of a number to a 32-bit signed number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pi32        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt32Ex(const char *pszValue, char **ppszNext, unsigned uBase, int32_t *pi32)
{
    int64_t i64;
    int rc = RTStrToInt64Ex(pszValue, ppszNext, uBase, &i64);
    if (RT_SUCCESS(rc))
    {
        int32_t i32 = (int32_t)i64;
        if (i64 != (int64_t)i32)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi32)
        *pi32 = (int32_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt32Ex);


/**
 * Converts a string representation of a number to a 32-bit signed number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VINF_SUCCESS
 * @retval  VERR_TRAILING_CHARS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pi32        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt32Full(const char *pszValue, unsigned uBase, int32_t *pi32)
{
    int64_t i64;
    int rc = RTStrToInt64Full(pszValue, uBase, &i64);
    if (RT_SUCCESS(rc))
    {
        int32_t i32 = (int32_t)i64;
        if (i64 != (int64_t)i32)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi32)
        *pi32 = (int32_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt32Full);


/**
 * Converts a string representation of a number to a 32-bit signed number.
 * The base is guessed.
 *
 * @returns 32-bit signed number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(int32_t) RTStrToInt32(const char *pszValue)
{
    int32_t i32;
    int rc = RTStrToInt32Ex(pszValue, NULL, 0, &i32);
    if (RT_SUCCESS(rc))
        return i32;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToInt32);


/**
 * Converts a string representation of a number to a 16-bit signed number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pi16        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt16Ex(const char *pszValue, char **ppszNext, unsigned uBase, int16_t *pi16)
{
    int64_t i64;
    int rc = RTStrToInt64Ex(pszValue, ppszNext, uBase, &i64);
    if (RT_SUCCESS(rc))
    {
        int16_t i16 = (int16_t)i64;
        if (i64 != (int64_t)i16)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi16)
        *pi16 = (int16_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt16Ex);


/**
 * Converts a string representation of a number to a 16-bit signed number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VINF_SUCCESS
 * @retval  VERR_TRAILING_CHARS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pi16        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt16Full(const char *pszValue, unsigned uBase, int16_t *pi16)
{
    int64_t i64;
    int rc = RTStrToInt64Full(pszValue, uBase, &i64);
    if (RT_SUCCESS(rc))
    {
        int16_t i16 = (int16_t)i64;
        if (i64 != (int64_t)i16)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi16)
        *pi16 = (int16_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt16Full);


/**
 * Converts a string representation of a number to a 16-bit signed number.
 * The base is guessed.
 *
 * @returns 16-bit signed number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(int16_t) RTStrToInt16(const char *pszValue)
{
    int16_t i16;
    int rc = RTStrToInt16Ex(pszValue, NULL, 0, &i16);
    if (RT_SUCCESS(rc))
        return i16;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToInt16);


/**
 * Converts a string representation of a number to a 8-bit signed number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pi8        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt8Ex(const char *pszValue, char **ppszNext, unsigned uBase, int8_t *pi8)
{
    int64_t i64;
    int rc = RTStrToInt64Ex(pszValue, ppszNext, uBase, &i64);
    if (RT_SUCCESS(rc))
    {
        int8_t i8 = (int8_t)i64;
        if (i64 != (int64_t)i8)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi8)
        *pi8 = (int8_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt8Ex);


/**
 * Converts a string representation of a number to a 8-bit signed number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VINF_SUCCESS
 * @retval  VERR_TRAILING_CHARS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If the function will look for known prefixes before defaulting to 10.
 * @param   pi8         Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt8Full(const char *pszValue, unsigned uBase, int8_t *pi8)
{
    int64_t i64;
    int rc = RTStrToInt64Full(pszValue, uBase, &i64);
    if (RT_SUCCESS(rc))
    {
        int8_t i8 = (int8_t)i64;
        if (i64 != (int64_t)i8)
            rc = VWRN_NUMBER_TOO_BIG;
    }
    if (pi8)
        *pi8 = (int8_t)i64;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToInt8Full);


/**
 * Converts a string representation of a number to a 8-bit signed number.
 * The base is guessed.
 *
 * @returns 8-bit signed number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(int8_t) RTStrToInt8(const char *pszValue)
{
    int8_t i8;
    int rc = RTStrToInt8Ex(pszValue, NULL, 0, &i8);
    if (RT_SUCCESS(rc))
        return i8;
    return 0;
}
RT_EXPORT_SYMBOL(RTStrToInt8);


RTDECL(int) RTStrConvertHexBytes(char const *pszHex, void *pv, size_t cb, uint32_t fFlags)
{
    size_t      cbDst;
    uint8_t    *pbDst;
    const char *pszSrc;

    AssertPtrReturn(pszHex, VERR_INVALID_POINTER);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

    cbDst  = cb;
    pbDst  = (uint8_t *)pv;
    pszSrc = pszHex;
    for (;;)
    {
        /* Pick the next two digit from the string. */
        char ch = *pszSrc++;
        unsigned char uchDigit1 = g_auchDigits[(unsigned char)ch];
        unsigned char uchDigit2;
        if (uchDigit1 >= 16)
        {
            if (!ch)
                return cbDst == 0 ? VINF_SUCCESS : VERR_BUFFER_UNDERFLOW;

            while (ch == ' ' || ch == '\t')
                ch = *pszSrc++;
            return ch ? VWRN_TRAILING_CHARS : VWRN_TRAILING_SPACES;
        }

        ch = *pszSrc++;
        uchDigit2 = g_auchDigits[(unsigned char)ch];
        if (uchDigit2 >= 16)
            return VERR_UNEVEN_INPUT;

        /* Add the byte to the output buffer. */
        if (!cbDst)
            return VERR_BUFFER_OVERFLOW;
        cbDst--;
        *pbDst++ = (uchDigit1 << 4) | uchDigit2;
    }
}
RT_EXPORT_SYMBOL(RTStrConvertHexBytes);

