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
 @File          fm_rtc.c

 @Description   FM RTC driver implementation.

 @Cautions      None
*//***************************************************************************/
#include <linux/math64.h>
#include "error_ext.h"
#include "debug_ext.h"
#include "string_ext.h"
#include "part_ext.h"
#include "xx_ext.h"
#include "ncsw_ext.h"

#include "fm_rtc.h"
#include "fm_common.h"



/*****************************************************************************/
static t_Error CheckInitParameters(t_FmRtc *p_Rtc)
{
    struct rtc_cfg  *p_RtcDriverParam = p_Rtc->p_RtcDriverParam;
    int                 i;

    if ((p_RtcDriverParam->src_clk != E_FMAN_RTC_SOURCE_CLOCK_EXTERNAL) &&
        (p_RtcDriverParam->src_clk != E_FMAN_RTC_SOURCE_CLOCK_SYSTEM) &&
        (p_RtcDriverParam->src_clk != E_FMAN_RTC_SOURCE_CLOCK_OSCILATOR))
        RETURN_ERROR(MAJOR, E_INVALID_CLOCK, ("Source clock undefined"));

    if (p_Rtc->outputClockDivisor == 0)
    {
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("Divisor for output clock (should be positive)"));
    }

    for (i=0; i < FM_RTC_NUM_OF_ALARMS; i++)
    {
        if ((p_RtcDriverParam->alarm_polarity[i] != E_FMAN_RTC_ALARM_POLARITY_ACTIVE_LOW) &&
            (p_RtcDriverParam->alarm_polarity[i] != E_FMAN_RTC_ALARM_POLARITY_ACTIVE_HIGH))
        {
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("Alarm %d signal polarity", i));
        }
    }
    for (i=0; i < FM_RTC_NUM_OF_EXT_TRIGGERS; i++)
    {
        if ((p_RtcDriverParam->trigger_polarity[i] != E_FMAN_RTC_TRIGGER_ON_FALLING_EDGE) &&
            (p_RtcDriverParam->trigger_polarity[i] != E_FMAN_RTC_TRIGGER_ON_RISING_EDGE))
        {
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("Trigger %d signal polarity", i));
        }
    }

    return E_OK;
}

/*****************************************************************************/
static void RtcExceptions(t_Handle h_FmRtc)
{
    t_FmRtc             *p_Rtc = (t_FmRtc *)h_FmRtc;
    struct rtc_regs     *p_MemMap;
    register uint32_t   events;

    ASSERT_COND(p_Rtc);
    p_MemMap = p_Rtc->p_MemMap;

    events = fman_rtc_check_and_clear_event(p_MemMap);
    if (events & FMAN_RTC_TMR_TEVENT_ALM1)
    {
        if (p_Rtc->alarmParams[0].clearOnExpiration)
        {
            fman_rtc_set_timer_alarm_l(p_MemMap, 0, 0);
            fman_rtc_disable_interupt(p_MemMap, FMAN_RTC_TMR_TEVENT_ALM1);
        }
        ASSERT_COND(p_Rtc->alarmParams[0].f_AlarmCallback);
        p_Rtc->alarmParams[0].f_AlarmCallback(p_Rtc->h_App, 0);
    }
    if (events & FMAN_RTC_TMR_TEVENT_ALM2)
    {
        if (p_Rtc->alarmParams[1].clearOnExpiration)
        {
            fman_rtc_set_timer_alarm_l(p_MemMap, 1, 0);
            fman_rtc_disable_interupt(p_MemMap, FMAN_RTC_TMR_TEVENT_ALM2);
        }
        ASSERT_COND(p_Rtc->alarmParams[1].f_AlarmCallback);
        p_Rtc->alarmParams[1].f_AlarmCallback(p_Rtc->h_App, 1);
    }
    if (events & FMAN_RTC_TMR_TEVENT_PP1)
    {
        ASSERT_COND(p_Rtc->periodicPulseParams[0].f_PeriodicPulseCallback);
        p_Rtc->periodicPulseParams[0].f_PeriodicPulseCallback(p_Rtc->h_App, 0);
    }
    if (events & FMAN_RTC_TMR_TEVENT_PP2)
    {
        ASSERT_COND(p_Rtc->periodicPulseParams[1].f_PeriodicPulseCallback);
        p_Rtc->periodicPulseParams[1].f_PeriodicPulseCallback(p_Rtc->h_App, 1);
    }
    if (events & FMAN_RTC_TMR_TEVENT_ETS1)
    {
        ASSERT_COND(p_Rtc->externalTriggerParams[0].f_ExternalTriggerCallback);
        p_Rtc->externalTriggerParams[0].f_ExternalTriggerCallback(p_Rtc->h_App, 0);
    }
    if (events & FMAN_RTC_TMR_TEVENT_ETS2)
    {
        ASSERT_COND(p_Rtc->externalTriggerParams[1].f_ExternalTriggerCallback);
        p_Rtc->externalTriggerParams[1].f_ExternalTriggerCallback(p_Rtc->h_App, 1);
    }
}


