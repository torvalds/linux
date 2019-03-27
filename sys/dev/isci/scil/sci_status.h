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
#ifndef _SCI_STATUS_H_
#define _SCI_STATUS_H_

/**
 * @file
 *
 * @brief This file contains all of the return status codes utilized across
 *        the various sub-components in SCI.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * @enum  _SCI_STATUS
 * @brief This is the general return status enumeration for non-IO, non-task
 *        management related SCI interface methods.
 */
typedef enum _SCI_STATUS
{
   /**
    * This member indicates successful completion.
    */
   SCI_SUCCESS = 0,

   /**
    * This value indicates that the calling method completed successfully,
    * but that the IO may have completed before having it's start method
    * invoked.  This occurs during SAT translation for requests that do
    * not require an IO to the target or for any other requests that may
    * be completed without having to submit IO.
    */
   SCI_SUCCESS_IO_COMPLETE_BEFORE_START,

   /**
    *  This Value indicates that the SCU hardware returned an early response
    *  because the io request specified more data than is returned by the
    *  target device (mode pages, inquiry data, etc.). The completion routine
    *  will handle this case to get the actual number of bytes transferred.
    */
   SCI_SUCCESS_IO_DONE_EARLY,

   /**
    * This member indicates that the object for which a state change is
    * being requested is already in said state.
    */
   SCI_WARNING_ALREADY_IN_STATE,

   /**
    * This member indicates interrupt coalescence timer may cause SAS
    * specification compliance issues (i.e. SMP target mode response
    * frames must be returned within 1.9 milliseconds).
    */
   SCI_WARNING_TIMER_CONFLICT,

   /**
    * This field indicates a sequence of action is not completed yet. Mostly,
    * this status is used when multiple ATA commands are needed in a SATI translation.
    */
   SCI_WARNING_SEQUENCE_INCOMPLETE,

   /**
    * This member indicates that there was a general failure.
    */
   SCI_FAILURE,

   /**
    * This member indicates that the SCI implementation is unable to complete
    * an operation due to a critical flaw the prevents any further operation
    * (i.e. an invalid pointer).
    */
   SCI_FATAL_ERROR,

   /**
    * This member indicates the calling function failed, because the state
    * of the controller is in a state that prevents successful completion.
    */
   SCI_FAILURE_INVALID_STATE,

   /**
    * This member indicates the calling function failed, because there is
    * insufficient resources/memory to complete the request.
    */
   SCI_FAILURE_INSUFFICIENT_RESOURCES,

   /**
    * This member indicates the calling function failed, because the
    * controller object required for the operation can't be located.
    */
   SCI_FAILURE_CONTROLLER_NOT_FOUND,

   /**
    * This member indicates the calling function failed, because the
    * discovered controller type is not supported by the library.
    */
   SCI_FAILURE_UNSUPPORTED_CONTROLLER_TYPE,

   /**
    * This member indicates the calling function failed, because the
    * requested initialization data version isn't supported.
    */
   SCI_FAILURE_UNSUPPORTED_INIT_DATA_VERSION,

   /**
    * This member indicates the calling function failed, because the
    * requested configuration of SAS Phys into SAS Ports is not supported.
    */
   SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION,

   /**
    * This member indicates the calling function failed, because the
    * requested protocol is not supported by the remote device, port,
    * or controller.
    */
   SCI_FAILURE_UNSUPPORTED_PROTOCOL,

   /**
    * This member indicates the calling function failed, because the
    * requested information type is not supported by the SCI implementation.
    */
   SCI_FAILURE_UNSUPPORTED_INFORMATION_TYPE,

   /**
    * This member indicates the calling function failed, because the
    * device already exists.
    */
   SCI_FAILURE_DEVICE_EXISTS,

   /**
    * This member indicates the calling function failed, because adding
    * a phy to the object is not possible.
    */
   SCI_FAILURE_ADDING_PHY_UNSUPPORTED,

   /**
    * This member indicates the calling function failed, because the
    * requested information type is not supported by the SCI implementation.
    */
   SCI_FAILURE_UNSUPPORTED_INFORMATION_FIELD,

   /**
    * This member indicates the calling function failed, because the SCI
    * implementation does not support the supplied time limit.
    */
   SCI_FAILURE_UNSUPPORTED_TIME_LIMIT,

   /**
    * This member indicates the calling method failed, because the SCI
    * implementation does not contain the specified Phy.
    */
   SCI_FAILURE_INVALID_PHY,

   /**
    * This member indicates the calling method failed, because the SCI
    * implementation does not contain the specified Port.
    */
   SCI_FAILURE_INVALID_PORT,

    /**
     * This member indicates the calling method was partly successful
     * The port was reset but not all phys in port are operational
     */
    SCI_FAILURE_RESET_PORT_PARTIAL_SUCCESS,

    /**
     * This member indicates that calling method failed
     * The port reset did not complete because none of the phys are operational
     */
    SCI_FAILURE_RESET_PORT_FAILURE,

   /**
    * This member indicates the calling method failed, because the SCI
    * implementation does not contain the specified remote device.
    */
   SCI_FAILURE_INVALID_REMOTE_DEVICE,

   /**
    * This member indicates the calling method failed, because the remote
    * device is in a bad state and requires a reset.
    */
   SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED,

   /**
    * This member indicates the calling method failed, because the SCI
    * implementation does not contain or support the specified IO tag.
    */
   SCI_FAILURE_INVALID_IO_TAG,

   /**
    * This member indicates that the operation failed and the user should
    * check the response data associated with the IO.
    */
   SCI_FAILURE_IO_RESPONSE_VALID,

   /**
    * This member indicates that the operation failed, the failure is
    * controller implementation specific, and the response data associated
    * with the request is not valid.  You can query for the controller
    * specific error information via scic_request_get_controller_status()
    */
   SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR,

   /**
    * This member indicated that the operation failed because the
    * user requested this IO to be terminated.
    */
   SCI_FAILURE_IO_TERMINATED,

   /**
    * This member indicates that the operation failed and the associated
    * request requires a SCSI abort task to be sent to the target.
    */
   SCI_FAILURE_IO_REQUIRES_SCSI_ABORT,

   /**
    * This member indicates that the operation failed because the supplied
    * device could not be located.
    */
   SCI_FAILURE_DEVICE_NOT_FOUND,

   /**
    * This member indicates that the operation failed because the
    * objects association is required and is not correctly set.
    */
   SCI_FAILURE_INVALID_ASSOCIATION,

   /**
    * This member indicates that the operation failed, because a timeout
    * occurred.
    */
   SCI_FAILURE_TIMEOUT,

   /**
    * This member indicates that the operation failed, because the user
    * specified a value that is either invalid or not supported.
    */
   SCI_FAILURE_INVALID_PARAMETER_VALUE,

   /**
    * This value indicates that the operation failed, because the number
    * of messages (MSI-X) is not supported.
    */
   SCI_FAILURE_UNSUPPORTED_MESSAGE_COUNT,

   /**
    * This value indicates that the method failed due to a lack of
    * available NCQ tags.
    */
   SCI_FAILURE_NO_NCQ_TAG_AVAILABLE,

   /**
    * This value indicates that a protocol violation has occurred on the
    * link.
    */
   SCI_FAILURE_PROTOCOL_VIOLATION,

   /**
    * This value indicates a failure condition that retry may help to clear.
    */
   SCI_FAILURE_RETRY_REQUIRED,

   /**
    * This field indicates the retry limit was reached when a retry is attempted
    */
   SCI_FAILURE_RETRY_LIMIT_REACHED,

   /**
    * This member indicates the calling method was partly successful.
    * Mostly, this status is used when a LUN_RESET issued to an expander attached
    * STP device in READY NCQ substate needs to have RNC suspended/resumed
    * before posting TC.
    */
   SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS,

   /**
    * This field indicates an illegal phy connection based on the routing attribute
    * of both expander phy attached to each other.
    */
   SCI_FAILURE_ILLEGAL_ROUTING_ATTRIBUTE_CONFIGURATION,

   /**
    * This field indicates a CONFIG ROUTE INFO command has a response with function result
    * INDEX DOES NOT EXIST, usually means exceeding max route index.
    */
   SCI_FAILURE_EXCEED_MAX_ROUTE_INDEX,

   /**
    * This value indicates that an unsupported PCI device ID has been
    * specified.  This indicates that attempts to invoke
    * scic_library_allocate_controller() will fail.
    */
   SCI_FAILURE_UNSUPPORTED_PCI_DEVICE_ID

} SCI_STATUS;

