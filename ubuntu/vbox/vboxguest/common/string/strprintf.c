/* $Id: strprintf.cpp $ */
/** @file
 * IPRT - String Formatters.
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


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** strbufoutput() argument structure. */
typedef struct STRBUFARG
{
    /** Pointer to current buffer position. */
    char   *psz;
    /** Number of bytes left in the buffer - not including the trailing zero. */
    size_t  cch;
} STRBUFARG;
/** Pointer to a strbufoutput() argument structure. */
typedef STRBUFARG *PSTRBUFARG;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(size_t) strbufoutput(void *pvArg, const char *pachChars, size_t cbChars);


/**
 * Output callback.
 *
 * @returns number of bytes written.
 * @param   pvArg       Pointer to a STRBUFARG structure.
 * @param   pachChars   Pointer to an array of utf-8 characters.
 * @param   cbChars     Number of bytes in the character array pointed to by pachChars.
 */
static DECLCALLBACK(size_t) strbufoutput(void *pvArg, const char *pachChars, size_t cbChars)
{
    PSTRBUFARG  pArg = (PSTRBUFARG)pvArg;
    char *pszCur = pArg->psz; /* We actually have to spell this out for VS2010, or it will load for each case. */

    cbChars = RT_MIN(pArg->cch, cbChars);
    if (cbChars)
    {
        pArg->cch -= cbChars;

        /* Note! For VS2010/64 we need at least 7 case statements before it generates a jump table. */
        switch (cbChars)
        {
            default:
                memcpy(pszCur, pachChars, cbChars);
                break;
            case 8: pszCur[7] = pachChars[7]; RT_FALL_THRU();
            case 7: pszCur[6] = pachChars[6]; RT_FALL_THRU();
            case 6: pszCur[5] = pachChars[5]; RT_FALL_THRU();
            case 5: pszCur[4] = pachChars[4]; RT_FALL_THRU();
            case 4: pszCur[3] = pachChars[3]; RT_FALL_THRU();
            case 3: pszCur[2] = pachChars[2]; RT_FALL_THRU();
            case 2: pszCur[1] = pachChars[1]; RT_FALL_THRU();
            case 1: pszCur[0] = pachChars[0]; RT_FALL_THRU();
            case 0:
                break;
        }
        pArg->psz = pszCur += cbChars;
    }
    *pszCur = '\0';

    return cbChars;
}


RTDECL(size_t) RTStrPrintf(char *pszBuffer, size_t cchBuffer, const char *pszFormat, ...)
{
    /* Explicitly inline RTStrPrintfV + RTStrPrintfExV here because this is a frequently use API. */
    STRBUFARG Arg;
    va_list args;
    size_t cbRet;

    AssertMsgReturn(cchBuffer, ("Excellent idea! Format a string with no space for the output!\n"), 0);
    Arg.psz = pszBuffer;
    Arg.cch = cchBuffer - 1;

    va_start(args, pszFormat);
    cbRet = RTStrFormatV(strbufoutput, &Arg, NULL, NULL, pszFormat, args);
    va_end(args);

    return cbRet;
}
RT_EXPORT_SYMBOL(RTStrPrintf);


RTDECL(size_t) RTStrPrintfExV(PFNSTRFORMAT pfnFormat, void *pvArg, char *pszBuffer, size_t cchBuffer, const char *pszFormat, va_list args)
{
    STRBUFARG Arg;
    AssertMsgReturn(cchBuffer, ("Excellent idea! Format a string with no space for the output!\n"), 0);
    Arg.psz = pszBuffer;
    Arg.cch = cchBuffer - 1;
    return RTStrFormatV(strbufoutput, &Arg, pfnFormat, pvArg, pszFormat, args);
}
RT_EXPORT_SYMBOL(RTStrPrintfExV);


RTDECL(size_t) RTStrPrintfV(char *pszBuffer, size_t cchBuffer, const char *pszFormat, va_list args)
{
    return RTStrPrintfExV(NULL, NULL, pszBuffer, cchBuffer, pszFormat, args);
}
RT_EXPORT_SYMBOL(RTStrPrintfV);


RTDECL(size_t) RTStrPrintfEx(PFNSTRFORMAT pfnFormat, void *pvArg, char *pszBuffer, size_t cchBuffer, const char *pszFormat, ...)
{
    va_list args;
    size_t cbRet;
    va_start(args, pszFormat);
    cbRet = RTStrPrintfExV(pfnFormat, pvArg, pszBuffer, cchBuffer, pszFormat, args);
    va_end(args);
    return cbRet;
}
RT_EXPORT_SYMBOL(RTStrPrintfEx);

