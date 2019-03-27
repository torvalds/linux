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
#ifndef _SCIF_CONTROLLER_H_
#define _SCIF_CONTROLLER_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods that can be called
 *        by an SCIF user on a SCIF controller object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>


/**
 * @brief This method will attempt to construct a framework controller object
 *        utilizing the supplied parameter information.
 *
 * @param[in]  library This parameter specifies the handle to the framework
 *             library object associated with the controller being constructed.
 * @param[in]  controller This parameter specifies the framework controller to
 *             be constructed.
 * @param[in]  user_object This parameter is a reference to the SCIL users
 *             controller object and will be used to associate with the
 *             framework controller.
 *
 * @return Indicate if the controller was successfully constructed or if
 *         it failed in some way.
 * @retval SCI_SUCCESS This value is returned if the controller was
 *         successfully constructed.
 * @retval SCI_FAILURE_UNSUPPORTED_INIT_DATA_VERSION This value is returned
 *         if the controller does not support the supplied oem parameter
 *         data version.
 * @retval SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION This value is returned
 *         if the controller doesn't support the port configuration scheme
 *         (APC or MPC).
 */
SCI_STATUS scif_controller_construct(
   SCI_LIBRARY_HANDLE_T      library,
   SCI_CONTROLLER_HANDLE_T   controller,
   void *                    user_object
);

/**
 * @brief This method will initialize the SCI Framework controller object.
 *        This includes initialization of the associated core controller.
 *
 * @param[in]  controller This parameter specifies the controller to be
 *             initialized.
 *
 * @return Indicate if the controller was successfully initialized or if
 *         it failed in some way.
 * @retval SCI_SUCCESS This value is returned if the controller hardware
 *         was successfully initialized.
 */
SCI_STATUS scif_controller_initialize(
   SCI_CONTROLLER_HANDLE_T  controller
);

/**
 * @brief This method returns the suggested scif_controller_start()
 *        timeout amount.  The user is free to use any timeout value,
 *        but this method provides the suggested minimum start timeout
 *        value.  The returned value is based upon empirical information
 *        determined as a result of interoperability testing.
 *
 * @param[in]  controller the handle to the controller object for which
 *             to return the suggested start timeout.
 *
 * @return  This method returns the number of milliseconds for the
 *          suggested start operation timeout.
 */
U32 scif_controller_get_suggested_start_timeout(
   SCI_CONTROLLER_HANDLE_T  controller
);

/**
 * @brief This method will start the SCIF controller.  The SCI User completion
 *        callback is called when the following conditions are met:
 *        -# the return status of this method is SCI_SUCCESS.
 *        -# after all of the phys have successfully started or been given
 *           the opportunity to start.
 *
 * @pre   The controller must be in the INITIALIZED or STARTED state.
 *
 * @param[in]  controller the handle to the controller object to start.
 * @param[in]  timeout This parameter specifies the number of milliseconds
 *             in which the start operation should complete.
 *
 * @return Indicate if the controller start method succeeded or failed in
 *         some way.
 * @retval SCI_SUCCESS if the start operation succeeded.
 * @retval SCI_WARNING_ALREADY_IN_STATE if the controller is already in
 *         the STARTED state.
 * @retval SCI_FAILURE_INVALID_STATE if the controller is not either in
 *         the INITIALIZED or STARTED states.
 * @retval SCI_FAILURE_INVALID_MEMORY_DESCRIPTOR if there are
 *         inconsistent or invalid values in the supplied
 *         SCI_PHYSICAL_MEMORY_DESCRIPTOR array.
 * @retval SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION This value is
 *         returned if the phy to port allocation cannot be supported.
 *
 * @see For additional information please refer to: scic_controller_start()
 */
SCI_STATUS scif_controller_start(
   SCI_CONTROLLER_HANDLE_T  controller,
   U32                      timeout
);

/**
 * @brief This method will stop an individual framework controller object. This
 *        includes quiescing IOs, releasing affiliations, and other shutdown
 *        related operations. This method will invoke the associated user
 *        callback upon completion.  The completion callback is called when
 *        the following conditions are met:
 *           -# the method return status is SCI_SUCCESS.
 *           -# the controller has been quiesced.
 *        This method will ensure that all framework IO requests are quiesced
 *        and any additional framework operations are halted.
 *
 * @pre   The controller must be in the STARTED or STOPPED state.
 *
 * @param[in]  controller the handle to the controller object to stop.
 * @param[in]  timeout This parameter specifies the number of milliseconds
 *             in which the stop operation should complete.
 *
 * @return Indicate if the controller stop method succeeded or failed in
 *         some way.
 * @retval SCI_SUCCESS if the stop operation successfully began.
 * @retval SCI_WARNING_ALREADY_IN_STATE if the controller is already in
 *         the STOPPED state.
 * @retval SCI_FAILURE_INVALID_STATE if the controller is not either in
 *         the STARTED or STOPPED states.
 *
 * @see For additional information please refer to: scic_controller_stop()
 */
