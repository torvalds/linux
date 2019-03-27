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
#ifndef _SCIC_REMOTE_DEVICE_H_
#define _SCIC_REMOTE_DEVICE_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods that can be called
 *        by an SCIC user on the device object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>
#include <dev/isci/scil/intel_sas.h>


/**
 * @brief
 */
typedef enum SCIC_REMOTE_DEVICE_NOT_READY_REASON_CODE
{
   SCIC_REMOTE_DEVICE_NOT_READY_START_REQUESTED,
   SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED,
   SCIC_REMOTE_DEVICE_NOT_READY_SATA_REQUEST_STARTED,
   SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED,
   SCIC_REMOTE_DEVICE_NOT_READY_SMP_REQUEST_STARTED,

   SCIC_REMOTE_DEVICE_NOT_READY_REASON_CODE_MAX

} SCIC_REMOTE_DEVICE_NOT_READY_REASON_CODE_T;

/**
 * @brief This method simply returns the maximum memory space needed to
 *        store a remote device object.
 *
 * @return a positive integer value indicating the size (in bytes) of the
 *         remote device object.
 */
U32 scic_remote_device_get_object_size(
   void
);

/**
 * @brief This method will perform the construction common to all
 *        remote device objects.
 *
 * @note  It isn't necessary to call scic_remote_device_destruct() for
 *        device objects that have only called this method for construction.
 *        Once subsequent construction methods have been invoked (e.g.
 *        scic_remote_device_da_construct()), then destruction should occur.
 * @note
 *
 * @param[in]  port This parameter specifies the SAS/SATA Port handle
 *             corresponding to the port through which this device
 *             is to be accessed.
 * @param[in]  remote_device_memory This parameter specifies the memory
 *             location to be used by the SCIC implementation to store the
 *             SCIC REMOTE DEVICE.
 * @param[out] new_remote_device_handle An opaque remote device handle to
 *             be used by the SCIC user for all subsequent remote device
 *             operations.
 *
 * @return none
 */
void scic_remote_device_construct(
   SCI_PORT_HANDLE_T            port,
   void                       * remote_device_memory,
   SCI_REMOTE_DEVICE_HANDLE_T * new_remote_device_handle
);

/**
 * @brief This method will construct a SCIC_REMOTE_DEVICE object for a
 *        direct attached (da) device.  The information (e.g. IAF, Signature
 *        FIS, etc.) necessary to build the device is known to the SCI Core
 *        since it is contained in the scic_phy object.
 *
 * @pre The user must have previously called scic_remote_device_construct()
 *
 * @note  Remote device objects are a limited resource.  As such, they
 *        must be protected.  Thus calls to construct and destruct are
 *        mutually exclusive and non-reentrant.
 *
 * @param[in]  remote_device This parameter specifies the remote device to be
 *             destructed.
 *
 * @return Indicate if the remote device was successfully constructed.
 * @retval SCI_SUCCESS Returned if the device was successfully constructed.
 * @retval SCI_FAILURE_DEVICE_EXISTS Returned if the device has already
 *         been constructed.  If it's an additional phy for the target, then
 *         call scic_remote_device_da_add_phy().
 * @retval SCI_FAILURE_UNSUPPORTED_PROTOCOL Returned if the supplied
 *         parameters necessitate creation of a remote device for which
 *         the protocol is not supported by the underlying controller
 *         hardware.
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES This value is returned if
 *         the core controller associated with the supplied parameters
 *         is unable to support additional remote devices.
 */
