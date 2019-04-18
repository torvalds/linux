/* $Id: initterm.h $ */
/** @file
 * IPRT - Initialization & Termination.
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

#ifndef IPRT_INCLUDED_INTERNAL_initterm_h
#define IPRT_INCLUDED_INTERNAL_initterm_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN

#ifdef IN_RING0

/**
 * Platform specific initialization.
 *
 * @returns IPRT status code.
 */
DECLHIDDEN(int)  rtR0InitNative(void);

/**
 * Platform specific termination.
 */
DECLHIDDEN(void) rtR0TermNative(void);

# ifdef RT_OS_LINUX
/* in alloc-r0drv0-linux.c */
DECLHIDDEN(void) rtR0MemExecCleanup(void);
# endif

#endif /* IN_RING0 */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_initterm_h */

