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
#ifndef _SCIC_LIBRARY_H_
#define _SCIC_LIBRARY_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods that can be called
 *        by an SCI Core user on the library object.  The library is the
 *        container of all other objects being managed (i.e. controllers,
 *        target devices, sas ports, etc.).
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>


/**
 * @enum  _SCIC_LIBRARY_IO_MODE
 * @brief This enumeration depicts the different IO modes in which the SCI
 *        library and it's controllers can operate.
 */
typedef enum _SCIC_LIBRARY_IO_MODE
{
   /**
    * In this mode the SCI library will operate in a polling mode for
    * operations.  In other words, the library will not return from a
    * send io method until the completion for the IO has been received.
    */
   SCIC_IO_MODE_POLLING,

   /**
    * In this mode the SCI library returns after committing the IO request
    * to the controller hardware.  Completion of the request will occur
    * asynchronously.
    */
   SCIC_IO_MODE_ASYNCHRONOUS

} SCIC_LIBRARY_IO_MODE;


struct sci_pci_common_header;

/**
 * @brief This method will contsruct the core library based on the supplied
 *        parameter information.  By default, libraries are considered
 *        "ready" as soon as they are constructed.
 *
 * @param[in]  library_memory a pointer to the memory at which the
 *             library object is located.
 * @param[in]  max_controller_count the maximum number of controllers that
 *             this library can manage.
 *
 * @return An opaque library handle to be used by the SCI user for all
 *         subsequent library operations.
 */
SCI_LIBRARY_HANDLE_T scic_library_construct(
   void *                         library_memory,
   U8                             max_controller_count
);

/**
 * This method sets the PCI header information required for proper
 * controller object creation/allocation.
 *
 * @param[in]  library the handle to the library object for which to allocate
 *             a controller.
 * @param[in]  pci_header a pointer to the pci header data for the pci
 *             device for which this library is being created.
 *
 * @return none
 */
void scic_library_set_pci_info(
   SCI_LIBRARY_HANDLE_T           library,
   struct sci_pci_common_header * pci_header
);

/**
 * @brief This method returns the size of the core library object.
 *
 * @param[in]  max_controller_count the maximum number of controllers that
 *             this library can manage.
 *
 * @return a positive integer value indicating the size (in bytes) of the
 *         library object.
 */
U32 scic_library_get_object_size(
   U8  max_controller_count
);

/**
 *
 *
 */
U8 scic_library_get_pci_device_controller_count(
   SCI_LIBRARY_HANDLE_T  library
);

/**
 * @brief This method will allocate the next available core controller object
 *        that can be managed by this core library.
 *
 * @param[in]  library the handle to the library object for which to allocate
 *             a controller.
 * @param[out] new_controller This parameter specifies a pointer to the
 *             controller handle that was added to the library.

 * @return Indicate if the controller was successfully allocated or if iti
 *         failed in some way.
 * @retval SCI_SUCCESS if the controller was successfully allocated.
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES if the library has no more
 *         available controller objects to allocate.
 */
SCI_STATUS scic_library_allocate_controller(
   SCI_LIBRARY_HANDLE_T      library,
   SCI_CONTROLLER_HANDLE_T * new_controller
);

/**
 * @brief This method will attempt to free the supplied controller to the
 *        library.
 *
 * @param[in]  library the handle to the library object for which to free
 *             a controller.
 * @param[in]  controller the handle to the controller object to be freed
 *             from the library.
 *
 * @return Indicate if the controller was successfully freed or if it failed
 *         in some way.
 * @retval SCI_SUCCESS if the controller was successfully freed.
 * @retval SCI_FAILURE_CONTROLLER_NOT_FOUND if the supplied controller is
 *         not managed by the supplied library.
 */
SCI_STATUS scic_library_free_controller(
   SCI_LIBRARY_HANDLE_T     library,
   SCI_CONTROLLER_HANDLE_T  controller
);

/**
 * @brief This method returns the maximum size (in bytes) that an individual
 *        SGL element can address using this library.
 *
 * @note  SGL size is restricted to the lowest common denominator across all
 *        controllers managed by the library.
 * @todo  Does the byte count have to be DWORD aligned?
 *
 * @param[in]  library the handle to the library object for which to
 *             determine the maximum SGL size.
 *
 * @return Return the maximum size (in bytes) for an SGE for any controller
 *         managed by this library.
 */
U32 scic_library_get_max_sge_size(
   SCI_LIBRARY_HANDLE_T  library
);

/**
 * @brief This method returns the maximum number of SGL elements for a
 *        single IO request using this library.
 *
 * @note  SGE count is restricted to the lowest common denominator across all
 *        controllers managed by the library.
 *
 * @param[in]  library the handle to the library object for which to
 *             determine the maximum number of SGEs per IO request.
 *
 * @return Return the maximum number of SGEs for an IO request for any
 *         controller in this library.
 */
U32 scic_library_get_max_sge_count(
   SCI_LIBRARY_HANDLE_T  library
);

/**
 * @brief This method returns the maximum length for any IO request that
 *        can be handled by the underlying controllers
 *
 * @note  IO length is restricted to the lowest common denominator across all
 *        controllers managed by the library.
 *
 * @param[in]  library the handle to the library object for which to
 *             determine the maximum length for all IO requests.
 *
 * @return Return the maximum length for all IO requests for any
 *         controller in this library.
 */
U32 scic_library_get_max_io_length(
   SCI_LIBRARY_HANDLE_T  library
);

/**
 * @brief This method returns the minimum number of timers needed.  If the
 *        user supplies timers less then the number specified via this
 *        call, then the user runs the risk of improper operation.
 *
 * @return This method returns a value representing the minimum number of
 *         timers required by this framework implementation
 */
U16 scic_library_get_min_timer_count(
   void
);

/**
 * @brief This method returns the maximum number of timers that could
 *        be ever be in use by this component at a given time.
 *
 * @return This method returns a value representing the minimum number of
 *         timers required by this framework implementation
 */
U16 scic_library_get_max_timer_count(
   void
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_LIBRARY_H_