SCI_STATUS scic_remote_device_da_construct(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This method will construct an SCIC_REMOTE_DEVICE object for an
 *        expander attached (ea) device from an SMP Discover Response.
 *
 * @pre The user must have previously called scic_remote_device_construct()
 *
 * @note  Remote device objects are a limited resource.  As such, they
 *        must be protected.  Thus calls to construct and destruct are
 *        mutually exclusive and non-reentrant.
 *
 * @param[in]  remote_device This parameter specifies the remote device to be
 *             destructed.
 * @param[in]  discover_response This parameter specifies the SMP
 *             Discovery Response to be used in device creation.
 *
 * @return Indicate if the remote device was successfully constructed.
 * @retval SCI_SUCCESS Returned if the device was successfully constructed.
 * @retval SCI_FAILURE_DEVICE_EXISTS Returned if the device has already
 *         been constructed.  If it's an additional phy for the target, then
 *         call scic_ea_remote_device_add_phy().
 * @retval SCI_FAILURE_UNSUPPORTED_PROTOCOL Returned if the supplied
 *         parameters necessitate creation of a remote device for which
 *         the protocol is not supported by the underlying controller
 *         hardware.
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES This value is returned if
 *         the core controller associated with the supplied parameters
 *         is unable to support additional remote devices.
 */
SCI_STATUS scic_remote_device_ea_construct(
   SCI_REMOTE_DEVICE_HANDLE_T   remote_device,
   SMP_RESPONSE_DISCOVER_T    * discover_response
);

/**
 * @brief This method is utilized to free up a core's remote device object.
 *
 * @note  Remote device objects are a limited resource.  As such, they
 *        must be protected.  Thus calls to construct and destruct are
 *        mutually exclusive and non-reentrant.
 *
 * @param[in]  remote_device This parameter specifies the remote device to be
 *             destructed.
 *
 * @return The return value shall indicate if the device was successfully
 *         destructed or if some failure occurred.
 * @retval SCI_STATUS This value is returned if the device is successfully
 *         destructed.
 * @retval SCI_FAILURE_INVALID_REMOTE_DEVICE This value is returned if the
 *         supplied device isn't valid (e.g. it's already been destoryed,
 *         the handle isn't valid, etc.).
 */
SCI_STATUS scic_remote_device_destruct(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

#if !defined(DISABLE_WIDE_PORTED_TARGETS)
/**
 * @brief This method will attempt to set port width for a remote device.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which to set new port width.
 * @param[in]  new_port_width The new port width to update.
 *
 * @return Indicate if the device port width was successfully updated.
 * @retval SCI_SUCCESS This value is returned when port width update was successful.
 * @retval SCI_FAILURE The port width update failed.
 */
SCI_STATUS scic_remote_device_set_port_width(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   U8                          new_port_width
);

/**
 * @brief This method retrieve the SCIC's record of a remote device's port width.
 *
 * @param[in]  remote_device This parameter specifies the remote device
 *             object for which to retrieve the port width value.
 *
 * @return The SCIC's record of a remote device's port width
 */
U8 scic_remote_device_get_port_width(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

#define scic_remote_device_da_add_phy(device, phy) SCI_FAILURE
#define scic_remote_device_ea_add_phy(device, response) SCI_FAILURE
#define scic_remote_device_remove_phy(device) SCI_FAILURE

#else // !defined(DISABLE_WIDE_PORTED_TARGETS)

#define scic_remote_device_set_port_width(device, port_width) SCI_FAILURE
#define scic_remote_device_get_port_width(device) (1)

#define scic_remote_device_da_add_phy(device, phy) SCI_FAILURE
#define scic_remote_device_ea_add_phy(device, response) SCI_FAILURE
#define scic_remote_device_remove_phy(device) SCI_FAILURE

#endif // !defined(DISABLE_WIDE_PORTED_TARGETS)

/**
 * @brief This method will start the supplied remote device.  This method
 *        enables normal IO requests to flow through to the remote device.
 *
 * @param[in]  remote_device This parameter specifies the device to be
 *             started.
 * @param[in]  timeout This parameter specifies the number of milliseconds
 *             in which the start operation should complete.
 *
 * @return An indication of whether the device was successfully started.
 * @retval SCI_SUCCESS This value is returned if the device was successfully
 *         started.
 * @retval SCI_FAILURE_INVALID_PHY This value is returned if the user attempts
 *         to start the device when there have been no phys added to it.
 */
SCI_STATUS scic_remote_device_start(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   U32                         timeout
);

/**
 * @brief This method will stop both transmission and reception of link
 *        activity for the supplied remote device.  This method disables
 *        normal IO requests from flowing through to the remote device.
 *
 * @param[in]  remote_device This parameter specifies the device to be
 *             stopped.
 * @param[in]  timeout This parameter specifies the number of milliseconds
 *             in which the stop operation should complete.
 *
 * @return An indication of whether the device was successfully stopped.
 * @retval SCI_SUCCESS This value is returned if the transmission and reception
 *         for the device was successfully stopped.
 */
SCI_STATUS scic_remote_device_stop(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   U32                         timeout
);

/**
 * @brief This method will reset the device making it ready for operation.
 *        This method must be called anytime the device is reset either
 *        through a SMP phy control or a port hard reset request.
 *
 * @note This method does not actually cause the device hardware to be reset.
 *       This method resets the software object so that it will be operational
 *       after a device hardware reset completes.
 *
 * @param[in]  remote_device This parameter specifies the device to be
 *             reset.
 *
 * @return An indication of whether the device reset was accepted.
 * @retval SCI_SUCCESS This value is returned if the device reset is started.
 */
SCI_STATUS scic_remote_device_reset(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This method informs the device object that the reset operation is
 *        complete and the device can resume operation again.
 *
 * @param[in]  remote_device This parameter specifies the device which is to
 *             be informed of the reset complete operation.
 *
 * @return An indication that the device is resuming operation.
 * @retval SCI_SUCCESS the device is resuming operation.
 */
SCI_STATUS scic_remote_device_reset_complete(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This method returns the suggested target reset timeout.  SAS and
 *        SATA devices have different timeout values in milliseconds for
 *        target reset operations.
 *
 * @param[in]  remote_device This parameter specifies the device which is to
 *             be informed of the reset complete operation.
 *
 * @return The suggested reset timeout value for the specified target device
 *         in milliseconds.
 */
U32 scic_remote_device_get_suggested_reset_timeout(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This method will set the maximum link speed to be utilized
 *        when connections are established for the supplied remote device.
 *
 * @pre The remote device must previously have been stopped for this
 *      call to succeed.
 *
 * @param[in]  remote_device This parameter specifies the device for which
 *             to set the maximum connection rate.
 * @param[in]  connection_rate This parameter specifies the maximum link rate
 *             to be utilized for all connections to the supplied remote
 *             device.
 *
 * @return An indication as to whether the connection rate was successfully
 *         updated.
 * @retval SCI_SUCCESS This value is returned if the connection rate was
 *         successfully updated.
 * @retval SCI_FAILURE_INVALID_STATE This value is returned if the remote
 *         device is not in a stopped state or some other state that allows
 *         for a maximum connection rate change.
 */
SCI_STATUS scic_remote_device_set_max_connection_rate(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_SAS_LINK_RATE           connection_rate
);

/**
 * @brief This method simply returns the link rate at which communications
 *        to the remote device occur.
 *
 * @param[in]  remote_device This parameter specifies the device for which
 *             to get the connection rate.
 *
 * @return Return the link rate at which we transfer for the supplied
 *         remote device.
 */
SCI_SAS_LINK_RATE scic_remote_device_get_connection_rate(
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This method will indicate which protocols are supported by this
 *        remote device.
 *
 * @param[in]  remote_device This parameter specifies the device for which
 *             to return the protocol.
 * @param[out] protocols This parameter specifies the output values, from
 *             the remote device object, which indicate the protocols
 *             supported by the supplied remote_device.
 *
 * @return The type of protocols supported by this device.  The values are
 *         returned as part of a bit mask in order to allow for multi-protocol
 *         support.
 */
void scic_remote_device_get_protocols(
   SCI_REMOTE_DEVICE_HANDLE_T          remote_device,
   SMP_DISCOVER_RESPONSE_PROTOCOLS_T * protocols
);

/**
 * @brief This method will indicate the SAS address for the remote device.
 *
 * @param[in]  remote_device This parameter specifies the device for which
 *             to return the SAS address.
 * @param[out] sas_address This parameter specifies a pointer to a SAS
 *             address structure into which the core will copy the SAS
 *             address for the remote device.
 *
 * @return none
 */
void scic_remote_device_get_sas_address(
   SCI_REMOTE_DEVICE_HANDLE_T   remote_device,
   SCI_SAS_ADDRESS_T          * sas_address
);

#if !defined(DISABLE_ATAPI)
/**
 * This method first decide whether a device is a stp target, then
 *    decode the signature fis of a DA STP device to tell whether it
 *    is a standard end disk or an ATAPI device.
 *
 * @param[in] this_device The device whose type is to be decided.
 *
 * @return BOOL Indicate a device is ATAPI device or not.
 */
BOOL scic_remote_device_is_atapi(
   SCI_REMOTE_DEVICE_HANDLE_T device_handle
);
#else // !defined(DISABLE_ATAPI)
#define scic_remote_device_is_atapi(device_handle) FALSE
#endif // !defined(DISABLE_ATAPI)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_REMOTE_DEVICE_H_

