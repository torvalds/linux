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
#ifndef _SCIF_IO_REQUEST_H_
#define _SCIF_IO_REQUEST_H_

/**
 * @file
 *
 * @brief This file contains the structures and interface methods that
 *        can be referenced and used by the SCI user for the SCI IO request
 *        object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>


/**
 * @brief This method simply returns the size required to construct an SCI
 *        based IO request object (includes core & framework object size).
 *
 * @return Return the size of the SCI IO request object.
 */
U32 scif_io_request_get_object_size(
   void
);

/**
* @brief This method simply the number of data bytes transferred for a
*        STP or SSP io request.
*
* @param[in] scif_io_request This parameter specifies the framework IO
*            handle to retrieve the number of data bytes transferred.
*
* @return Return the number of data bytes transferred by the io request
*/
U32 scif_io_request_get_number_of_bytes_transferred(
   void * scif_io_request
);

/**
 * @brief This method is called by the SCIF user to construct an IO request.
 *        This method will construct a SCIC IO request internally.  The memory
 *        for the core IO request is passed as a parameter to this method.
 *
 * @note  The SCI framework implementation will create an association between
 *        the user IO request object and the framework IO request object.
 *
 * @param[in]  scif_controller the handle to the framework controller object
 *             for which to build an IO request.
 * @param[in]  scif_remote_device This parameter specifies the framework
 *             remote device with which this IO request is to be associated.
 * @param[in]  io_tag This parameter specifies the IO tag to be associated
 *             with this request.  If SCI_CONTROLLER_INVALID_IO_TAG is
 *             passed, then a copy of the request is built internally.  The
 *             request will be copied into the actual controller request
 *             memory when the IO tag is allocated internally during the
 *             scif_controller_start_io() method.
 * @param[in]  user_io_request_object This parameter specifies the user
 *             IO request to be utilized during IO construction.  This IO
 *             pointer will become the associated object for the framework
 *             IO request object.
 * @param[in]  io_request_memory This parameter specifies the memory
 *             to be utilized in the construction of the framework IO request.
 * @param[in]  scif_io_request This parameter specifies the handle to be
 *             utilized for all further interactions with this IO request
 *             object.
 *
 * @return Indicate if the controller successfully built the IO request.
 * @retval SCI_SUCCESS This value is returned if the IO request was
 *         successfully built.
 */
SCI_STATUS scif_io_request_construct(
   SCI_CONTROLLER_HANDLE_T      scif_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scif_remote_device,
   U16                          io_tag,
   void                       * user_io_request_object,
   void                       * io_request_memory,
   SCI_IO_REQUEST_HANDLE_T    * scif_io_request
);

/**
 * @brief This method simply returns the SCI Core object handle that is
 *        associated with the supplied SCI Framework object.
 *
 * @param[in]  scif_io_request This parameter specifies the framework IO
 *             for which to return the associated core IO request object.
 *
 * @return This method returns a handle to the core IO request object
 *         associated with the framework IO request object.
 * @retval SCI_INVALID_HANDLE This return value indicates that the SCI Core
 *         IO request handle for the supplied framework IO is invalid.
 */
SCI_IO_REQUEST_HANDLE_T scif_io_request_get_scic_handle(
   SCI_IO_REQUEST_HANDLE_T scif_io_request
);

/**
 * @brief This method returns the address of the response information unit.
 *        This call is only valid if the completion status for the io request
 *        is SCI_FAILURE_IO_RESPONSE_VALID.
 *
 * @param[in]  scif_io_request This parameter specifies the framework IO
 *             for which to return the associated core IO request object.
 *
 * @return The address for the response information unit.
 */
void * scif_io_request_get_response_iu_address(
   SCI_IO_REQUEST_HANDLE_T scif_io_request
);

/**
 * @brief This method will build an Framework SSP Passthrough IO request based
 *        on the user information supplied in the pass-through IO request object.
 *        In case of pass through request construction, the driver creates the
 *        sci core request object and pass that to the framework
 *
 * @pre
 *
 * @param[in]  scif_controller. Not used in the function but kept to maintain uniformity
 *             with other io construct functions
 * @param[in]  scif_remote_device. This parameter is the device.
 * @param[in]  scic_io_request. This parameter is the scic request already constructed
 * @param[in]  user_io_request_object, the user io request
 * @param[in]  io_request_memory, the scif offset in the user_io_request_object.
 *
 * @param[out]  the contructed scif request. This points to the same location as io_request_memory
 *
 * @return Indicate if framework IO request is successfully built.
 * @retval SCI_SUCCESS This value is returned if the IO request was
 *         successfully built.
 */
SCI_STATUS scif_io_request_construct_with_core (
   SCI_CONTROLLER_HANDLE_T      scif_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scif_remote_device,
   void                       * scic_io_request,
   void                       * user_io_request_object,
   void                       * io_request_memory,
   SCI_IO_REQUEST_HANDLE_T    * scif_io_request
);

/**
 * @brief This method will build the basic scif and scic io request object based
 *        on the user information supplied in the pass-through IO request object.
 *        This function will not build the protocol specific part of the request
 *        but set up the memory areas of scif and scic set the association.
 *
 * @pre
 *
 * @param[in]  scif_controller the handle to the framework controller object
 *             for which to build an IO request.
 * @param[in]  scif_remote_device This parameter specifies the framework
 *             remote device with which this IO request is to be associated.
 * @param[in]  io_tag This parameter specifies the IO tag to be associated
 *             with this request.  If SCI_CONTROLLER_INVALID_IO_TAG is
 *             passed, then a copy of the request is built internally.  The
 *             request will be copied into the actual controller request
 *             memory when the IO tag is allocated internally during the
 *             scif_controller_start_io() method.
 * @param[in]  user_io_request_object This parameter specifies the user
 *             IO request to be utilized during IO construction.  This IO
 *             pointer will become the associated object for the framework
 *             IO request object.
 * @param[in]  io_request_memory This parameter specifies the memory
 *             to be utilized in the construction of the framework IO request.
 * @param[in]  scif_io_request This parameter specifies the handle to be
 *             utilized for all further interactions with this IO request
 *             object.
 *
 * @return Indicate if the controller successfully built the IO request.
 * @retval SCI_SUCCESS This value is returned if the IO request was
 *         successfully built.
 */
SCI_STATUS scif_request_construct(
   SCI_CONTROLLER_HANDLE_T      scif_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scif_remote_device,
   U16                          io_tag,
   void                       * user_io_request_object,
   void                       * io_request_memory,
   SCI_IO_REQUEST_HANDLE_T    * scif_io_request
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_IO_REQUEST_H_

