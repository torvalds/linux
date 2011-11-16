/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/fs.h>       /* file system operations */
#include <asm/uaccess.h>    /* user space access */

#include "mali_ukk.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_kernel_session_manager.h"
#include "mali_ukk_wrappers.h"

int gp_start_job_wrapper(struct mali_session_data *session_data, _mali_uk_gp_start_job_s __user *uargs)
{
    _mali_uk_gp_start_job_s kargs;
    _mali_osk_errcode_t err;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);
    MALI_CHECK_NON_NULL(session_data, -EINVAL);

	if (!access_ok(VERIFY_WRITE, uargs, sizeof(_mali_uk_gp_start_job_s)))
	{
		return -EFAULT;
	}

    if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_gp_start_job_s))) return -EFAULT;

    kargs.ctx = session_data;
    err = _mali_ukk_gp_start_job(&kargs);
    if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

    kargs.ctx = NULL; /* prevent kernel address to be returned to user space */
    if (0 != copy_to_user(uargs, &kargs, sizeof(_mali_uk_gp_start_job_s)))
	{
		/*
		 * If this happens, then user space will not know that the job was actually started,
		 * and if we return a queued job, then user space will still think that one is still queued.
		 * This will typically lead to a deadlock in user space.
		 * This could however only happen if user space deliberately passes a user buffer which
		 * passes the access_ok(VERIFY_WRITE) check, but isn't fully writable at the time of copy_to_user().
		 * The official Mali driver will never attempt to do that, and kernel space should not be affected.
		 * That is why we do not bother to do a complex rollback in this very very very rare case.
		 */
		return -EFAULT;
	}

    return 0;
}

int gp_abort_job_wrapper(struct mali_session_data *session_data, _mali_uk_gp_abort_job_s __user *uargs)
{
    _mali_uk_gp_abort_job_s kargs;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);
    MALI_CHECK_NON_NULL(session_data, -EINVAL);

    if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_gp_abort_job_s))) return -EFAULT;

    kargs.ctx = session_data;
    _mali_ukk_gp_abort_job(&kargs);

    return 0;
}


int gp_get_core_version_wrapper(struct mali_session_data *session_data, _mali_uk_get_gp_core_version_s __user *uargs)
{
    _mali_uk_get_gp_core_version_s kargs;
    _mali_osk_errcode_t err;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);
    MALI_CHECK_NON_NULL(session_data, -EINVAL);

    kargs.ctx = session_data;
    err =  _mali_ukk_get_gp_core_version(&kargs);
    if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

	/* no known transactions to roll-back */

    if (0 != put_user(kargs.version, &uargs->version)) return -EFAULT;

    return 0;
}

int gp_suspend_response_wrapper(struct mali_session_data *session_data, _mali_uk_gp_suspend_response_s __user *uargs)
{
    _mali_uk_gp_suspend_response_s kargs;
    _mali_osk_errcode_t err;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);
    MALI_CHECK_NON_NULL(session_data, -EINVAL);

    if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_gp_suspend_response_s))) return -EFAULT;

    kargs.ctx = session_data;
    err = _mali_ukk_gp_suspend_response(&kargs);
    if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

    if (0 != put_user(kargs.cookie, &uargs->cookie)) return -EFAULT;

    /* no known transactions to roll-back */
    return 0;
}

int gp_get_number_of_cores_wrapper(struct mali_session_data *session_data, _mali_uk_get_gp_number_of_cores_s __user *uargs)
{
    _mali_uk_get_gp_number_of_cores_s kargs;
    _mali_osk_errcode_t err;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);
    MALI_CHECK_NON_NULL(session_data, -EINVAL);

    kargs.ctx = session_data;
    err = _mali_ukk_get_gp_number_of_cores(&kargs);
    if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

	/* no known transactions to roll-back */

    if (0 != put_user(kargs.number_of_cores, &uargs->number_of_cores)) return -EFAULT;

    return 0;
}
