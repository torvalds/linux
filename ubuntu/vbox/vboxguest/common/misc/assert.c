/* $Id: assert.cpp $ */
/** @file
 * IPRT - Assertions, common code.
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
#include <iprt/assert.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>
#ifdef IN_RING3
# include <stdio.h>
#endif
#include "internal/assert.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The last assert message, 1st part. */
RTDATADECL(char)                    g_szRTAssertMsg1[1024];
RT_EXPORT_SYMBOL(g_szRTAssertMsg1);
/** The last assert message, 2nd part. */
RTDATADECL(char)                    g_szRTAssertMsg2[4096];
RT_EXPORT_SYMBOL(g_szRTAssertMsg2);
/** The length of the g_szRTAssertMsg2 content.
 * @remarks Race.  */
static uint32_t volatile            g_cchRTAssertMsg2;
/** The last assert message, expression. */
RTDATADECL(const char * volatile)   g_pszRTAssertExpr;
RT_EXPORT_SYMBOL(g_pszRTAssertExpr);
/** The last assert message, function name. */
RTDATADECL(const char *  volatile)  g_pszRTAssertFunction;
RT_EXPORT_SYMBOL(g_pszRTAssertFunction);
/** The last assert message, file name. */
RTDATADECL(const char * volatile)   g_pszRTAssertFile;
RT_EXPORT_SYMBOL(g_pszRTAssertFile);
/** The last assert message, line number. */
RTDATADECL(uint32_t volatile)       g_u32RTAssertLine;
RT_EXPORT_SYMBOL(g_u32RTAssertLine);


/** Set if assertions are quiet. */
static bool volatile                g_fQuiet = false;
/** Set if assertions may panic. */
static bool volatile                g_fMayPanic = true;


RTDECL(bool) RTAssertSetQuiet(bool fQuiet)
{
    return ASMAtomicXchgBool(&g_fQuiet, fQuiet);
}
RT_EXPORT_SYMBOL(RTAssertSetQuiet);


RTDECL(bool) RTAssertAreQuiet(void)
{
    return ASMAtomicUoReadBool(&g_fQuiet);
}
RT_EXPORT_SYMBOL(RTAssertAreQuiet);


RTDECL(bool) RTAssertSetMayPanic(bool fMayPanic)
{
    return ASMAtomicXchgBool(&g_fMayPanic, fMayPanic);
}
RT_EXPORT_SYMBOL(RTAssertSetMayPanic);


RTDECL(bool) RTAssertMayPanic(void)
{
    return ASMAtomicUoReadBool(&g_fMayPanic);
}
RT_EXPORT_SYMBOL(RTAssertMayPanic);


RTDECL(void) RTAssertMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    /*
     * Fill in the globals.
     */
    ASMAtomicUoWritePtr(&g_pszRTAssertExpr, pszExpr);
    ASMAtomicUoWritePtr(&g_pszRTAssertFile, pszFile);
    ASMAtomicUoWritePtr(&g_pszRTAssertFunction, pszFunction);
    ASMAtomicUoWriteU32(&g_u32RTAssertLine, uLine);
    RTStrPrintf(g_szRTAssertMsg1, sizeof(g_szRTAssertMsg1),
                "\n!!Assertion Failed!!\n"
                "Expression: %s\n"
                "Location  : %s(%d) %s\n",
                pszExpr, pszFile, uLine, pszFunction);

    /*
     * If not quiet, make noise.
     */
    if (!RTAssertAreQuiet())
    {
        RTERRVARS SavedErrVars;
        RTErrVarsSave(&SavedErrVars);

#ifdef IN_RING0
# ifdef IN_GUEST_R0
        RTLogBackdoorPrintf("\n!!Assertion Failed!!\n"
                            "Expression: %s\n"
                            "Location  : %s(%d) %s\n",
                            pszExpr, pszFile, uLine, pszFunction);
# endif
        /** @todo fully integrate this with the logger... play safe a bit for now.  */
        rtR0AssertNativeMsg1(pszExpr, uLine, pszFile, pszFunction);

#else  /* !IN_RING0 */
# if !defined(IN_RING3) && !defined(LOG_NO_COM)
#  if 0 /* Enable this iff you have a COM port and really want this debug info. */
        RTLogComPrintf("\n!!Assertion Failed!!\n"
                       "Expression: %s\n"
                       "Location  : %s(%d) %s\n",
                       pszExpr, pszFile, uLine, pszFunction);
#  endif
# endif

        PRTLOGGER pLog = RTLogRelGetDefaultInstance();
        if (pLog)
        {
            RTLogRelPrintf("\n!!Assertion Failed!!\n"
                           "Expression: %s\n"
                           "Location  : %s(%d) %s\n",
                           pszExpr, pszFile, uLine, pszFunction);
# ifndef IN_RC /* flushing is done automatically in RC */
            RTLogFlush(pLog);
# endif
        }

# ifndef LOG_ENABLED
        if (!pLog)
# endif
        {
            pLog = RTLogDefaultInstance();
            if (pLog)
            {
                RTLogPrintf("\n!!Assertion Failed!!\n"
                            "Expression: %s\n"
                            "Location  : %s(%d) %s\n",
                            pszExpr, pszFile, uLine, pszFunction);
# ifndef IN_RC /* flushing is done automatically in RC */
                RTLogFlush(pLog);
# endif
            }
        }

# ifdef IN_RING3
        /* print to stderr, helps user and gdb debugging. */
        fprintf(stderr,
                "\n!!Assertion Failed!!\n"
                "Expression: %s\n"
                "Location  : %s(%d) %s\n",
                VALID_PTR(pszExpr) ? pszExpr : "<none>",
                VALID_PTR(pszFile) ? pszFile : "<none>",
                uLine,
                VALID_PTR(pszFunction) ? pszFunction : "");
        fflush(stderr);
# endif
#endif /* !IN_RING0 */

        RTErrVarsRestore(&SavedErrVars);
    }
}
RT_EXPORT_SYMBOL(RTAssertMsg1);


