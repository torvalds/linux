/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/******************************************************************************
 @File          fm_rtc.h

 @Description   Memory map and internal definitions for FM RTC IEEE1588 Timer driver.

 @Cautions      None
*//***************************************************************************/

#ifndef __FM_RTC_H__
#define __FM_RTC_H__

#include "std_ext.h"
#include "fm_rtc_ext.h"


#define __ERR_MODULE__  MODULE_FM_RTC

/* General definitions */

#define ACCUMULATOR_OVERFLOW            ((uint64_t)(1LL << 32))
#define DEFAULT_OUTPUT_CLOCK_DIVISOR     0x00000002
#define DEFAULT_BYPASS      			 FALSE
#define DEFAULT_CLOCK_PERIOD             1000



typedef struct t_FmRtcAlarm
{
    t_FmRtcExceptionsCallback   *f_AlarmCallback;
    bool                        clearOnExpiration;
} t_FmRtcAlarm;

typedef struct t_FmRtcPeriodicPulse
{
    t_FmRtcExceptionsCallback   *f_PeriodicPulseCallback;
} t_FmRtcPeriodicPulse;

typedef struct t_FmRtcExternalTrigger
{
    t_FmRtcExceptionsCallback   *f_ExternalTriggerCallback;
} t_FmRtcExternalTrigger;


/**************************************************************************//**
 @Description RTC FM driver control structure.
*//***************************************************************************/
typedef struct t_FmRtc
{
    t_Part                  *p_Part;            /**< Pointer to the integration device              */
    t_Handle                h_Fm;
    t_Handle                h_App;              /**< Application handle */
    struct rtc_regs			*p_MemMap;
    uint32_t                clockPeriodNanoSec; /**< RTC clock period in nano-seconds (for FS mode) */
    uint32_t                srcClkFreqMhz;
    uint16_t                outputClockDivisor; /**< Output clock divisor (for FS mode) */
    t_FmRtcAlarm            alarmParams[FM_RTC_NUM_OF_ALARMS];
    t_FmRtcPeriodicPulse    periodicPulseParams[FM_RTC_NUM_OF_PERIODIC_PULSES];
    t_FmRtcExternalTrigger  externalTriggerParams[FM_RTC_NUM_OF_EXT_TRIGGERS];
    struct rtc_cfg 			*p_RtcDriverParam;  /**< RTC Driver parameters (for Init phase) */
} t_FmRtc;


#endif /* __FM_RTC_H__ */
