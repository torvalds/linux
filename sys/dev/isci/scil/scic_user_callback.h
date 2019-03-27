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
#ifndef _SCIC_USER_CALLBACK_H_
#define _SCIC_USER_CALLBACK_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods/macros that must
 *        be implemented by an SCI Core user.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>
#include <dev/isci/scil/sci_controller.h>

/**
 * @brief This callback method asks the user to create a timer and provide
 *        a handle for this timer for use in further timer interactions.
 *
 * @warning The "timer_callback" method should be executed in a mutually
 *          exlusive manner from the controller completion handler
 *          handler (refer to scic_controller_get_handler_methods()).
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this timer is to be associated.
 * @param[in]  timer_callback This parameter specifies the callback method
 *             to be invoked whenever the timer expires.
 * @param[in]  cookie This parameter specifies a piece of information that
 *             the user must retain.  This cookie is to be supplied by the
 *             user anytime a timeout occurs for the created timer.
 *
 * @return This method returns a handle to a timer object created by the
 *         user.  The handle will be utilized for all further interactions
 *         relating to this timer.
 */
void * scic_cb_timer_create(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCI_TIMER_CALLBACK_T      timer_callback,
   void                    * cookie
);

/**
 * @brief This callback method asks the user to destroy the supplied timer.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this timer is to associated.
 * @param[in]  timer This parameter specifies the timer to be destroyed.
 *
 * @return none
 */
void scic_cb_timer_destroy(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * timer
);

/**
 * @brief This callback method asks the user to start the supplied timer.
 *
 * @warning All timers in the system started by the SCI Core are one shot
 *          timers.  Therefore, the SCI user should make sure that it
 *          removes the timer from it's list when a timer actually fires.
 *          Additionally, SCI Core user's should be able to handle
 *          calls from the SCI Core to stop a timer that may already
 *          be stopped.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this timer is to associated.
 * @param[in]  timer This parameter specifies the timer to be started.
 * @param[in]  milliseconds This parameter specifies the number of
 *             milliseconds for which to stall.  The operating system driver
 *             is allowed to round this value up where necessary.
 *
 * @return none
 */
void scic_cb_timer_start(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * timer,
   U32                       milliseconds
);

/**
 * @brief This callback method asks the user to stop the supplied timer.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this timer is to associated.
 * @param[in]  timer This parameter specifies the timer to be stopped.
 *
 * @return none
 */
void scic_cb_timer_stop(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * timer
);

/**
 * @brief This method is called when the core requires the OS driver
 *        to stall execution.  This method is utilized during initialization
 *        or non-performance paths only.
 *
 * @param[in]  microseconds This parameter specifies the number of
 *             microseconds for which to stall.  The operating system driver
 *             is allowed to round this value up where necessary.
 *
 * @return none.
 */
void scic_cb_stall_execution(
   U32  microseconds
);

/**
 * @brief This user callback will inform the user that the controller has
 *        finished the start process.
 *
 * @param[in]  controller This parameter specifies the controller that was
 *             started.
 * @param[in]  completion_status This parameter specifies the results of
 *             the start operation.  SCI_SUCCESS indicates successful
 *             completion.
 *
 * @return none
 */
void scic_cb_controller_start_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_STATUS               completion_status
);

/**
 * @brief This user callback will inform the user that the controller has
 *        finished the stop process.
 *
 * @param[in]  controller This parameter specifies the controller that was
 *             stopped.
 * @param[in]  completion_status This parameter specifies the results of
 *             the stop operation.  SCI_SUCCESS indicates successful
 *             completion.
 *
 * @return none
 */
void scic_cb_controller_stop_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_STATUS               completion_status
);

/**
 * @brief This user callback will inform the user that an IO request has
 *        completed.
 *
 * @param[in]  controller This parameter specifies the controller on
 *             which the IO is completing.
 * @param[in]  remote_device This parameter specifies the remote device on
 *             which this IO request is completing.
 * @param[in]  io_request This parameter specifies the IO request that has
 *             completed.
 * @param[in]  completion_status This parameter specifies the results of
 *             the IO request operation.  SCI_SUCCESS indicates successful
 *             completion.
 *
 * @return none
 */