/**
 * Worker for RTAssertMsg2V and RTAssertMsg2AddV
 *
 * @param   fInitial            True if it's RTAssertMsg2V, otherwise false.
 * @param   pszFormat           The message format string.
 * @param   va                  The format arguments.
 */
static void rtAssertMsg2Worker(bool fInitial, const char *pszFormat, va_list va)
{
    va_list vaCopy;
    size_t  cch;

    /*
     * The global first.
     */
    if (fInitial)
    {
        va_copy(vaCopy, va);
        cch = RTStrPrintfV(g_szRTAssertMsg2, sizeof(g_szRTAssertMsg2), pszFormat, vaCopy);
        ASMAtomicWriteU32(&g_cchRTAssertMsg2, (uint32_t)cch);
        va_end(vaCopy);
    }
    else
    {
        cch = ASMAtomicReadU32(&g_cchRTAssertMsg2);
        if (cch < sizeof(g_szRTAssertMsg2) - 4)
        {
            va_copy(vaCopy, va);
            cch += RTStrPrintfV(&g_szRTAssertMsg2[cch], sizeof(g_szRTAssertMsg2) - cch, pszFormat, vaCopy);
            ASMAtomicWriteU32(&g_cchRTAssertMsg2, (uint32_t)cch);
            va_end(vaCopy);
        }
    }

    /*
     * If not quiet, make some noise.
     */
    if (!RTAssertAreQuiet())
    {
        RTERRVARS SavedErrVars;
        RTErrVarsSave(&SavedErrVars);

#ifdef IN_RING0
# ifdef IN_GUEST_R0
        va_copy(vaCopy, va);
        RTLogBackdoorPrintfV(pszFormat, vaCopy);
        va_end(vaCopy);
# endif
        /** @todo fully integrate this with the logger... play safe a bit for now.  */
        rtR0AssertNativeMsg2V(fInitial, pszFormat, va);

#else  /* !IN_RING0 */
# if !defined(IN_RING3) && !defined(LOG_NO_COM)
#  if 0 /* Enable this iff you have a COM port and really want this debug info. */
        va_copy(vaCopy, va);
        RTLogComPrintfV(pszFormat, vaCopy);
        va_end(vaCopy);
#  endif
# endif

        PRTLOGGER pLog = RTLogRelGetDefaultInstance();
        if (pLog)
        {
            va_copy(vaCopy, va);
            RTLogRelPrintfV(pszFormat, vaCopy);
            va_end(vaCopy);
# ifndef IN_RC /* flushing is done automatically in RC */
            RTLogFlush(pLog);
# endif
        }

        pLog = RTLogDefaultInstance();
        if (pLog)
        {
            va_copy(vaCopy, va);
            RTLogPrintfV(pszFormat, vaCopy);
            va_end(vaCopy);
# ifndef IN_RC /* flushing is done automatically in RC */
            RTLogFlush(pLog);
#endif
        }

# ifdef IN_RING3
        /* print to stderr, helps user and gdb debugging. */
        char szMsg[sizeof(g_szRTAssertMsg2)];
        va_copy(vaCopy, va);
        RTStrPrintfV(szMsg, sizeof(szMsg), pszFormat, vaCopy);
        va_end(vaCopy);
        fprintf(stderr, "%s", szMsg);
        fflush(stderr);
# endif
#endif /* !IN_RING0 */

        RTErrVarsRestore(&SavedErrVars);
    }
}


RTDECL(void) RTAssertMsg2V(const char *pszFormat, va_list va)
{
    rtAssertMsg2Worker(true /*fInitial*/, pszFormat, va);
}
RT_EXPORT_SYMBOL(RTAssertMsg2V);


RTDECL(void) RTAssertMsg2AddV(const char *pszFormat, va_list va)
{
    rtAssertMsg2Worker(false /*fInitial*/, pszFormat, va);
}
RT_EXPORT_SYMBOL(RTAssertMsg2AddV);

