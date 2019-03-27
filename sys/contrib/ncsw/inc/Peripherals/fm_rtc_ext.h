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


/**************************************************************************//**
 @File          fm_rtc_ext.h

 @Description   External definitions and API for FM RTC IEEE1588 Timer Module.

 @Cautions      None.
*//***************************************************************************/

#ifndef __FM_RTC_EXT_H__
#define __FM_RTC_EXT_H__


#include "error_ext.h"
#include "std_ext.h"
#include "fsl_fman_rtc.h"

/**************************************************************************//**

 @Group         FM_grp Frame Manager API

 @Description   FM API functions, definitions and enums

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         fm_rtc_grp FM RTC

 @Description   FM RTC functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         fm_rtc_init_grp FM RTC Initialization Unit

 @Description   FM RTC initialization API.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   FM RTC Alarm Polarity Options.
*//***************************************************************************/
typedef enum e_FmRtcAlarmPolarity
{
    e_FM_RTC_ALARM_POLARITY_ACTIVE_HIGH = E_FMAN_RTC_ALARM_POLARITY_ACTIVE_HIGH,    /**< Active-high output polarity */
    e_FM_RTC_ALARM_POLARITY_ACTIVE_LOW = E_FMAN_RTC_ALARM_POLARITY_ACTIVE_LOW     /**< Active-low output polarity */
} e_FmRtcAlarmPolarity;

/**************************************************************************//**
 @Description   FM RTC Trigger Polarity Options.
*//***************************************************************************/
typedef enum e_FmRtcTriggerPolarity
{
    e_FM_RTC_TRIGGER_ON_RISING_EDGE = E_FMAN_RTC_TRIGGER_ON_RISING_EDGE,    /**< Trigger on rising edge */
    e_FM_RTC_TRIGGER_ON_FALLING_EDGE = E_FMAN_RTC_TRIGGER_ON_FALLING_EDGE   /**< Trigger on falling edge */
} e_FmRtcTriggerPolarity;

/**************************************************************************//**
 @Description   IEEE1588 Timer Module FM RTC Optional Clock Sources.
*//***************************************************************************/
typedef enum e_FmSrcClock
{
    e_FM_RTC_SOURCE_CLOCK_EXTERNAL = E_FMAN_RTC_SOURCE_CLOCK_EXTERNAL,  /**< external high precision timer reference clock */
    e_FM_RTC_SOURCE_CLOCK_SYSTEM = E_FMAN_RTC_SOURCE_CLOCK_SYSTEM,    /**< MAC system clock */
    e_FM_RTC_SOURCE_CLOCK_OSCILATOR = E_FMAN_RTC_SOURCE_CLOCK_OSCILATOR  /**< RTC clock oscilator */
}e_FmSrcClk;

/**************************************************************************//**
 @Description   FM RTC configuration parameters structure.

                This structure should be passed to FM_RTC_Config().
*//***************************************************************************/
typedef struct t_FmRtcParams
{
    t_Handle                 h_Fm;               /**< FM Handle*/
    uintptr_t                baseAddress;        /**< Base address of FM RTC registers */
    t_Handle                 h_App;              /**< A handle to an application layer object; This handle will
                                                      be passed by the driver upon calling the above callbacks */
} t_FmRtcParams;


/**************************************************************************//**
 @Function      FM_RTC_Config

 @Description   Configures the FM RTC module according to user's parameters.

                The driver assigns default values to some FM RTC parameters.
                These parameters can be overwritten using the advanced
                configuration routines.

 @Param[in]     p_FmRtcParam    - FM RTC configuration parameters.

 @Return        Handle to the new FM RTC object; NULL pointer on failure.

 @Cautions      None
*//***************************************************************************/
t_Handle FM_RTC_Config(t_FmRtcParams *p_FmRtcParam);

/**************************************************************************//**
 @Function      FM_RTC_Init

 @Description   Initializes the FM RTC driver and hardware.

 @Param[in]     h_FmRtc - Handle to FM RTC object.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously created using FM_RTC_Config().
*//***************************************************************************/
t_Error FM_RTC_Init(t_Handle h_FmRtc);

