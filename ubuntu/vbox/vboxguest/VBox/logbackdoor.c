/* $Id: logbackdoor.cpp $ */
/** @file
 * VirtualBox Runtime - Guest Backdoor Logging.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
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
#include <VBox/log.h>
#include "internal/iprt.h"
#include <iprt/asm-amd64-x86.h>
#include <iprt/string.h>
#ifdef IN_GUEST_R3
# include <VBox/VBoxGuestLib.h>
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(size_t) rtLogBackdoorOutput(void *pv, const char *pachChars, size_t cbChars);


RTDECL(size_t) RTLogBackdoorPrintf(const char *pszFormat, ...)
{
    va_list args;
    size_t cb;

    va_start(args, pszFormat);
    cb = RTLogBackdoorPrintfV(pszFormat, args);
    va_end(args);

    return cb;
}

RT_EXPORT_SYMBOL(RTLogBackdoorPrintf);


RTDECL(size_t) RTLogBackdoorPrintfV(const char *pszFormat, va_list args)
{
    return RTLogFormatV(rtLogBackdoorOutput, NULL, pszFormat, args);
}

RT_EXPORT_SYMBOL(RTLogBackdoorPrintfV);


/**
 * Callback for RTLogFormatV which writes to the backdoor.
 * See PFNRTSTROUTPUT() for details.
 */
static DECLCALLBACK(size_t) rtLogBackdoorOutput(void *pvArg, const char *pachChars, size_t cbChars)
{
    RT_NOREF_PV(pvArg);
    RTLogWriteUser(pachChars, cbChars);
    return cbChars;
}


RTDECL(void) RTLogWriteUser(const char *pch, size_t cb)
{
#ifdef IN_GUEST_R3
    VbglR3WriteLog(pch, cb);
#else  /* !IN_GUEST_R3 */
    const uint8_t *pau8 = (const uint8_t *)pch;
    if (cb > 1)
        ASMOutStrU8(RTLOG_DEBUG_PORT, pau8, cb);
    else if (cb)
        ASMOutU8(RTLOG_DEBUG_PORT, *pau8);
#endif /* !IN_GUEST_R3 */
}

RT_EXPORT_SYMBOL(RTLogWriteUser);

