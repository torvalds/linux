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
 * @file mali_linux_dvfs_pause_resume.c
 * Implementation of the Mali pause/resume functionality
 */
#if USING_MALI_PMM
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/module.h>
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_platform.h"
#include "mali_linux_pm.h"
#include "mali_linux_dvfs_pause_resume.h"
#include "mali_pmm.h"
#include "mali_kernel_license.h"
#ifdef CONFIG_PM
#if MALI_LICENSE_IS_GPL

/* Mali Pause Resume APIs */
int mali_dev_dvfs_pause()
{
	int err = 0;
	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);
	if ((mali_dvfs_device_state ==  _MALI_DEVICE_SUSPEND) || (mali_device_state == _MALI_DEVICE_SUSPEND_IN_PROGRESS)
	      || (mali_device_state == _MALI_DEVICE_SUSPEND))
	{
		err = -EPERM;
	}
	if ((mali_dvfs_device_state ==  _MALI_DEVICE_RESUME) && (!err))
	{
		mali_device_suspend(MALI_PMM_EVENT_DVFS_PAUSE, &dvfs_pm_thread);
		mali_dvfs_device_state = _MALI_DEVICE_SUSPEND;
	}
	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
	return err;
}

EXPORT_SYMBOL(mali_dev_dvfs_pause);

int mali_dev_dvfs_resume()
{
	int err = 0;
	_mali_osk_lock_wait(lock, _MALI_OSK_LOCKMODE_RW);
	if ((mali_dvfs_device_state == _MALI_DEVICE_RESUME) || (mali_device_state == _MALI_DEVICE_SUSPEND_IN_PROGRESS)
	     || (mali_device_state == _MALI_DEVICE_SUSPEND))
	{
		err = -EPERM;
	}
	if (!err)
	{
		mali_device_resume(MALI_PMM_EVENT_DVFS_RESUME, &dvfs_pm_thread);
		mali_dvfs_device_state = _MALI_DEVICE_RESUME;
	}
	_mali_osk_lock_signal(lock, _MALI_OSK_LOCKMODE_RW);
	return err;
}

EXPORT_SYMBOL(mali_dev_dvfs_resume);

#endif /* MALI_LICENSE_IS_GPL */
#endif /* CONFIG_PM */
#endif /* USING_MALI_PMM */
