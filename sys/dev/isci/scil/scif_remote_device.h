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
#ifndef _SCIF_REMOTE_DEVICE_H_
#define _SCIF_REMOTE_DEVICE_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods that can be called
 *        by an SCI Framework user on a remote device object.  The
 *        framework remote device object provides management of resets for
 *        the remote device, IO thresholds, potentially NCQ tag management,
 *        etc.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>
#include <dev/isci/scil/intel_sas.h>


/**
 * This constant is utilized to inform the user that there is no defined
 * maximum request queue depth associated with a remote device.
 */
#define SCIF_REMOTE_DEVICE_NO_MAX_QUEUE_DEPTH  0xFFFF

/**
 * @brief This method simply returns the maximum memory space needed to
 *        store a remote device object.  The value returned includes enough
 *        space for the framework and core device objects.
 *
 * @return a positive integer value indicating the size (in bytes) of the
 *         remote device object.
 */
U32 scif_remote_device_get_object_size(
   void
);

/**
 * @brief This method performs the construction common to all device object
 *        types in the framework.
 *
 * @note
 *      - Remote device objects in the core are a limited resource.  Since
 *        the framework construction/destruction methods wrap the core, the
 *        user must ensure that a construct or destruct method is never
 *        invoked when an existing construct or destruct method is ongoing.
 *        This method shall be utilized for discovered direct attached
 *        devices.
 *      - It isn't necessary to call scif_remote_device_destruct() for
 *        device objects that have only called this method for construction.
 *        Once subsequent construction methods have been invoked (e.g.
 *        scif_remote_device_da_construct()), then destruction should occur.
 *
 * @param[in]  domain This parameter specifies the domain in which this
 *             remote device is contained.
 * @param[in]  remote_device_memory This parameter specifies the memory
 *             location into which this method shall construct the new
 *             framework device object.
 * @param[out] new_scif_remote_device_handle This parameter specifies the
 *             handle to be used to communicate with the newly constructed
 *             framework remote device.
 *
 * @return none
 */
void scif_remote_device_construct(
   SCI_DOMAIN_HANDLE_T          domain,
   void                       * remote_device_memory,
   SCI_REMOTE_DEVICE_HANDLE_T * new_scif_remote_device_handle
);

/**
 * @brief This method constructs a new framework remote device object.  The
 *        remote device object shall remember it's counterpart core device
 *        object as well as the domain in which it is contained.
 *
 * @note  Remote device objects in the core are a limited resource.  Since
 *        the framework construction/destruction methods wrap the core, the
 *        user must ensure that a construct or destruct method is never
 *        invoked when an existing construct or destruct method is ongoing.
 *        This method shall be utilized for discovered direct attached
 *        devices.
 *
 * @pre The user must have previously called scif_remote_device_construct()
 *
 * @param[in]  remote_device This parameter specifies the framework device
 *             for which to perform direct attached specific construction.
 * @param[in]  sas_address  This parameter specifies the SAS address of the
 *             remote device object being constructed.
 * @param[in]  protocols This parameter specifies the protocols supported
 *             by the remote device to be constructed.
 *
 * @return Indicate if the remote device was successfully constructed.
 * @retval SCI_SUCCESS This value is returned if the remote device was
 *         successfully constructed.
 * @retval SCI_FAILURE_DEVICE_EXISTS Returned if the device has already
 *         been constructed.
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES This value is returned if
 *         the core controller associated with the supplied parameters
 *         is unable to support additional remote devices.
 */
SCI_STATUS scif_remote_device_da_construct(
   SCI_REMOTE_DEVICE_HANDLE_T                   remote_device,
   SCI_SAS_ADDRESS_T                          * sas_address,
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T * protocols
);

