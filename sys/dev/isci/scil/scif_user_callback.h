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
#ifndef _SCIF_USER_CALLBACK_H_
#define _SCIF_USER_CALLBACK_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods/macros that must
 *        be implemented by an SCI Framework user.
 */


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>
#include <dev/isci/scil/sci_controller.h>
#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/sci_memory_descriptor_list.h>


/**
 * @brief This callback method asks the user to create a timer and provide
 *        a handle for this timer for use in further timer interactions.
 *
 * @warning The "timer_callback" method should be executed in a mutually
 *          exlusive manner from the controller completion handler
 *          handler (refer to scic_controller_get_handler_methods()).
 *
 * @param[in]  timer_callback This parameter specifies the callback method
 *             to be invoked whenever the timer expires.
 * @param[in]  controller This parameter specifies the controller with
 *             which this timer is to be associated.
 * @param[in]  cookie This parameter specifies a piece of information that
 *             the user must retain.  This cookie is to be supplied by the
 *             user anytime a timeout occurs for the created timer.
 *
 * @return This method returns a handle to a timer object created by the
 *         user.  The handle will be utilized for all further interactions
 *         relating to this timer.
 */
void * scif_cb_timer_create(
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
void scif_cb_timer_destroy(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * timer
);

/**
 * @brief This callback method asks the user to start the supplied timer.
 *
 * @warning All timers in the system started by the SCI Framework are one
 *          shot timers.  Therefore, the SCI user should make sure that it
 *          removes the timer from it's list when a timer actually fires.
 *          Additionally, SCI Framework user's should be able to handle
 *          calls from the SCI Framework to stop a timer that may already
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
void scif_cb_timer_start(
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
void scif_cb_timer_stop(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * timer
);

/**
 * @brief This callback method asks the user to associate the supplied
 *        lock with an operating environment specific locking construct.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this lock is to be associated.
 * @param[in]  lock This parameter specifies the lock for which the
 *             user should associate an operating environment specific
 *             locking object.
 *
 * @see The SCI_LOCK_LEVEL enumeration for more information.
 *
 * @return none.
 */
void scif_cb_lock_associate(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCI_LOCK_HANDLE_T         lock
);

/**
 * @brief This callback method asks the user to de-associate the supplied
 *        lock with an operating environment specific locking construct.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this lock is to be de-associated.
 * @param[in]  lock This parameter specifies the lock for which the
 *             user should de-associate an operating environment specific
 *             locking object.
 *
 * @see The SCI_LOCK_LEVEL enumeration for more information.
 *
 * @return none.
 */
void scif_cb_lock_disassociate(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCI_LOCK_HANDLE_T         lock
);


/**
 * @brief This callback method asks the user to acquire/get the lock.
 *        This method should pend until the lock has been acquired.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this lock is associated.
 * @param[in]  lock This parameter specifies the lock to be acquired.
 *
 * @return none
 */
void scif_cb_lock_acquire(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCI_LOCK_HANDLE_T         lock
);

/**
 * @brief This callback method asks the user to release a lock.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this lock is associated.
 * @param[in]  lock This parameter specifies the lock to be released.
 *
 * @return none
 */
void scif_cb_lock_release(
   SCI_CONTROLLER_HANDLE_T   controller,
   SCI_LOCK_HANDLE_T         lock
);

/**
 * @brief This user callback will inform the user that the controller has
 *        had a serious unexpected error.  The user should not the error,
 *        disable interrupts, and wait for current ongoing processing to
 *        complete.  Subsequently, the user should reset the controller.
 *
 * @param[in]  controller This parameter specifies the controller that had
 *             an error.
 *
 * @return none
 */
void scif_cb_controller_error(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_CONTROLLER_ERROR error
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
void scif_cb_controller_start_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_STATUS               completion_status
);

/**
 * @brief This user callback will inform the user that the controller has
 *        finished the stop process. Note, after user calls
 *        scif_controller_stop(), before user receives this controller stop
 *        complete callback, user should not expect any callback from
 *        framework, such like scif_cb_domain_change_notification().
 *
 * @param[in]  controller This parameter specifies the controller that was
 *             stopped.
 * @param[in]  completion_status This parameter specifies the results of
 *             the stop operation.  SCI_SUCCESS indicates successful
 *             completion.
 *
 * @return none
 */
void scif_cb_controller_stop_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_STATUS               completion_status
);