void scic_cb_io_request_complete(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     io_request,
   SCI_IO_STATUS               completion_status
);

/**
 * @brief This method simply returns the virtual address associated
 *        with the scsi_io and byte_offset supplied parameters.
 *
 * @note This callback is not utilized in the fast path.  The expectation
 *       is that this method is utilized for items such as SCSI to ATA
 *       translation for commands like INQUIRY, READ CAPACITY, etc.
 *
 * @param[in] scic_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] byte_offset This parameter specifies the offset into the data
 *            buffers pointed to by the SGL.  The byte offset starts at 0
 *            and continues until the last byte pointed to be the last SGL
 *            element.
 *
 * @return A virtual address pointer to the location specified by the
 *         parameters.
 */
U8 *scic_cb_io_request_get_virtual_address_from_sgl(
   void * scic_user_io_request,
   U32    byte_offset
);

/**
 * @brief This user callback will inform the user that a task management
 *        request completed.
 *
 * @param[in]  controller This parameter specifies the controller on
 *             which the task management request is completing.
 * @param[in]  remote_device This parameter specifies the remote device on
 *             which this task management request is completing.
 * @param[in]  task_request This parameter specifies the task management
 *             request that has completed.
 * @param[in]  completion_status This parameter specifies the results of
 *             the IO request operation.  SCI_SUCCESS indicates successful
 *             completion.
 *
 * @return none
 */
void scic_cb_task_request_complete(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_TASK_REQUEST_HANDLE_T   task_request,
   SCI_TASK_STATUS             completion_status
);

#ifndef SCI_GET_PHYSICAL_ADDRESS_OPTIMIZATION_ENABLED
/**
 * @brief This callback method asks the user to provide the physical
 *        address for the supplied virtual address when building an
 *        io request object.
 *
 * @param[in] controller This parameter is the core controller object
 *            handle.
 * @param[in] io_request This parameter is the io request object handle
 *            for which the physical address is being requested.
 * @param[in] virtual_address This parameter is the virtual address which
 *            is to be returned as a physical address.
 * @param[out] physical_address The physical address for the supplied virtual
 *        address.
 *
 * @return None.
 */
void scic_cb_io_request_get_physical_address(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCI_IO_REQUEST_HANDLE_T   io_request,
   void                    * virtual_address,
   SCI_PHYSICAL_ADDRESS    * physical_address
);
#endif // SCI_GET_PHYSICAL_ADDRESS_OPTIMIZATION_ENABLED

/**
 * @brief This callback method asks the user to provide the number of
 *        bytes to be transferred as part of this request.
 *
 * @param[in] scic_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the number of payload data bytes to be
 *         transferred for this IO request.
 */
U32 scic_cb_io_request_get_transfer_length(
   void * scic_user_io_request
);

/**
 * @brief This callback method asks the user to provide the data direction
 *        for this request.
 *
 * @param[in] scic_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the value of SCI_IO_REQUEST_DATA_OUT or
 *         SCI_IO_REQUEST_DATA_IN, or SCI_IO_REQUEST_NO_DATA.
 */
SCI_IO_REQUEST_DATA_DIRECTION scic_cb_io_request_get_data_direction(
   void * scic_user_io_request
);

#ifdef ENABLE_OSSL_COPY_BUFFER
/**
 * @brief This method is presently utilized in the PIO path,
 *        copies from UF buffer to the SGL buffer. This method
 *        can be served for other OS related copies.
 *
 * @param[in] scic_user_io_request. This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] source addr. Address of UF buffer.
 * @param[in] offset. This parameter specifies the offset into the data
 *            buffers pointed to by the SGL.  The byte offset starts at 0
 *            and continues until the last byte pointed to be the last SGL
 *            element.
 * @param[in] length. data length
 *
 * @return    None
 */