/*****************************************************************************/
t_Handle FM_RTC_Config(t_FmRtcParams *p_FmRtcParam)
{
    t_FmRtc *p_Rtc;

    SANITY_CHECK_RETURN_VALUE(p_FmRtcParam, E_NULL_POINTER, NULL);

    /* Allocate memory for the FM RTC driver parameters */
    p_Rtc = (t_FmRtc *)XX_Malloc(sizeof(t_FmRtc));
    if (!p_Rtc)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM RTC driver structure"));
        return NULL;
    }

    memset(p_Rtc, 0, sizeof(t_FmRtc));

    /* Allocate memory for the FM RTC driver parameters */
    p_Rtc->p_RtcDriverParam = (struct rtc_cfg *)XX_Malloc(sizeof(struct rtc_cfg));
    if (!p_Rtc->p_RtcDriverParam)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM RTC driver parameters"));
        XX_Free(p_Rtc);
        return NULL;
    }

    memset(p_Rtc->p_RtcDriverParam, 0, sizeof(struct rtc_cfg));

    /* Store RTC configuration parameters */
    p_Rtc->h_Fm = p_FmRtcParam->h_Fm;

    /* Set default RTC configuration parameters */
    fman_rtc_defconfig(p_Rtc->p_RtcDriverParam);

    p_Rtc->outputClockDivisor = DEFAULT_OUTPUT_CLOCK_DIVISOR;
    p_Rtc->p_RtcDriverParam->bypass = DEFAULT_BYPASS;
    p_Rtc->clockPeriodNanoSec = DEFAULT_CLOCK_PERIOD; /* 1 usec */


    /* Store RTC parameters in the RTC control structure */
    p_Rtc->p_MemMap = (struct rtc_regs *)UINT_TO_PTR(p_FmRtcParam->baseAddress);
    p_Rtc->h_App    = p_FmRtcParam->h_App;

    return p_Rtc;
}

