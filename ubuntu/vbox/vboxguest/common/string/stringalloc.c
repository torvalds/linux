/* $Id: stringalloc.cpp $ */
/** @file
 * IPRT - String Manipulation.
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
#include "internal/iprt.h"

#ifndef IN_RING0
# include <iprt/alloca.h>
#endif
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include "internal/string.h"



RTDECL(char *) RTStrAllocTag(size_t cb, const char *pszTag)
{
    char *psz = (char *)RTMemAllocTag(RT_MAX(cb, 1), pszTag);
    if (psz)
        *psz = '\0';
    return psz;
}
RT_EXPORT_SYMBOL(RTStrAllocTag);


RTDECL(int) RTStrAllocExTag(char **ppsz, size_t cb, const char *pszTag)
{
    char *psz = *ppsz = (char *)RTMemAllocTag(RT_MAX(cb, 1), pszTag);
    if (psz)
    {
        *psz = '\0';
        return VINF_SUCCESS;
    }
    return VERR_NO_STR_MEMORY;
}
RT_EXPORT_SYMBOL(RTStrAllocExTag);


RTDECL(int) RTStrReallocTag(char **ppsz, size_t cbNew, const char *pszTag)
{
    char *pszOld = *ppsz;
    if (!cbNew)
    {
        RTMemFree(pszOld);
        *ppsz = NULL;
    }
    else if (pszOld)
    {
        char *pszNew = (char *)RTMemReallocTag(pszOld, cbNew, pszTag);
        if (!pszNew)
            return VERR_NO_STR_MEMORY;
        pszNew[cbNew - 1] = '\0';
        *ppsz = pszNew;
    }
    else
    {
        char *pszNew = (char *)RTMemAllocTag(cbNew, pszTag);
        if (!pszNew)
            return VERR_NO_STR_MEMORY;
        pszNew[0] = '\0';
        pszNew[cbNew - 1] = '\0';
        *ppsz = pszNew;
    }
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTStrReallocTag);

RTDECL(void)  RTStrFree(char *pszString)
{
    if (pszString)
        RTMemTmpFree(pszString);
}
RT_EXPORT_SYMBOL(RTStrFree);


RTDECL(char *) RTStrDupTag(const char *pszString, const char *pszTag)
{
#if defined(__cplusplus)
    AssertPtr(pszString);
#endif
    size_t cch = strlen(pszString) + 1;
    char *psz = (char *)RTMemAllocTag(cch, pszTag);
    if (psz)
        memcpy(psz, pszString, cch);
    return psz;
}
RT_EXPORT_SYMBOL(RTStrDupTag);


RTDECL(int)  RTStrDupExTag(char **ppszString, const char *pszString, const char *pszTag)
{
#if defined(__cplusplus)
    AssertPtr(ppszString);
    AssertPtr(pszString);
#endif

    size_t cch = strlen(pszString) + 1;
    char *psz = (char *)RTMemAllocTag(cch, pszTag);
    if (psz)
    {
        memcpy(psz, pszString, cch);
        *ppszString = psz;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}
RT_EXPORT_SYMBOL(RTStrDupExTag);


RTDECL(char *) RTStrDupNTag(const char *pszString, size_t cchMax, const char *pszTag)
{
#if defined(__cplusplus)
    AssertPtr(pszString);
#endif
    char const *pszEnd = RTStrEnd(pszString, cchMax);
    size_t      cch    = pszEnd ? (uintptr_t)pszEnd - (uintptr_t)pszString : cchMax;
    char       *pszDst = (char *)RTMemAllocTag(cch + 1, pszTag);
    if (pszDst)
    {
        memcpy(pszDst, pszString, cch);
        pszDst[cch] = '\0';
    }
    return pszDst;
}
RT_EXPORT_SYMBOL(RTStrDupNTag);


RTDECL(int) RTStrAAppendTag(char **ppsz, const char *pszAppend, const char *pszTag)
{
    if (!pszAppend)
        return VINF_SUCCESS;
    return RTStrAAppendNTag(ppsz, pszAppend, RTSTR_MAX, pszTag);
}


RTDECL(int) RTStrAAppendNTag(char **ppsz, const char *pszAppend, size_t cchAppend, const char *pszTag)
{
    size_t cchOrg;
    char  *pszNew;

    if (!cchAppend)
        return VINF_SUCCESS;
    if (cchAppend == RTSTR_MAX)
        cchAppend = strlen(pszAppend);
    else
        Assert(cchAppend == RTStrNLen(pszAppend, cchAppend));

    cchOrg = *ppsz ? strlen(*ppsz) : 0;
    pszNew = (char *)RTMemReallocTag(*ppsz, cchOrg + cchAppend + 1, pszTag);
    if (!pszNew)
        return VERR_NO_STR_MEMORY;

    memcpy(&pszNew[cchOrg], pszAppend, cchAppend);
    pszNew[cchOrg + cchAppend] = '\0';

    *ppsz = pszNew;
    return VINF_SUCCESS;
}


#ifndef IN_RING0

/* XXX Currently not needed anywhere. alloca() induces some linker problems for ring 0 code
 * with newer versions of VCC */

