/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _UAPI_KBASE_CSF_ERRORS_DUMPFAULT_H_
#define _UAPI_KBASE_CSF_ERRORS_DUMPFAULT_H_

/**
 * enum dumpfault_error_type - Enumeration to define errors to be dumped
 *
 * @DF_NO_ERROR:                       No pending error
 * @DF_CSG_SUSPEND_TIMEOUT:            CSG suspension timeout
 * @DF_CSG_TERMINATE_TIMEOUT:          CSG group termination timeout
 * @DF_CSG_START_TIMEOUT:              CSG start timeout
 * @DF_CSG_RESUME_TIMEOUT:             CSG resume timeout
 * @DF_CSG_EP_CFG_TIMEOUT:             CSG end point configuration timeout
 * @DF_CSG_STATUS_UPDATE_TIMEOUT:      CSG status update timeout
 * @DF_PROGRESS_TIMER_TIMEOUT:         Progress timer timeout
 * @DF_FW_INTERNAL_ERROR:              Firmware internal error
 * @DF_CS_FATAL:                       CS fatal error
 * @DF_CS_FAULT:                       CS fault error
 * @DF_FENCE_WAIT_TIMEOUT:             Fence wait timeout
 * @DF_PROTECTED_MODE_EXIT_TIMEOUT:    P.mode exit timeout
 * @DF_PROTECTED_MODE_ENTRY_FAILURE:   P.mode entrance failure
 * @DF_PING_REQUEST_TIMEOUT:           Ping request timeout
 * @DF_CORE_DOWNSCALE_REQUEST_TIMEOUT: DCS downscale request timeout
 * @DF_TILER_OOM:                      Tiler Out-of-memory error
 * @DF_GPU_PAGE_FAULT:                 GPU page fault
 * @DF_BUS_FAULT:                      MMU BUS Fault
 * @DF_GPU_PROTECTED_FAULT:            GPU P.mode fault
 * @DF_AS_ACTIVE_STUCK:                AS active stuck
 * @DF_GPU_SOFT_RESET_FAILURE:         GPU soft reset falure
 *
 * This is used for kbase to notify error type of an event whereby
 * user space client will dump relevant debugging information via debugfs.
 * @DF_NO_ERROR is used to indicate no pending fault, thus the client will
 * be blocked on reading debugfs file till a fault happens.
 */
enum dumpfault_error_type {
	DF_NO_ERROR = 0,
	DF_CSG_SUSPEND_TIMEOUT,
	DF_CSG_TERMINATE_TIMEOUT,
	DF_CSG_START_TIMEOUT,
	DF_CSG_RESUME_TIMEOUT,
	DF_CSG_EP_CFG_TIMEOUT,
	DF_CSG_STATUS_UPDATE_TIMEOUT,
	DF_PROGRESS_TIMER_TIMEOUT,
	DF_FW_INTERNAL_ERROR,
	DF_CS_FATAL,
	DF_CS_FAULT,
	DF_FENCE_WAIT_TIMEOUT,
	DF_PROTECTED_MODE_EXIT_TIMEOUT,
	DF_PROTECTED_MODE_ENTRY_FAILURE,
	DF_PING_REQUEST_TIMEOUT,
	DF_CORE_DOWNSCALE_REQUEST_TIMEOUT,
	DF_TILER_OOM,
	DF_GPU_PAGE_FAULT,
	DF_BUS_FAULT,
	DF_GPU_PROTECTED_FAULT,
	DF_AS_ACTIVE_STUCK,
	DF_GPU_SOFT_RESET_FAILURE,
};

#endif /* _UAPI_KBASE_CSF_ERRORS_DUMPFAULT_H_ */
