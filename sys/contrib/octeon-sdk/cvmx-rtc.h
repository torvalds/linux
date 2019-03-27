/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/







/**
 * @file
 *
 * This file provides support for real time clocks on some boards
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */


#ifndef __CVMX_RTC_H__
#define __CVMX_RTC_H__

#include "cvmx-sysinfo.h"
#include "cvmx-thunder.h"
#include "cvmx-cn3010-evb-hs5.h"

/**
 * Supported RTC options
 */
typedef enum
{
    CVMX_RTC_READ            = 0x1,  /**< Device supports read access */
    CVMX_RTC_WRITE           = 0x2,  /**< Device supports write access */
    CVMX_RTC_TIME_EPOCH      = 0x10, /**< Time stored as seconds from epoch */
    CVMX_RTC_TIME_CAL        = 0x20, /**< Time stored as calendar */
} cvmx_rtc_options_t;

/**
 * Return options supported by the RTC device
 *
 * @return Supported options, or 0 if RTC is not supported
 */
static inline cvmx_rtc_options_t cvmx_rtc_supported(void)
{
    static int supported = -1;

    if (supported < 0) {
	switch (cvmx_sysinfo_get()->board_type)
	{
	case CVMX_BOARD_TYPE_THUNDER:
	    supported = CVMX_RTC_READ | CVMX_RTC_WRITE | CVMX_RTC_TIME_EPOCH;
	    break;

	case CVMX_BOARD_TYPE_EBH3000:
	case CVMX_BOARD_TYPE_CN3010_EVB_HS5:
	case CVMX_BOARD_TYPE_EBH5200:
	    supported = CVMX_RTC_READ | CVMX_RTC_WRITE | CVMX_RTC_TIME_CAL;
	    break;

	default:
	    supported = 0;
	    break;
	}

#ifdef CVMX_RTC_DEBUG
	cvmx_dprintf("Board type: %s, RTC support: 0x%x\n",
	       cvmx_board_type_to_string(cvmx_sysinfo_get()->board_type),
	       supported);
#endif
    }

    return (cvmx_rtc_options_t) supported;
}

/**
 * Read time from RTC device.
 *
 * Time is expressed in seconds from epoch (Jan 1 1970 at 00:00:00 UTC)
 *
 * @return Time in seconds or 0 if RTC is not supported
 */
static inline uint32_t cvmx_rtc_read(void)
{
    switch (cvmx_sysinfo_get()->board_type)
    {
    case CVMX_BOARD_TYPE_THUNDER:
        return cvmx_rtc_ds1374_read();
        break;

    default:
	return cvmx_rtc_ds1337_read();
	break;
    }
}

/**
 * Write time to the RTC device
 *
 * @param time    Number of seconds from epoch (Jan 1 1970 at 00:00:00 UTC)
 *
 * @return Zero on success or device-specific error on failure.
 */
static inline uint32_t cvmx_rtc_write(uint32_t time)
{
    switch (cvmx_sysinfo_get()->board_type)
    {
    case CVMX_BOARD_TYPE_THUNDER:
        return cvmx_rtc_ds1374_write(time);
        break;

    default:
	return cvmx_rtc_ds1337_write(time);
	break;
    }
}

#endif    /* __CVMX_RTC_H__  */