/**************************************************************************//**
 @Function      FM_RTC_Free

 @Description   Frees the FM RTC object and all allocated resources.

 @Param[in]     h_FmRtc - Handle to FM RTC object.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously created using FM_RTC_Config().
*//***************************************************************************/
t_Error FM_RTC_Free(t_Handle h_FmRtc);


/**************************************************************************//**
 @Group         fm_rtc_adv_config_grp  FM RTC Advanced Configuration Unit

 @Description   FM RTC advanced configuration functions.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      FM_RTC_ConfigPeriod

 @Description   Configures the period of the timestamp if different than
                default [DEFAULT_clockPeriod].

 @Param[in]     h_FmRtc         - Handle to FM RTC object.
 @Param[in]     period          - Period in nano-seconds.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously created using FM_RTC_Config().
*//***************************************************************************/
t_Error FM_RTC_ConfigPeriod(t_Handle h_FmRtc, uint32_t period);

/**************************************************************************//**
 @Function      FM_RTC_ConfigSourceClock

 @Description   Configures the source clock of the RTC.

 @Param[in]     h_FmRtc         - Handle to FM RTC object.
 @Param[in]     srcClk          - Source clock selection.
 @Param[in]     freqInMhz       - the source-clock frequency (in MHz).

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously created using FM_RTC_Config().
*//***************************************************************************/
t_Error FM_RTC_ConfigSourceClock(t_Handle      h_FmRtc,
                                 e_FmSrcClk    srcClk,
                                 uint32_t      freqInMhz);

/**************************************************************************//**
 @Function      FM_RTC_ConfigPulseRealignment

 @Description   Configures the RTC to automatic FIPER pulse realignment in
                response to timer adjustments [DEFAULT_pulseRealign]

                In this mode, the RTC clock is identical to the source clock.
                This feature can be useful when the system contains an external
                RTC with inherent frequency compensation.

 @Param[in]     h_FmRtc     - Handle to FM RTC object.
 @Param[in]     enable      - TRUE to enable automatic realignment.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously created using FM_RTC_Config().
*//***************************************************************************/
t_Error FM_RTC_ConfigPulseRealignment(t_Handle h_FmRtc, bool enable);

/**************************************************************************//**
 @Function      FM_RTC_ConfigFrequencyBypass

 @Description   Configures the RTC to bypass the frequency compensation
                mechanism. [DEFAULT_bypass]

                In this mode, the RTC clock is identical to the source clock.
                This feature can be useful when the system contains an external
                RTC with inherent frequency compensation.

 @Param[in]     h_FmRtc     - Handle to FM RTC object.
 @Param[in]     enabled     - TRUE to bypass frequency compensation;
                              FALSE otherwise.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously created using FM_RTC_Config().
*//***************************************************************************/
t_Error FM_RTC_ConfigFrequencyBypass(t_Handle h_FmRtc, bool enabled);

/**************************************************************************//**
 @Function      FM_RTC_ConfigInvertedInputClockPhase

 @Description   Configures the RTC to invert the source clock phase on input.
                [DEFAULT_invertInputClkPhase]

 @Param[in]     h_FmRtc  - Handle to FM RTC object.
 @Param[in]     inverted    - TRUE to invert the source clock phase on input.
                              FALSE otherwise.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously created using FM_RTC_Config().
*//***************************************************************************/
t_Error FM_RTC_ConfigInvertedInputClockPhase(t_Handle h_FmRtc, bool inverted);

/**************************************************************************//**
 @Function      FM_RTC_ConfigInvertedOutputClockPhase

 @Description   Configures the RTC to invert the output clock phase.
                [DEFAULT_invertOutputClkPhase]

 @Param[in]     h_FmRtc  - Handle to FM RTC object.
 @Param[in]     inverted    - TRUE to invert the output clock phase.
                              FALSE otherwise.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously created using FM_RTC_Config().
*//***************************************************************************/
t_Error FM_RTC_ConfigInvertedOutputClockPhase(t_Handle h_FmRtc, bool inverted);