/**
 * @enum  _SCI_IO_STATUS
 * @brief This enumeration depicts all of the possible IO completion
 *        status values.  Each value in this enumeration maps directly to
 *        a value in the SCI_STATUS enumeration.  Please refer to that
 *        enumeration for detailed comments concerning what the status
 *        represents.
 * @todo Add the API to retrieve the SCU status from the core.
 * @todo Check to see that the following status are properly handled:
 *       - SCI_IO_FAILURE_UNSUPPORTED_PROTOCOL
 *       - SCI_IO_FAILURE_INVALID_IO_TAG
 */
typedef enum _SCI_IO_STATUS
{
   SCI_IO_SUCCESS                         = SCI_SUCCESS,
   SCI_IO_FAILURE                         = SCI_FAILURE,
   SCI_IO_SUCCESS_COMPLETE_BEFORE_START   = SCI_SUCCESS_IO_COMPLETE_BEFORE_START,
   SCI_IO_SUCCESS_IO_DONE_EARLY           = SCI_SUCCESS_IO_DONE_EARLY,
   SCI_IO_FAILURE_INVALID_STATE           = SCI_FAILURE_INVALID_STATE,
   SCI_IO_FAILURE_INSUFFICIENT_RESOURCES  = SCI_FAILURE_INSUFFICIENT_RESOURCES,
   SCI_IO_FAILURE_UNSUPPORTED_PROTOCOL    = SCI_FAILURE_UNSUPPORTED_PROTOCOL,
   SCI_IO_FAILURE_RESPONSE_VALID          = SCI_FAILURE_IO_RESPONSE_VALID,
   SCI_IO_FAILURE_CONTROLLER_SPECIFIC_ERR = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR,
   SCI_IO_FAILURE_TERMINATED              = SCI_FAILURE_IO_TERMINATED,
   SCI_IO_FAILURE_REQUIRES_SCSI_ABORT     = SCI_FAILURE_IO_REQUIRES_SCSI_ABORT,
   SCI_IO_FAILURE_INVALID_PARAMETER_VALUE = SCI_FAILURE_INVALID_PARAMETER_VALUE,
   SCI_IO_FAILURE_NO_NCQ_TAG_AVAILABLE    = SCI_FAILURE_NO_NCQ_TAG_AVAILABLE,
   SCI_IO_FAILURE_PROTOCOL_VIOLATION      = SCI_FAILURE_PROTOCOL_VIOLATION,

   SCI_IO_FAILURE_REMOTE_DEVICE_RESET_REQUIRED = SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED,

   SCI_IO_FAILURE_RETRY_REQUIRED      = SCI_FAILURE_RETRY_REQUIRED,
   SCI_IO_FAILURE_RETRY_LIMIT_REACHED = SCI_FAILURE_RETRY_LIMIT_REACHED,
   SCI_IO_FAILURE_INVALID_REMOTE_DEVICE = SCI_FAILURE_INVALID_REMOTE_DEVICE
} SCI_IO_STATUS;

