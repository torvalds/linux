/* $Id: assert.h $ */
/** @file
 * IPRT - Internal RTAssert header
 */

/*
 * Copyright (C) 2009-2019 Oracle Corporation
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

#ifndef IPRT_INCLUDED_INTERNAL_assert_h
#define IPRT_INCLUDED_INTERNAL_assert_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

RT_C_DECLS_BEGIN

#ifdef IN_RING0

/**
 * Print the 1st part of an assert message to whatever native facility is best
 * fitting.
 *
 * @param   pszExpr     Expression. Can be NULL.
 * @param   uLine       Location line number.
 * @param   pszFile     Location file name.
 * @param   pszFunction Location function name.
 */
DECLHIDDEN(void) rtR0AssertNativeMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction);

/**
 * Print the 2nd (optional) part of an assert message to whatever native
 * facility is best fitting.
 *
 * @param   fInitial    Whether it's the initial (true) or an additional (false)
 *                      message.
 * @param   pszFormat   Printf like format string.
 * @param   va          Arguments to that string.
 */
DECLHIDDEN(void) rtR0AssertNativeMsg2V(bool fInitial, const char *pszFormat, va_list va);

#endif

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_assert_h */