void scic_cb_io_request_copy_buffer(
   void * scic_user_io_request,
   U8   *source_addr,
   U32   offset,
   U32   length
);
#endif

#ifndef SCI_SGL_OPTIMIZATION_ENABLED
/**
 * @brief This callback method asks the user to provide the address
 *        to where the next Scatter-Gather Element is located.
 *
 * Details regarding usage:
 *   - Regarding the first SGE: the user should initialize an index,
 *     or a pointer, prior to construction of the request that will
 *     reference the very first scatter-gather element.  This is
 *     important since this method is called for every scatter-gather
 *     element, including the first element.
 *   - Regarding the last SGE: the user should return NULL from this
 *     method when this method is called and the SGL has exhausted
 *     all elements.
 *
 * @param[in] scic_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] current_sge_address This parameter specifies the address for
 *            the current SGE (i.e. the one that has just processed).
 * @param[out] next_sge An address specifying the location for the next
 *            scatter gather element to be processed.
 *
 * @return None
 */
void scic_cb_io_request_get_next_sge(
   void * scic_user_io_request,
   void * current_sge_address,
   void ** next_sge
);
#endif // SCI_SGL_OPTIMIZATION_ENABLED

/**
 * @brief This callback method asks the user to provide the contents of the
 *        "address" field in the Scatter-Gather Element.
 *
 * @param[in] scic_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] sge_address This parameter specifies the address for the
 *            SGE from which to retrieve the address field.
 *
 * @return A physical address specifying the contents of the SGE's address
 *         field.
 */
SCI_PHYSICAL_ADDRESS scic_cb_sge_get_address_field(
   void * scic_user_io_request,
   void * sge_address
);

/**
 * @brief This callback method asks the user to provide the contents of the
 *        "length" field in the Scatter-Gather Element.
 *
 * @param[in] scic_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] sge_address This parameter specifies the address for the
 *            SGE from which to retrieve the address field.
 *
 * @return This method returns the length field specified inside the SGE
 *         referenced by the sge_address parameter.
 */
U32 scic_cb_sge_get_length_field(
   void * scic_user_io_request,
   void * sge_address
);

/**
 * @brief This callback method asks the user to provide the address for
 *        the command descriptor block (CDB) associated with this IO request.
 *
 * @param[in] scic_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the virtual address of the CDB.
 */
void * scic_cb_ssp_io_request_get_cdb_address(
   void * scic_user_io_request
);

/**
 * @brief This callback method asks the user to provide the length of
 *        the command descriptor block (CDB) associated with this IO request.
 *
 * @param[in] scic_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the length of the CDB.
 */
U32 scic_cb_ssp_io_request_get_cdb_length(
   void * scic_user_io_request
);

/**
 * @brief This callback method asks the user to provide the Logical Unit (LUN)
 *        associated with this IO request.
 *
 * @note The contents of the value returned from this callback are defined
 *       by the protocol standard (e.g. T10 SAS specification).  Please
 *       refer to the transport command information unit description
 *       in the associated standard.
 *
 * @param[in] scic_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the LUN associated with this request.
 * @todo This should be U64?
 */
U32 scic_cb_ssp_io_request_get_lun(
   void * scic_user_io_request
);

/**
 * @brief This callback method asks the user to provide the task attribute
 *        associated with this IO request.
 *
 * @note The contents of the value returned from this callback are defined
 *       by the protocol standard (e.g. T10 SAS specification).  Please
 *       refer to the transport command information unit description
 *       in the associated standard.
 *
 * @param[in] scic_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the task attribute associated with this
 *         IO request.
 */
U32 scic_cb_ssp_io_request_get_task_attribute(
   void * scic_user_io_request
);

/**
 * @brief This callback method asks the user to provide the command priority
 *        associated with this IO request.
 *
 * @note The contents of the value returned from this callback are defined
 *       by the protocol standard (e.g. T10 SAS specification).  Please
 *       refer to the transport command information unit description
 *       in the associated standard.
 *
 * @param[in] scic_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the command priority associated with this
 *         IO request.
 */
