/* $Id: logcom.cpp $ */
/** @file
 * IPRT - Logging to Serial Port.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifndef IPRT_UART_BASE
/** The port address of the COM port to log to.
 *
 * To override the default (COM1) append IPRT_UART_BASE=0xWXYZ to DEFS in your
 * LocalConfig.kmk. Alternatively you can edit this file, but the don't forget
 * to also update the default found in VBox/asmdefs.h.
 *
 * Standard port assignments are: COM1=0x3f8, COM2=0x2f8, COM3=0x3e8, COM4=0x2e8.
 */
# define IPRT_UART_BASE 0x3f8
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/log.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) /** @todo consider fixing the config instead. */
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/stdarg.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(size_t) rtLogComOutput(void *pv, const char *pachChars, size_t cbChars);


/**
 * Prints a formatted string to the serial port used for logging.
 *
 * @returns Number of bytes written.
 * @param   pszFormat   Format string.
 * @param   ...         Optional arguments specified in the format string.
 */
RTDECL(size_t) RTLogComPrintf(const char *pszFormat, ...)
{
    va_list     args;
    size_t      cb;
    va_start(args, pszFormat);
    cb = RTLogComPrintfV(pszFormat, args);
    va_end(args);

    return cb;
}
RT_EXPORT_SYMBOL(RTLogComPrintf);


/**
 * Prints a formatted string to the serial port used for logging.
 *
 * @returns Number of bytes written.
 * @param   pszFormat   Format string.
 * @param   args        Optional arguments specified in the format string.
 */
RTDECL(size_t) RTLogComPrintfV(const char *pszFormat, va_list args)
{
    return RTLogFormatV(rtLogComOutput, NULL, pszFormat, args);
}
RT_EXPORT_SYMBOL(RTLogComPrintfV);


/**
 * Callback for RTLogFormatV which writes to the com port.
 * See PFNLOGOUTPUT() for details.
 */
static DECLCALLBACK(size_t) rtLogComOutput(void *pv, const char *pachChars, size_t cbChars)
{
    NOREF(pv);
    if (cbChars)
        RTLogWriteCom(pachChars, cbChars);
    return cbChars;
}


/**
 * Write log buffer to COM port.
 *
 * @param   pach        Pointer to the buffer to write.
 * @param   cb          Number of bytes to write.
 */
RTDECL(void) RTLogWriteCom(const char *pach, size_t cb)
{
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    const uint8_t *pu8;
    for (pu8 = (const uint8_t *)pach; cb-- > 0; pu8++)
    {
        register unsigned cMaxWait;
        register uint8_t  u8;

        /* expand \n -> \r\n */
        if (*pu8 == '\n')
            RTLogWriteCom("\r", 1);

        /* Check if port is ready. */
        cMaxWait = ~0U;
        do
        {
            u8 = ASMInU8(IPRT_UART_BASE + 5);
            cMaxWait--;
        } while (!(u8 & 0x20) && u8 != 0xff && cMaxWait);

        /* write */
        ASMOutU8(IPRT_UART_BASE, *pu8);
    }
#else
    /* PORTME? */
#endif
}
RT_EXPORT_SYMBOL(RTLogWriteCom);

