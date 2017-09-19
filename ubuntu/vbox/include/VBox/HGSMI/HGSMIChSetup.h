/** @file
 * VBox Host Guest Shared Memory Interface (HGSMI), sHost/Guest shared part.
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

#ifndef ___VBox_HGSMI_HGSMIChSetup_h
#define ___VBox_HGSMI_HGSMIChSetup_h

#include <VBox/HGSMI/HGSMI.h>

/* HGSMI setup and configuration channel commands and data structures. */
#define HGSMI_CC_HOST_FLAGS_LOCATION 0 /* Tell the host the location of HGSMIHOSTFLAGS structure,
                                        * where the host can write information about pending
                                        * buffers, etc, and which can be quickly polled by
                                        * the guest without a need to port IO.
                                        */

typedef struct HGSMIBUFFERLOCATION
{
    HGSMIOFFSET offLocation;
    HGSMISIZE   cbLocation;
} HGSMIBUFFERLOCATION;
AssertCompileSize(HGSMIBUFFERLOCATION, 8);

/* HGSMI setup and configuration data structures. */
/* host->guest commands pending, should be accessed under FIFO lock only */
#define HGSMIHOSTFLAGS_COMMANDS_PENDING    UINT32_C(0x1)
/* IRQ is fired, should be accessed under VGAState::lock only  */
#define HGSMIHOSTFLAGS_IRQ                 UINT32_C(0x2)
#ifdef VBOX_WITH_WDDM
/* one or more guest commands is completed, should be accessed under FIFO lock only */
# define HGSMIHOSTFLAGS_GCOMMAND_COMPLETED UINT32_C(0x4)
/* watchdog timer interrupt flag (used for debugging), should be accessed under VGAState::lock only */
# define HGSMIHOSTFLAGS_WATCHDOG           UINT32_C(0x8)
#endif
/* vsync interrupt flag, should be accessed under VGAState::lock only */
#define HGSMIHOSTFLAGS_VSYNC               UINT32_C(0x10)
/** monitor hotplug flag, should be accessed under VGAState::lock only */
#define HGSMIHOSTFLAGS_HOTPLUG             UINT32_C(0x20)
/** Cursor capability state change flag, should be accessed under
 * VGAState::lock only.  @see VBVACONF32. */
#define HGSMIHOSTFLAGS_CURSOR_CAPABILITIES UINT32_C(0x40)

typedef struct HGSMIHOSTFLAGS
{
    /* host flags can be accessed and modified in multiple threads concurrently,
     * e.g. CrOpenGL HGCM and GUI threads when to completing HGSMI 3D and Video Accel respectively,
     * EMT thread when dealing with HGSMI command processing, etc.
     * Besides settings/cleaning flags atomically, some each flag has its own special sync restrictions,
     * see commants for flags definitions above */
    volatile uint32_t u32HostFlags;
    uint32_t au32Reserved[3];
} HGSMIHOSTFLAGS;
AssertCompileSize(HGSMIHOSTFLAGS, 16);

#endif

