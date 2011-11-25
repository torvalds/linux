/**
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_kernel_pm.c
 * Implementation of the Linux Power Management for Mali GPU kernel driver
 */

#if USING_MALI_PMM
#include <linux/sched.h>

#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif /* CONFIG_PM_RUNTIME */

#include <linux/platform_device.h>
#include <linux/version.h>
#include <asm/current.h>
#include <linux/suspend.h>

#include "mali_platform.h" 
#include "mali_osk.h"
#include "mali_uk_types.h"
#include "mali_pmm.h"
#include "mali_ukk.h"
#include "mali_kernel_common.h"
#include "mali_kernel_license.h"
#include "mali_kernel_pm.h"
#include "mali_device_pause_resume.h"
#include "mali_linux_pm.h"

#if MALI_GPU_UTILIZATION
#include "mali_kernel_utilization.h"
#endif /* MALI_GPU_UTILIZATION */

#if MALI_POWER_MGMT_TEST_SUITE
#ifdef CONFIG_PM
#include "mali_linux_pm_testsuite.h"
#include "mali_platform_pmu_internal_testing.h"
unsigned int pwr_mgmt_status_reg = 0;
#endif /* CONFIG_PM */
#endif /* MALI_POWER_MGMT_TEST_SUITE */

static int is_os_pmm_thread_waiting = 0;

/* kernel should be configured with power management support */
#ifdef CONFIG_PM

/* License should be GPL */
#if MALI_LICENSE_IS_GPL

/* Linux kernel major version */
#define LINUX_KERNEL_MAJOR_VERSION 2

/* Linux kernel minor version */
#define LINUX_KERNEL_MINOR_VERSION 6

/* Linux kernel development version */
#define LINUX_KERNEL_DEVELOPMENT_VERSION 29

#ifdef CONFIG_PM_DEBUG
static const char* const mali_states[_MALI_MAX_DEBUG_OPERATIONS] = {
	[_MALI_DEVICE_SUSPEND] = "suspend",
	[_MALI_DEVICE_RESUME] = "resume",
	[_MALI_DVFS_PAUSE_EVENT] = "dvfs_pause",
	[_MALI_DVFS_RESUME_EVENT] = "dvfs_resume",
};

#endif /* CONFIG_PM_DEBUG */

#ifdef CONFIG_PM_RUNTIME
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
static int mali_pwr_suspend_notifier(struct notifier_block *nb,unsigned long event,void* dummy);

static struct notifier_block mali_pwr_notif_block = {
	.notifier_call = mali_pwr_suspend_notifier
};
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */

/* Power management thread pointer */
struct task_struct *pm_thread;

/* dvfs power management thread */
struct task_struct *dvfs_pm_thread;

/* is wake up needed */
short is_wake_up_needed = 0;
int timeout_fired = 2;
unsigned int is_mali_pmm_testsuite_enabled = 0;

_mali_device_power_states mali_device_state = _MALI_DEVICE_RESUME;
_mali_device_power_states mali_dvfs_device_state = _MALI_DEVICE_RESUME;
_mali_osk_lock_t *lock;

#if MALI_POWER_MGMT_TEST_SUITE

const char* const mali_pmm_recording_events[_MALI_DEVICE_MAX_PMM_EVENTS] = {
	[_MALI_DEVICE_PMM_TIMEOUT_EVENT] = "timeout",
	[_MALI_DEVICE_PMM_JOB_SCHEDULING_EVENTS] = "job_scheduling",
	[_MALI_DEVICE_PMM_REGISTERED_CORES] = "cores",

};

unsigned int mali_timeout_event_recording_on = 0;
unsigned int mali_job_scheduling_events_recording_on = 0;
unsigned int is_mali_pmu_present = 0;
#endif /* MALI_POWER_MGMT_TEST_SUITE */

/* Function prototypes */
static int mali_pm_probe(struct platform_device *pdev);
static int mali_pm_remove(struct platform_device *pdev);

/* Mali device suspend function */
static int mali_pm_suspend(struct device *dev);

/* Mali device resume function */
static int mali_pm_resume(struct device *dev);

/* Run time suspend and resume functions */
#ifdef CONFIG_PM_RUNTIME
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
static int mali_device_runtime_suspend(struct device *dev);
static int mali_device_runtime_resume(struct device *dev);
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */

/* OS suspend and resume callbacks */
#if !MALI_PMM_RUNTIME_JOB_CONTROL_ON
#ifndef CONFIG_PM_RUNTIME
#if (LINUX_VERSION_CODE < KERNEL_VERSION(LINUX_KERNEL_MAJOR_VERSION,LINUX_KERNEL_MINOR_VERSION,LINUX_KERNEL_DEVELOPMENT_VERSION))
static int mali_pm_os_suspend(struct platform_device *pdev, pm_message_t state);
#else
static int mali_pm_os_suspend(struct device *dev);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(LINUX_KERNEL_MAJOR_VERSION,LINUX_KERNEL_MINOR_VERSION,LINUX_KERNEL_DEVELOPMENT_VERSION))
static int mali_pm_os_resume(struct platform_device *pdev);
#else
static int mali_pm_os_resume(struct device *dev);
#endif
#endif /* CONFIG_PM_RUNTIME */
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */

/* OS Hibernation suspend callback */
static int mali_pm_os_suspend_on_hibernation(struct device *dev);

/* OS Hibernation resume callback */
static int mali_pm_os_resume_on_hibernation(struct device *dev);

static void _mali_release_pm(struct device* device);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(LINUX_KERNEL_MAJOR_VERSION,LINUX_KERNEL_MINOR_VERSION,LINUX_KERNEL_DEVELOPMENT_VERSION))
static const struct dev_pm_ops mali_dev_pm_ops = {

#ifdef CONFIG_PM_RUNTIME
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
	.runtime_suspend = mali_device_runtime_suspend,
	.runtime_resume = mali_device_runtime_resume,
#endif  /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif  /* CONFIG_PM_RUNTIME */

#ifndef CONFIG_PM_RUNTIME
#if !MALI_PMM_RUNTIME_JOB_CONTROL_ON
	.suspend = mali_pm_os_suspend,
	.resume = mali_pm_os_resume,
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */
	.freeze = mali_pm_os_suspend_on_hibernation,
	.poweroff = mali_pm_os_suspend_on_hibernation,
	.thaw = mali_pm_os_resume_on_hibernation,
	.restore = mali_pm_os_resume_on_hibernation,
};
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(LINUX_KERNEL_MAJOR_VERSION,LINUX_KERNEL_MINOR_VERSION,LINUX_KERNEL_DEVELOPMENT_VERSION))
struct pm_ext_ops mali_pm_operations = {
	.base = {
		.freeze = mali_pm_os_suspend_on_hibernation,
		.thaw =   mali_pm_os_resume_on_hibernation,
		.poweroff = mali_pm_os_resume_on_hibernation,
		.restore = mali_pm_os_resume_on_hibernation,
	},
};
#endif

static struct platform_driver mali_plat_driver = {
	.probe  = mali_pm_probe,
	.remove = mali_pm_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(LINUX_KERNEL_MAJOR_VERSION,LINUX_KERNEL_MINOR_VERSION,LINUX_KERNEL_DEVELOPMENT_VERSION))
#ifndef CONFIG_PM_RUNTIME
#if !MALI_PMM_RUNTIME_JOB_CONTROL_ON
	.suspend = mali_pm_os_suspend,
	.resume  = mali_pm_os_resume,
#endif /* CONFIG_PM_RUNTIME */
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
	.pm = &mali_pm_operations,
#endif

	.driver         = {
		.name   = "mali_dev",
		.owner  = THIS_MODULE,
		.bus = &platform_bus_type,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(LINUX_KERNEL_MAJOR_VERSION,LINUX_KERNEL_MINOR_VERSION,LINUX_KERNEL_DEVELOPMENT_VERSION))
		.pm = &mali_dev_pm_ops,
#endif
		},
};

/* Mali GPU platform device */
struct platform_device mali_gpu_device = {
	.name = "mali_dev",
	.id = 0,
	.dev.release = _mali_release_pm
};

/** This function is called when platform device is unregistered. This function
 * is necessary when the platform device is unregistered.
 */
static void _mali_release_pm(struct device *device)
{
	MALI_DEBUG_PRINT(4, ("OSPMM: MALI Platform device removed\n" ));
}

#if MALI_POWER_MGMT_TEST_SUITE
void mali_is_pmu_present(void)
{
	int temp = 0;
	temp = pmu_get_power_up_down_info();
	if (4095 == temp)
	{
		is_mali_pmu_present = 0;
	}
	else
	{
		is_mali_pmu_present = 1;
	}
}
#endif /* MALI_POWER_MGMT_TEST_SUITE */
#endif /* MALI_LICENSE_IS_GPL */

#if MALI_LICENSE_IS_GPL