U32 scic_cb_ssp_io_request_get_command_priority(
   void * scic_user_io_request
);

/**
 * @brief This callback method asks the user if the received RX frame data is
 *        to be copied to the SGL or should be stored by the SCI core to be
 *        retrieved later with the scic_io_request_get_rx_frame().
 *
 * @param[in] scic_user_io_request This parameter points to the user's IO
 *       request object.  It is a cookie that allows the user to provide the
 *       necessary information for this callback.
 *
 * @return This method returns TRUE if the SCI core should copy the received
 *         frame data to the SGL location or FALSE if the SCI user wants to
 *         retrieve the frame data at a later time.
 */
BOOL scic_cb_io_request_do_copy_rx_frames(
   void * scic_user_io_request
);

/**
 * @brief This callback method asks the user to return the SAT protocol
 *        definition for this IO request.  This method is only called by the
 *        SCI core if the request type constructed is SATA.
 *
 * @param[in] scic_user_io_request This parameter points to the user's IO
 *       request object.  It is a cookie that allows the user to provide the
 *       necessary information for this callback.
 *
 * @return This method returns one of the sat.h defined protocols for the
 *         given io request.
 */
U8 scic_cb_request_get_sat_protocol(
   void * scic_user_io_request
);

/**
 * @brief This callback method asks the user to indicate if the IO is initially
 *           constructed or is reconstructed using the recycled memory.
 *
 * @param[in] scic_user_io_request This parameter points to the user's IO
 *       request object.  It is a cookie that allows the user to provide the
 *       necessary information for this callback.
 *
 * @return This method returns TRUE if the request is initial constructed.
 *         This method returns FALSE if the request is constructed using recycled
 *         memory. For many scic user, this method mostly always returns TRUE.
 */
BOOL scic_cb_request_is_initial_construction(
   void * scic_user_io_request
);

/**
 * @brief This method returns the Logical Unit to be utilized for this
 *        task management request.
 *
 * @note The contents of the value returned from this callback are defined
 *       by the protocol standard (e.g. T10 SAS specification).  Please
 *       refer to the transport task information unit description
 *       in the associated standard.
 *
 * @param[in] scic_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the LUN associated with this request.
 * @todo This should be U64?
 */
U32 scic_cb_ssp_task_request_get_lun(
   void * scic_user_task_request
);

/**
 * @brief This method returns the task management function to be utilized
 *        for this task request.
 *
 * @note The contents of the value returned from this callback are defined
 *       by the protocol standard (e.g. T10 SAS specification).  Please
 *       refer to the transport task information unit description
 *       in the associated standard.
 *
 * @param[in] scic_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns an unsigned byte representing the task
 *         management function to be performed.
 */
U8 scic_cb_ssp_task_request_get_function(
   void * scic_user_task_request
);

/**
 * @brief This method returns the task management IO tag to be managed.
 *        Depending upon the task management function the value returned
 *        from this method may be ignored.
 *
 * @param[in] scic_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns an unsigned 16-bit word depicting the IO
 *         tag to be managed.
 */
U16 scic_cb_ssp_task_request_get_io_tag_to_manage(
   void * scic_user_task_request
);

/**
 * @brief This callback method asks the user to provide the virtual
 *        address of the response data buffer for the supplied IO request.
 *
 * @param[in] scic_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the virtual address for the response data buffer
 *         associated with this IO request.
 */
void * scic_cb_ssp_task_request_get_response_data_address(
   void * scic_user_task_request
);

/**
 * @brief This callback method asks the user to provide the length of the
 *        response data buffer for the supplied IO request.
 *
 * @param[in] scic_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the length of the response buffer data
 *         associated with this IO request.
 */
U32 scic_cb_ssp_task_request_get_response_data_length(
   void * scic_user_task_request
);