/**************************************************************************//**
 @Function      FM_RTC_ConfigOutputClockDivisor

 @Description   Configures the divisor for generating the output clock from
                the RTC clock. [DEFAULT_outputClockDivisor]

 @Param[in]     h_FmRtc  - Handle to FM RTC object.
 @Param[in]     divisor     - Divisor for generation of the output clock.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously created using FM_RTC_Config().
*//***************************************************************************/
t_Error FM_RTC_ConfigOutputClockDivisor(t_Handle h_FmRtc, uint16_t divisor);

/**************************************************************************//**
 @Function      FM_RTC_ConfigAlarmPolarity

 @Description   Configures the polarity (active-high/active-low) of a specific
                alarm signal. [DEFAULT_alarmPolarity]

 @Param[in]     h_FmRtc      - Handle to FM RTC object.
 @Param[in]     alarmId         - Alarm ID.
 @Param[in]     alarmPolarity   - Alarm polarity.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously created using FM_RTC_Config().
*//***************************************************************************/
t_Error FM_RTC_ConfigAlarmPolarity(t_Handle             h_FmRtc,
                                   uint8_t              alarmId,
                                   e_FmRtcAlarmPolarity alarmPolarity);

/**************************************************************************//**
 @Function      FM_RTC_ConfigExternalTriggerPolarity

 @Description   Configures the polarity (rising/falling edge) of a specific
                external trigger signal. [DEFAULT_triggerPolarity]

 @Param[in]     h_FmRtc      - Handle to FM RTC object.
 @Param[in]     triggerId       - Trigger ID.
 @Param[in]     triggerPolarity - Trigger polarity.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously created using FM_RTC_Config().
*//***************************************************************************/
t_Error FM_RTC_ConfigExternalTriggerPolarity(t_Handle               h_FmRtc,
                                             uint8_t                triggerId,
                                             e_FmRtcTriggerPolarity triggerPolarity);

/** @} */ /* end of fm_rtc_adv_config_grp */
/** @} */ /* end of fm_rtc_init_grp */


/**************************************************************************//**
 @Group         fm_rtc_control_grp FM RTC Control Unit

 @Description   FM RTC runtime control API.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      t_FmRtcExceptionsCallback

 @Description   Exceptions user callback routine, used for RTC different mechanisms.

 @Param[in]     h_App       - User's application descriptor.
 @Param[in]     id          - source id.
*//***************************************************************************/
typedef void (t_FmRtcExceptionsCallback) ( t_Handle  h_App, uint8_t id);

/**************************************************************************//**
 @Description   FM RTC alarm parameters.
*//***************************************************************************/
typedef struct t_FmRtcAlarmParams {
    uint8_t                     alarmId;            /**< 0 or 1 */
    uint64_t                    alarmTime;          /**< In nanoseconds, the time when the alarm
                                                         should go off - must be a multiple of
                                                         the RTC period */
    t_FmRtcExceptionsCallback   *f_AlarmCallback;   /**< This routine will be called when RTC
                                                         reaches alarmTime */
    bool                        clearOnExpiration;  /**< TRUE to turn off the alarm once expired. */
} t_FmRtcAlarmParams;

/**************************************************************************//**
 @Description   FM RTC Periodic Pulse parameters.
*//***************************************************************************/
typedef struct t_FmRtcPeriodicPulseParams {
    uint8_t                     periodicPulseId;            /**< 0 or 1 */
    uint64_t                    periodicPulsePeriod;        /**< In Nanoseconds. Must be
                                                                 a multiple of the RTC period */
    t_FmRtcExceptionsCallback   *f_PeriodicPulseCallback;   /**< This routine will be called every
                                                                 periodicPulsePeriod. */
} t_FmRtcPeriodicPulseParams;

