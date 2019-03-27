/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _SCIF_TASK_REQUEST_H_
#define _SCIF_TASK_REQUEST_H_

/**
 * @file
 *
 * @brief This file contains the structures and interface methods that
 *        can be referenced and used by the SCI user for the SCI task
 *        management request object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>


/**
 * @brief This method simply returns the size required to construct an SCI
 *        based task request object (includes core & framework object size).
 *
 * @return Retrun the size of the SCI task request object.
 */
U32 scif_task_request_get_object_size(
   void
);

/**
 * @brief This method is called by the SCIF user to construct a task
 *        management request.  This method will construct a SCIC task request
 *        internally.
 *
 * @note  The SCI framework implementation will create an association between
 *        the user task request object and the framework task request object.
 *
 * @param[in]  scif_controller the handle to the framework controller object
 *             for which to build an IO request.
 * @param[in]  scif_remote_device This parameter specifies the framework
 *             remote device with which this task request is to be associated.
 * @param[in]  io_tag This parameter specifies the IO tag to be associated
 *             with this request.  If SCI_CONTROLLER_INVALID_IO_TAG is
 *             passed, then a copy of the request is built internally.  The
 *             request will be copied into the actual controller request
 *             memory when the IO tag is allocated internally during the
 *             scif_controller_start_task() method.
 * @param[in]  user_task_request_object This parameter specifies the user
 *             task request to be utilized during task construction.  This task
 *             pointer will become the associated object for the framework
 *             task request object.
 * @param[in]  task_request_memory This parameter specifies the memory
 *             to be utilized in the construction of the framework task request.
 * @param[in]  scif_task_request This parameter specifies the handle to be
 *             utilized for all further interactions with this task request
 *             object.
 *
 * @return Indicate if the controller successfully built the task request.
 * @retval SCI_SUCCESS This value is returned if the task request was
 *         successfully built.
 */
SCI_STATUS scif_task_request_construct(
   SCI_CONTROLLER_HANDLE_T      scif_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scif_remote_device,
   U16                          io_tag,
   void                       * user_task_request_object,
   void                       * task_request_memory,
   SCI_TASK_REQUEST_HANDLE_T  * scif_task_request
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_TASK_REQUEST_H_