SCI_STATUS scif_controller_stop(
   SCI_CONTROLLER_HANDLE_T  controller,
   U32                      timeout
);

/**
 * @brief This method will reset the supplied framework controller regardless
 *        of the state of said controller.  This operation is considered
 *        destructive.  Outstanding IO requests are not aborted or completed
 *        at the actual remote device.  However, the framework will
 *        manufacture completion callbacks to the OS driver for the IO
 *        requests.
 *
 * @param[in]  controller the handle to the controller object to reset.
 *
 * @return Indicate if the controller reset method succeeded or failed in
 *         some way.
 * @retval SCI_SUCCESS if the reset operation successfully started.
 * @retval SCI_FATAL_ERROR if the controller reset operation is unable to
 *         complete.
 *
 * @see For additional information please refer to: scic_controller_reset()
 */
SCI_STATUS scif_controller_reset(
   SCI_CONTROLLER_HANDLE_T  controller
);

/**
 * @brief This method returns the SCI Core controller handle associated
 *        with this controller.
 *
 * @param[in]  scif_controller the handle to the controller object for which
 *             to retrieve the core specific controller handle
 *
 * @return Return the SCI core controller handle associated with the supplied
 *         framework controller.
 */
SCI_CONTROLLER_HANDLE_T scif_controller_get_scic_handle(
   SCI_CONTROLLER_HANDLE_T   scif_controller
);

/**
 * @brief This method is called by the SCIF user to send/start a framework
 *        IO request.
 *
 * @param[in]  controller the handle to the controller object for which
 *             to start an IO request.
 * @param[in]  remote_device the handle to the remote device object for which
 *             to start an IO request.
 * @param[in]  io_request the handle to the io request object to start.
 * @param[in]  io_tag This parameter specifies a previously allocated IO tag
 *             that the user desires to be utilized for this request.
 *             This parameter is optional.  The user is allowed to supply
 *             SCI_CONTROLLER_INVALID_IO_TAG as the value for this parameter.
 *             @see scic_controller_allocate_tag() for more information
 *             on allocating a tag.
 *
 * @return Indicate if the controller successfully started the IO request.
 * @retval SCI_IO_SUCCESS if the IO request was successfully started.
 *
 * @see For additional information please refer to: scic_controller_start_io()
 *
 * @todo Determine the failure situations and return values.
 */
SCI_IO_STATUS scif_controller_start_io(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     io_request,
   U16                         io_tag
);

/**
 * @brief This method is called by the SCIF user to send/start a framework
 *        task management request.
 *
 * @param[in]  controller the handle to the controller object for which
 *             to start the task management request.
 * @param[in]  remote_device the handle to the remote device object for which
 *             to start the task management request.
 * @param[in]  task_request the handle to the task request object to start.
 * @param[in]  io_tag This parameter specifies a previously allocated IO tag
 *             that the user desires to be utilized for this request.  Note
 *             this not the io_tag of the request being managed.  It is to
 *             be utilized for the task request itself.
 *             This parameter is optional.  The user is allowed to supply
 *             SCI_CONTROLLER_INVALID_IO_TAG as the value for this parameter.
 *             @see scic_controller_allocate_tag() for more information
 *             on allocating a tag.
 *
 * @return Indicate if the controller successfully started the IO request.
 * @retval SCI_TASK_SUCCESS if the task request was successfully started.
 *
 * @see For additional information please refer to: scic_controller_start_task()
 *
 * @todo Determine the failure situations and return values.
 */
SCI_TASK_STATUS scif_controller_start_task(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_TASK_REQUEST_HANDLE_T   task_request,
   U16                         io_tag
);

/**
 * @brief This method is called by the SCI user to complete a previously
 *        started IO request.  After this method is invoked, the user should
 *        consider the IO request as invalid until it is properly reused
 *        (i.e. re-constructed).
 *
 * @param[in]  controller The handle to the controller object for which
 *             to complete the IO request.
 * @param[in]  remote_device The handle to the remote device object for which
 *             to complete the IO request.
 * @param[in]  io_request the handle to the io request object to complete.
 *
 * @return Indicate if the controller successfully completed the IO request.
 * @retval SCI_SUCCESS if the completion process was successful.
 *
 * @see For additional information please refer to:
 *      scic_controller_complete_io()
 */
