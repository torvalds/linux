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
#ifndef _SCIC_CONTROLLER_H_
#define _SCIC_CONTROLLER_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods that can be called
 *        by an SCIC user on a controller object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>
#include <dev/isci/scil/sci_controller.h>
#include <dev/isci/scil/scic_config_parameters.h>

/**
 * @enum
 *
 * Allowed PORT configuration modes
 *
 * APC Automatic PORT configuration mode is defined by the OEM configuration
 * parameters providing no PHY_MASK parameters for any PORT. i.e. There are
 * no phys assigned to any of the ports at start.
 *
 * MPC Manual PORT configuration mode is defined by the OEM configuration
 * parameters providing a PHY_MASK value for any PORT.  It is assumed that
 * any PORT with no PHY_MASK is an invalid port and not all PHYs must be
 * assigned. A PORT_PHY mask that assigns just a single PHY to a port and no
 * other PHYs being assigned is sufficient to declare manual PORT configuration.
 */
enum SCIC_PORT_CONFIGURATION_MODE
{
   SCIC_PORT_MANUAL_CONFIGURATION_MODE,
   SCIC_PORT_AUTOMATIC_CONFIGURATION_MODE
};

/**
 * @enum _SCIC_INTERRUPT_TYPE
 *
 * @brief This enumeration depicts the various types of interrupts that
 *        are potentially supported by a SCI Core implementation.
 */
typedef enum _SCIC_INTERRUPT_TYPE
{
   SCIC_LEGACY_LINE_INTERRUPT_TYPE,
   SCIC_MSIX_INTERRUPT_TYPE,

   /**
    * This enumeration value indicates the use of polling.
    */
   SCIC_NO_INTERRUPTS

} SCIC_INTERRUPT_TYPE;

/**
 * @typedef SCIC_CONTROLLER_INTERRUPT_HANDLER
 *
 * @brief This method is called by the SCI user in order to have the SCI
 *        implementation handle the interrupt.  This method performs
 *        minimal processing to allow for streamlined interrupt time usage.
 * @note
 *        TRUE: returned if there is an interrupt to process and it was
 *              processed.
 *        FALSE: returned if no interrupt was processed.
 *
 */
typedef BOOL (*SCIC_CONTROLLER_INTERRUPT_HANDLER)(
   SCI_CONTROLLER_HANDLE_T  controller
);

/**
 * @brief This method is called by the SCI user to process completions
 *        generated as a result of a previously handled interrupt.  This
 *        method will result in the completion of IO requests and handling
 *        of other controller generated events.  This method should be
 *        called some time after the interrupt handler.
 *
 * @note  Most, if not all, of the user callback APIs are invoked from within
 *        this API.  As a result, the user should be cognizant of the operating
 *        level at which they invoke this API.
 *
 */
typedef void (*SCIC_CONTROLLER_COMPLETION_HANDLER)(
   SCI_CONTROLLER_HANDLE_T  controller
);

/**
 * @struct SCIC_CONTROLLER_HANDLER_METHODS
 *
 * @brief This structure contains an interrupt handler and completion
 *        handler function pointers.
 */
typedef struct SCIC_CONTROLLER_HANDLER_METHODS
{
   SCIC_CONTROLLER_INTERRUPT_HANDLER  interrupt_handler;
   SCIC_CONTROLLER_COMPLETION_HANDLER completion_handler;

} SCIC_CONTROLLER_HANDLER_METHODS_T;

/**
 * @brief This method will attempt to construct a controller object
 *        utilizing the supplied parameter information.
 *
 * @param[in]  library This parameter specifies the handle to the library
 *             object associated with the controller being constructed.
 * @param[in]  controller This parameter specifies the controller to be
 *             constructed.
 * @param[in]  user_object This parameter is a reference to the SCIL users
 *             controller object and will be used to associate with the core
 *             controller.
 *
 * @return Indicate if the controller was successfully constructed or if
 *         it failed in some way.
 * @retval SCI_SUCCESS This value is returned if the controller was
 *         successfully constructed.
 * @retval SCI_WARNING_TIMER_CONFLICT This value is returned if the
 *         interrupt coalescence timer may cause SAS compliance issues
 *         for SMP Target mode response processing.
 * @retval SCI_FAILURE_UNSUPPORTED_CONTROLLER_TYPE This value is returned if
 *         the controller does not support the supplied type.
 * @retval SCI_FAILURE_UNSUPPORTED_INIT_DATA_VERSION This value is returned
 *         if the controller does not support the supplied initialization
 *         data version.
 */