/**************************************************************************//**
 @Description   FM RTC Periodic Pulse parameters.
*//***************************************************************************/
typedef struct t_FmRtcExternalTriggerParams {
    uint8_t                     externalTriggerId;              /**< 0 or 1 */
    bool                        usePulseAsInput;                /**< Use the pulse interrupt instead of
                                                                     an external signal */
    t_FmRtcExceptionsCallback   *f_ExternalTriggerCallback;     /**< This routine will be called every
                                                                     periodicPulsePeriod. */
} t_FmRtcExternalTriggerParams;


/**************************************************************************//**
 @Function      FM_RTC_Enable

 @Description   Enable the RTC (time count is started).

                The user can select to resume the time count from previous
                point, or to restart the time count.

 @Param[in]     h_FmRtc     - Handle to FM RTC object.
 @Param[in]     resetClock  - Restart the time count from zero.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
*//***************************************************************************/
t_Error FM_RTC_Enable(t_Handle h_FmRtc, bool resetClock);

/**************************************************************************//**
 @Function      FM_RTC_Disable

 @Description   Disables the RTC (time count is stopped).

 @Param[in]     h_FmRtc - Handle to FM RTC object.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
*//***************************************************************************/
t_Error FM_RTC_Disable(t_Handle h_FmRtc);

/**************************************************************************//**
 @Function      FM_RTC_SetClockOffset

 @Description   Sets the clock offset (usually relative to another clock).

                The user can pass a negative offset value.

 @Param[in]     h_FmRtc  - Handle to FM RTC object.
 @Param[in]     offset   - New clock offset (in nanoseconds).

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
*//***************************************************************************/
t_Error FM_RTC_SetClockOffset(t_Handle h_FmRtc, int64_t offset);

/**************************************************************************//**
 @Function      FM_RTC_SetAlarm

 @Description   Schedules an alarm event to a given RTC time.

 @Param[in]     h_FmRtc             - Handle to FM RTC object.
 @Param[in]     p_FmRtcAlarmParams  - Alarm parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
                Must be called only prior to FM_RTC_Enable().
*//***************************************************************************/
t_Error FM_RTC_SetAlarm(t_Handle h_FmRtc, t_FmRtcAlarmParams *p_FmRtcAlarmParams);

/**************************************************************************//**
 @Function      FM_RTC_SetPeriodicPulse

 @Description   Sets a periodic pulse.

 @Param[in]     h_FmRtc                         - Handle to FM RTC object.
 @Param[in]     p_FmRtcPeriodicPulseParams      - Periodic pulse parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
                Must be called only prior to FM_RTC_Enable().
*//***************************************************************************/
t_Error FM_RTC_SetPeriodicPulse(t_Handle h_FmRtc, t_FmRtcPeriodicPulseParams *p_FmRtcPeriodicPulseParams);

/**************************************************************************//**
 @Function      FM_RTC_ClearPeriodicPulse

 @Description   Clears a periodic pulse.

 @Param[in]     h_FmRtc                         - Handle to FM RTC object.
 @Param[in]     periodicPulseId                 - Periodic pulse id.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
*//***************************************************************************/
t_Error FM_RTC_ClearPeriodicPulse(t_Handle h_FmRtc, uint8_t periodicPulseId);

/**************************************************************************//**
 @Function      FM_RTC_SetExternalTrigger

 @Description   Sets an external trigger indication and define a callback
                routine to be called on such event.

 @Param[in]     h_FmRtc                         - Handle to FM RTC object.
 @Param[in]     p_FmRtcExternalTriggerParams    - External Trigger parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
*//***************************************************************************/
t_Error FM_RTC_SetExternalTrigger(t_Handle h_FmRtc, t_FmRtcExternalTriggerParams *p_FmRtcExternalTriggerParams);

/**************************************************************************//**
 @Function      FM_RTC_ClearExternalTrigger

 @Description   Clears external trigger indication.

 @Param[in]     h_FmRtc                         - Handle to FM RTC object.
 @Param[in]     id                              - External Trigger id.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
*//***************************************************************************/
t_Error FM_RTC_ClearExternalTrigger(t_Handle h_FmRtc, uint8_t id);

