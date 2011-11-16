/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_pmm_policy.h
 * Defines the power management module policies
 */

#ifndef __MALI_PMM_POLICY_H__
#define __MALI_PMM_POLICY_H__

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup pmmapi Power Management Module APIs
 *
 * @{
 *
 * @defgroup pmmapi_policy Power Management Module Policies
 *
 * @{
 */

/** @brief Generic timer for use with policies
 */
typedef struct _pmm_policy_timer
{
	u32 timeout;                    /**< Timeout for this timer in ticks */
	mali_pmm_event_id event_id;     /**< Event id that will be raised when timer expires */
	_mali_osk_timer_t *timer;       /**< Timer */
	mali_bool set;                  /**< Timer set */
	mali_bool expired;              /**< Timer expired - event needs to be raised */
	u32 start;                      /**< Timer start ticks */
} _pmm_policy_timer_t;

/** @brief Policy timer initialization
 *
 * This will create a timer for use in policies, but won't start it
 *
 * @param pptimer An empty timer structure to be initialized
 * @param timeout Timeout in ticks for the timer
 * @param id Event id that will be raised on timeout
 * @return _MALI_OSK_ERR_OK if the policy could be initialized, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t pmm_policy_timer_init( _pmm_policy_timer_t *pptimer, u32 timeout, mali_pmm_event_id id );

/** @brief Policy timer termination
 *
 * This will clean up a timer that was previously used in policies, it
 * will also stop it if started
 *
 * @param pptimer An initialized timer structure to be terminated
 */
void pmm_policy_timer_term( _pmm_policy_timer_t *pptimer );

/** @brief Policy timer start
 *
 * This will start a previously created timer for use in policies
 * When the timer expires after the initialized timeout it will raise
 * a PMM event of the event id given on initialization
 * As data for the event it will pass the start time of the timer
 *
 * @param pptimer A previously initialized policy timer
 * @return MALI_TRUE if the timer was started, MALI_FALSE if it is already started
 */
mali_bool pmm_policy_timer_start( _pmm_policy_timer_t *pptimer );

/** @brief Policy timer stop
 *
 * This will stop a previously created timer for use in policies
 *
 * @param pptimer A previously started policy timer
 * @return MALI_TRUE if the timer was stopped, MALI_FALSE if it is already stopped
 */
mali_bool pmm_policy_timer_stop( _pmm_policy_timer_t *pptimer );

/** @brief Policy timer stop
 *
 * This raise an event for an expired timer
 *
 * @param pptimer An expired policy timer
 * @return MALI_TRUE if an event was raised, else MALI_FALSE
 */
mali_bool pmm_policy_timer_raise_event( _pmm_policy_timer_t *pptimer );

/** @brief Policy timer valid checker
 *
 * This will check that a timer was started after a given time
 *
 * @param timer_start Time the timer was started
 * @param other_start Time when another event or action occurred
 * @return MALI_TRUE if the timer was started after the other time, else MALI_FALSE
 */
mali_bool pmm_policy_timer_valid( u32 timer_start, u32 other_start );


/** @brief Common policy initialization
 *
 * This will initialize the current policy
 *
 * @note Any previously initialized policy should be terminated first
 *
 * @return _MALI_OSK_ERR_OK if the policy could be initialized, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t pmm_policy_init( _mali_pmm_internal_state_t *pmm );

/** @brief Common policy termination
 *
 * This will terminate the current policy.
 * @note This can be called when a policy has not been initialized
 */
void pmm_policy_term( _mali_pmm_internal_state_t *pmm );

/** @brief Common policy state changer
 *
 * Given the next available event message, this routine passes it to
 * the current policy for processing
 *
 * @param pmm internal PMM state
 * @param event PMM event to process
 * @return _MALI_OSK_ERR_OK if the policy state completed okay, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t pmm_policy_process( _mali_pmm_internal_state_t *pmm, mali_pmm_message_t *event );


/** @brief Common policy checker
 *
 * If a policy timer fires then this function will be called to
 * allow the policy to take the correct action
 *
 * @param pmm internal PMM state
 */
void pmm_policy_check_policy( _mali_pmm_internal_state_t *pmm );

/** @} */ /* End group pmmapi_policy */
/** @} */ /* End group pmmapi */

#ifdef __cplusplus
}
#endif

#endif /* __MALI_PMM_POLICY_H__ */