SCI_STATUS scic_controller_construct(
   SCI_LIBRARY_HANDLE_T      library,
   SCI_CONTROLLER_HANDLE_T   controller,
   void *                    user_object
);

/**
 * @brief This method will enable all controller interrupts.
 *
 * @param[in]  controller This parameter specifies the controller for which
 *             to enable interrupts.
 *
 * @return none
 */
void scic_controller_enable_interrupts(
   SCI_CONTROLLER_HANDLE_T      controller
);

/**
 * @brief This method will disable all controller interrupts.
 *
 * @param[in]  controller This parameter specifies the controller for which
 *             to disable interrupts.
 *
 * @return none
 */
void scic_controller_disable_interrupts(
   SCI_CONTROLLER_HANDLE_T      controller
);

/**
 * @brief This method will return provide function pointers for the
 *        interrupt handler and completion handler.  The interrupt handler
 *        is expected to be invoked at interrupt time.  The completion
 *        handler is scheduled to run as a result of the interrupt handler.
 *        The completion handler performs the bulk work for processing
 *        silicon events.
 *
 * @param[in]  interrupt_type This parameter informs the core which type
 *             of interrupt/completion methods are being requested. These
 *             are the types: SCIC_LEGACY_LINE_INTERRUPT_TYPE,
 *             SCIC_MSIX_INTERRUPT_TYPE, SCIC_NO_INTERRUPTS (POLLING)
 * @param[in]  message_count This parameter informs the core the
 *             number of MSI-X messages to be utilized.  This parameter must
 *             be 0 when requesting legacy line based handlers.
 * @param[in]  handler_methods The caller provides a pointer to a buffer of
 *             type SCIC_CONTROLLER_HANDLER_METHODS_T. The size depends on
 *             the combination of the interrupt_type and message_count input
 *             parameters:
 *             SCIC_LEGACY_LINE_INTERRUPT_TYPE:
 *             - size = sizeof(SCIC_CONTROLLER_HANDLER_METHODS_T)
 *             SCIC_MSIX_INTERRUPT_TYPE:
 *             - size = message_count*sizeof(SCIC_CONTROLLER_HANDLER_METHODS_T)
 * @param[out] handler_methods SCIC fills out the caller's buffer with the
 *             appropriate interrupt and completion handlers based on the info
 *             provided in the interrupt_type and message_count input
 *             parameters. For SCIC_LEGACY_LINE_INTERRUPT_TYPE, the buffer
 *             receives a single SCIC_CONTROLLER_HANDLER_METHODS_T element
 *             regardless that the message_count parameter is zero.
 *             For SCIC_MSIX_INTERRUPT_TYPE, the buffer receives an array of
 *             elements of type SCIC_CONTROLLER_HANDLER_METHODS_T where the
 *             array size is equivalent to the message_count parameter. The
 *             array is zero-relative where entry zero corresponds to
 *             message-vector zero, entry one corresponds to message-vector one,
 *             and so forth.
 *
 * @return Indicate if the handler retrieval operation was successful.
 * @retval SCI_SUCCESS This value is returned if retrieval succeeded.
 * @retval SCI_FAILURE_UNSUPPORTED_MESSAGE_COUNT This value is returned
 *         if the user supplied an unsupported number of MSI-X messages.
 *         For legacy line interrupts the only valid value is 0.
 */
SCI_STATUS scic_controller_get_handler_methods(
   SCIC_INTERRUPT_TYPE                  interrupt_type,
   U16                                  message_count,
   SCIC_CONTROLLER_HANDLER_METHODS_T *  handler_methods
);

/**
 * @brief This method will initialize the controller hardware managed by
 *        the supplied core controller object.  This method will bring the
 *        physical controller hardware out of reset and enable the core to
 *        determine the capabilities of the hardware being managed.  Thus,
 *        the core controller can determine it's exact physical (DMA capable)
 *        memory requirements.
 *
 * @pre   The SCI Core user must have called scic_controller_construct()
 *        on the supplied controller object previously.
 *
 * @param[in]  controller This parameter specifies the controller to be
 *             initialized.
 *
 * @return Indicate if the controller was successfully initialized or if
 *         it failed in some way.
 * @retval SCI_SUCCESS This value is returned if the controller hardware
 *         was successfully initialized.
 */