/*****************************************************************************/
t_Error FM_RTC_Init(t_Handle h_FmRtc)
{
    t_FmRtc             *p_Rtc = (t_FmRtc *)h_FmRtc;
    struct rtc_cfg      *p_RtcDriverParam;
    struct rtc_regs     *p_MemMap;
    uint32_t            freqCompensation = 0;
    uint64_t            tmpDouble;
    bool                init_freq_comp = FALSE;

    p_RtcDriverParam = p_Rtc->p_RtcDriverParam;
    p_MemMap = p_Rtc->p_MemMap;

    if (CheckInitParameters(p_Rtc)!=E_OK)
        RETURN_ERROR(MAJOR, E_CONFLICT,
                     ("Init Parameters are not Valid"));

    /* TODO check that no timestamping MACs are working in this stage. */

    /* find source clock frequency in Mhz */
    if (p_Rtc->p_RtcDriverParam->src_clk != E_FMAN_RTC_SOURCE_CLOCK_SYSTEM)
        p_Rtc->srcClkFreqMhz = p_Rtc->p_RtcDriverParam->ext_src_clk_freq;
    else
        p_Rtc->srcClkFreqMhz = (uint32_t)(FmGetMacClockFreq(p_Rtc->h_Fm));

    /* if timer in Master mode Initialize TMR_CTRL */
    /* We want the counter (TMR_CNT) to count in nano-seconds */
    if (!p_RtcDriverParam->timer_slave_mode && p_Rtc->p_RtcDriverParam->bypass)
        p_Rtc->clockPeriodNanoSec = (1000 / p_Rtc->srcClkFreqMhz);
    else
    {
        /* Initialize TMR_ADD with the initial frequency compensation value:
           freqCompensation = (2^32 / frequency ratio) */
        /* frequency ratio = sorce clock/rtc clock =
         * (p_Rtc->srcClkFreqMhz*1000000))/ 1/(p_Rtc->clockPeriodNanoSec * 1000000000) */
        init_freq_comp = TRUE;
        freqCompensation = (uint32_t)DIV_CEIL(ACCUMULATOR_OVERFLOW * 1000,
                                              p_Rtc->clockPeriodNanoSec * p_Rtc->srcClkFreqMhz);
    }

    /* check the legality of the relation between source and destination clocks */
    /* should be larger than 1.0001 */
    tmpDouble = 10000 * (uint64_t)p_Rtc->clockPeriodNanoSec * (uint64_t)p_Rtc->srcClkFreqMhz;
    if ((tmpDouble) <= 10001)
        RETURN_ERROR(MAJOR, E_CONFLICT,
              ("Invalid relation between source and destination clocks. Should be larger than 1.0001"));

    fman_rtc_init(p_RtcDriverParam,
             p_MemMap,
             FM_RTC_NUM_OF_ALARMS,
             FM_RTC_NUM_OF_PERIODIC_PULSES,
             FM_RTC_NUM_OF_EXT_TRIGGERS,
             init_freq_comp,
             freqCompensation,
             p_Rtc->outputClockDivisor);

    /* Register the FM RTC interrupt */
    FmRegisterIntr(p_Rtc->h_Fm, e_FM_MOD_TMR, 0, e_FM_INTR_TYPE_NORMAL, RtcExceptions , p_Rtc);

    /* Free parameters structures */
    XX_Free(p_Rtc->p_RtcDriverParam);
    p_Rtc->p_RtcDriverParam = NULL;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_Free(t_Handle h_FmRtc)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);

    if (p_Rtc->p_RtcDriverParam)
    {
        XX_Free(p_Rtc->p_RtcDriverParam);
    }
    else
    {
        FM_RTC_Disable(h_FmRtc);
    }

    /* Unregister FM RTC interrupt */
    FmUnregisterIntr(p_Rtc->h_Fm, e_FM_MOD_TMR, 0, e_FM_INTR_TYPE_NORMAL);
    XX_Free(p_Rtc);

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_ConfigSourceClock(t_Handle         h_FmRtc,
                                    e_FmSrcClk    srcClk,
                                    uint32_t      freqInMhz)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    p_Rtc->p_RtcDriverParam->src_clk = (enum fman_src_clock)srcClk;
    if (srcClk != e_FM_RTC_SOURCE_CLOCK_SYSTEM)
        p_Rtc->p_RtcDriverParam->ext_src_clk_freq = freqInMhz;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_ConfigPeriod(t_Handle h_FmRtc, uint32_t period)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    p_Rtc->clockPeriodNanoSec = period;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_ConfigFrequencyBypass(t_Handle h_FmRtc, bool enabled)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    p_Rtc->p_RtcDriverParam->bypass = enabled;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_ConfigInvertedInputClockPhase(t_Handle h_FmRtc, bool inverted)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    p_Rtc->p_RtcDriverParam->invert_input_clk_phase = inverted;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_ConfigInvertedOutputClockPhase(t_Handle h_FmRtc, bool inverted)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    p_Rtc->p_RtcDriverParam->invert_output_clk_phase = inverted;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_ConfigOutputClockDivisor(t_Handle h_FmRtc, uint16_t divisor)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    p_Rtc->outputClockDivisor = divisor;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_ConfigPulseRealignment(t_Handle h_FmRtc, bool enable)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    p_Rtc->p_RtcDriverParam->pulse_realign = enable;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_ConfigAlarmPolarity(t_Handle             h_FmRtc,
                                   uint8_t              alarmId,
                                   e_FmRtcAlarmPolarity alarmPolarity)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    if (alarmId >= FM_RTC_NUM_OF_ALARMS)
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("Alarm ID"));

    p_Rtc->p_RtcDriverParam->alarm_polarity[alarmId] =
        (enum fman_rtc_alarm_polarity)alarmPolarity;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_ConfigExternalTriggerPolarity(t_Handle               h_FmRtc,
                                             uint8_t                triggerId,
                                             e_FmRtcTriggerPolarity triggerPolarity)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    if (triggerId >= FM_RTC_NUM_OF_EXT_TRIGGERS)
    {
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("External trigger ID"));
    }

    p_Rtc->p_RtcDriverParam->trigger_polarity[triggerId] =
        (enum fman_rtc_trigger_polarity)triggerPolarity;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_Enable(t_Handle h_FmRtc, bool resetClock)
{
    t_FmRtc         *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    fman_rtc_enable(p_Rtc->p_MemMap, resetClock);
    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_Disable(t_Handle h_FmRtc)
{
    t_FmRtc         *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    /* TODO A check must be added here, that no timestamping MAC's
     * are working in this stage. */
    fman_rtc_disable(p_Rtc->p_MemMap);

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_SetClockOffset(t_Handle h_FmRtc, int64_t offset)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    fman_rtc_set_timer_offset(p_Rtc->p_MemMap, offset);
    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_SetAlarm(t_Handle h_FmRtc, t_FmRtcAlarmParams *p_FmRtcAlarmParams)
{
    t_FmRtc         *p_Rtc = (t_FmRtc *)h_FmRtc;
    uint64_t        tmpAlarm;
    bool            enable = FALSE;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    if (p_FmRtcAlarmParams->alarmId >= FM_RTC_NUM_OF_ALARMS)
    {
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("Alarm ID"));
    }

    if (p_FmRtcAlarmParams->alarmTime < p_Rtc->clockPeriodNanoSec)
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION,
                     ("Alarm time must be equal or larger than RTC period - %d nanoseconds",
                      p_Rtc->clockPeriodNanoSec));
    tmpAlarm = p_FmRtcAlarmParams->alarmTime;
    if (do_div(tmpAlarm, p_Rtc->clockPeriodNanoSec))
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION,
                     ("Alarm time must be a multiple of RTC period - %d nanoseconds",
                      p_Rtc->clockPeriodNanoSec));

    if (p_FmRtcAlarmParams->f_AlarmCallback)
    {
        p_Rtc->alarmParams[p_FmRtcAlarmParams->alarmId].f_AlarmCallback = p_FmRtcAlarmParams->f_AlarmCallback;
        p_Rtc->alarmParams[p_FmRtcAlarmParams->alarmId].clearOnExpiration = p_FmRtcAlarmParams->clearOnExpiration;
        enable = TRUE;
    }

    fman_rtc_set_alarm(p_Rtc->p_MemMap, p_FmRtcAlarmParams->alarmId, (unsigned long)tmpAlarm, enable);

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_SetPeriodicPulse(t_Handle h_FmRtc, t_FmRtcPeriodicPulseParams *p_FmRtcPeriodicPulseParams)
{
    t_FmRtc         *p_Rtc = (t_FmRtc *)h_FmRtc;
    bool            enable = FALSE;
    uint64_t        tmpFiper;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    if (p_FmRtcPeriodicPulseParams->periodicPulseId >= FM_RTC_NUM_OF_PERIODIC_PULSES)
    {
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("Periodic pulse ID"));
    }
    if (fman_rtc_is_enabled(p_Rtc->p_MemMap))
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("Can't set Periodic pulse when RTC is enabled."));
    if (p_FmRtcPeriodicPulseParams->periodicPulsePeriod < p_Rtc->clockPeriodNanoSec)
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION,
                     ("Periodic pulse must be equal or larger than RTC period - %d nanoseconds",
                      p_Rtc->clockPeriodNanoSec));
    tmpFiper = p_FmRtcPeriodicPulseParams->periodicPulsePeriod;
    if (do_div(tmpFiper, p_Rtc->clockPeriodNanoSec))
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION,
                     ("Periodic pulse must be a multiple of RTC period - %d nanoseconds",
                      p_Rtc->clockPeriodNanoSec));
    if (tmpFiper & 0xffffffff00000000LL)
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION,
                     ("Periodic pulse/RTC Period must be smaller than 4294967296",
                      p_Rtc->clockPeriodNanoSec));

    if (p_FmRtcPeriodicPulseParams->f_PeriodicPulseCallback)
    {
        p_Rtc->periodicPulseParams[p_FmRtcPeriodicPulseParams->periodicPulseId].f_PeriodicPulseCallback =
                                                                p_FmRtcPeriodicPulseParams->f_PeriodicPulseCallback;
        enable = TRUE;
    }
    fman_rtc_set_periodic_pulse(p_Rtc->p_MemMap, p_FmRtcPeriodicPulseParams->periodicPulseId, (uint32_t)tmpFiper, enable);
    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_ClearPeriodicPulse(t_Handle h_FmRtc, uint8_t periodicPulseId)
{
    t_FmRtc     *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    if (periodicPulseId >= FM_RTC_NUM_OF_PERIODIC_PULSES)
    {
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("Periodic pulse ID"));
    }

    p_Rtc->periodicPulseParams[periodicPulseId].f_PeriodicPulseCallback = NULL;
    fman_rtc_clear_periodic_pulse(p_Rtc->p_MemMap, periodicPulseId);

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_SetExternalTrigger(t_Handle h_FmRtc, t_FmRtcExternalTriggerParams *p_FmRtcExternalTriggerParams)
{
    t_FmRtc     *p_Rtc = (t_FmRtc *)h_FmRtc;
    bool        enable = FALSE;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    if (p_FmRtcExternalTriggerParams->externalTriggerId >= FM_RTC_NUM_OF_EXT_TRIGGERS)
    {
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("External Trigger ID"));
    }

    if (p_FmRtcExternalTriggerParams->f_ExternalTriggerCallback)
    {
        p_Rtc->externalTriggerParams[p_FmRtcExternalTriggerParams->externalTriggerId].f_ExternalTriggerCallback = p_FmRtcExternalTriggerParams->f_ExternalTriggerCallback;
        enable = TRUE;
    }

    fman_rtc_set_ext_trigger(p_Rtc->p_MemMap, p_FmRtcExternalTriggerParams->externalTriggerId, enable, p_FmRtcExternalTriggerParams->usePulseAsInput);
    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_ClearExternalTrigger(t_Handle h_FmRtc, uint8_t externalTriggerId)
{
    t_FmRtc     *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    if (externalTriggerId >= FM_RTC_NUM_OF_EXT_TRIGGERS)
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("External Trigger ID"));

    p_Rtc->externalTriggerParams[externalTriggerId].f_ExternalTriggerCallback = NULL;

    fman_rtc_clear_external_trigger(p_Rtc->p_MemMap, externalTriggerId);

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_GetExternalTriggerTimeStamp(t_Handle             h_FmRtc,
                                              uint8_t           triggerId,
                                              uint64_t          *p_TimeStamp)
{
    t_FmRtc     *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    if (triggerId >= FM_RTC_NUM_OF_EXT_TRIGGERS)
        RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("External trigger ID"));

    *p_TimeStamp = fman_rtc_get_trigger_stamp(p_Rtc->p_MemMap, triggerId)*p_Rtc->clockPeriodNanoSec;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_GetCurrentTime(t_Handle h_FmRtc, uint64_t *p_Ts)
{
    t_FmRtc     *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    *p_Ts = fman_rtc_get_timer(p_Rtc->p_MemMap)*p_Rtc->clockPeriodNanoSec;

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_SetCurrentTime(t_Handle h_FmRtc, uint64_t ts)
{
    t_FmRtc     *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    do_div(ts, p_Rtc->clockPeriodNanoSec);
    fman_rtc_set_timer(p_Rtc->p_MemMap, (int64_t)ts);

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_GetFreqCompensation(t_Handle h_FmRtc, uint32_t *p_Compensation)
{
    t_FmRtc     *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    *p_Compensation = fman_rtc_get_frequency_compensation(p_Rtc->p_MemMap);

    return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_SetFreqCompensation(t_Handle h_FmRtc, uint32_t freqCompensation)
{
    t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

    SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

    /* set the new freqCompensation */
    fman_rtc_set_frequency_compensation(p_Rtc->p_MemMap, freqCompensation);

    return E_OK;
}

#ifdef CONFIG_PTP_1588_CLOCK_DPAA
/*****************************************************************************/
t_Error FM_RTC_EnableInterrupt(t_Handle h_FmRtc, uint32_t events)
{
	t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

	SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
	SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

	/* enable interrupt */
	fman_rtc_enable_interupt(p_Rtc->p_MemMap, events);

	return E_OK;
}

/*****************************************************************************/
t_Error FM_RTC_DisableInterrupt(t_Handle h_FmRtc, uint32_t events)
{
	t_FmRtc *p_Rtc = (t_FmRtc *)h_FmRtc;

	SANITY_CHECK_RETURN_ERROR(p_Rtc, E_INVALID_HANDLE);
	SANITY_CHECK_RETURN_ERROR(!p_Rtc->p_RtcDriverParam, E_INVALID_STATE);

	/* disable interrupt */
	fman_rtc_disable_interupt(p_Rtc->p_MemMap, events);

	return E_OK;
}
#endif
