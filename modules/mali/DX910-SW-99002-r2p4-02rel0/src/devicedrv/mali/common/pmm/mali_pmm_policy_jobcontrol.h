/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
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

#ifndef __MALI_PMM_POLICY_JOBCONTROL_H__
#define __MALI_PMM_POLICY_JOBCONTROL_H__

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup pmmapi_policy Power Management Module Policies
 *
 * @{
 */

/** @brief The jobcontrol policy inactivity latency timeout (in ticks)
 * before the hardware is switched off
 *
 * @note Setting this low whilst tracing or producing debug output can
 * cause alot of timeouts to fire which can affect the PMM behaviour
 */
#define MALI_PMM_POLICY_JOBCONTROL_INACTIVITY_TIMEOUT 50

/** @brief Job control policy initialization
 *
 * @return _MALI_OSK_ERR_OK if the policy could be initialized, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t pmm_policy_init_job_control(_mali_pmm_internal_state_t *pmm);

/** @brief Job control policy termination
 */
void pmm_policy_term_job_control(void);

/** @brief Job control policy state changer
 *
 * Given the next available event message, this routine processes it
 * for the policy and changes state as needed.
 *
 * Job control policy depends on events from the Mali cores, and will
 * power down all cores after an inactivity latency timeout. It will
 * power the cores back on again when a job is scheduled to run.
 *
 * @param pmm internal PMM state
 * @param event PMM event to process
 * @return _MALI_OSK_ERR_OK if the policy state completed okay, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t pmm_policy_process_job_control( _mali_pmm_internal_state_t *pmm, mali_pmm_message_t *event );

/** @brief Job control policy checker
 *
 * The latency timer has fired and we need to raise the correct event to
 * handle it
 *
 * @param pmm internal PMM state
 */
void pmm_policy_check_job_control(void);

/** @} */ /* End group pmmapi_policy */

#ifdef __cplusplus
}
#endif

#endif /* __MALI_PMM_POLICY_JOBCONTROL_H__ */