SCI_STATUS scic_controller_initialize(
   SCI_CONTROLLER_HANDLE_T   controller
);

/**
 * @brief This method returns the suggested scic_controller_start()
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
U32 scic_controller_get_suggested_start_timeout(
   SCI_CONTROLLER_HANDLE_T  controller
);

/**
 * @brief This method will start the supplied core controller.  This method
 *        will start the staggered spin up operation.  The SCI User completion
 *        callback is called when the following conditions are met:
 *        -# the return status of this method is SCI_SUCCESS.
 *        -# after all of the phys have successfully started or been given
 *           the opportunity to start.
 *
 * @pre   The SCI Core user must have filled in the physical memory
 *        descriptor structure via the
 *        sci_controller_get_memory_descriptor_list() method.
 * @pre   The SCI Core user must have invoked the scic_controller_initialize()
 *        method prior to invoking this method.
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
 */
SCI_STATUS scic_controller_start(
   SCI_CONTROLLER_HANDLE_T  controller,
   U32                      timeout
);

/**
 * @brief This method will stop an individual controller object.This method
 *        will invoke the associated user callback upon completion.  The
 *        completion callback is called when the following conditions are met:
 *           -# the method return status is SCI_SUCCESS.
 *           -# the controller has been quiesced.
 *        This method will ensure that all IO requests are quiesced, phys
 *        are stopped, and all additional operation by the hardware is halted.
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
 */
SCI_STATUS scic_controller_stop(
   SCI_CONTROLLER_HANDLE_T  controller,
   U32                      timeout
);

/**
 * @brief This method will reset the supplied core controller regardless of
 *        the state of said controller.  This operation is considered
 *        destructive.  In other words, all current operations are wiped
 *        out.  No IO completions for outstanding devices occur.  Outstanding
 *        IO requests are not aborted or completed at the actual remote
 *        device.
 *
 * @param[in]  controller the handle to the controller object to reset.
 *
 * @return Indicate if the controller reset method succeeded or failed in
 *         some way.
 * @retval SCI_SUCCESS if the reset operation successfully started.
 * @retval SCI_FATAL_ERROR if the controller reset operation is unable to
 *         complete.
 */
SCI_STATUS scic_controller_reset(
   SCI_CONTROLLER_HANDLE_T  controller
);

/**
 * @brief This method is called by the SCI user to send/start an IO request.
 *        If the method invocation is successful, then the IO request has
 *        been queued to the hardware for processing.
 *
 * @warning
 *         - IO tags are a protected resource.  It is incumbent upon the
 *           SCI Core user to ensure that each of the methods that may
 *           allocate or free available IO tags are handled in a mutually
 *           exclusive manner.  This method is one of said methods requiring
 *           proper critical code section protection (e.g. semaphore,
 *           spin-lock, etc.).
 *         - For SATA, the user is required to manage NCQ tags.  As a
 *           result, it is expected the user will have set the NCQ tag
 *           field in the host to device register FIS prior to calling
 *           this method.  There is also a requirement for the user
 *           to call scic_stp_io_set_ncq_tag() prior to invoking the
 *           scic_controller_start_io() method.
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
 * @todo Determine the failure situations and return values.
 */
SCI_IO_STATUS scic_controller_start_io(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     io_request,
   U16                         io_tag
);

#if !defined(DISABLE_TASK_MANAGEMENT)

/**
 * @brief This method is called by the SCIC user to send/start a framework
 *        task management request.
 *
 * @warning
 *         - IO tags are a protected resource.  It is incumbent upon the
 *           SCI Core user to ensure that each of the methods that may
 *           allocate or free available IO tags are handled in a mutually
 *           exclusive manner.  This method is one of said methods requiring
 *           proper critical code section protection (e.g. semaphore,
 *           spin-lock, etc.).
 *         - The user must synchronize this task with completion queue
 *           processing.  If they are not synchronized then it is possible
 *           for the io requests that are being managed by the task request
 *           can complete before starting the task request.
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
 * @retval SCI_TASK_FAILURE_REQUIRES_SCSI_ABORT This value is returned if
 *         there is/are task(s) outstanding that require termination or
 *         completion before this request can succeed.
 */
SCI_TASK_STATUS scic_controller_start_task(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_TASK_REQUEST_HANDLE_T   task_request,
   U16                         io_tag
);