/**
 * @brief In this method the user is expected to log the supplied
 *        error information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is an error from the core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void scic_cb_logger_log_error(
   SCI_LOGGER_HANDLE_T   logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);


/**
 * @brief In this method the user is expected to log the supplied warning
 *        information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a warning from the core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void scic_cb_logger_log_warning(
   SCI_LOGGER_HANDLE_T   logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);


/**
 * @brief In this method the user is expected to log the supplied debug
 *        information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a debug message from the core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void scic_cb_logger_log_info(
   SCI_LOGGER_HANDLE_T   logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);


/**
 * @brief In this method the user is expected to log the supplied function
 *        trace information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a function trace (i.e. entry/exit) message from the
 *        core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void scic_cb_logger_log_trace(
   SCI_LOGGER_HANDLE_T   logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);


/**
 * @brief In this method the user is expected to log the supplied state
 *        transition information. The user must be capable of handling
 *        variable length argument lists and should consider prepending the
 *        fact that this is a warning from the core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void scic_cb_logger_log_states(
   SCI_LOGGER_HANDLE_T   logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);


/**
 * @brief In this method the user must return the base address register (BAR)
 *        value for the supplied base address register number.
 *
 * @param[in] controller The controller for which to retrieve the bar number.
 * @param[in] bar_number This parameter depicts the BAR index/number to be read.
 *
 * @return Return a pointer value indicating the contents of the BAR.
 * @retval NULL indicates an invalid BAR index/number was specified.
 * @retval All other values indicate a valid VIRTUAL address from the BAR.
 */
void * scic_cb_pci_get_bar(
   SCI_CONTROLLER_HANDLE_T  controller,
   U16                      bar_number
);

/**
 * @brief In this method the user must read from PCI memory via access.
 *        This method is used for access to memory space and IO space.
 *
 * @param[in]  controller The controller for which to read a DWORD.
 * @param[in]  address This parameter depicts the address from
 *             which to read.
 *
 * @return The value being returned from the PCI memory location.
 *
 * @todo This PCI memory access calls likely need to be optimized into macro?
 */
U32 scic_cb_pci_read_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * address
);

/**
 * @brief In this method the user must write to PCI memory via access.
 *        This method is used for access to memory space and IO space.
 *
 * @param[in]  controller The controller for which to read a DWORD.
 * @param[in]  address This parameter depicts the address into
 *             which to write.
 * @param[out] write_value This parameter depicts the value being written
 *             into the PCI memory location.
 *
 * @todo This PCI memory access calls likely need to be optimized into macro?
 */
void scic_cb_pci_write_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * address,
   U32                       write_value
);

/**
 * @brief This method informs the user when a stop operation on the port
 *        has completed.
 *
 * @param[in] controller This parameter represents the controller which
 *            contains the port.
 * @param[in] port This parameter specifies the SCI port object for which
 *            the callback is being invoked.
 * @param[in] completion_status This parameter specifies the status for
 *            the operation being completed.
 *
 * @return none
 */
void scic_cb_port_stop_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_STATUS               completion_status
);

/**
 * @brief This method informs the user when a hard reset on the port
 *        has completed.  This hard reset could have been initiated by the
 *        user or by the remote port.
 *
 * @param[in] controller This parameter represents the controller which
 *            contains the port.
 * @param[in] port This parameter specifies the SCI port object for which
 *            the callback is being invoked.
 * @param[in] completion_status This parameter specifies the status for
 *            the operation being completed.
 *
 * @return none
 */
void scic_cb_port_hard_reset_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_STATUS               completion_status
);

/**
 * @brief This method informs the user that the port is now in a ready
 *        state and can be utilized to issue IOs.
 *
 * @param[in] controller This parameter represents the controller which
 *            contains the port.
 * @param[in] port This parameter specifies the SCI port object for which
 *            the callback is being invoked.
 *
 * @return none
 */
void scic_cb_port_ready(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port
);

