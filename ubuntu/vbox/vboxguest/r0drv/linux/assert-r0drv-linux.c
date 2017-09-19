/* $Id: assert-r0drv-linux.c $ */
/** @file
 * IPRT -  Assertion Workers, Ring-0 Drivers, Linux.
 */

/*
 * Copyright (C) 2007-2017 Oracle Corporation
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
#include "the-linux-kernel.h"
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>
#include <iprt/asm.h>

#include "internal/assert.h"


DECLHIDDEN(void) rtR0AssertNativeMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    IPRT_LINUX_SAVE_EFL_AC();
    printk(KERN_EMERG
           "\r\n!!Assertion Failed!!\r\n"
           "Expression: %s\r\n"
           "Location  : %s(%d) %s\r\n",
           pszExpr, pszFile, uLine, pszFunction);
    IPRT_LINUX_RESTORE_EFL_AC();
}


DECLHIDDEN(void) rtR0AssertNativeMsg2V(bool fInitial, const char *pszFormat, va_list va)
{
    char szMsg[256];
    IPRT_LINUX_SAVE_EFL_AC();

    RTStrPrintfV(szMsg, sizeof(szMsg) - 1, pszFormat, va);
    szMsg[sizeof(szMsg) - 1] = '\0';
    printk(KERN_EMERG "%s", szMsg);

    NOREF(fInitial);
    IPRT_LINUX_RESTORE_EFL_AC();
}


RTR0DECL(void) RTR0AssertPanicSystem(void)
{
    panic("%s%s", g_szRTAssertMsg1, g_szRTAssertMsg2);
}
RT_EXPORT_SYMBOL(RTR0AssertPanicSystem);