RTDECL(int) RTStrAAppendExNVTag(char **ppsz, size_t cPairs, va_list va, const char *pszTag)
{
    AssertPtr(ppsz);
    if (!cPairs)
        return VINF_SUCCESS;

    /*
     * Determine the length of each string and calc the new total.
     */
    struct RTStrAAppendExNVStruct
    {
        const char *psz;
        size_t      cch;
    } *paPairs = (struct RTStrAAppendExNVStruct *)alloca(cPairs * sizeof(*paPairs));
    AssertReturn(paPairs, VERR_NO_STR_MEMORY);

    size_t  cchOrg      = *ppsz ? strlen(*ppsz) : 0;
    size_t  cchNewTotal = cchOrg;
    for (size_t i = 0; i < cPairs; i++)
    {
        const char *psz = va_arg(va, const char *);
        size_t      cch = va_arg(va, size_t);
        AssertPtrNull(psz);
        Assert(cch == RTSTR_MAX || cch == RTStrNLen(psz, cch));

        if (cch == RTSTR_MAX)
            cch = psz ? strlen(psz) : 0;
        cchNewTotal += cch;

        paPairs[i].cch = cch;
        paPairs[i].psz = psz;
    }
    cchNewTotal++;                      /* '\0' */

    /*
     * Try reallocate the string.
     */
    char *pszNew = (char *)RTMemReallocTag(*ppsz, cchNewTotal, pszTag);
    if (!pszNew)
        return VERR_NO_STR_MEMORY;

    /*
     * Do the appending.
     */
    size_t off = cchOrg;
    for (size_t i = 0; i < cPairs; i++)
    {
        memcpy(&pszNew[off], paPairs[i].psz, paPairs[i].cch);
        off += paPairs[i].cch;
    }
    Assert(off + 1 == cchNewTotal);
    pszNew[off] = '\0';

    /* done */
    *ppsz = pszNew;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTStrAAppendExNVTag);

#endif


RTDECL(int) RTStrATruncateTag(char **ppsz, size_t cchNew, const char *pszTag)
{
    char *pszNew;
    char *pszOld = *ppsz;
    if (!cchNew)
    {
        if (pszOld && *pszOld)
        {
            *pszOld = '\0';
            pszNew = (char *)RTMemReallocTag(pszOld, 1, pszTag);
            if (pszNew)
                *ppsz = pszNew;
        }
    }
    else
    {
        char *pszZero;
        AssertPtrReturn(pszOld, VERR_OUT_OF_RANGE);
        AssertReturn(cchNew < ~(size_t)64, VERR_OUT_OF_RANGE);
        pszZero = RTStrEnd(pszOld, cchNew + 63);
        AssertReturn(!pszZero || (size_t)(pszZero - pszOld) >= cchNew, VERR_OUT_OF_RANGE);
        pszOld[cchNew] = '\0';
        if (!pszZero)
        {
            pszNew = (char *)RTMemReallocTag(pszOld,  cchNew + 1, pszTag);
            if (pszNew)
                *ppsz = pszNew;
        }
    }
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTStrATruncateTag);