static int mali_wait_for_power_management_policy_event(void)
{
	int err = 0;
	for (; ;)
	{
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current))
		{
			err = -EINTR;
			break;
		}
		if (is_wake_up_needed == 1)
		{
			break;
		}
		schedule();
	}
	__set_current_state(TASK_RUNNING);
	is_wake_up_needed =0;
	return err;
}

/** This function is invoked when mali device is suspended
 */
int mali_device_suspend(unsigned int event_id, struct task_struct **pwr_mgmt_thread)
{
	int err = 0;	
	_mali_uk_pmm_message_s event = {
	                               NULL,
	                               event_id,
	                               timeout_fired};
	*pwr_mgmt_thread = current;
	MALI_DEBUG_PRINT(4, ("OSPMM: MALI device is being suspended\n" ));
	_mali_ukk_pmm_event_message(&event);
	is_os_pmm_thread_waiting = 1;
	err = mali_wait_for_power_management_policy_event();
	is_os_pmm_thread_waiting = 0;
	return err;
}

/** This function is called when Operating system wants to power down
 * the mali GPU device.
 */
static int mali_pm_suspend(struct device *dev)
{
	int err = 0;
	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);
#if MALI_GPU_UTILIZATION
	mali_utilization_suspend();
#endif /* MALI_GPU_UTILIZATION */
	if ((mali_device_state == _MALI_DEVICE_SUSPEND))
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return err;
	}
	err = mali_device_suspend(MALI_PMM_EVENT_OS_POWER_DOWN, &pm_thread);
	mali_device_state = _MALI_DEVICE_SUSPEND;
	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
	return err;
}

#ifndef CONFIG_PM_RUNTIME
#if !MALI_PMM_RUNTIME_JOB_CONTROL_ON
#if (LINUX_VERSION_CODE < KERNEL_VERSION(LINUX_KERNEL_MAJOR_VERSION,LINUX_KERNEL_MINOR_VERSION,LINUX_KERNEL_DEVELOPMENT_VERSION))
static int mali_pm_os_suspend(struct platform_device *pdev, pm_message_t state)
#else
static int mali_pm_os_suspend(struct device *dev)
#endif
{
	int err = 0;
	err = mali_pm_suspend(NULL);
	return err;
}
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */

#ifdef CONFIG_PM_RUNTIME
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
static int mali_pwr_suspend_notifier(struct notifier_block *nb,unsigned long event,void* dummy)
{
	int err = 0;
	switch (event)
	{
		case PM_SUSPEND_PREPARE:
			err = mali_pm_suspend(NULL);
		break;

		case PM_POST_SUSPEND:
			err = mali_pm_resume(NULL);
		break;
		default:
		break;
	}
	return 0;
}
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */

/** This function is called when mali GPU device is to be resumed.
 */
int mali_device_resume(unsigned int event_id, struct task_struct **pwr_mgmt_thread)
{
	int err = 0;
	_mali_uk_pmm_message_s event = {
	                               NULL,
	                               event_id,
	                               timeout_fired};
	*pwr_mgmt_thread = current;
	MALI_DEBUG_PRINT(4, ("OSPMM: MALI device is being resumed\n" ));
	_mali_ukk_pmm_event_message(&event);
	MALI_DEBUG_PRINT(4, ("OSPMM: MALI Power up  event is scheduled\n" ));
	is_os_pmm_thread_waiting = 1;
	err = mali_wait_for_power_management_policy_event();
	is_os_pmm_thread_waiting = 0;
	return err;
}

/** This function is called when mali GPU device is to be resumed
 */

static int mali_pm_resume(struct device *dev)
{
	int err = 0;
	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);
	if (mali_device_state == _MALI_DEVICE_RESUME)
	{
		_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
		return err;
	}
	err = mali_device_resume(MALI_PMM_EVENT_OS_POWER_UP, &pm_thread);
	mali_device_state = _MALI_DEVICE_RESUME;
	mali_dvfs_device_state = _MALI_DEVICE_RESUME;
	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
	return err;
}

#ifndef CONFIG_PM_RUNTIME
#if !MALI_PMM_RUNTIME_JOB_CONTROL_ON
#if (LINUX_VERSION_CODE < KERNEL_VERSION(LINUX_KERNEL_MAJOR_VERSION,LINUX_KERNEL_MINOR_VERSION,LINUX_KERNEL_DEVELOPMENT_VERSION))
static int mali_pm_os_resume(struct platform_device *pdev)
#else
static int mali_pm_os_resume(struct device *dev)
#endif
{
	int err = 0;
	err = mali_pm_resume(NULL);
	return err;
}
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */

static int mali_pm_os_suspend_on_hibernation(struct device *dev)
{
	int err = 0;
	err = mali_pm_suspend(NULL);
	return err;
}

static int mali_pm_os_resume_on_hibernation(struct device *dev)
{
	int err = 0;
	err = mali_pm_resume(NULL);
	return err;
}

#ifdef CONFIG_PM_RUNTIME
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
/** This function is called when runtime suspend of mali device is required.
 */
static int mali_device_runtime_suspend(struct device *dev)
{
	MALI_DEBUG_PRINT(4, ("PMMDEBUG: Mali device Run time suspended \n" ));
	return 0;
}

/** This function is called when runtime resume of mali device is required.
 */
static int mali_device_runtime_resume(struct device *dev)
{
	MALI_DEBUG_PRINT(4, ("PMMDEBUG: Mali device Run time Resumed \n" ));
	return 0;
}
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /*  CONFIG_PM_RUNTIME */

#ifdef CONFIG_PM_DEBUG

/** This function is used for debugging purposes when the user want to see
 * which power management operations are supported for
 * mali device.
 */
static ssize_t show_file(struct device *dev, struct device_attribute *attr, char *buf)
{
	char *str = buf;
#if !MALI_POWER_MGMT_TEST_SUITE
	int pm_counter = 0;
	for (pm_counter = 0; pm_counter<_MALI_MAX_DEBUG_OPERATIONS; pm_counter++) 
	{
		str += sprintf(str, "%s  ", mali_states[pm_counter]);
	}
#else
	str += sprintf(str, "%d  ",pwr_mgmt_status_reg);
#endif
	if (str != buf)
	{
		*(str-1) = '\n';
	}
	return (str-buf);
}

/** This function is called when user wants to suspend the mali GPU device in order
 * to simulate the power up and power down events.
 */
static ssize_t store_file(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;

#if MALI_POWER_MGMT_TEST_SUITE
	int test_flag_dvfs = 0;
        pwr_mgmt_status_reg = 0;
        mali_is_pmu_present();

#endif
	if (!strncmp(buf,mali_states[_MALI_DEVICE_SUSPEND],strlen(mali_states[_MALI_DEVICE_SUSPEND])))
	{
		MALI_DEBUG_PRINT(4, ("PMMDEBUG: MALI suspend Power operation is scheduled\n" ));
		err = mali_pm_suspend(NULL);
	}

#if MALI_POWER_MGMT_TEST_SUITE
	else if (!strncmp(buf,mali_pmm_recording_events[_MALI_DEVICE_PMM_REGISTERED_CORES],strlen(mali_pmm_recording_events[_MALI_DEVICE_PMM_REGISTERED_CORES])))
	{
		MALI_DEBUG_PRINT(4, ("PMMDEBUG: MALI Device get number of registerd cores\n" ));
		pwr_mgmt_status_reg = _mali_pmm_cores_list();
		return count;
	}
	else if (!strncmp(buf,mali_pmm_recording_events[_MALI_DEVICE_PMM_TIMEOUT_EVENT],strlen(mali_pmm_recording_events[_MALI_DEVICE_PMM_TIMEOUT_EVENT])))
	{
		MALI_DEBUG_PRINT(4, ("PMMDEBUG: MALI timeout event recording is enabled\n" ));
		mali_timeout_event_recording_on = 1;
	}
	else if (!strncmp(buf,mali_pmm_recording_events[_MALI_DEVICE_PMM_JOB_SCHEDULING_EVENTS],strlen(mali_pmm_recording_events[_MALI_DEVICE_PMM_JOB_SCHEDULING_EVENTS])))
	{
		MALI_DEBUG_PRINT(4, ("PMMDEBUG: MALI Job scheduling  events recording is enabled\n" ));
		mali_job_scheduling_events_recording_on = 1;
	}
#endif /* MALI_POWER_MGMT_TEST_SUITE */

	else if (!strncmp(buf,mali_states[_MALI_DEVICE_RESUME],strlen(mali_states[_MALI_DEVICE_RESUME]))) 
	{
		MALI_DEBUG_PRINT(4, ("PMMDEBUG: MALI Resume Power operation is scheduled\n" ));
		err = mali_pm_resume(NULL);
	}
	else if (!strncmp(buf,mali_states[_MALI_DVFS_PAUSE_EVENT],strlen(mali_states[_MALI_DVFS_PAUSE_EVENT])))
	{
		MALI_DEBUG_PRINT(4, ("PMMDEBUG: MALI DVFS Pause Power operation is scheduled\n" ));
		err = mali_dev_pause();
#if MALI_POWER_MGMT_TEST_SUITE
		test_flag_dvfs = 1;
#endif /* MALI_POWER_MGMT_TEST_SUITE */
	}
	else if (!strncmp(buf,mali_states[_MALI_DVFS_RESUME_EVENT],strlen(mali_states[_MALI_DVFS_RESUME_EVENT])))
	{
		MALI_DEBUG_PRINT(4, ("PMMDEBUG: MALI DVFS Resume Power operation is scheduled\n" ));
		err = mali_dev_resume();
#if MALI_POWER_MGMT_TEST_SUITE
		test_flag_dvfs = 1;
#endif /* MALI_POWER_MGMT_TEST_SUITE */
	}
	else 
	{
		MALI_DEBUG_PRINT(4, ("PMMDEBUG: Invalid Power Mode Operation selected\n" ));
	}
#if MALI_POWER_MGMT_TEST_SUITE
	if (test_flag_dvfs == 1)
	{
		if (err)
		{
			pwr_mgmt_status_reg = 2;
		}
		else
		{
			pwr_mgmt_status_reg = 1;
		}
	}
	else
	{
		if (1 == is_mali_pmu_present)
		{
			pwr_mgmt_status_reg = pmu_get_power_up_down_info();
		}
	}
#endif /* MALI_POWER_MGMT_TEST_SUITE */
	return count;
}

