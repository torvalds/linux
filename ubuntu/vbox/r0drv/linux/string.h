/* $Id: string.h $ */
/** @file
 * IPRT - wrapper for the linux kernel asm/string.h.
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

#ifndef ___string_h
#define ___string_h

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN
#ifndef bool /* Linux 2.6.19 C++ nightmare */
#define bool bool_type
#define true true_type
#define false false_type
#define _Bool int
#define bool_type_r0drv_string_h__
#endif
#include <linux/types.h>
#include <linux/string.h>
#ifdef bool_type_r0drv_string_h__
#undef bool
#undef true
#undef false
#undef bool_type_r0drv_string_h__
#endif
char *strpbrk(const char *pszStr, const char *pszChars)
#if defined(__THROW)
    __THROW
#endif
    ;

RT_C_DECLS_END

#endif

