/* $Id: logformat.cpp $ */
/** @file
 * IPRT - Log Formatter.
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
#include <iprt/log.h>
#include "internal/iprt.h"

#include <iprt/string.h>
#include <iprt/assert.h>
#ifdef IN_RING3
# include <iprt/thread.h>
# include <iprt/err.h>
#endif

#include <iprt/stdarg.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(size_t) rtlogFormatStr(void *pvArg, PFNRTSTROUTPUT pfnOutput,
                                           void *pvArgOutput, const char **ppszFormat,
                                           va_list *pArgs, int cchWidth, int cchPrecision,
                                           unsigned fFlags, char chArgSize);


/**
 * Partial vsprintf worker implementation.
 *
 * @returns number of bytes formatted.
 * @param   pfnOutput   Output worker.
 *                      Called in two ways. Normally with a string an it's length.
 *                      For termination, it's called with NULL for string, 0 for length.
 * @param   pvArg       Argument to output worker.
 * @param   pszFormat   Format string.
 * @param   args        Argument list.
 */
RTDECL(size_t) RTLogFormatV(PFNRTSTROUTPUT pfnOutput, void *pvArg, const char *pszFormat, va_list args)
{
    return RTStrFormatV(pfnOutput, pvArg, rtlogFormatStr, NULL, pszFormat, args);
}
RT_EXPORT_SYMBOL(RTLogFormatV);


/**
 * Callback to format VBox formatting extentions.
 * See @ref pg_rt_str_format for a reference on the format types.
 *
 * @returns The number of bytes formatted.
 * @param   pvArg           Formatter argument.
 * @param   pfnOutput       Pointer to output function.
 * @param   pvArgOutput     Argument for the output function.
 * @param   ppszFormat      Pointer to the format string pointer. Advance this till the char
 *                          after the format specifier.
 * @param   pArgs           Pointer to the argument list. Use this to fetch the arguments.
 * @param   cchWidth        Format Width. -1 if not specified.
 * @param   cchPrecision    Format Precision. -1 if not specified.
 * @param   fFlags          Flags (RTSTR_NTFS_*).
 * @param   chArgSize       The argument size specifier, 'l' or 'L'.
 */
static DECLCALLBACK(size_t) rtlogFormatStr(void *pvArg, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                           const char **ppszFormat, va_list *pArgs, int cchWidth,
                                           int cchPrecision, unsigned fFlags, char chArgSize)
{
    char ch = *(*ppszFormat)++;

    AssertMsgFailed(("Invalid logger format type '%%%c%.10s'!\n", ch, *ppszFormat)); NOREF(ch);

    NOREF(pvArg); NOREF(pfnOutput); NOREF(pvArgOutput); NOREF(pArgs); NOREF(cchWidth);
    NOREF(cchPrecision); NOREF(fFlags); NOREF(chArgSize);
    return 0;
}