/* Device attribute file */
static DEVICE_ATTR(file, 0644, show_file, store_file);
#endif /* CONFIG_PM_DEBUG */

static int mali_pm_remove(struct platform_device *pdev)
{
#ifdef CONFIG_PM_DEBUG
	device_remove_file(&mali_gpu_device.dev, &dev_attr_file);
#endif /* CONFIG_PM_DEBUG */
#ifdef CONFIG_PM_RUNTIME
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
	pm_runtime_disable(&pdev->dev);
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */
	return 0;
}

/** This function is called when the device is probed */
static int mali_pm_probe(struct platform_device *pdev)
{
#ifdef CONFIG_PM_DEBUG
	int err;
	err = device_create_file(&mali_gpu_device.dev, &dev_attr_file);
	if (err)
	{
		MALI_DEBUG_PRINT(4, ("PMMDEBUG: Error in creating device file\n" ));
	}
#endif /* CONFIG_PM_DEBUG */
	return 0;
}

/** This function is called when Mali GPU device is initialized
 */
int _mali_dev_platform_register(void)
{
	int err;
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON	
	set_mali_parent_power_domain((void *)&mali_gpu_device);
#endif

#ifdef CONFIG_PM_RUNTIME
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
	err = register_pm_notifier(&mali_pwr_notif_block);
	if (err)
	{
		return err;
	}
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */
	err = platform_device_register(&mali_gpu_device);
	lock = _mali_osk_lock_init((_mali_osk_lock_flags_t)( _MALI_OSK_LOCKFLAG_READERWRITER | _MALI_OSK_LOCKFLAG_ORDERED), 0, 0);
	if (!err) 
	{
		err = platform_driver_register(&mali_plat_driver);
		if (err)
		{
			_mali_osk_lock_term(lock);
#ifdef CONFIG_PM_RUNTIME
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
			unregister_pm_notifier(&mali_pwr_notif_block);
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */
			platform_device_unregister(&mali_gpu_device);
		}
	}
	return err;
}

/** This function is called when Mali GPU device is unloaded
 */
void _mali_dev_platform_unregister(void)
{
	_mali_osk_lock_term(lock);

#ifdef CONFIG_PM_RUNTIME
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
	unregister_pm_notifier(&mali_pwr_notif_block);
#endif /* MALI_PMM_RUNTIME_JOB_CONTROL_ON */
#endif /* CONFIG_PM_RUNTIME */

	platform_driver_unregister(&mali_plat_driver);
	platform_device_unregister(&mali_gpu_device);
}

int mali_get_ospmm_thread_state(void)
{
	return is_os_pmm_thread_waiting;
}

#endif /* MALI_LICENSE_IS_GPL */
#endif /* CONFIG_PM */

#if MALI_STATE_TRACKING
u32 mali_pmm_dump_os_thread_state( char *buf, u32 size )
{
	return snprintf(buf, size, "OSPMM: OS PMM thread is waiting: %s\n", is_os_pmm_thread_waiting ? "true" : "false");
}
#endif /* MALI_STATE_TRACKING */
#endif /* USING_MALI_PMM */