/**
 * @brief This method will perform core specific completion operations for
 *        task management request. After this method is invoked, the user should
 *        consider the task request as invalid until it is properly reused
 *        (i.e. re-constructed).
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
SCI_STATUS scic_controller_complete_task(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_TASK_REQUEST_HANDLE_T   task_request
);

#else // !defined(DISABLE_TASK_MANAGEMENT)

#define scic_controller_start_task(controller, dev, task, tag) SCI_TASK_FAILURE
#define scic_controller_complete_task(controller, dev, task) SCI_FAILURE

#endif // !defined(DISABLE_TASK_MANAGEMENT)

/**
 * @brief This method is called by the SCI Core user to terminate an ongoing
 *        (i.e. started) core IO request.  This does not abort the IO request
 *        at the target, but rather removes the IO request from the host
 *        controller.
 *
 * @param[in]  controller the handle to the controller object for which
 *             to terminate a request.
 * @param[in]  remote_device the handle to the remote device object for which
 *             to terminate a request.
 * @param[in]  request the handle to the io or task management request
 *             object to terminate.
 *
 * @return Indicate if the controller successfully began the terminate process
 *         for the IO request.
 * @retval SCI_SUCCESS if the terminate process was successfully started for
 *         the request.
 *
 * @todo Determine the failure situations and return values.
 */
SCI_STATUS scic_controller_terminate_request(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     request
);

/**
 * @brief This method will perform core specific completion operations for
 *        an IO request.  After this method is invoked, the user should
 *        consider the IO request as invalid until it is properly reused
 *        (i.e. re-constructed).
 *
 * @warning
 *        - IO tags are a protected resource.  It is incumbent upon the
 *          SCI Core user to ensure that each of the methods that may
 *          allocate or free available IO tags are handled in a mutually
 *          exclusive manner.  This method is one of said methods requiring
 *          proper critical code section protection (e.g. semaphore,
 *          spin-lock, etc.).
 *        - If the IO tag for a request was allocated, by the SCI Core user,
 *          using the scic_controller_allocate_io_tag() method, then it is
 *          the responsibility of the caller to invoke the
 *          scic_controller_free_io_tag() method to free the tag (i.e. this
 *          method will not free the IO tag).
 *
 * @param[in]  controller The handle to the controller object for which
 *             to complete the IO request.
 * @param[in]  remote_device The handle to the remote device object for which
 *             to complete the IO request.
 * @param[in]  io_request the handle to the io request object to complete.
 *
 * @return Indicate if the controller successfully completed the IO request.
 * @retval SCI_SUCCESS if the completion process was successful.
 */
SCI_STATUS scic_controller_complete_io(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     io_request
);


/**
 * @brief This method simply provides the user with a unique handle for a
 *        given SAS/SATA core port index.
 *
 * @param[in]  controller This parameter represents the handle to the
 *             controller object from which to retrieve a port (SAS or
 *             SATA) handle.
 * @param[in]  port_index This parameter specifies the port index in
 *             the controller for which to retrieve the port handle.
 *             0 <= port_index < maximum number of phys.
 * @param[out] port_handle This parameter specifies the retrieved port handle
 *             to be provided to the caller.
 *
 * @return Indicate if the retrieval of the port handle was successful.
 * @retval SCI_SUCCESS This value is returned if the retrieval was successful.
 * @retval SCI_FAILURE_INVALID_PORT This value is returned if the supplied
 *         port id is not in the supported range.
 */
SCI_STATUS scic_controller_get_port_handle(
   SCI_CONTROLLER_HANDLE_T   controller,
   U8                        port_index,
   SCI_PORT_HANDLE_T       * port_handle
);

/**
 * @brief This method simply provides the user with a unique handle for a
 *        given SAS/SATA phy index/identifier.
 *
 * @param[in]  controller This parameter represents the handle to the
 *             controller object from which to retrieve a phy (SAS or
 *             SATA) handle.
 * @param[in]  phy_index This parameter specifies the phy index in
 *             the controller for which to retrieve the phy handle.
 *             0 <= phy_index < maximum number of phys.
 * @param[out] phy_handle This parameter specifies the retrieved phy handle
 *             to be provided to the caller.
 *
 * @return Indicate if the retrieval of the phy handle was successful.
 * @retval SCI_SUCCESS This value is returned if the retrieval was successful.
 * @retval SCI_FAILURE_INVALID_PHY This value is returned if the supplied phy
 *         id is not in the supported range.
 */