/**
 * @brief This method simply returns the virtual address associated
 *        with the scsi_io and byte_offset supplied parameters.
 *
 * @note This callback is not utilized in the fast path.  The expectation
 *       is that this method is utilized for items such as SCSI to ATA
 *       translation for commands like INQUIRY, READ CAPACITY, etc.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
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
U8 * scif_cb_io_request_get_virtual_address_from_sgl(
   void * scif_user_io_request,
   U32    byte_offset
);

#ifdef ENABLE_OSSL_COPY_BUFFER
/**
 * @brief This method is presently utilized in the PIO path,
 *        copies from UF buffer to the SGL buffer. This method
 *        can be served for other OS related copies.
 *
 * @param[in] user_io_request. This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] source addr. Address of UF buffer.
 * @param[in] offset. This parameter specifies the offset into the data
 *            buffers pointed to by the SGL.  The byte offset starts at 0
 *            and continues until the last byte pointed to be the last SGL
 *            element.
 * @param[in] length.
 *
 * @return    None
 */
void scif_cb_io_request_copy_buffer(
   void * scic_user_io_request,
   U8   *source_addr,
   U32   offset,
   U32   length
);
#endif

/**
 * @brief This user callback will inform the user that an IO request has
 *        completed.
 *
 * @param[in]  controller This parameter specifies the controller on
 *             which the IO request is completing.
 * @param[in]  remote_device This parameter specifies the remote device on
 *             which this request is completing.
 * @param[in]  io_request This parameter specifies the IO request that has
 *             completed.
 * @param[in]  completion_status This parameter specifies the results of
 *             the IO request operation.  SCI_IO_SUCCESS indicates
 *             successful completion.
 *
 * @return none
 */
void scif_cb_io_request_complete(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     io_request,
   SCI_IO_STATUS               completion_status
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
 *             the IO request operation.  SCI_TASK_SUCCESS indicates
 *             successful completion.
 *
 * @return none
 */
void scif_cb_task_request_complete(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_TASK_REQUEST_HANDLE_T   task_request,
   SCI_TASK_STATUS             completion_status
);

/**
 * @brief This callback method asks the user to provide the number of
 *        bytes to be transferred as part of this request.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the number of payload data bytes to be
 *         transferred for this IO request.
 */
U32 scif_cb_io_request_get_transfer_length(
   void * scif_user_io_request
);

/**
 * @brief This callback method asks the user to provide the data direction
 *        for this request.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the value of SCI_IO_REQUEST_DATA_OUT,
 *         SCI_IO_REQUEST_DATA_IN, or SCI_IO_REQUEST_NO_DATA.
 */
SCI_IO_REQUEST_DATA_DIRECTION scif_cb_io_request_get_data_direction(
   void * scif_user_io_request
);

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
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] current_sge_address This parameter specifies the address for
 *            the current SGE (i.e. the one that has just processed).
 * @param[out] next_sge An address specifying the location for the next scatter
 *         gather element to be processed.
 *
 * @return None.
 */
void scif_cb_io_request_get_next_sge(
   void * scif_user_io_request,
   void * current_sge_address,
   void ** next_sge
);
#endif

/**
 * @brief This callback method asks the user to provide the contents of the
 *        "address" field in the Scatter-Gather Element.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] sge_address This parameter specifies the address for the
 *            SGE from which to retrieve the address field.
 *
 * @return A physical address specifying the contents of the SGE's address
 *         field.
 */
SCI_PHYSICAL_ADDRESS scif_cb_sge_get_address_field(
   void * scif_user_io_request,
   void * sge_address
);

/**
 * @brief This callback method asks the user to provide the contents of the
 *        "length" field in the Scatter-Gather Element.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] sge_address This parameter specifies the address for the
 *            SGE from which to retrieve the address field.
 *
 * @return This method returns the length field specified inside the SGE
 *         referenced by the sge_address parameter.
 */
U32 scif_cb_sge_get_length_field(
   void * scif_user_io_request,
   void * sge_address
);

/**
 * @brief This callback method asks the user to provide the address for
 *        the command descriptor block (CDB) associated with this IO request.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the virtual address of the CDB.
 */
void * scif_cb_io_request_get_cdb_address(
   void * scif_user_io_request
);

/**
 * @brief This callback method asks the user to provide the length of
 *        the command descriptor block (CDB) associated with this IO request.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the length of the CDB.
 */
