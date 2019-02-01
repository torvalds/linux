/* $Id: errinfo.cpp $ */
/** @file
 * IPRT - Error Info, Setters.
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
#include "internal/iprt.h"
#include <iprt/errcore.h>

#include <iprt/assert.h>
#include <iprt/string.h>


RTDECL(int) RTErrInfoSet(PRTERRINFO pErrInfo, int rc, const char *pszMsg)
{
    if (pErrInfo)
    {
        AssertPtr(pErrInfo);
        Assert((pErrInfo->fFlags & RTERRINFO_FLAGS_MAGIC_MASK) == RTERRINFO_FLAGS_MAGIC);

        RTStrCopy(pErrInfo->pszMsg, pErrInfo->cbMsg, pszMsg);
        pErrInfo->rc      = rc;
        pErrInfo->fFlags |= RTERRINFO_FLAGS_SET;
    }
    return rc;
}


RTDECL(int) RTErrInfoSetF(PRTERRINFO pErrInfo, int rc, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTErrInfoSetV(pErrInfo, rc, pszFormat, va);
    va_end(va);
    return rc;
}


RTDECL(int) RTErrInfoSetV(PRTERRINFO pErrInfo, int rc, const char *pszFormat, va_list va)
{
    if (pErrInfo)
    {
        AssertPtr(pErrInfo);
        Assert((pErrInfo->fFlags & RTERRINFO_FLAGS_MAGIC_MASK) == RTERRINFO_FLAGS_MAGIC);

        RTStrPrintfV(pErrInfo->pszMsg, pErrInfo->cbMsg, pszFormat, va);
        pErrInfo->rc      = rc;
        pErrInfo->fFlags |= RTERRINFO_FLAGS_SET;
    }
    return rc;
}


RTDECL(int) RTErrInfoAdd(PRTERRINFO pErrInfo, int rc, const char *pszMsg)
{
    if (pErrInfo)
    {
        AssertPtr(pErrInfo);
        if (pErrInfo->fFlags & RTERRINFO_FLAGS_SET)
            RTStrCat(pErrInfo->pszMsg, pErrInfo->cbMsg, pszMsg);
        else
        {
            while (*pszMsg == ' ')
                pszMsg++;
            return RTErrInfoSet(pErrInfo, rc, pszMsg);
        }
    }
    return rc;
}


RTDECL(int) RTErrInfoAddF(PRTERRINFO pErrInfo, int rc, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTErrInfoAddV(pErrInfo, rc, pszFormat, va);
    va_end(va);
    return rc;
}


RTDECL(int) RTErrInfoAddV(PRTERRINFO pErrInfo, int rc, const char *pszFormat, va_list va)
{
    if (pErrInfo)
    {
        AssertPtr(pErrInfo);
        Assert((pErrInfo->fFlags & RTERRINFO_FLAGS_MAGIC_MASK) == RTERRINFO_FLAGS_MAGIC);
        if (pErrInfo->fFlags & RTERRINFO_FLAGS_SET)
        {
            char *pszOut = (char *)memchr(pErrInfo->pszMsg, '\0', pErrInfo->cbMsg - 2);
            if (pszOut)
                RTStrPrintfV(pszOut, &pErrInfo->pszMsg[pErrInfo->cbMsg] - pszOut, pszFormat, va);
        }
        else
        {
            while (*pszFormat == ' ')
                pszFormat++;
            return RTErrInfoSetV(pErrInfo, rc, pszFormat, va);
        }
    }
    return rc;
}

