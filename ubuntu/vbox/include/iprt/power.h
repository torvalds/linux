/** @file
 * IPRT - Power management.
 */

/*
 * Copyright (C) 2008-2016 Oracle Corporation
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

#ifndef ___iprt_power_h
#define ___iprt_power_h

#include <iprt/cdefs.h>
#include <iprt/types.h>


RT_C_DECLS_BEGIN

/** @defgroup grp_rt_power RTPower - Power management
 * @ingroup grp_rt
 * @{
 */

#ifdef IN_RING0

/**
 * MP event, see FNRTPOWERNOTIFICATION.
 */
typedef enum RTPOWEREVENT
{
    /** The system will go into suspend mode. */
    RTPOWEREVENT_SUSPEND = 1,
    /** The system has resumed. */
    RTPOWEREVENT_RESUME
} RTPOWEREVENT;

/**
 * Notification callback.
 *
 * The context this is called in differs a bit from platform to
 * platform, so be careful while in here.
 *
 * @param   enmEvent    The event.
 * @param   pvUser      The user argument.
 */
typedef DECLCALLBACK(void) FNRTPOWERNOTIFICATION(RTPOWEREVENT enmEvent, void *pvUser);
/** Pointer to a FNRTPOWERNOTIFICATION(). */
typedef FNRTPOWERNOTIFICATION *PFNRTPOWERNOTIFICATION;

/**
 * Registers a notification callback for power events.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NO_MEMORY if a registration record cannot be allocated.
 * @retval  VERR_ALREADY_EXISTS if the pfnCallback and pvUser already exist
 *          in the callback list.
 *
 * @param   pfnCallback     The callback.
 * @param   pvUser          The user argument to the callback function.
 */
RTDECL(int) RTPowerNotificationRegister(PFNRTPOWERNOTIFICATION pfnCallback, void *pvUser);

/**
 * This deregisters a notification callback registered via RTPowerNotificationRegister().
 *
 * The pfnCallback and pvUser arguments must be identical to the registration call
 * of we won't find the right entry.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_FOUND if no matching entry was found.
 *
 * @param   pfnCallback     The callback.
 * @param   pvUser          The user argument to the callback function.
 */
RTDECL(int) RTPowerNotificationDeregister(PFNRTPOWERNOTIFICATION pfnCallback, void *pvUser);

/**
 * This calls all registered power management callback handlers registered via RTPowerNotificationRegister().
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 *
 * @param   enmEvent        Power Management event
 */
RTDECL(int) RTPowerSignalEvent(RTPOWEREVENT enmEvent);

#endif /* IN_RING0 */

/** @} */

RT_C_DECLS_END

#endif