U32 scif_cb_io_request_get_cdb_length(
   void * scif_user_io_request
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
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the LUN associated with this request.
 */
U32 scif_cb_io_request_get_lun(
   void * scif_user_io_request
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
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the task attribute associated with this
 *         IO request.
 */
U32 scif_cb_io_request_get_task_attribute(
   void * scif_user_io_request
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
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the command priority associated with this
 *         IO request.
 */
U32 scif_cb_io_request_get_command_priority(
   void * scif_user_io_request
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
 * @param[in] scif_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the LUN associated with this request.
 * @todo This should be U64?
 */
U32 scif_cb_task_request_get_lun(
   void * scif_user_task_request
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
 * @param[in] scif_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns an unsigned byte representing the task
 *         management function to be performed.
 */
U8 scif_cb_task_request_get_function(
   void * scif_user_task_request
);

/**
 * @brief This method returns the task management IO tag to be managed.
 *        Depending upon the task management function the value returned
 *        from this method may be ignored.
 *
 * @param[in] scif_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns an unsigned 16-bit word depicting the IO
 *         tag to be managed.
 */
U16 scif_cb_task_request_get_io_tag_to_manage(
   void * scif_user_task_request
);

/**
 * @brief This callback method asks the user to provide the virtual
 *        address of the response data buffer for the supplied IO request.
 *
 * @param[in] scif_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the virtual address for the response data buffer
 *         associated with this IO request.
 */
void * scif_cb_task_request_get_response_data_address(
   void * scif_user_task_request
);

/**
 * @brief This callback method asks the user to provide the length of the
 *        response data buffer for the supplied IO request.
 *
 * @param[in] scif_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the length of the response buffer data
 *         associated with this IO request.
 */
U32 scif_cb_task_request_get_response_data_length(
   void * scif_user_task_request
);

/**
 * @brief In this method the user is expected to log the supplied
 *        error information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is an error from the framework.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void scif_cb_logger_log_error(
   SCI_LOGGER_HANDLE_T   logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);

/**
 * @brief In this method the user is expected to log the supplied warning
 *        information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a warning from the framework.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void scif_cb_logger_log_warning(
   SCI_LOGGER_HANDLE_T   logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);

/**
 * @brief In this method the user is expected to log the supplied debug
 *        information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a debug message from the framework.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void scif_cb_logger_log_info(
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
 *        framework.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void scif_cb_logger_log_trace(
   SCI_LOGGER_HANDLE_T   logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);


/**
 * @brief In this method the user is expected to log the supplied state
 *        transition information.  The user must be capable of handling
 *        variable length argument lists and should consider prepending the
 *        fact that this is an error from the framework.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void scif_cb_logger_log_states(
   SCI_LOGGER_HANDLE_T   logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);


/**
 * @brief This callback method informs the framework user that something
 *        in the supplied domain has changed (e.g. a device was added or
 *        removed).
 *
 * This callback is called by the framework outside of discovery or
 * target reset processes.  Specifically, domain changes occurring
 * during these processes are handled by the framework.  For example,
 * in the case of Serial Attached SCSI, reception of a BROADCAST (CHANGE)
 * during discovery will cause discovery to restart.  Thus, discovery
 * does not complete until all BCNs are processed. Note, during controller
 * stopping/reset process, the framework user should not expect this call
 * back.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 *
 * @return none
 */
void scif_cb_domain_change_notification(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_DOMAIN_HANDLE_T      domain
);


/**
 * @brief This callback method informs the framework user that a previously
 *        requested discovery operation on the domain has completed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  completion_status This parameter indicates the results of the
 *             discovery operation.
 *
 * @return none
 */
void scif_cb_domain_discovery_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_DOMAIN_HANDLE_T      domain,
   SCI_STATUS               completion_status
);

/**
 * @brief This callback method informs the framework user that a previously
 *        requested reset operation on the domain has completed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  completion_status This parameter indicates the results of the
 *             reset operation.
 *
 * @return none
 */
void scif_cb_domain_reset_complete(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_DOMAIN_HANDLE_T      domain,
   SCI_STATUS               completion_status
);

/**
 * @brief This callback method informs the framework user that the domain
 *        is ready and capable of processing IO requests for devices found
 *        inside it.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 *
 * @return none
 */
void scif_cb_domain_ready(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_DOMAIN_HANDLE_T      domain
);

/**
 * @brief This callback method informs the framework user that the domain
 *        is no longer ready. Thus, it is incapable of processing IO
 *        requests for devices found inside it.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 *
 * @return none
 */
void scif_cb_domain_not_ready(
   SCI_CONTROLLER_HANDLE_T  controller,
   SCI_DOMAIN_HANDLE_T      domain
);

/**
 * @brief This callback method informs the framework user that a new
 *        direct attached device was found in the domain.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  sas_address This parameter specifies the SAS address of
 *             the new device.
 * @param[in]  protocols This parameter specifies the protocols
 *             supported by the newly discovered device.
 *
 * @return none
 */
void scif_cb_domain_da_device_added(
   SCI_CONTROLLER_HANDLE_T                      controller,
   SCI_DOMAIN_HANDLE_T                          domain,
   SCI_SAS_ADDRESS_T                          * sas_address,
   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T * protocols
);

/**
 * @brief This callback method informs the framework user that a new
 *        expander attached device was found in the domain.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  containing_device This parameter specifies the remote
 *             device that contains the device that was added.
 * @param[in]  smp_response This parameter specifies the SMP response
 *             data associated with the newly discovered device.
 *
 * @return none
 */
void scif_cb_domain_ea_device_added(
   SCI_CONTROLLER_HANDLE_T      controller,
   SCI_DOMAIN_HANDLE_T          domain,
   SCI_REMOTE_DEVICE_HANDLE_T   containing_device,
   SMP_RESPONSE_DISCOVER_T    * smp_response
);

/**
 * @brief This callback method informs the framework user that a device
 *        has been removed from the domain.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  remote_device This parameter specifies the device object with
 *             which this callback is associated.
 *
 * @return none
 */
void scif_cb_domain_device_removed(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_DOMAIN_HANDLE_T         domain,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This callback method informs the framework user that the remote
 *        device is ready and capable of processing IO requests.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  remote_device This parameter specifies the device object with
 *             which this callback is associated.
 *
 * @return none
 */
void scif_cb_remote_device_ready(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_DOMAIN_HANDLE_T         domain,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This callback method informs the framework user that the remote
 *        device is not ready.  Thus, it is incapable of processing IO
 *        requests.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  remote_device This parameter specifies the device object with
 *             which this callback is associated.
 *
 * @return none
 */
void scif_cb_remote_device_not_ready(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_DOMAIN_HANDLE_T         domain,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device
);

/**
 * @brief This callback method informs the framework user that the remote
 *        device failed.  This typically occurs shortly after the device
 *        has been discovered, during the configuration phase for the device.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  remote_device This parameter specifies the device object with
 *             which this callback is associated.
 * @param[in]  status This parameter specifies the specific failure condition
 *             associated with this device failure.
 *
 * @return none
 */
void scif_cb_remote_device_failed(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_DOMAIN_HANDLE_T         domain,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_STATUS                  status
);



/**
 * @brief This callback method creates an OS specific deferred task
 *        for internal usage. The handler to deferred task is stored by OS
 *        driver.
 *
 * @param[in] controller This parameter specifies the controller object
 *            with which this callback is associated.
 *
 * @return none
 */
void scif_cb_start_internal_io_task_create(
   SCI_CONTROLLER_HANDLE_T controller
);


/**
 * @brief This callback method schedules a OS specific deferred task.
 *
 * @param[in] controller This parameter specifies the controller
 *            object with which this callback is associated.
 * @param[in] start_internal_io_task_routine This parameter specifies the
 *            sci start_internal_io routine.
 * @param[in] context This parameter specifies a handle to a parameter
 *            that will be passed into the "start_internal_io_task_routine"
 *            when it is invoked.
 *
 * @return none
 */
void scif_cb_start_internal_io_task_schedule(
   SCI_CONTROLLER_HANDLE_T controller,
   FUNCPTR                 start_internal_io_task_routine,
   void                  * context
);

/**
 * @brief This method will be invoked to allocate memory dynamically.
 *
 * @param[in]  controller This parameter represents the controller
 *             object for which to allocate memory.
 * @param[out] mde This parameter represents the memory descriptor to
 *             be filled in by the user that will reference the newly
 *             allocated memory.
 *
 * @return none
 */
void scif_cb_controller_allocate_memory(
   SCI_CONTROLLER_HANDLE_T            controller,
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T * mde
);

/**
 * @brief This method will be invoked to allocate memory dynamically.
 *
 * @param[in]  controller This parameter represents the controller
 *             object for which to allocate memory.
 * @param[out] mde This parameter represents the memory descriptor to
 *             be filled in by the user that will reference the newly
 *             allocated memory.
 *
 * @return none
 */
void scif_cb_controller_free_memory(
   SCI_CONTROLLER_HANDLE_T            controller,
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T * mde
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_USER_CALLBACK_H_

