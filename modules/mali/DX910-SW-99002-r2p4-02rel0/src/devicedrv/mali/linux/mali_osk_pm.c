/**
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_pm.c
 * Implementation of the callback functions from common power management
 */

#include <linux/sched.h>

#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif /* CONFIG_PM_RUNTIME */

#include <linux/platform_device.h>

#include "mali_platform.h"
#include "mali_osk.h"
#include "mali_uk_types.h"
#include "mali_pmm.h"
#include "mali_kernel_common.h"
#include "mali_kernel_license.h"
#include "mali_linux_pm.h"
#include "mali_linux_pm_testsuite.h"

#if MALI_LICENSE_IS_GPL
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
#ifdef CONFIG_PM_RUNTIME
static int is_runtime =0;
#endif /* CONFIG_PM_RUNTIME */
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* MALI_LICENSE_IS_GPL */

#if MALI_POWER_MGMT_TEST_SUITE

#ifdef CONFIG_PM
unsigned int mali_pmm_events_triggered_mask = 0;
#endif /* CONFIG_PM */

void _mali_osk_pmm_policy_events_notifications(mali_pmm_event_id mali_pmm_event)
{
#if MALI_LICENSE_IS_GPL
#ifdef CONFIG_PM

	switch (mali_pmm_event)
	{
		case MALI_PMM_EVENT_JOB_QUEUED:
			if (mali_job_scheduling_events_recording_on == 1)
			{
				mali_pmm_events_triggered_mask |= (1<<0);
			}
		break;

		case MALI_PMM_EVENT_JOB_SCHEDULED:
			if (mali_job_scheduling_events_recording_on == 1)
			{
				mali_pmm_events_triggered_mask |= (1<<1);
			}
		break;

		case MALI_PMM_EVENT_JOB_FINISHED:
			if (mali_job_scheduling_events_recording_on == 1)
			{
				mali_pmm_events_triggered_mask |= (1<<2);
				mali_job_scheduling_events_recording_on = 0;
				pwr_mgmt_status_reg = mali_pmm_events_triggered_mask;
			}
		break;

		case MALI_PMM_EVENT_TIMEOUT:
			if (mali_timeout_event_recording_on == 1)
			{
				pwr_mgmt_status_reg = (1<<3);
				mali_timeout_event_recording_on = 0;
			}
		break;

		default:

		break;

	}
#endif /* CONFIG_PM */

#endif /* MALI_LICENSE_IS_GPL */
}
#endif /* MALI_POWER_MGMT_TEST_SUITE */

/** This function is called when the Mali device has completed power up
 * operation.
 */
void _mali_osk_pmm_power_up_done(mali_pmm_message_data data)
{
#if MALI_LICENSE_IS_GPL
#ifdef CONFIG_PM
	is_wake_up_needed = 1;
	wake_up_process(pm_thread);
	MALI_DEBUG_PRINT(4, ("OSPMM: MALI OSK Power up Done\n" ));
	return;
#endif /* CONFIG_PM */
#endif /* MALI_LICENSE_IS_GPL */
}

/** This function is called when the Mali device has completed power down
 * operation.
 */
void _mali_osk_pmm_power_down_done(mali_pmm_message_data data)
{
#if MALI_LICENSE_IS_GPL
#ifdef CONFIG_PM
	is_wake_up_needed = 1;
#if MALI_POWER_MGMT_TEST_SUITE
	if (is_mali_pmu_present == 0)
	{
		pwr_mgmt_status_reg = _mali_pmm_cores_list();
	}
#endif /* MALI_POWER_MGMT_TEST_SUITE */
	wake_up_process(pm_thread);
	MALI_DEBUG_PRINT(4, ("OSPMM: MALI Power down Done\n" ));
	return;

#endif /* CONFIG_PM */
#endif /* MALI_LICENSE_IS_GPL */
}

/** This function is invoked when mali device is idle.
*/
_mali_osk_errcode_t _mali_osk_pmm_dev_idle(void)
{
	_mali_osk_errcode_t err = 0;
#if MALI_LICENSE_IS_GPL
#ifdef CONFIG_PM_RUNTIME
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON

	err = pm_runtime_put_sync(&(mali_gpu_device.dev));	
	if(err)
	{
		MALI_DEBUG_PRINT(4, ("OSPMM: Error in _mali_osk_pmm_dev_idle\n" ));	
	}
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */
#endif /* MALI_LICENSE_IS_GPL */
	return err;
}

/** This funtion is invoked when mali device needs to be activated.
*/
void _mali_osk_pmm_dev_activate(void)
{
	
#if MALI_LICENSE_IS_GPL
#ifdef CONFIG_PM_RUNTIME
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
	int err = 0;
	if(is_runtime == 0)
	{
		pm_suspend_ignore_children(&(mali_gpu_device.dev), true);
		pm_runtime_enable(&(mali_gpu_device.dev));
 		pm_runtime_get_sync(&(mali_gpu_device.dev));
		is_runtime = 1;
	}
	else
	{
		err = pm_runtime_get_sync(&(mali_gpu_device.dev));
	}
	if(err)
        {
		MALI_DEBUG_PRINT(4, ("OSPMM: Error in _mali_osk_pmm_dev_activate\n" ));
        }
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */
#endif /* MALI_LICENSE_IS_GPL */
}

void _mali_osk_pmm_ospmm_cleanup( void )
{
#if MALI_LICENSE_IS_GPL
#ifdef CONFIG_PM
	int thread_state;
	thread_state = mali_get_ospmm_thread_state();
	if (thread_state)
	{
		_mali_osk_pmm_dvfs_operation_done(0);
	}
#endif /* CONFIG_PM */
#endif /* MALI_LICENSE_IS_GPL */
}

void _mali_osk_pmm_dvfs_operation_done(mali_pmm_message_data data)
{
#if MALI_LICENSE_IS_GPL
#ifdef CONFIG_PM
	is_wake_up_needed = 1;
	wake_up_process(dvfs_pm_thread);
	MALI_DEBUG_PRINT(4, ("OSPMM: MALI OSK DVFS Operation done\n" ));
	return;
#endif /* CONFIG_PM */
#endif /* MALI_LICENSE_IS_GPL */
}