/**
 * @brief This method informs the user that the port is now not in a ready
 *        (i.e. busy) state and can't be utilized to issue IOs.
 *
 * @param[in] controller This parameter represents the controller which
 *            contains the port.
 * @param[in] port This parameter specifies the SCI port object for which
 *            the callback is being invoked.
 * @param[in] reason_code This parameter specifies the reason for the port
 *            not ready callback.
 *
 * @return none
 */
void scic_cb_port_not_ready(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   U32                      reason_code
);

/**
 * @brief This method informs the SCI Core user that a phy/link became
 *        ready, but the phy is not allowed in the port.  In some
 *        situations the underlying hardware only allows for certain phy
 *        to port mappings.  If these mappings are violated, then this
 *        API is invoked.
 *
 * @param[in] controller This parameter represents the controller which
 *            contains the port.
 * @param[in] port This parameter specifies the SCI port object for which
 *            the callback is being invoked.
 * @param[in] phy This parameter specifies the phy that came ready, but the
 *            phy can't be a valid member of the port.
 *
 * @return none
 */
void scic_cb_port_invalid_link_up(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
);

/**
 * @brief This callback method informs the user that a broadcast change
 *        primitive was received.
 *
 * @param[in] controller This parameter represents the controller which
 *            contains the port.
 * @param[in] port This parameter specifies the SCI port object for which
 *            the callback is being invoked.  For instances where the phy
 *            on which the primitive was received is not part of a port, this
 *            parameter will be SCI_INVALID_HANDLE_T.
 * @param[in] phy This parameter specifies the phy on which the primitive
 *            was received.
 *
 * @return none
 */
void scic_cb_port_bc_change_primitive_recieved(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
);

/**
 * @brief This callback method informs the user that a broadcast SES
 *        primitive was received.
 *
 * @param[in] controller This parameter represents the controller which
 *            contains the port.
 * @param[in] port This parameter specifies the SCI port object for which
 *            the callback is being invoked.  For instances where the phy
 *            on which the primitive was received is not part of a port, this
 *            parameter will be SCI_INVALID_HANDLE_T.
 * @param[in] phy This parameter specifies the phy on which the primitive
 *            was received.
 *
 * @return none
 */
void scic_cb_port_bc_ses_primitive_recieved(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
);

/**
 * @brief This callback method informs the user that a broadcast EXPANDER
 *        primitive was received.
 *
 * @param[in] controller This parameter represents the controller which
 *            contains the port.
 * @param[in] port This parameter specifies the SCI port object for which
 *            the callback is being invoked.  For instances where the phy
 *            on which the primitive was received is not part of a port, this
 *            parameter will be SCI_INVALID_HANDLE_T.
 * @param[in] phy This parameter specifies the phy on which the primitive
 *            was received.
 *
 * @return none
 */
void scic_cb_port_bc_expander_primitive_recieved(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
);

/**
 * @brief This callback method informs the user that a broadcast ASYNCHRONOUS
 *        EVENT (AEN) primitive was received.
 *
 * @param[in] controller This parameter represents the controller which
 *            contains the port.
 * @param[in] port This parameter specifies the SCI port object for which
 *            the callback is being invoked.  For instances where the phy
 *            on which the primitive was received is not part of a port, this
 *            parameter will be SCI_INVALID_HANDLE_T.
 * @param[in] phy This parameter specifies the phy on which the primitive
 *            was received.
 *
 * @return none
 */
void scic_cb_port_bc_aen_primitive_recieved(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
);

/**
 * @brief This callback method informs the user that a phy has become
 *        operational and is capable of communicating with the remote end
 *        point.
 *
 * @param[in] controller This parameter represents the controller
 *            associated with the phy.
 * @param[in] port This parameter specifies the port object for which the
 *            user callback is being invoked.  There may be conditions where
 *            this parameter can be SCI_INVALID_HANDLE
 * @param[in] phy This parameter specifies the phy object for which the
 *            user callback is being invoked.
 *
 * @return none
 */
