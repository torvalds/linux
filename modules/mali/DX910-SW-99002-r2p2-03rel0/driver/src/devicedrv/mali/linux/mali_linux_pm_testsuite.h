/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef __MALI_LINUX_PM_TESTSUITE_H__
#define __MALI_LINUX_PM_TESTSUITE_H__

#if USING_MALI_PMM
#if MALI_POWER_MGMT_TEST_SUITE
#ifdef CONFIG_PM

typedef enum
{
        _MALI_DEVICE_PMM_TIMEOUT_EVENT,
        _MALI_DEVICE_PMM_JOB_SCHEDULING_EVENTS,
	_MALI_DEVICE_PMM_REGISTERED_CORES,
        _MALI_DEVICE_MAX_PMM_EVENTS

} _mali_device_pmm_recording_events;

extern unsigned int mali_timeout_event_recording_on;
extern unsigned int mali_job_scheduling_events_recording_on;
extern unsigned int pwr_mgmt_status_reg;
extern unsigned int is_mali_pmm_testsuite_enabled;
extern unsigned int is_mali_pmu_present;

#endif /* CONFIG_PM */
#endif /* MALI_POWER_MGMT_TEST_SUITE */
#endif /* USING_MALI_PMM */
#endif /* __MALI_LINUX_PM_TESTSUITE_H__ */


