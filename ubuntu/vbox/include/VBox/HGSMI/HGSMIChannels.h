/** @file
 *
 * VBox Host Guest Shared Memory Interface (HGSMI).
 * Host/Guest shared part.
 * Channel identifiers.
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


#ifndef __HGSMIChannels_h__
#define __HGSMIChannels_h__


/* Each channel has an 8 bit identifier. There are a number of predefined
 * (hardcoded) channels.
 *
 * HGSMI_CH_HGSMI channel can be used to map a string channel identifier
 * to a free 16 bit numerical value. values are allocated in range
 * [HGSMI_CH_STRING_FIRST;HGSMI_CH_STRING_LAST].
 *
 */


/* Predefined channel identifiers. Used internally by VBOX to simplify the channel setup. */
#define HGSMI_CH_RESERVED     (0x00) /* A reserved channel value. */

#define HGSMI_CH_HGSMI        (0x01) /* HGCMI: setup and configuration channel. */

#define HGSMI_CH_VBVA         (0x02) /* Graphics: VBVA. */
#define HGSMI_CH_SEAMLESS     (0x03) /* Graphics: Seamless with a single guest region. */
#define HGSMI_CH_SEAMLESS2    (0x04) /* Graphics: Seamless with separate host windows. */
#define HGSMI_CH_OPENGL       (0x05) /* Graphics: OpenGL HW acceleration. */


/* Dynamically allocated channel identifiers. */
#define HGSMI_CH_STRING_FIRST (0x20) /* The first channel index to be used for string mappings (inclusive). */
#define HGSMI_CH_STRING_LAST  (0xff) /* The last channel index for string mappings (inclusive). */


/* Check whether the channel identifier is allocated for a dynamic channel. */
#define HGSMI_IS_DYNAMIC_CHANNEL(_channel) (((uint8_t)(_channel) & 0xE0) != 0)


#endif /* !__HGSMIChannels_h__*/