SCI_STATUS scic_controller_get_phy_handle(
   SCI_CONTROLLER_HANDLE_T   controller,
   U8                        phy_index,
   SCI_PHY_HANDLE_T        * phy_handle
);

/**
 * @brief This method will allocate a tag from the pool of free IO tags.
 *        Direct allocation of IO tags by the SCI Core user is optional.
 *        The scic_controller_start_io() method will allocate an IO
 *        tag if this method is not utilized and the tag is not
 *        supplied to the IO construct routine.  Direct allocation of IO tags
 *        may provide additional performance improvements in environments
 *        capable of supporting this usage model.  Additionally, direct
 *        allocation of IO tags also provides additional flexibility to the
 *        SCI Core user.  Specifically, the user may retain IO tags across
 *        the lives of multiple IO requests.
 *
 * @warning IO tags are a protected resource.  It is incumbent upon the
 *          SCI Core user to ensure that each of the methods that may
 *          allocate or free available IO tags are handled in a mutually
 *          exclusive manner.  This method is one of said methods requiring
 *          proper critical code section protection (e.g. semaphore,
 *          spin-lock, etc.).
 *
 * @param[in]  controller the handle to the controller object for which to
 *             allocate the tag.
 *
 * @return An unsigned integer representing an available IO tag.
 * @retval SCI_CONTROLLER_INVALID_IO_TAG This value is returned if there
 *         are no currently available tags to be allocated.
 * @retval All return other values indicate a legitimate tag.
 */
U16 scic_controller_allocate_io_tag(
   SCI_CONTROLLER_HANDLE_T  controller
);

/**
 * @brief This method will free an IO tag to the pool of free IO tags.
 *        This method provides the SCI Core user more flexibility with
 *        regards to IO tags.  The user may desire to keep an IO tag after
 *        an IO request has completed, because they plan on re-using the
 *        tag for a subsequent IO request.  This method is only legal if
 *        the tag was allocated via scic_controller_allocate_io_tag().
 *
 * @warning
 *        - IO tags are a protected resource.  It is incumbent upon the
 *          SCI Core user to ensure that each of the methods that may
 *          allocate or free available IO tags are handled in a mutually
 *          exclusive manner.  This method is one of said methods requiring
 *          proper critical code section protection (e.g. semaphore,
 *          spin-lock, etc.).
 *        - If the IO tag for a request was allocated, by the SCI Core user,
 *          using the scic_controller_allocate_io_tag() method, then it is
 *          the responsibility of the caller to invoke this method to free
 *          the tag.
 *
 * @param[in]  controller This parameter specifies the handle to the
 *             controller object for which to free/return the tag.
 * @param[in]  io_tag This parameter represents the tag to be freed to the
 *             pool of available tags.
 *
 * @return This method returns an indication of whether the tag was
 *         successfully put back (freed) to the pool of available tags.
 * @retval SCI_SUCCESS This return value indicates the tag was successfully
 *         placed into the pool of available IO tags.
 * @retval SCI_FAILURE_INVALID_IO_TAG This value is returned if the supplied
 *         tag is not a valid IO tag value.
 */
SCI_STATUS scic_controller_free_io_tag(
   SCI_CONTROLLER_HANDLE_T  controller,
   U16                      io_tag
);

/**
 * @brief This method returns the size of the core's scratch RAM.
 *
 * @return Size of the scratch RAM in dwords.
 */
U32 scic_controller_get_scratch_ram_size(
   SCI_CONTROLLER_HANDLE_T   controller
);

/**
 * @brief This method allows the user to read a U32 from the core's
 *        scratch RAM.
 *
 * @param[in]  controller This parameter represents the handle to the
 *             controller object for which to read scratch RAM.
 * @param[in]  offset The offset (in dwords) into the scratch RAM.
 * @param[out] value The location where the read value should be stored.
 *
 * @return Indicate if the user specified a valid offset into the
 *         scratch RAM.
 * @retval SCI_SUCCESS The scratch RAM was successfully read.
 * @retval SCI_FAILURE_INVALID_PARAMETER_VALUE The user specified an
 *          invalid offset.
 */
SCI_STATUS scic_controller_read_scratch_ram_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   U32                       offset,
   U32                     * value
);