void scic_cb_port_link_up(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
);

/**
 * @brief This callback method informs the user that a phy is no longer
 *        operational and is not capable of communicating with the remote end
 *        point.
 *
 * @param[in] controller This parameter represents the controller
 *            associated with the phy.
 * @param[in] port This parameter specifies the port object for which the
 *            user callback is being invoked.  There may be conditions where
 *            this parameter can be SCI_INVALID_HANDLE
 * @param[in] phy This parameter specifies the phy object for which the
 *            user callback is being invoked.
 *
 * @return none
 */
void scic_cb_port_link_down(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_PORT_HANDLE_T        port,
   SCI_PHY_HANDLE_T         phy
);

/**
 * @brief This user callback method will inform the user that a start
 *        operation has completed.
 *
 * @param[in] controller This parameter specifies the core controller
 *            associated with the completion callback.
 * @param[in] remote_device This parameter specifies the remote device
 *            associated with the completion callback.
 * @param[in] completion_status This parameter specifies the completion
 *            status for the operation.
 *
 * @return none
 */
void scic_cb_remote_device_start_complete(
   SCI_CONTROLLER_HANDLE_T    controller,
   SCI_REMOTE_DEVICE_HANDLE_T remote_device,
   SCI_STATUS                 completion_status
);

/**
 * @brief This user callback method will inform the user that a stop
 *        operation has completed.
 *
 * @param[in] controller This parameter specifies the core controller
 *            associated with the completion callback.
 * @param[in] remote_device This parameter specifies the remote device
 *            associated with the completion callback.
 * @param[in] completion_status This parameter specifies the completion
 *            status for the operation.
 *
 * @return none
 */
void scic_cb_remote_device_stop_complete(
   SCI_CONTROLLER_HANDLE_T    controller,
   SCI_REMOTE_DEVICE_HANDLE_T remote_device,
   SCI_STATUS                 completion_status
);

/**
 * @brief This user callback method will inform the user that a remote
 *        device is now capable of handling IO requests.
 *
 * @param[in] controller This parameter specifies the core controller
 *            associated with the completion callback.
 * @param[in] remote_device This parameter specifies the remote device
 *            associated with the callback.
 *
 * @return none
 */
void scic_cb_remote_device_ready(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This user callback method will inform the user that a remote
 *        device is no longer capable of handling IO requests (until a
 *        ready callback is invoked).
 *
 * @param[in] controller This parameter specifies the core controller
 *            associated with the completion callback.
 * @param[in] remote_device This parameter specifies the remote device
 *            associated with the callback.
 * @param[in] reason_code This paramete specifies the reason the remote
 *            device is not ready.
 *
 * @return none
 */
void scic_cb_remote_device_not_ready(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   U32                         reason_code
);


/**
 * @brief This user callback method will inform the user that this controller
 *        is having unexpected error. The user can choose to reset the controller.
 * @param[in] controller The controller that is failed at the moment.
 *
 * @return none
 */
void scic_cb_controller_error(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_CONTROLLER_ERROR        error
);


#if !defined(DISABLE_ATAPI)
/**
 * @brief This user callback gets from stp packet io's user request
 *           the CDB address.
 * @param[in] scic_user_io_request
 *
 * @return The cdb address.
 */
void * scic_cb_stp_packet_io_request_get_cdb_address(
   void * scic_user_io_request
);

/**
 * @brief This user callback gets from stp packet io's user request
 *           the CDB length.
 * @param[in] scic_user_io_request
 *
 * @return The cdb length.
 */
U32 scic_cb_stp_packet_io_request_get_cdb_length(
   void * scic_user_io_request
);
#else //!defined(DISABLE_ATAPI)
#define scic_cb_stp_packet_io_request_get_cdb_address(scic_user_io_request) NULL
#define scic_cb_stp_packet_io_request_get_cdb_length(scic_user_io_request) 0
#endif //!defined(DISABLE_ATAPI)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_USER_CALLBACK_H_