SCI_STATUS scif_controller_complete_io(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     io_request
);

/**
 * @brief This method will perform framework specific completion operations for
 *        a task management request. After this method is invoked, the user
 *        should consider the task request as invalid until it is properly
 *        reused (i.e. re-constructed).
 *
 * @param[in]  controller The handle to the controller object for which
 *             to complete the task management request.
 * @param[in]  remote_device The handle to the remote device object for which
 *             to complete the task management request.
 * @param[in]  task_request the handle to the task management request object
 *             to complete.
 *
 * @return Indicate if the controller successfully completed the task
 *         management request.
 * @retval SCI_SUCCESS if the completion process was successful.
 */
SCI_STATUS scif_controller_complete_task(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_TASK_REQUEST_HANDLE_T   task_request
);

/**
 * @brief This method simply provides the user with a unique handle for a
 *        given SAS/SATA domain index.
 *
 * @param[in]  controller This parameter represents the handle to the
 *             controller object from which to retrieve a domain (SAS or
 *             SATA) handle.
 * @param[in]  port_index This parameter specifies the domain index in
 *             the controller for which to retrieve the domain handle.
 *             @note 0 <= port_index < maximum number of phys.
 * @param[out] domain_handle This parameter specifies the retrieved domain
 *             handle to be provided to the caller.
 *
 * @return Indicate if the retrieval of the domain handle was successful.
 * @retval SCI_SUCCESS This value is returned if the retrieval was successful.
 * @retval SCI_FAILURE_INVALID_PORT This value is returned if the supplied
 *         port index is not invalid.
 */
SCI_STATUS scif_controller_get_domain_handle(
   SCI_CONTROLLER_HANDLE_T   controller,
   U8                        port_index,
   SCI_DOMAIN_HANDLE_T     * domain_handle
);

/**
 * @brief This method allows the user to configure the SCI Framework
 *        into either a performance mode or a memory savings mode.
 *
 * @param[in]  controller This parameter represents the handle to the
 *             controller object for which to update the operating
 *             mode.
 * @param[in]  mode This parameter specifies the new mode for the
 *             controller.
 *
 * @return Indicate if the user successfully change the operating mode
 *         of the controller.
 * @retval SCI_SUCCESS The user successfully updated the mode.
 */
SCI_STATUS scif_controller_set_mode(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCI_CONTROLLER_MODE       mode
);

/**
 * @brief This method simply returns the T10 SCSI to ATA Translation (SAT)
 *        specification version to which this translator is compliant for
 *        supported commands.
 *
 * @return An integer value indicating the SAT version to which this
 *         translator complies.
 */
U32 scif_controller_get_sat_compliance_version(
   void
);

/**
 * @brief This method simply returns the revision of the T10 SCSI to ATA
 *        Translation (SAT) specification version to which this translator
 *        is compliant for supported commands.
 *
 * @return An integer value indicating the revision of the SAT version
 *         to which this translator complies.
 */
U32 scif_controller_get_sat_compliance_version_revision(
   void
);

/**
 * @brief This method is called by the SCI user to start internal io.
 */
typedef void (*SCI_START_INTERNAL_IO_ROUTINE)(
   SCI_CONTROLLER_HANDLE_T controller
);

#if !defined(DISABLE_INTERRUPTS)
/**
 * @brief This method allows the user to configure the interrupt coalescence.
 *           Please refer to the comment header for
 *           scic_controller_set_interrupt_coalescence() to find details.
 */
SCI_STATUS scif_controller_set_interrupt_coalescence(
   SCI_CONTROLLER_HANDLE_T controller,
   U32                     coalesce_number,
   U32                     coalesce_timeout
);

/**
 * @brief This method retrieves the interrupt coalescence information.
 *           Please refer to the comment header for
 *           scic_controller_get_interrupt_coalescence() to find details.
 */
void scif_controller_get_interrupt_coalescence(
   SCI_CONTROLLER_HANDLE_T controller,
   U32                   * coalesce_number,
   U32                   * coalesce_timeout
);

#else // !defined(DISABLE_INTERRUPTS)

#define scif_controller_set_interrupt_coalescence(controller, num, timeout) \
        SCI_FAILURE
#define scif_controller_get_interrupt_coalescence(controller, num, timeout)

#endif // !defined(DISABLE_INTERRUPTS)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_CONTROLLER_H_

