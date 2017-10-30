/* $Id: mp-r0drv.h $ */
/** @file
 * IPRT - Multiprocessor, Ring-0 Driver, Internal Header.
 */

/*
 * Copyright (C) 2008-2017 Oracle Corporation
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

#ifndef ___r0drv_mp_r0drv_h
#define ___r0drv_mp_r0drv_h

#include <iprt/mp.h>

RT_C_DECLS_BEGIN

/**
 * MP callback
 *
 * @param   idCpu       CPU id
 * @param   pvUser1     The first user argument.
 * @param   pvUser2     The second user argument.
 */
typedef DECLCALLBACK(void) FNMPWORKER(RTCPUID idCpu, void *pvUser1, void *pvUser2);
/** Pointer to a FNMPWORKER(). */
typedef FNMPWORKER *PFNMPWORKER;

/**
 * RTMpOn* argument packet used by the host specific callback
 * wrapper functions.
 */
typedef struct RTMPARGS
{
    PFNMPWORKER pfnWorker;
    void       *pvUser1;
    void       *pvUser2;
    RTCPUID     idCpu;
    RTCPUID     idCpu2;
    uint32_t volatile cHits;
#ifdef RT_OS_WINDOWS
    /** Turns out that KeFlushQueuedDpcs doesn't necessarily wait till all
     * callbacks are done.  So, do reference counting to make sure we don't free
     * this structure befor all CPUs have completely handled their requests.  */
    int32_t volatile  cRefs;
#endif
#ifdef RT_OS_LINUX
    PRTCPUSET   pWorkerSet;
#endif
} RTMPARGS;
/** Pointer to a RTMpOn* argument packet. */
typedef RTMPARGS *PRTMPARGS;

/* Called from initterm-r0drv.cpp: */
DECLHIDDEN(int)  rtR0MpNotificationInit(void);
DECLHIDDEN(void) rtR0MpNotificationTerm(void);

/* The following is only relevant when using mpnotifcation-r0drv.cpp: */
DECLHIDDEN(int)  rtR0MpNotificationNativeInit(void);
DECLHIDDEN(void) rtR0MpNotificationNativeTerm(void);
DECLHIDDEN(void) rtMpNotificationDoCallbacks(RTMPEVENT enmEvent, RTCPUID idCpu);

RT_C_DECLS_END

#endif