/**
 * @brief This method allows the user to write a U32 to the core's
 *        scratch RAM.
 *
 * @param[in]  controller This parameter represents the handle to the
 *             controller object for which to write scratch RAM.
 * @param[in]  offset The offset (in dwords) into the scratch RAM.
 * @param[out] value The value to be written to scratch RAM.
 *
 * @return Indicate if the user specified a valid offset into the
 *         scratch RAM.
 * @retval SCI_SUCCESS The scratch RAM was successfully written.
 * @retval SCI_FAILURE_INVALID_PARAMETER_VALUE The user specified an
 *          invalid offset.
 */
SCI_STATUS scic_controller_write_scratch_ram_dword(
    SCI_CONTROLLER_HANDLE_T   controller,
    U32                       offset,
    U32                       value
);

/**
 * @brief This method allows the user to configure the SCI core into
 *        either a performance mode or a memory savings mode.
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
SCI_STATUS scic_controller_set_mode(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCI_CONTROLLER_MODE       mode
);


#if !defined(DISABLE_INTERRUPTS)
/**
 * @brief This method allows the user to configure the interrupt coalescence.
 *
 * @param[in]  controller This parameter represents the handle to the
 *                controller object for which its interrupt coalesce register
 *                is overridden.
 *
 * @param[in]  coalesce_number Used to control the number of entries in the
 *                Completion Queue before an interrupt is generated. If the
 *                number of entries exceed this number, an interrupt will be
 *                generated. The valid range of the input is [0, 256].
 *                A setting of 0 results in coalescing being disabled.
 * @param[in]  coalesce_timeout Timeout value in microseconds. The valid range
 *                of the input is [0, 2700000] . A setting of 0 is allowed and
 *                results in no interrupt coalescing timeout.
 *
 * @return Indicate if the user successfully set the interrupt coalesce parameters.
 * @retval SCI_SUCCESS The user successfully updated the interrutp coalescence.
 * @retval SCI_FAILURE_INVALID_PARAMETER_VALUE The user input value is out of range.
 */
SCI_STATUS scic_controller_set_interrupt_coalescence(
   SCI_CONTROLLER_HANDLE_T controller,
   U32                     coalesce_number,
   U32                     coalesce_timeout
);

/**
 * @brief This method retrieves the interrupt coalescing values
 *
 * @param[in] controller This parameter specifies the controller for
 *            which its interrupt coalescing number is read.
 *
 * @param[out] coalesce_number, interrupt coalescing number read from controller.
 *
 * @param[out] coalesce_timeout, timeout value in microseconds.
 *
 * @return None
 */
void scic_controller_get_interrupt_coalescence(
   SCI_CONTROLLER_HANDLE_T   controller,
   U32                     * coalesce_number,
   U32                     * coalesce_timeout
);
#else // !defined(DISABLE_INTERRUPTS)
#define scic_controller_set_interrupt_coalescence(controller, num, timeout) \
        SCI_FAILURE
#define scic_controller_get_interrupt_coalescence(controller, num, timeout)
#endif // !defined(DISABLE_INTERRUPTS)


/**
 * @brief This method suspend the controller, reinitialize RAMs, then resume
 *           the controller.
 *
 * @param[in] controller This parameter specifies the controller which is transitioning.
 *
 * @param[in] restrict_completions This parameter specifies whether the controller should
 *               ignore completion processing for non-fastpath events.  This will cause
 *               the completions to be thrown away.
 *
 * @return SCI_STATUS The status of controller transition.
 */
SCI_STATUS scic_controller_transition(
   SCI_CONTROLLER_HANDLE_T   controller,
   BOOL                      restrict_completions
);


/**
 * @brief This method suspends the controller.
 *
 * @param[in] controller This parameter specifies the controller which is to be suspended.
 *
 * @return SCI_STATUS The status of controller suspend.
 */
SCI_STATUS scic_controller_suspend(
   SCI_CONTROLLER_HANDLE_T   controller
);

/**
 * @brief This method resumes the controller.
 *
 * @param[in] controller This parameter specifies the controller which is to be resumed.
 *
 * @return SCI_STATUS The status of controller resume.
 */
SCI_STATUS scic_controller_resume(
   SCI_CONTROLLER_HANDLE_T   controller
);

SCI_STATUS scic_controller_get_max_ports(
   SCI_CONTROLLER_HANDLE_T   controller,
   U8                      * count
);

SCI_STATUS scic_controller_get_max_phys(
   SCI_CONTROLLER_HANDLE_T   controller,
   U8                      * count
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_CONTROLLER_H_