/**
 * @brief This method constructs a new framework remote device object.  The
 *        remote device object shall remember it's counterpart core device
 *        object as well as the domain in which it is contained.
 *
 * @pre The user must have previously called scif_remote_device_construct()
 *
 * @note  Remote device objects in the core are a limited resource.  Since
 *        the framework construction/destruction methods wrap the core, the
 *        user must ensure that a construct or destruct method is never
 *        invoked when an existing construct or destruct method is ongoing.
 *        This method shall be utilized for discovered expander attached
 *        devices.
 *
 * @param[in]  remote_device This parameter specifies the framework device
 *             for which to perform expander specific construction.
 * @param[in]  containing_device This parameter specifies the remote
 *             device (i.e. an expander) that contains the device being
 *             constructed.
 * @param[in]  smp_response This parameter specifies the SMP_RESPONSE_DISCOVER
 *             associated with the remote device being constructed.
 *
 * @return Indicate if the remote device was successfully constructed.
 * @retval SCI_SUCCESS This value is returned if the remote device was
 *         successfully constructed.
 * @retval SCI_FAILURE_DEVICE_EXISTS Returned if the device has already
 *         been constructed.
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES This value is returned if
 *         the core controller associated with the supplied parameters
 *         is unable to support additional remote devices.
 */
SCI_STATUS scif_remote_device_ea_construct(
   SCI_REMOTE_DEVICE_HANDLE_T   remote_device,
   SCI_REMOTE_DEVICE_HANDLE_T   containing_device,
   SMP_RESPONSE_DISCOVER_T    * smp_response
);


/**
 * @brief This method is utilized to free up a framework's remote
 *        device object.
 *
 * @note  Remote device objects in the core are a limited resource.  Since
 *        the framework construction/destruction methods wrap the core, the
 *        user must ensure that a construct or destruct method is never
 *        invoked when an existing construct or destruct method is ongoing.
 *
 * @param[in]  remote_device This parameter specifies the remote device to be
 *             destructed.
 *
 * @return The return value shall indicate if the device was successfully
 *         destructed or if some failure occurred.
 * @retval SCI_STATUS This value is returned if the device is successfully
 *         destructed.
 * @retval SCI_FAILURE_INVALID_REMOTE_DEVICE This value is returned if the
 *         supplied device isn't valid (e.g. it's already been destructed,
 *         the handle isn't valid, etc.).
 */
SCI_STATUS scif_remote_device_destruct(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This method simply returns the SCI Core object handle that is
 *        associated with the supplied SCI Framework object.
 *
 * @param[in]  remote_device This parameter specifies the framework device
 *             for which to return the associated core remote device.
 *
 * @return This method returns a handle to the core remote device object
 *         associated with the framework remote device object.
 * @retval SCI_INVALID_HANDLE This return value indicates that the SCI Core
 *         remote device handle for the supplied framework device is invalid.
 */
SCI_REMOTE_DEVICE_HANDLE_T scif_remote_device_get_scic_handle(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This method returns the maximum queue depth supported for the
 *        supplied target by this SCI Framework impementation.
 *
 * @param[in]  remote_device This parameter specifies the framework
 *             device for which to return the maximum queue depth.
 *
 * @return This method returns a value indicating the maximum number of
 *         IO requests that can be outstanding for the target at any
 *         point in time.
 * @retval SCIF_REMOTE_DEVICE_NO_MAX_QUEUE_DEPTH This value is returned
 *         when there is no defined maximum queue depth for the target.
 */
U16 scif_remote_device_get_max_queue_depth(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This method will return the handle to the parent device of the
 *        remote device.
 *
 * @param[in]  remote_device This parameter specifies the device for which
 *             to return the parent device.
 * @param[out] containing_device This parameter specifies the device
 *             handle, from the remote device object, which indicate
 *             the parent device of the supplied remote_device.
 *
 * @return none
 */
SCI_STATUS scif_remote_device_get_containing_device(
   SCI_REMOTE_DEVICE_HANDLE_T          remote_device,
   SCI_REMOTE_DEVICE_HANDLE_T        * containing_device
);

/**
 * @brief This method returns the number of IO currently started
 *        to the supplied target.  It does not include task
 *        management requests.
 *
 * @param[in]  remote_device This parameter specifies the framework
 *             device for which to return the number of started IO.
 *
 * @return This method returns a value indicating the number of started
 *         IO requests.
 */
U32 scif_remote_device_get_started_io_count(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_REMOTE_DEVICE_H_