/**
 * @enum  _SCI_TASK_STATUS
 * @brief This enumeration depicts all of the possible task completion
 *        status values.  Each value in this enumeration maps directly to
 *        a value in the SCI_STATUS enumeration.  Please refer to that
 *        enumeration for detailed comments concerning what the status
 *        represents.
 * @todo Check to see that the following status are properly handled:
 */
typedef enum _SCI_TASK_STATUS
{
   SCI_TASK_SUCCESS                         = SCI_SUCCESS,
   SCI_TASK_FAILURE                         = SCI_FAILURE,
   SCI_TASK_FAILURE_INVALID_STATE           = SCI_FAILURE_INVALID_STATE,
   SCI_TASK_FAILURE_INSUFFICIENT_RESOURCES  = SCI_FAILURE_INSUFFICIENT_RESOURCES,
   SCI_TASK_FAILURE_UNSUPPORTED_PROTOCOL    = SCI_FAILURE_UNSUPPORTED_PROTOCOL,
   SCI_TASK_FAILURE_INVALID_TAG             = SCI_FAILURE_INVALID_IO_TAG,
   SCI_TASK_FAILURE_RESPONSE_VALID          = SCI_FAILURE_IO_RESPONSE_VALID,
   SCI_TASK_FAILURE_CONTROLLER_SPECIFIC_ERR = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR,
   SCI_TASK_FAILURE_TERMINATED              = SCI_FAILURE_IO_TERMINATED,
   SCI_TASK_FAILURE_INVALID_PARAMETER_VALUE = SCI_FAILURE_INVALID_PARAMETER_VALUE,

   SCI_TASK_FAILURE_REMOTE_DEVICE_RESET_REQUIRED = SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED,
   SCI_TASK_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS = SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS

} SCI_TASK_STATUS;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCI_STATUS_H_

