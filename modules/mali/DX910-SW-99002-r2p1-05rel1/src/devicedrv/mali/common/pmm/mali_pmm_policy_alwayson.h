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
 * @file mali_pmm_policy_alwayson.h
 * Defines the power management module policy for always on
 */

#ifndef __MALI_PMM_POLICY_ALWAYSON_H__
#define __MALI_PMM_POLICY_ALWAYSON_H__

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup pmmapi_policy Power Management Module Policies
 *
 * @{
 */

/** @brief Always on policy initialization
 *
 * @return _MALI_OSK_ERR_OK if the policy could be initialized, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t pmm_policy_init_always_on(void);

/** @brief Always on policy termination
 */
void pmm_policy_term_always_on(void);

/** @brief Always on policy state changer
 *
 * Given the next available event message, this routine processes it
 * for the policy and changes state as needed.
 *
 * Always on policy will ignore all events and keep the Mali cores on
 * all the time
 *
 * @param pmm internal PMM state
 * @param event PMM event to process
 * @return _MALI_OSK_ERR_OK if the policy state completed okay, or a suitable
 * _mali_osk_errcode_t otherwise.
 */
_mali_osk_errcode_t pmm_policy_process_always_on( _mali_pmm_internal_state_t *pmm, mali_pmm_message_t *event );

/** @} */ /* End group pmmapi_policies */

#ifdef __cplusplus
}
#endif

#endif /* __MALI_PMM_POLICY_ALWAYSON_H__ */
