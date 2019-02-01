/* $Id: logrel.cpp $ */
/** @file
 * Runtime VBox - Release Logger.
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
#include <iprt/log.h>
#include "internal/iprt.h"

#ifndef IN_RC
# include <iprt/alloc.h>
# include <iprt/process.h>
# include <iprt/semaphore.h>
# include <iprt/thread.h>
# include <iprt/mp.h>
#endif
#ifdef IN_RING3
# include <iprt/file.h>
# include <iprt/path.h>
#endif
#include <iprt/time.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/param.h>

#include <iprt/stdarg.h>
#include <iprt/string.h>
#include <iprt/ctype.h>
#ifdef IN_RING3
# include <iprt/alloca.h>
# include <stdio.h>
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IN_RC
/** Default release logger instance. */
extern "C" DECLIMPORT(RTLOGGERRC)   g_RelLogger;
#else /* !IN_RC */
/** Default release logger instance. */
static PRTLOGGER                    g_pRelLogger;
#endif /* !IN_RC */


RTDECL(PRTLOGGER)   RTLogRelGetDefaultInstance(void)
{
#ifdef IN_RC
    return &g_RelLogger;
#else /* !IN_RC */
    return g_pRelLogger;
#endif /* !IN_RC */
}
RT_EXPORT_SYMBOL(RTLogRelGetDefaultInstance);


RTDECL(PRTLOGGER)   RTLogRelGetDefaultInstanceEx(uint32_t fFlagsAndGroup)
{
#ifdef IN_RC
    PRTLOGGER pLogger = &g_RelLogger;
#else /* !IN_RC */
    PRTLOGGER pLogger = g_pRelLogger;
#endif /* !IN_RC */
    if (pLogger)
    {
        if (pLogger->fFlags & RTLOGFLAGS_DISABLED)
            pLogger = NULL;
        else
        {
            uint16_t const fFlags = RT_LO_U16(fFlagsAndGroup);
            uint16_t const iGroup = RT_HI_U16(fFlagsAndGroup);
            if (   iGroup != UINT16_MAX
                 && (   (pLogger->afGroups[iGroup < pLogger->cGroups ? iGroup : 0] & (fFlags | (uint32_t)RTLOGGRPFLAGS_ENABLED))
                     != (fFlags | (uint32_t)RTLOGGRPFLAGS_ENABLED)))
            pLogger = NULL;
        }
    }
    return pLogger;
}
RT_EXPORT_SYMBOL(RTLogRelGetDefaultInstanceEx);


#ifndef IN_RC
/**
 * Sets the default logger instance.
 *
 * @returns iprt status code.
 * @param   pLogger     The new default release logger instance.
 */
RTDECL(PRTLOGGER) RTLogRelSetDefaultInstance(PRTLOGGER pLogger)
{
    return ASMAtomicXchgPtrT(&g_pRelLogger, pLogger, PRTLOGGER);
}
RT_EXPORT_SYMBOL(RTLogRelSetDefaultInstance);
#endif /* !IN_RC */


/**
 * Write to a logger instance, defaulting to the release one.
 *
 * This function will check whether the instance, group and flags makes up a
 * logging kind which is currently enabled before writing anything to the log.
 *
 * @param   pLogger     Pointer to logger instance. If NULL the default release instance is attempted.
 * @param   fFlags      The logging flags.
 * @param   iGroup      The group.
 *                      The value ~0U is reserved for compatibility with RTLogLogger[V] and is
 *                      only for internal usage!
 * @param   pszFormat   Format string.
 * @param   args        Format arguments.
 */
RTDECL(void) RTLogRelLoggerV(PRTLOGGER pLogger, unsigned fFlags, unsigned iGroup, const char *pszFormat, va_list args)
{
    /*
     * A NULL logger means default instance.
     */
    if (!pLogger)
    {
        pLogger = RTLogRelGetDefaultInstance();
        if (!pLogger)
            return;
    }
    RTLogLoggerExV(pLogger, fFlags, iGroup, pszFormat, args);
}
RT_EXPORT_SYMBOL(RTLogRelLoggerV);


/**
 * vprintf like function for writing to the default release log.
 *
 * @param   pszFormat   Printf like format string.
 * @param   args        Optional arguments as specified in pszFormat.
 *
 * @remark The API doesn't support formatting of floating point numbers at the moment.
 */
RTDECL(void) RTLogRelPrintfV(const char *pszFormat, va_list args)
{
    RTLogRelLoggerV(NULL, 0, ~0U, pszFormat, args);
}
RT_EXPORT_SYMBOL(RTLogRelPrintfV);


/**
 * Changes the buffering setting of the default release logger.
 *
 * This can be used for optimizing longish logging sequences.
 *
 * @returns The old state.
 * @param   fBuffered       The new state.
 */
RTDECL(bool) RTLogRelSetBuffering(bool fBuffered)
{
    PRTLOGGER pLogger = RTLogRelGetDefaultInstance();
    if (pLogger)
        return RTLogSetBuffering(pLogger, fBuffered);
    return false;
}
RT_EXPORT_SYMBOL(RTLogRelSetBuffering);

