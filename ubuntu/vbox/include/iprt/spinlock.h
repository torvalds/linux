/** @file
 * IPRT - Spinlocks.
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

#ifndef ___iprt_spinlock_h
#define ___iprt_spinlock_h

#include <iprt/cdefs.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN


/** @defgroup grp_rt_spinlock   RTSpinlock - Spinlocks
 * @ingroup grp_rt
 * @{
 */

/**
 * Creates a spinlock.
 *
 * @returns iprt status code.
 * @param   pSpinlock   Where to store the spinlock handle.
 * @param   fFlags      Creation flags, see RTSPINLOCK_FLAGS_XXX.
 * @param   pszName     Spinlock name, for debugging purposes.  String lifetime
 *                      must be the same as the lock as it won't be copied.
 */
RTDECL(int)  RTSpinlockCreate(PRTSPINLOCK pSpinlock, uint32_t fFlags, const char *pszName);

/** @name RTSPINLOCK_FLAGS_XXX
 * @{ */
/** Disable interrupts when taking the spinlock, making it interrupt safe
 * (sans NMI of course).
 *
 * This is generally the safest option, though it isn't really required unless
 * the data being protect is also accessed from interrupt handler context. */
#define RTSPINLOCK_FLAGS_INTERRUPT_SAFE     RT_BIT(1)
/** No need to disable interrupts, the protect code/data is not used by
 * interrupt handlers. */
#define RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE   RT_BIT(2)
/** @}  */

/**
 * Destroys a spinlock created by RTSpinlockCreate().
 *
 * @returns iprt status code.
 * @param   Spinlock    Spinlock returned by RTSpinlockCreate().
 */
RTDECL(int)  RTSpinlockDestroy(RTSPINLOCK Spinlock);

/**
 * Acquires the spinlock.
 *
 * @param   Spinlock    The spinlock to acquire.
 */
RTDECL(void) RTSpinlockAcquire(RTSPINLOCK Spinlock);

/**
 * Releases the spinlock.
 *
 * @param   Spinlock    The spinlock to acquire.
 */
RTDECL(void) RTSpinlockRelease(RTSPINLOCK Spinlock);


/** @} */

RT_C_DECLS_END

#endif