/**************************************************************************//**
 @Function      FM_RTC_GetExternalTriggerTimeStamp

 @Description   Reads the External Trigger TimeStamp.

 @Param[in]     h_FmRtc                 - Handle to FM RTC object.
 @Param[in]     triggerId               - External Trigger id.
 @Param[out]    p_TimeStamp             - External Trigger timestamp (in nanoseconds).

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
*//***************************************************************************/
t_Error FM_RTC_GetExternalTriggerTimeStamp(t_Handle             h_FmRtc,
                                           uint8_t              triggerId,
                                           uint64_t             *p_TimeStamp);

/**************************************************************************//**
 @Function      FM_RTC_GetCurrentTime

 @Description   Returns the current RTC time.

 @Param[in]     h_FmRtc - Handle to FM RTC object.
 @Param[out]    p_Ts - returned time stamp (in nanoseconds).

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
*//***************************************************************************/
t_Error FM_RTC_GetCurrentTime(t_Handle h_FmRtc, uint64_t *p_Ts);

/**************************************************************************//**
 @Function      FM_RTC_SetCurrentTime

 @Description   Sets the current RTC time.

 @Param[in]     h_FmRtc - Handle to FM RTC object.
 @Param[in]     ts - The new time stamp (in nanoseconds).

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
*//***************************************************************************/
t_Error FM_RTC_SetCurrentTime(t_Handle h_FmRtc, uint64_t ts);

/**************************************************************************//**
 @Function      FM_RTC_GetFreqCompensation

 @Description   Retrieves the frequency compensation value

 @Param[in]     h_FmRtc         - Handle to FM RTC object.
 @Param[out]    p_Compensation  - A pointer to the returned value of compensation.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
*//***************************************************************************/
t_Error FM_RTC_GetFreqCompensation(t_Handle h_FmRtc, uint32_t *p_Compensation);

/**************************************************************************//**
 @Function      FM_RTC_SetFreqCompensation

 @Description   Sets a new frequency compensation value.

 @Param[in]     h_FmRtc             - Handle to FM RTC object.
 @Param[in]     freqCompensation    - The new frequency compensation value to set.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      h_FmRtc must have been previously initialized using FM_RTC_Init().
*//***************************************************************************/
t_Error FM_RTC_SetFreqCompensation(t_Handle h_FmRtc, uint32_t freqCompensation);

#ifdef CONFIG_PTP_1588_CLOCK_DPAA
/**************************************************************************//**
*@Function      FM_RTC_EnableInterrupt
*
*@Description   Enable interrupt of FM RTC.
*
*@Param[in]     h_FmRtc             - Handle to FM RTC object.
*@Param[in]     events              - Interrupt events.
*
*@Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_RTC_EnableInterrupt(t_Handle h_FmRtc, uint32_t events);

/**************************************************************************//**
*@Function      FM_RTC_DisableInterrupt
*
*@Description   Disable interrupt of FM RTC.
*
*@Param[in]     h_FmRtc             - Handle to FM RTC object.
*@Param[in]     events              - Interrupt events.
*
*@Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_RTC_DisableInterrupt(t_Handle h_FmRtc, uint32_t events);
#endif

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
/**************************************************************************//**
 @Function      FM_RTC_DumpRegs

 @Description   Dumps all FM registers

 @Param[in]     h_FmRtc      A handle to an FM RTC Module.

 @Return        E_OK on success;

 @Cautions      Allowed only FM_Init().
*//***************************************************************************/
t_Error FM_RTC_DumpRegs(t_Handle h_FmRtc);
#endif /* (defined(DEBUG_ERRORS) && ... */

/** @} */ /* end of fm_rtc_control_grp */
/** @} */ /* end of fm_rtc_grp */
/** @} */ /* end of FM_grp group */


#endif /* __FM_RTC_EXT_H__ */
