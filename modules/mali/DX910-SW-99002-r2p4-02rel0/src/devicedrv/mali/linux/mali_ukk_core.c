/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/fs.h>       /* file system operations */
#include <linux/slab.h>     /* memort allocation functions */
#include <asm/uaccess.h>    /* user space access */

#include "mali_ukk.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_kernel_session_manager.h"
#include "mali_ukk_wrappers.h"

int get_api_version_wrapper(struct mali_session_data *session_data, _mali_uk_get_api_version_s __user *uargs)
{
	_mali_uk_get_api_version_s kargs;
    _mali_osk_errcode_t err;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);

    if (0 != get_user(kargs.version, &uargs->version)) return -EFAULT;

    kargs.ctx = session_data;
    err = _mali_ukk_get_api_version(&kargs);
    if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

    if (0 != put_user(kargs.version, &uargs->version)) return -EFAULT;
    if (0 != put_user(kargs.compatible, &uargs->compatible)) return -EFAULT;

    return 0;
}

int get_system_info_size_wrapper(struct mali_session_data *session_data, _mali_uk_get_system_info_size_s __user *uargs)
{
	_mali_uk_get_system_info_size_s kargs;
    _mali_osk_errcode_t err;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);

    kargs.ctx = session_data;
    err = _mali_ukk_get_system_info_size(&kargs);
    if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

    if (0 != put_user(kargs.size, &uargs->size)) return -EFAULT;

    return 0;
}

int get_system_info_wrapper(struct mali_session_data *session_data, _mali_uk_get_system_info_s __user *uargs)
{
	_mali_uk_get_system_info_s kargs;
    _mali_osk_errcode_t err;
    _mali_system_info *system_info_user;
    _mali_system_info *system_info_kernel;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);

    if (0 != get_user(kargs.system_info, &uargs->system_info)) return -EFAULT;
    if (0 != get_user(kargs.size, &uargs->size)) return -EFAULT;

    /* A temporary kernel buffer for the system_info datastructure is passed through the system_info
     * member. The ukk_private member will point to the user space destination of this buffer so
     * that _mali_ukk_get_system_info() can correct the pointers in the system_info correctly
     * for user space.
     */
    system_info_kernel = kmalloc(kargs.size, GFP_KERNEL);
    if (NULL == system_info_kernel) return -EFAULT;

    system_info_user = kargs.system_info;
    kargs.system_info = system_info_kernel;
    kargs.ukk_private = (u32)system_info_user;
    kargs.ctx = session_data;

    err = _mali_ukk_get_system_info(&kargs);
    if (_MALI_OSK_ERR_OK != err)
    {
        kfree(system_info_kernel);
        return map_errcode(err);
    }

    if (0 != copy_to_user(system_info_user, system_info_kernel, kargs.size))
    {
        kfree(system_info_kernel);
        return -EFAULT;
    }

    kfree(system_info_kernel);
    return 0;
}

int wait_for_notification_wrapper(struct mali_session_data *session_data, _mali_uk_wait_for_notification_s __user *uargs)
{
    _mali_uk_wait_for_notification_s kargs;
    _mali_osk_errcode_t err;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);

    kargs.ctx = session_data;
    err = _mali_ukk_wait_for_notification(&kargs);
    if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

	if(_MALI_NOTIFICATION_CORE_SHUTDOWN_IN_PROGRESS != kargs.type)
	{
		kargs.ctx = NULL; /* prevent kernel address to be returned to user space */
		if (0 != copy_to_user(uargs, &kargs, sizeof(_mali_uk_wait_for_notification_s))) return -EFAULT;
	}
	else
	{
		if (0 != put_user(kargs.type, &uargs->type)) return -EFAULT;
	}

    return 0;
}

int post_notification_wrapper(struct mali_session_data *session_data, _mali_uk_post_notification_s __user *uargs)
{
	_mali_uk_post_notification_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);

	kargs.ctx = session_data;

	if (0 != get_user(kargs.type, &uargs->type))
	{
		return -EFAULT;
	}

	err = _mali_ukk_post_notification(&kargs);
	if (_MALI_OSK_ERR_OK != err)
	{
		return map_errcode(err);
	}

	return 0;
}
